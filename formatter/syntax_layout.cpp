#include "syntax_layout.hpp"

#include <parser/operator_info.hpp>

#include <algorithm>
#include <limits>
#include <numeric>
#include <optional>
#include <stdexcept>

namespace formatter {
namespace {
using Index = uint32_t;
using parser::Node;
using parser::NodeKind;
using parser::PunctuationRole;
using tokenizer::TokenKind;
constexpr Index None = UINT32_MAX;
using Position = uint64_t;
constexpr Position GapPosition(Index i)
{
    return Position(i) * 2;
}
constexpr Position TokenPosition(Index i)
{
    return GapPosition(i) + 1;
}
enum class Boundary : uint8_t { Preserve, Flat, Soft, Hard, Conditional };
struct Gap
{
    Index Owner = None;
    Boundary Shape = Boundary::Preserve;
    layout::GapKind Broken = layout::GapKind::Flat;
    bool Closing = false;
    bool Generated = false;
    bool Reindent = false;
    // Style-owned indentation demand survives a scope fence on topology.
    bool PartialIndent = false;
    Index RequiredWidth = 0;
    int32_t ExtraIndent = 0;
    bool OverrideIndent = false;
    bool Frozen = false;
    uint16_t NewlineCount = 1;
    bool Vertical = false;
    bool PreserveIndent = false;
    uint8_t VerticalPriority = 0;
    Index MarkDelimiter = None;
    GapContext Context = GapContext::Preserved;
    bool ContextKnown = false;
    // A flat header boundary still rebuilds source topology, so horizontal
    // Preserve spelling must use the same safe fallback before fitting.
    bool CanonicalFlat = false;
    bool ControlSeparator = false;
};
struct Group
{
    Position Begin;
    Position End;
    layout::GroupSpec Spec;
    IndentationPolicy Indent;
    bool HasAnchor = false;
    layout::GroupId Id = layout::InvalidId;
    layout::AnchorId Anchor = layout::InvalidId;
    layout::AnchorId Block = layout::InvalidId;
    Index PrefixGap = None;
    bool AnchorAfterGap = false;
    uint32_t LogicalIndent = 0;
    bool SaturatedIndent = false;
};
struct IndentBasis
{
    uint32_t Column;
    bool Saturated;
};

constexpr IndentBasis AccumulateIndent(IndentBasis parent, int32_t delta)
{
    const auto logical = int64_t(parent.Column) + delta;
    return {uint32_t(std::clamp<int64_t>(logical, 0, int64_t(UINT32_MAX) - 1)),
            parent.Saturated || logical >= int64_t(UINT32_MAX)};
}
// A later negative offset cannot undo information lost by an ancestor clamp.
static_assert(AccumulateIndent(
        AccumulateIndent({UINT32_MAX - 2, false}, 3),
        -32).Saturated);
static_assert(!AccumulateIndent({UINT32_MAX - 2, false}, 1).Saturated);
struct Syntax
{
    const Node* TreeNode;
    Index Parent;
    Index FirstChild = None;
    Index NextSibling = None;
    Index Root = None;
    Index PreviousSibling = None;
    Index Operator = None;
    bool Safe = true;
    bool String = false;
    bool Definition = false;
    bool ItemSequence = false;
    parser::TokenRange SequenceBody = {};
    parser::TokenRange Attached = {};
};
struct ScopeName
{
    std::string_view Name;
    Index Pair;
    bool Begin;
};
struct VisualRange
{
    Index First;
    Index End;
};
struct Comma
{
    Index Owner = None;
    Index Existing = None;
};
Index AddWidth(Index a, Index b)
{
    return a > None - b ? None : a + b;
}
bool HasLine(std::string_view text)
{
    return text.find('\n') != std::string_view::npos;
}
Index HorizontalColumns(std::string_view text, uint8_t tabWidth)
{
    Index width = 0;
    for (char c : text) {
        if (c == '\n' || c == '\r') {
            return None;
        }
        width = AddWidth(width, c == '\t' ? tabWidth : 1);
    }
    return width;
}
bool IsControl(NodeKind kind)
{
    switch (kind) {
        case NodeKind::IfStatement:
        case NodeKind::ForStatement:
        case NodeKind::WhileStatement:
        case NodeKind::DoWhileStatement:
        case NodeKind::SwitchStatement:
        case NodeKind::CatchClause:
        case NodeKind::CaseLabel:
            return true;
        default:
            return false;
    }
}
bool IsSpine(NodeKind kind)
{
    return kind == NodeKind::CallExpression ||
        kind == NodeKind::MemberExpression;
}

class Planner
{
public:
    Planner(
        const parser::SyntaxTree& tree,
        const CompiledPolicies& policies,
        const LibraryAnnotations& annotations,
        const SourceAnalysis& safety,
        const SyntaxSpacing& spacing,
        layout::Document& document,
        std::pmr::memory_resource& scratch,
        const SyntaxLayoutOptions& options)
        : Tree_(tree)
        , Policies_(policies)
        , Annotations_(annotations)
        , Safety_(safety)
        , Spacing_(spacing)
        , Document_(document)
        , Options_(options)
        , Syntax_(&scratch)
        , Groups_(&scratch)
        , Gaps_(&scratch)
        , Commas_(&scratch)
        , Removed_(&scratch)
        , Order_(&scratch)
        , Stack_(&scratch)
        , LastChild_(&scratch)
        , BlockOf_(&scratch)
        , PrefixProtection_(&scratch)
        , Visual_(&scratch)
        , RootName_(&scratch)
        , ScopeNames_(&scratch)
    { }
    SyntaxLayoutStatus Build()
    {
        const auto count = Tree_.TokenCount();
        if (
            Safety_.Gaps().size() != size_t(count) + 1 ||
                Spacing_.Gaps().size() != size_t(count) + 1)
        {
            return {SyntaxLayoutError::MismatchedInputs};
        }
        Gaps_.resize(size_t(count) + 1);
        Commas_.resize(size_t(count) + 1);
        Removed_.resize(count);
        PrefixProtection_.resize(size_t(count) + 1);
        for (Index i = 0; i < count; ++i) {
            const auto token = Tree_.Token(i).Text;
            PrefixProtection_[i + 1] =
                PrefixProtection_[i] +
                    (ContextProtected({token.Begin, token.End}) ? 1 : 0);
        }
        for (auto id : Options_.GeneratedAdjacencies) {
            if (id > count) {
                return {SyntaxLayoutError::InvalidGeneratedAdjacency, id};
            }
            Gaps_[id].Generated = true;
        }
        ProjectSyntax();
        AnalyzeVisualScopes();
        for (Index i = 0; i < Syntax_.size(); ++i) {
            const auto& node = *Syntax_[i].TreeNode;
            PlanHeaderAndBody(i);
            if (!Syntax_[i].Safe) {
                if (ListFor(node.Kind)) {
                    PreserveUnplannedListContext(i);
                }
                continue;
            }
            if (const auto *list = ListFor(node.Kind)) {
                PlanList(i, *list);
            }
            PlanExpression(i);
            PlanChain(i);
            PlanAttribute(i);
        }
        PlanVisualScopes();
        PlanVerticalGaps();
        if (!State_) {
            return State_;
        }
        ResolveSafety();
        SummarizeSegments();
        SortForest();
        if (!State_) {
            return State_;
        }
        Emit();
        if (!State_) {
            return State_;
        }
        const auto sealed = Document_.Seal();
        if (!sealed) {
            return {SyntaxLayoutError::CoreFailure, None, sealed};
        }
        return {};
    }
private:
    const parser::SyntaxTree& Tree_;
    const CompiledPolicies& Policies_;
    const LibraryAnnotations& Annotations_;
    const SourceAnalysis& Safety_;
    const SyntaxSpacing& Spacing_;
    layout::Document& Document_;
    const SyntaxLayoutOptions& Options_;
    std::pmr::vector<Syntax> Syntax_;
    std::pmr::vector<Group> Groups_;
    std::pmr::vector<Gap> Gaps_;
    std::pmr::vector<Comma> Commas_;
    std::pmr::vector<uint8_t> Removed_;
    std::pmr::vector<Index> Order_;
    std::pmr::vector<Index> Stack_;
    std::pmr::vector<Index> LastChild_;
    std::pmr::vector<Index> BlockOf_;
    std::pmr::vector<Index> PrefixProtection_;
    std::pmr::vector<VisualRange> Visual_;
    std::pmr::string RootName_;
    std::pmr::vector<ScopeName> ScopeNames_;
    SyntaxLayoutStatus State_;
    size_t NextPermission_ = 0;
    // Scope exclusions prohibit edits, not understanding the surrounding
    // grammar. Keep real reason unions (including scope + semantic reasons)
    // stronger. Edit queries continue to use the unfiltered safety analysis.
    bool ContextProtected(parser::SourceRange source) const
    {
        const auto protection = Safety_.Protection();
        auto first =
            std::lower_bound(
                protection.begin(),
                protection.end(),
                source.Begin,
                [] (
                    const ProtectedSourceRange& range,
                    parser::SourceOffset point) { return range.Source.End <=
                        point; });
        for (;
            first != protection.end() && first->Source.Begin < source.End;
            ++first)
        {
            if (
                (uint16_t(first->Reasons) &
                            ~uint16_t(ProtectionReason::OutsideEditScope)) != 0)
            {
                return true;
            }
        }
        return false;
    }
    void Fail(SyntaxLayoutError error, Index token)
    {
        if (State_) {
            State_ ={error, token};
        }
    }
    // Preorder projection visits each node once. Reverse order computes shared
    // callee roots/string eligibility and safety. No overlapping range scans.
    void ProjectSyntax()
    {
        struct Pending
        {
            const Node* NodePtr;
            Index Parent;
        };
        std::pmr::vector<Pending> pending(Syntax_.get_allocator().resource());
        pending.push_back({&Tree_.Root(), None});
        while (!pending.empty()) {
            auto item = pending.back();
            pending.pop_back();
            const auto id = Index(Syntax_.size());
            Syntax_.push_back({item.NodePtr, item.Parent});
            LastChild_.push_back(None);
            BlockOf_.push_back(None);
            switch (item.NodePtr->Kind) {
                case NodeKind::TranslationUnit:
                case NodeKind::NamespaceDeclaration:
                case NodeKind::RecordDeclaration:
                case NodeKind::CompoundStatement:
                case NodeKind::LambdaBody:
                case NodeKind::LinkageSpecification:
                case NodeKind::ExportDeclaration:
                    Syntax_[id].ItemSequence = true;
                    break;
                case NodeKind::PreprocessingConditional:
                case NodeKind::PreprocessingBranch:
                    Syntax_[id].ItemSequence =
                        item.Parent != None &&
                            Syntax_[item.Parent].ItemSequence;
                    break;
                default:
                    break;
            }
            if (item.Parent != None) {
                if (LastChild_[item.Parent] == None) {
                    Syntax_[item.Parent].FirstChild = id;
                } else {
                    Syntax_[LastChild_[item.Parent]].NextSibling = id;
                    Syntax_[id].PreviousSibling = LastChild_[item.Parent];
                }
                LastChild_[item.Parent] = id;
            }
            // Linked siblings need not be copied/reversed. A sibling
            // continuation is pushed before the first child, keeping the
            // traversal bounded.
            if (item.NodePtr->NextSibling && item.Parent != None) {
                pending.push_back({item.NodePtr->NextSibling, item.Parent});
            }
            if (item.NodePtr->FirstChild) {
                pending.push_back({item.NodePtr->FirstChild, id});
            }
        }
        for (Index i = Index(Syntax_.size()); i-- > 0;) {
            auto& entry = Syntax_[i];
            const auto& node = *entry.TreeNode;
            entry.Safe =
                node.Tokens.Begin <= node.Tokens.End &&
                    node.Tokens.End <= Tree_.TokenCount();
            switch (node.Kind) {
                case NodeKind::UnparsedSyntax:
                case NodeKind::UnknownTypeName:
                case NodeKind::MissingToken:
                case NodeKind::UnexpectedToken:
                case NodeKind::AmbiguousStatement:
                case NodeKind::Error:
                    entry.Safe = false;
                    break;
                default:
                    break;
            }
            if (entry.Safe) {
                entry.Safe =
                    PrefixProtection_[node.Tokens.Begin] ==
                        PrefixProtection_[node.Tokens.End];
            }
            for (auto child = entry.FirstChild;
                child != None;
                child = Syntax_[child].NextSibling)
            {
                entry.Safe = entry.Safe && Syntax_[child].Safe;
            }
            if (entry.ItemSequence) {
                entry.SequenceBody = node.Tokens;
                if (
                    node.Kind != NodeKind::TranslationUnit &&
                        node.Kind != NodeKind::PreprocessingConditional &&
                        node.Kind != NodeKind::PreprocessingBranch)
                {
                    Index opening = None;
                    Index closing = None;
                    DirectTokens(
                        i,
                        [&] (Index token) {
                            if (
                                opening == None &&
                                    Tree_.Kind(token) ==
                                        TokenKind::Operator_OpenBrace)
                            {
                                opening = token;
                            } else if (
                                Tree_.Kind(token) ==
                                    TokenKind::Operator_CloseBrace)
                            {
                                closing = token;
                            }
                        });
                    // Only real sequence bodies own these gaps. In particular,
                    // an unbraced export wrapper and pre-brace trivia do not.
                    entry.ItemSequence =
                        opening != None && closing != None && opening < closing;
                    if (entry.ItemSequence) {
                        entry.SequenceBody ={opening + 1, closing};
                    }
                }
            }
            if (
                node.Kind == NodeKind::BinaryExpression ||
                    node.Kind == NodeKind::AssignmentExpression)
            {
                DirectTokens(
                    i,
                    [&] (Index token) {
                        const auto role = Tree_.Role(token);
                        if (
                            role == PunctuationRole::BinaryOperator ||
                                role == PunctuationRole::ComparisonOperator ||
                                role == PunctuationRole::ShiftOperator ||
                                role == PunctuationRole::AssignmentOperator)
                        {
                            entry.Operator = token;
                        }
                    });
            }
            entry.Attached = node.Tokens;
            if (const auto *trivia = Tree_.AssociatedTrivia(node)) {
                for (auto* item = trivia->Leading.First;
                    item;
                    item = item->Next)
                {
                    entry.Attached.Begin =
                        std::min(entry.Attached.Begin, item->Tokens.Begin);
                }
                for (auto* item = trivia->Trailing.First;
                    item;
                    item = item->Next)
                {
                    entry.Attached.End =
                        std::max(entry.Attached.End, item->Tokens.End);
                }
            }
            entry.Definition =
                node.Kind == NodeKind::FunctionDefinition ||
                    node.Kind == NodeKind::FunctionTryBlock;
            if (
                node.Kind == NodeKind::RecordDeclaration ||
                    node.Kind == NodeKind::EnumDeclaration)
            {
                DirectTokens(
                    i,
                    [&] (Index token) {
                        entry.Definition =
                            entry.Definition ||
                                Tree_.Kind(token) ==
                                    TokenKind::Operator_OpenBrace;
                    });
                for (auto child = entry.FirstChild;
                    child != None;
                    child = Syntax_[child].NextSibling)
                {
                    entry.Definition =
                        entry.Definition ||
                            Syntax_[child].TreeNode->Kind ==
                                NodeKind::EnumeratorList;
                }
            }
            if (
                node.Kind == NodeKind::TemplateDeclaration ||
                    node.Kind == NodeKind::ExportDeclaration)
            {
                for (auto child = entry.FirstChild;
                    child != None;
                    child = Syntax_[child].NextSibling)
                {
                    entry.Definition =
                        entry.Definition || Syntax_[child].Definition;
                }
            }
            entry.Root = i;
            if (IsSpine(node.Kind) && entry.FirstChild != None) {
                entry.Root = Syntax_[entry.FirstChild].Root;
                // Qualified names belong to the root identity, not a fluent
                // member boundary. Keep their complete parsed token range.
                if (node.Kind == NodeKind::MemberExpression) {
                    auto second = Syntax_[entry.FirstChild].NextSibling;
                    if (second != None) {
                        const auto op =
                            Syntax_[entry.FirstChild].TreeNode->Tokens.End;
                        if (
                            op < node.Tokens.End &&
                                Tree_.Kind(op) ==
                                    TokenKind::Operator_ScopeResolution)
                        {
                            entry.Root = i;
                        }
                    }
                }
            }
            if (
                node.Kind == NodeKind::LiteralExpression &&
                    !node.Tokens.Empty())
            {
                entry.String = true;
                for (Index t = node.Tokens.Begin; t < node.Tokens.End; ++t) {
                    entry.String =
                        entry.String &&
                            (Tree_.Kind(t) == TokenKind::Literal_String ||
                                    Tree_.Kind(t) == TokenKind::Comment);
                }
            }
        }
    }

    Index GroupAt(
        Position begin,
        Position end,
        layout::GroupMode mode,
        int32_t indent = 0,
        std::optional<IndentationPolicy> anchor = {})
    {
        if (begin >= end) {
            return None;
        }
        const auto id = Index(Groups_.size());
        Groups_.push_back({begin,
                           end,
                           {mode, indent},
                           anchor.value_or(IndentationPolicy{
                               ContinuationAlignment::Block, 0, 0}),
                           anchor.has_value()});
        return id;
    }
    void Own(
        Index gap,
        Index owner,
        Boundary shape,
        layout::GapKind broken = layout::GapKind::Flat,
        bool closing = false)
    {
        if (gap >= Gaps_.size() || owner == None) {
            return;
        }
        auto& plan = Gaps_[gap];
        if (plan.Owner != None && plan.Owner != owner) {
            Fail(SyntaxLayoutError::ConflictingGapOwners, gap);
            return;
        }
        plan.Owner = owner;
        plan.Context =
            shape == Boundary::Preserve
                ? GapContext::Preserved
                : GapContext::Rebuilt;
        plan.ContextKnown = true;
        plan.Shape = shape;
        plan.Broken = broken;
        plan.Closing = closing;
    }
    bool Uncertain(Index gap) const
    {
        return (uint8_t(Spacing_.Gaps()[gap].Constraint) &
                    uint8_t(SpacingConstraint::UncertainSyntax)) != 0;
    }
    bool RetainsBareCR(Index gap) const
    {
        return Policies_.LineEnding != LineEnding::Preserve &&
            Safety_.Gaps()[gap].HasBareCR;
    }
    bool GapMutable(Index gap) const
    {
        return gap < Gaps_.size() && !RetainsBareCR(gap) && !Uncertain(gap) &&
            !Gaps_[gap].Frozen && !Safety_.Gaps()[gap].Preserve &&
            !Safety_.Gaps()[gap].ContextSensitive;
    }
    const ListPolicy* ListFor(NodeKind kind) const
    {
        switch (kind) {
            case NodeKind::ArgumentList:
                return &Policies_.Calls;
            case NodeKind::ParameterList:
                return &Policies_.Parameters;
            case NodeKind::InitializerList:
                return &Policies_.Initializers;
            case NodeKind::LambdaCaptureList:
                return &Policies_.Captures;
            case NodeKind::EnumeratorList:
                return &Policies_.Enumerators;
            case NodeKind::Condition:
            case NodeKind::ForHeader:
                return &Policies_.ControlHeaders;
            case NodeKind::BaseClause:
                return &Policies_.BaseLists.Items;
            case NodeKind::ConstructorInitializerList:
                return &Policies_.ConstructorLists.Items;
            default:
                return nullptr;
        }
    }
    bool PinFirst(Index id) const
    {
        const auto parent = Syntax_[id].Parent;
        if (
            parent == None ||
                Syntax_[parent].TreeNode->Kind != NodeKind::CallExpression)
        {
            return false;
        }
        auto callee = Syntax_[parent].FirstChild;
        if (callee == None) {
            return false;
        }
        if (Syntax_[callee].TreeNode->Kind == NodeKind::MemberExpression) {
            callee = LastChild_[callee];
        }
        const auto name = Syntax_[callee].TreeNode->Tokens;
        if (
            name.Size() != 1 ||
                !Annotations_.FindMessagePin(Tree_.TokenText(name.Begin)))
        {
            return false;
        }
        auto first = Syntax_[id].FirstChild;
        if (
            first == None ||
                Syntax_[first].TreeNode->Kind != NodeKind::Argument)
        {
            return false;
        }
        const auto expression = Syntax_[first].FirstChild;
        return expression != None && Syntax_[expression].String &&
            Syntax_[expression].TreeNode->Tokens.Begin ==
                Syntax_[first].TreeNode->Tokens.Begin &&
            Syntax_[expression].TreeNode->Tokens.End ==
                Syntax_[first].TreeNode->Tokens.End;
    }
    void PreserveUnplannedListContext(Index id)
    {
        const auto& entry = Syntax_[id];
        const auto& node = *entry.TreeNode;
        if (node.Tokens.Size() < 2) {
            return;
        }
        // These edges/separators belong to the absent list combinator, not
        // to its category's default mode. Leave independently classified child
        // properties and the flat-only callee/declarator prefix alone. Explicit
        // Space/NoSpace and inserted-adjacency context still apply normally.
        auto preserve = [&] (Index gap) {
            if (gap >= Gaps_.size()) {
                return;
            }
            const auto* owner = Spacing_.Gaps()[gap].Owner;
            if (
                (owner == &node ||
                            (entry.Parent != None &&
                                    owner == Syntax_[entry.Parent].TreeNode)) &&
                    !Gaps_[gap].ContextKnown)
            {
                Gaps_[gap].Context = GapContext::Preserved;
                Gaps_[gap].ContextKnown = true;
            }
        };
        const bool prefixed =
            node.Kind == NodeKind::BaseClause ||
                node.Kind == NodeKind::ConstructorInitializerList;
        preserve(node.Tokens.Begin + 1);
        if (prefixed) {
            preserve(node.Tokens.Begin);
        } else {
            preserve(node.Tokens.End - 1);
        }
        for (auto child = entry.FirstChild;
            child != None;
            child = Syntax_[child].NextSibling)
        {
            const auto& part = *Syntax_[child].TreeNode;
            if (
                part.Kind == NodeKind::ListSeparator ||
                    part.Kind == NodeKind::RangeForSeparator)
            {
                preserve(part.Tokens.Begin);
                preserve(part.Tokens.Begin + 1);
            }
        }
        if (node.Kind == NodeKind::ForHeader) {
            DirectTokens(
                id,
                [&] (Index token) {
                    if (Tree_.Kind(token) == TokenKind::Operator_Semicolon) {
                        preserve(token);
                        preserve(token + 1);
                    }
                });
        }
    }
    void PlanList(Index id, const ListPolicy& policy)
    {
        const auto& node = *Syntax_[id].TreeNode;
        if (node.Tokens.Size() < 2) {
            return;
        }
        const bool prefixed =
            node.Kind == NodeKind::BaseClause ||
                node.Kind == NodeKind::ConstructorInitializerList;
        const auto begin = node.Tokens.Begin;
        const auto end = node.Tokens.End;
        const auto closer = prefixed ? end : end - 1;
        const bool canonical = policy.Layout == LayoutMode::Canonical;
        const auto group =
            GroupAt(
                prefixed ? GapPosition(begin) : GapPosition(begin + 1),
                GapPosition(end),
                layout::GroupMode::Fill,
                int32_t(policy.Indent.ContinuationOffset),
                policy.Indent);
        if (group == None) {
            return;
        }
        Groups_[group].Spec.StructuralBase = true;
        const bool empty = !prefixed && node.Tokens.Size() == 2;
        Groups_[group].Spec.ForceBreak =
            canonical && !empty && policy.Break == BreakRequirement::Always;
        if (empty && canonical) {
            Own(closer, group, Boundary::Flat, layout::GapKind::Flat, true);
            return;
        }
        if (canonical) {
            // Fill is used even for vertical lists: a broken first-item Soft
            // boundary can stay inline, while conditional separator Hard gaps
            // couple every subsequent item to the list's actual shape.
            auto firstKind =
                policy.FirstItem == FirstItemPlacement::NextLine &&
                        !PinFirst(id)
                    ? layout::GapKind::Hard
                    : layout::GapKind::Soft;
            Own(begin + 1, group, Boundary::Conditional, firstKind);
            if (!prefixed) {
                Own(
                    closer,
                    group,
                    Boundary::Conditional,
                    policy.Closer == CloserPlacement::OwnLine
                        ? layout::GapKind::Hard
                        : layout::GapKind::Flat,
                    true);
            }
        } else {
            Own(begin + 1, group, Boundary::Preserve);
            Reindent(begin + 1, group);
            if (!prefixed) {
                Own(
                    closer,
                    group,
                    Boundary::Preserve,
                    layout::GapKind::Flat,
                    true);
                Reindent(closer, group, true);
            }
        }
        const auto* header =
            node.Kind == NodeKind::BaseClause
                ? &Policies_.BaseLists
                : &Policies_.ConstructorLists;
        if (
            prefixed &&
                policy.Indent.Alignment == ContinuationAlignment::Delimiter)
        {
            Groups_[group].PrefixGap = begin + 1;
            Groups_[group].AnchorAfterGap =
                canonical
                    ? header->Colon == OperatorSide::Leading
                    : !HasLine(Safety_.Gaps()[begin + 1].Original);
            Gaps_[begin + 1].MarkDelimiter = group;
        }
        if (prefixed && !canonical) {
            Own(begin, group, Boundary::Preserve);
            Reindent(begin, group);
        }
        if (prefixed && canonical) {
            // Colon placement is part of the enclosing header, but this prefix
            // owner begins before the colon so its conditional is self-owned.
            Groups_[group].Begin = GapPosition(begin);
            Own(
                begin,
                group,
                Boundary::Conditional,
                header->Colon == OperatorSide::Leading
                    ? firstKindFor(policy)
                    : layout::GapKind::Flat);
            Own(
                begin + 1,
                group,
                Boundary::Conditional,
                header->Colon == OperatorSide::Trailing
                    ? firstKindFor(policy)
                    : layout::GapKind::Flat);
        }
        Index lastItem = None;
        Index lastSeparator = None;
        for (auto child = Syntax_[id].FirstChild;
            child != None;
            child = Syntax_[child].NextSibling)
        {
            const auto& part = *Syntax_[child].TreeNode;
            const auto t = part.Tokens.Begin;
            const bool separator =
                part.Kind == NodeKind::ListSeparator ||
                    part.Kind == NodeKind::RangeForSeparator;
            if (!separator) {
                if (
                    part.Kind != NodeKind::Comment &&
                        part.Kind != NodeKind::PreprocessingDirective &&
                        part.Tokens.Begin > begin && part.Tokens.End <= closer)
                {
                    lastItem = child;
                }
                continue;
            }
            lastSeparator = t;
            const auto side = prefixed ? header->Comma : OperatorSide::Trailing;
            if (canonical) {
                const auto split = side == OperatorSide::Leading ? t : t + 1;
                if (split < closer) {
                    Own(
                        split,
                        group,
                        Boundary::Conditional,
                        policy.Packing == ListPacking::VerticalWhenBroken
                            ? layout::GapKind::Hard
                            : layout::GapKind::Soft);
                }
                Own(
                    side == OperatorSide::Leading ? t + 1 : t,
                    group,
                    Boundary::Flat);
            } else {
                Own(t, group, Boundary::Preserve);
                if (t + 1 < closer) {
                    Own(t + 1, group, Boundary::Preserve);
                    Reindent(t + 1, group);
                }
                Reindent(t, group);
            }
        }
        // Control headers represent their semicolons as direct punctuation,
        // not ListSeparator children. Walk only disjoint direct-token spans.
        if (node.Kind == NodeKind::ForHeader) {
            DirectTokens(
                id,
                [&] (Index t) {
                    if (
                        Tree_.Kind(t) == TokenKind::Operator_Semicolon &&
                            t + 1 < closer)
                    {
                        if (canonical) {
                            Own(t, group, Boundary::Flat);
                            Own(
                                t + 1,
                                group,
                                Boundary::Conditional,
                                policy.Packing ==
                                        ListPacking::VerticalWhenBroken
                                    ? layout::GapKind::Hard
                                    : layout::GapKind::Soft);
                        } else {
                            Reindent(t + 1, group);
                        }
                    }
                });
        }
        if (
            node.Kind == NodeKind::InitializerList && lastItem != None &&
                Policies_.InitializersTrailingComma !=
                    TrailingCommaPolicy::Preserve)
        {
            const auto& item = *Syntax_[lastItem].TreeNode;
            const auto after = item.Tokens.End;
            // A recovered list may have only a parsed prefix of its last
            // element (for example, a type name before an unparsed braced
            // expression). Only a complete element followed by comments and
            // an optional owned trailing comma authorizes punctuation edits.
            if (
                item.Kind != NodeKind::InitializerElement ||
                    item.Tokens.Empty())
            {
                return;
            }
            bool existing = false;
            for (auto t = after; t < closer; ++t) {
                if (Tree_.Kind(t) == TokenKind::Comment) {
                    continue;
                }
                if (
                    t == lastSeparator &&
                        Tree_.Kind(t) == TokenKind::Operator_Comma && !existing)
                {
                    existing = true;
                    continue;
                }
                return;
            }
            const auto insertion = Tree_.Token(after - 1).Text.End;
            if (!GapMutable(closer) ||
                    // A trailing comment can separate the closer gap from the
                // actual insertion point. Check before removing any comma.
                (Policies_.InitializersTrailingComma ==
                                TrailingCommaPolicy::WhenMultiline &&
                            Safety_
                                .IntersectsImmutable(
                                    {insertion, insertion})) ||
                    (existing &&
                            (Safety_
                                .IntersectsImmutable(
                                    {
                                        Tree_.Token(lastSeparator).Text.Begin,
                                        Tree_.Token(lastSeparator).Text.End,
                                    }) || !GapMutable(lastSeparator) ||
                                    !Safety_.CanReplaceGap(lastSeparator, "") ||
                                    (!canonical &&
                                            HasLine(
                                                Safety_.Gaps()[lastSeparator]
                                                    .Original)))))
            {
                // This atomic punctuation edit also suppresses its preceding
                // gap. Until merged-gap transfer is available, keep the comma
                // when doing so would lose a lexical line terminator or a
                // preserved break. Other layout/indent owners remain active.
                return;
            }
            if (existing) {
                Removed_[lastSeparator] = true;
                // The deleted comma's preceding whitespace must not survive as
                // an orphan. Its following closer gap retains the edge rule.
                Gaps_[lastSeparator].Owner = group;
                Gaps_[lastSeparator].Shape = Boundary::Flat;
            }
            if (
                Policies_.InitializersTrailingComma ==
                    TrailingCommaPolicy::WhenMultiline)
            {
                Commas_[after] ={group, existing ? lastSeparator : None};
            }
        }
    }
    static layout::GapKind firstKindFor(const ListPolicy& policy)
    {
        return policy.FirstItem == FirstItemPlacement::NextLine
            ? layout::GapKind::Hard
            : layout::GapKind::Soft;
    }
    template <class Function>
    void DirectTokens(Index id, Function function) const
    {
        const auto& node = *Syntax_[id].TreeNode;
        auto cursor = node.Tokens.Begin;
        for (auto child = Syntax_[id].FirstChild;
            child != None;
            child = Syntax_[child].NextSibling)
        {
            const auto range = Syntax_[child].TreeNode->Tokens;
            for (; cursor < std::min(range.Begin, node.Tokens.End); ++cursor) {
                function(cursor);
            }
            cursor = std::max(cursor, range.End);
        }
        for (; cursor < node.Tokens.End; ++cursor) {
            function(cursor);
        }
    }
    bool SameOperatorSequence(Index parent, Index child) const
    {
        const auto& a = Syntax_[parent];
        const auto& b = Syntax_[child];
        if (
            !a.Safe || !b.Safe || a.Operator == None || b.Operator == None ||
                a.TreeNode->Kind != b.TreeNode->Kind)
        {
            return false;
        }
        const auto outer =
            parser::GetBinaryOperatorInfo(Tree_.Kind(a.Operator));
        const auto inner =
            parser::GetBinaryOperatorInfo(Tree_.Kind(b.Operator));
        return outer.Precedence != 0 && outer.Precedence == inner.Precedence &&
            outer.RightAssociative == inner.RightAssociative &&
            child ==
                (outer.RightAssociative ? LastChild_[parent] : a.FirstChild);
    }
    void PlanExpression(Index id)
    {
        const auto& node = *Syntax_[id].TreeNode;
        const auto kind = node.Kind;
        const bool declarationAssignment =
            !node.Tokens.Empty() &&
                ((kind == NodeKind::Initializer &&
                                Tree_.Kind(node.Tokens.Begin) ==
                                    TokenKind::Operator_Assign) ||
                        (kind == NodeKind::Enumerator &&
                                LastChild_[id] != None &&
                                Syntax_[LastChild_[id]].TreeNode->Kind ==
                                    NodeKind::EnumeratorValue));
        if (
            !declarationAssignment && kind != NodeKind::BinaryExpression &&
                kind != NodeKind::AssignmentExpression &&
                kind != NodeKind::ConditionalExpression &&
                kind != NodeKind::ParenthesizedExpression &&
                kind != NodeKind::SubscriptExpression &&
                kind != NodeKind::CastExpression &&
                !(kind == NodeKind::LiteralExpression && Syntax_[id].String))
        {
            return;
        }
        if (node.Tokens.Size() < 2) {
            return;
        }
        const auto parent = Syntax_[id].Parent;
        if (parent != None && SameOperatorSequence(parent, id)) {
            return;
        }
        const auto& policy = Policies_.Expressions;
        const bool canonical = policy.Layout == LayoutMode::Canonical;
        const bool threePart =
            kind == NodeKind::ConditionalExpression &&
                policy.TernaryShape == TernaryLayout::ThreePart;
        const auto group =
            GroupAt(
                kind == NodeKind::Initializer
                    ? GapPosition(node.Tokens.Begin)
                    : TokenPosition(node.Tokens.Begin),
                GapPosition(node.Tokens.End),
                threePart
                    ? layout::GroupMode::Consistent
                    : layout::GroupMode::Fill,
                int32_t(policy.Indent.ContinuationOffset),
                policy.Indent);
        Groups_[group].Spec.ExpressionContinuation = true;
        Groups_[group].Spec.ForceBreak =
            canonical && policy.Break == BreakRequirement::Always &&
                (declarationAssignment || kind == NodeKind::BinaryExpression ||
                        kind == NodeKind::AssignmentExpression ||
                        kind == NodeKind::ConditionalExpression ||
                        Syntax_[id].String);
        const auto boundary =
            policy.Packing == ListPacking::VerticalWhenBroken && !threePart
                ? Boundary::Conditional
                : Boundary::Soft;
        const auto planToken = [&] (Index t) {
            const auto role =
                declarationAssignment &&
                        Tree_.Kind(t) == TokenKind::Operator_Assign
                    ? PunctuationRole::AssignmentOperator
                    : Tree_.Role(t);
            if (
                canonical &&
                    (kind == NodeKind::ParenthesizedExpression ||
                            kind == NodeKind::SubscriptExpression ||
                            kind == NodeKind::CastExpression) &&
                    Tree_.Kind(t) != TokenKind::Comment)
            {
                if (t > node.Tokens.Begin) {
                    Own(t, group, Boundary::Flat);
                }
                if (t + 1 < node.Tokens.End) {
                    Own(t + 1, group, Boundary::Flat);
                }
            }
            const bool op =
                role == PunctuationRole::BinaryOperator ||
                    role == PunctuationRole::ComparisonOperator ||
                    role == PunctuationRole::ShiftOperator ||
                    role == PunctuationRole::AssignmentOperator ||
                    role == PunctuationRole::ConditionalDelimiter;
            if (
                op && (t > node.Tokens.Begin || declarationAssignment) &&
                    t + 1 < node.Tokens.End)
            {
                const auto side =
                    role == PunctuationRole::AssignmentOperator
                        ? policy.Assignment
                        : role == PunctuationRole::ShiftOperator
                            ? policy.Shift
                            : role == PunctuationRole::ConditionalDelimiter
                                ? policy.Ternary
                                : policy.Binary;
                if (canonical) {
                    Own(
                        side == OperatorSide::Leading ? t : t + 1,
                        group,
                        boundary,
                        layout::GapKind::Hard);
                    Own(
                        side == OperatorSide::Leading ? t + 1 : t,
                        group,
                        Boundary::Flat);
                } else {
                    Own(t, group, Boundary::Preserve);
                    Own(t + 1, group, Boundary::Preserve);
                    Reindent(t, group);
                    Reindent(t + 1, group);
                }
            }
            if (
                Syntax_[id].String && t > node.Tokens.Begin &&
                    Tree_.Kind(t) == TokenKind::Literal_String)
            {
                if (canonical) {
                    Own(t, group, boundary, layout::GapKind::Hard);
                } else {
                    Reindent(t, group);
                }
            }
        };
        auto segment = id;
        while (true) {
            DirectTokens(segment, planToken);
            if (Syntax_[segment].Operator == None) {
                break;
            }
            const auto info =
                parser::GetBinaryOperatorInfo(
                    Tree_.Kind(Syntax_[segment].Operator));
            const auto next =
                info.RightAssociative
                    ? LastChild_[segment]
                    : Syntax_[segment].FirstChild;
            if (next == None || !SameOperatorSequence(segment, next)) {
                break;
            }
            segment = next;
        }
    }
    void Reindent(
        Index gap,
        Index owner,
        bool closing = false,
        bool force = false)
    {
        if (
            (force || Policies_.IndentationKind != IndentationKind::Preserve) &&
                HasLine(Safety_.Gaps()[gap].Original))
        {
            Own(gap, owner, Boundary::Preserve, layout::GapKind::Flat, closing);
            Gaps_[gap].Reindent = true;
        }
    }
    std::optional<BracePlacement> BraceFor(Index id) const
    {
        const auto& node = *Syntax_[id].TreeNode;
        switch (node.Kind) {
            case NodeKind::FunctionDefinition:
            case NodeKind::FunctionTryBlock:
                return Policies_.Braces.Function;
            case NodeKind::NamespaceDeclaration:
                return Policies_.Braces.Namespace;
            case NodeKind::EnumDeclaration:
                return Policies_.Braces.Enum;
            case NodeKind::RecordDeclaration:
                if (Tree_.Kind(node.Tokens.Begin) == TokenKind::Keyword_struct)
                {
                    return Policies_.Braces.Struct;
                }
                if (Tree_.Kind(node.Tokens.Begin) == TokenKind::Keyword_union) {
                    return Policies_.Braces.Union;
                }
                return Policies_.Braces.Class;
            case NodeKind::LambdaExpression:
                return Policies_.Braces.Lambda;
            default:
                if (IsControl(node.Kind) || node.Kind == NodeKind::TryStatement)
                {
                    return node.Kind == NodeKind::CaseLabel
                        ? Policies_.Braces.Case
                        : Policies_.Braces.Control;
                }
                return std::nullopt;
        }
    }
    static bool TransparentHeader(NodeKind kind)
    {
        switch (kind) {
            case NodeKind::FunctionDeclaration:
            case NodeKind::DeclarationSpecifiers:
            case NodeKind::InitDeclaratorList:
            case NodeKind::InitDeclarator:
            case NodeKind::Declarator:
            case NodeKind::AbstractDeclarator:
            case NodeKind::DeclaratorName:
            case NodeKind::DestructorName:
            case NodeKind::OperatorFunctionName:
            case NodeKind::PointerOperator:
            case NodeKind::MemberPointerOperator:
            case NodeKind::NestedDeclarator:
            case NodeKind::FunctionDeclarator:
            case NodeKind::FunctionQualifier:
            case NodeKind::ExceptionSpecification:
            case NodeKind::TrailingReturnType:
            case NodeKind::TypeId:
            case NodeKind::CallingConventionSpecifier:
                return true;
            default:
                return false;
        }
    }
    void ProjectHeaderGaps(Index id, Index begin, Index end, Index group)
    {
        Stack_.clear();
        Stack_.push_back(id);
        while (!Stack_.empty()) {
            const auto current = Stack_.back();
            Stack_.pop_back();
            DirectTokens(
                current,
                [&] (Index t) {
                    const auto& node = *Syntax_[current].TreeNode;
                    const bool opaqueExceptionOperand =
                        node.Kind == NodeKind::ExceptionSpecification &&
                            t > node.Tokens.Begin + 1;
                    if (
                        !opaqueExceptionOperand && t > begin && t < end &&
                            GapMutable(t) &&
                            Tree_.Kind(t) != TokenKind::Comment &&
                            (Spacing_.Gaps()[t].Category ==
                                        SpacingOwner::DeclarationHeaders ||
                                    Spacing_.Gaps()[t].Category ==
                                        SpacingOwner::ControlHeaders))
                    {
                        Own(t, group, Boundary::Flat);
                        Gaps_[t].CanonicalFlat = true;
                    }
                });
            for (auto child = Syntax_[current].FirstChild;
                child != None;
                child = Syntax_[child].NextSibling)
            {
                const auto& node = *Syntax_[child].TreeNode;
                if (
                    !Syntax_[child].Safe || node.Tokens.End <= begin ||
                        node.Tokens.Begin >= end)
                {
                    continue;
                }
                if (TransparentHeader(node.Kind)) {
                    Stack_.push_back(child);
                } else if (
                    (node.Kind == NodeKind::ParameterList ||
                                node.Kind == NodeKind::Condition ||
                                node.Kind == NodeKind::ForHeader) &&
                        node.Tokens.Begin > begin &&
                        GapMutable(node.Tokens.Begin))
                {
                    // Its opener belongs to the declarator; the independently
                    // selected parameter topology starts after that opener.
                    Own(node.Tokens.Begin, group, Boundary::Flat);
                    Gaps_[node.Tokens.Begin].CanonicalFlat = true;
                }
            }
        }
    }
    static bool ControlledBody(NodeKind kind)
    {
        switch (kind) {
            case NodeKind::CompoundStatement:
            case NodeKind::DeclarationStatement:
            case NodeKind::ExpressionStatement:
            case NodeKind::ReturnStatement:
            case NodeKind::BreakStatement:
            case NodeKind::ContinueStatement:
            case NodeKind::GotoStatement:
            case NodeKind::ThrowStatement:
            case NodeKind::CoYieldStatement:
            case NodeKind::IfStatement:
            case NodeKind::ForStatement:
            case NodeKind::WhileStatement:
            case NodeKind::DoWhileStatement:
            case NodeKind::SwitchStatement:
            case NodeKind::TryStatement:
                return true;
            default:
                return false;
        }
    }
    void PlanControlledArms(Index id, BracePlacement brace)
    {
        const auto& node = *Syntax_[id].TreeNode;
        Index previousEnd = node.Tokens.Begin;
        Index previousBody = None;
        bool first = true;
        Index compactSeparators = None;
        for (auto child = Syntax_[id].FirstChild;
            child != None;
            child = Syntax_[child].NextSibling)
        {
            const auto& body = *Syntax_[child].TreeNode;
            const bool suffix =
                node.Kind == NodeKind::DoWhileStatement &&
                    previousBody != None && body.Kind == NodeKind::Condition;
            if ((!ControlledBody(body.Kind) && !suffix) || body.Tokens.Empty())
            {
                continue;
            }
            const auto precedingBody = previousBody;
            Index begin = node.Tokens.Begin;
            if (!first) {
                begin = None;
                auto part = Syntax_[previousBody].NextSibling;
                const auto end = std::min(body.Tokens.Begin, node.Tokens.End);
                for (Index t = previousEnd; t < end;) {
                    while (
                        part != None && part != child &&
                            Syntax_[part].TreeNode->Tokens.End <= t)
                    {
                        part = Syntax_[part].NextSibling;
                    }
                    if (
                        part != None && part != child &&
                            Syntax_[part].TreeNode->Tokens.Begin <= t)
                    {
                        // A descendant's else belongs to that child, not this
                        // arm. Half-open ranges retain a direct else at End.
                        t = std::min(Syntax_[part].TreeNode->Tokens.End, end);
                        part = Syntax_[part].NextSibling;
                        continue;
                    }
                    if (
                        Tree_.Kind(t) ==
                            (suffix
                                    ? TokenKind::Keyword_while
                                    : TokenKind::Keyword_else))
                    {
                        begin = t;
                        break;
                    }
                    ++t;
                }
            }
            const bool elseIf = !first && body.Kind == NodeKind::IfStatement;
            first = false;
            if (!suffix) {
                previousEnd = body.Tokens.End;
                previousBody = child;
            }
            if (begin == None || begin >= body.Tokens.Begin) {
                continue;
            }
            const bool braced = body.Kind == NodeKind::CompoundStatement;
            const auto opening = body.Tokens.Begin;
            bool safeHeader =
                PrefixProtection_[begin] ==
                        PrefixProtection_[opening + (braced ? 1 : 0)] &&
                    (!suffix || Syntax_[child].Safe);
            for (auto part = Syntax_[id].FirstChild;
                part != None;
                part = Syntax_[part].NextSibling)
            {
                const auto range = Syntax_[part].TreeNode->Tokens;
                if (
                    range.Begin >= begin && range.End <= opening &&
                        !Syntax_[part].Safe)
                {
                    safeHeader = false;
                }
            }
            const bool canonical =
                safeHeader &&
                    Policies_.ControlHeaders.Layout == LayoutMode::Canonical;
            if (begin != node.Tokens.Begin && GapMutable(begin)) {
                const auto& preceding = *Syntax_[precedingBody].TreeNode;
                // Only an adjacent, editable direct-parent boundary may join.
                // Trivia children and protected headers retain their topology.
                const bool adjacent =
                    preceding.Tokens.End == begin &&
                        Tree_.Kind(begin - 1) != TokenKind::Comment;
                const bool precedingBraced =
                    preceding.Kind == NodeKind::CompoundStatement;
                Boundary boundary = Boundary::Preserve;
                if (canonical && adjacent) {
                    if (precedingBraced) {
                        boundary = Boundary::Flat;
                    } else if (Policies_.Bodies.Layout == LayoutMode::Canonical)
                    {
                        boundary =
                            Policies_.Bodies.Shape == BodyShape::Expanded
                                ? Boundary::Hard
                                : Boundary::Soft;
                    }
                }
                Index prefix;
                if (boundary == Boundary::Soft) {
                    if (compactSeparators == None) {
                        compactSeparators =
                            GroupAt(
                                TokenPosition(node.Tokens.Begin),
                                GapPosition(node.Tokens.End),
                                layout::GroupMode::Consistent);
                        Groups_[compactSeparators].Spec.StructuralBase = true;
                    }
                    prefix = compactSeparators;
                } else {
                    prefix =
                        GroupAt(
                            GapPosition(begin),
                            TokenPosition(begin),
                            layout::GroupMode::Consistent);
                }
                Own(begin, prefix, boundary);
                if (boundary == Boundary::Preserve) {
                    Reindent(begin, prefix);
                } else {
                    Gaps_[begin].CanonicalFlat = true;
                    Gaps_[begin].ControlSeparator = true;
                }
            }
            // A do-while Condition is the real direct suffix, not another arm.
            // Opening brace placement has no authority over this header.
            if (suffix) {
                const auto header =
                    GroupAt(
                        TokenPosition(begin),
                        GapPosition(node.Tokens.End),
                        layout::GroupMode::Fill);
                Groups_[header].Spec.StructuralBase = true;
                if (canonical) {
                    ProjectHeaderGaps(id, begin, node.Tokens.End, header);
                }
                continue;
            }
            const auto header =
                GroupAt(
                    TokenPosition(begin),
                    braced
                        ? GapPosition(opening + 1)
                        : elseIf
                            ? TokenPosition(opening)
                            : GapPosition(opening),
                    layout::GroupMode::Fill);
            Groups_[header].Spec.StructuralBase = true;
            if (canonical) {
                ProjectHeaderGaps(id, begin, opening, header);
            }
            if (braced) {
                Own(
                    opening,
                    header,
                    !canonical
                        ? Boundary::Preserve
                        : brace == BracePlacement::NextLine
                            ? Boundary::Hard
                            : brace == BracePlacement::Attached
                                ? Boundary::Flat
                                : Boundary::Conditional,
                    layout::GapKind::Hard,
                    true);
                if (!canonical) {
                    Reindent(opening, header, true);
                }
            } else if (elseIf) {
                Own(
                    opening,
                    header,
                    canonical ? Boundary::Flat : Boundary::Preserve);
            } else {
                const auto group =
                    GroupAt(
                        GapPosition(opening),
                        GapPosition(body.Tokens.End),
                        layout::GroupMode::Consistent,
                        int32_t(Policies_.Bodies.IndentWidth));
                Groups_[group].Spec.StructuralBase = true;
                if (Policies_.Bodies.Layout == LayoutMode::Canonical) {
                    Own(
                        opening,
                        group,
                        Policies_.Bodies.Shape == BodyShape::Expanded
                            ? Boundary::Hard
                            : Boundary::Soft);
                } else {
                    Reindent(opening, group);
                }
            }
        }
    }
    void PlanHeaderAndBody(Index id)
    {
        const auto& node = *Syntax_[id].TreeNode;
        if (
            node.Kind == NodeKind::CompoundStatement ||
                node.Kind == NodeKind::LambdaBody)
        {
            const auto parent = Syntax_[id].Parent;
            const bool lambda =
                node.Kind == NodeKind::LambdaBody ||
                    (parent != None &&
                            Syntax_[parent].TreeNode->Kind ==
                                NodeKind::LambdaExpression);
            const auto previous = Syntax_[id].PreviousSibling;
            const bool labeled =
                previous != None &&
                    (Syntax_[previous].TreeNode->Kind == NodeKind::CaseLabel ||
                            Syntax_[previous].TreeNode->Kind ==
                                NodeKind::LabelStatement) &&
                    Syntax_[previous].TreeNode->Tokens.End == node.Tokens.Begin;
            if (
                !lambda && !labeled &&
                    (parent == None ||
                            (!BraceFor(parent) &&
                                    Syntax_[parent].TreeNode->Kind !=
                                        NodeKind::FunctionTryBlock)) &&
                    Policies_.Indentation.StandaloneBraces.Mode ==
                        StandaloneBlockBraceIndentation::Indent)
            {
                const auto scope =
                    GroupAt(
                        GapPosition(node.Tokens.Begin),
                        GapPosition(node.Tokens.End),
                        layout::GroupMode::Consistent,
                        Policies_.Indentation.StandaloneBraces.Offset);
                auto& gap = Gaps_[node.Tokens.Begin];
                gap.Owner = scope;
                gap.Reindent = true;
            }
            PlanBody(
                id,
                node.Tokens.Begin,
                node.Tokens.End - 1,
                lambda ? Policies_.LambdaBodies : Policies_.Bodies,
                false);
            return;
        }
        // The parser stores a function-try body in a separate child. Its
        // enclosing definition owns the complete declaration header once.
        if (
            node.Kind == NodeKind::FunctionTryBlock &&
                Syntax_[id].Parent != None &&
                Syntax_[Syntax_[id].Parent].TreeNode->Kind ==
                    NodeKind::FunctionDefinition)
        {
            return;
        }
        const auto brace = BraceFor(id);
        if (!brace || node.Tokens.Empty()) {
            return;
        }
        if (IsControl(node.Kind) && node.Kind != NodeKind::CaseLabel) {
            PlanControlledArms(id, *brace);
            return;
        }
        Index opening = None;
        // For nodes which store their body delimiters directly (records and
        // namespaces), direct-token spans locate the brace in one traversal.
        DirectTokens(
            id,
            [&] (Index t) {
                if (
                    opening == None &&
                        Tree_.Kind(t) == TokenKind::Operator_OpenBrace &&
                        (node.Kind == NodeKind::NamespaceDeclaration ||
                                Tree_.Role(t) ==
                                    PunctuationRole::RecordDelimiter ||
                                Tree_.Role(t) ==
                                    PunctuationRole::BlockDelimiter ||
                                Tree_.Role(t) ==
                                    PunctuationRole::LambdaDelimiter))
                {
                    opening = t;
                }
            });
        bool bodyChild = false;
        for (auto child = Syntax_[id].FirstChild;
            child != None;
            child = Syntax_[child].NextSibling)
        {
            const auto& part = *Syntax_[child].TreeNode;
            if (
                part.Kind == NodeKind::FunctionTryBlock &&
                    node.Kind == NodeKind::FunctionDefinition)
            {
                const auto body = Syntax_[child].FirstChild;
                if (
                    body != None &&
                        Syntax_[body].TreeNode->Kind ==
                            NodeKind::CompoundStatement)
                {
                    opening = Syntax_[body].TreeNode->Tokens.Begin;
                    bodyChild = true;
                    break;
                }
            }
            if (
                part.Kind == NodeKind::CompoundStatement ||
                    part.Kind == NodeKind::LambdaBody ||
                    (node.Kind == NodeKind::EnumDeclaration &&
                            part.Kind == NodeKind::EnumeratorList))
            {
                opening = part.Tokens.Begin;
                bodyChild = true;
                break;
            }
        }
        if (
            opening == None && node.Kind == NodeKind::CaseLabel &&
                Syntax_[id].NextSibling != None)
        {
            const auto next = Syntax_[id].NextSibling;
            const auto& body = *Syntax_[next].TreeNode;
            if (
                body.Kind == NodeKind::CompoundStatement &&
                    Syntax_[next].Safe && node.Tokens.End == body.Tokens.Begin)
            {
                opening = body.Tokens.Begin;
                bodyChild = true;
                const auto parent = Syntax_[id].Parent;
                if (parent != None && Gaps_[opening].Owner == BlockOf_[parent])
                {
                    Gaps_[opening].Owner = None;
                }
            }
        }
        if (opening == None || opening <= node.Tokens.Begin) {
            return;
        }
        const bool control =
            IsControl(node.Kind) || node.Kind == NodeKind::TryStatement;
        bool safeHeader =
            PrefixProtection_[node.Tokens.Begin] ==
                PrefixProtection_[opening + 1];
        for (auto child = Syntax_[id].FirstChild;
            child != None;
            child = Syntax_[child].NextSibling)
        {
            if (
                Syntax_[child].TreeNode->Tokens.End <= opening &&
                    !Syntax_[child].Safe)
            {
                safeHeader = false;
            }
        }
        const bool canonical =
            safeHeader &&
                (control
                            ? Policies_.ControlHeaders.Layout
                            : Policies_.DeclarationHeaders) ==
                    LayoutMode::Canonical;
        const auto group =
            GroupAt(
                TokenPosition(node.Tokens.Begin),
                GapPosition(opening + 1),
                layout::GroupMode::Fill);
        Groups_[group].Spec.StructuralBase = true;
        if (canonical) {
            Own(
                opening,
                group,
                *brace == BracePlacement::NextLine
                    ? Boundary::Hard
                    : *brace == BracePlacement::Attached
                        ? Boundary::Flat
                        : Boundary::Conditional,
                layout::GapKind::Hard,
                true);
            ProjectHeaderGaps(id, node.Tokens.Begin, opening, group);
        }
        if (!canonical) {
            Own(
                opening,
                group,
                Boundary::Preserve,
                layout::GapKind::Flat,
                true);
            Reindent(opening, group, true);
        }
        if (!bodyChild) {
            const auto close = Tree_.MatchingDelimiter(opening);
            if (close != None && close < node.Tokens.End) {
                PlanBody(
                    id,
                    opening,
                    close,
                    Policies_.Bodies,
                    node.Kind == NodeKind::RecordDeclaration);
            }
        }
    }
    int32_t BodyOffset(const BodyIndentControl& control, int32_t fallback) const
    {
        if (control.Mode == BodyIndentation::NoIndent) {
            return 0;
        }
        if (control.Mode == BodyIndentation::Indent) {
            return control.Offset;
        }
        return fallback;
    }

    void PlanBody(
        Index id,
        Index opening,
        Index close,
        const BodyPolicy& policy,
        bool record)
    {
        if (
            opening >= close || close >= Tree_.TokenCount() ||
                Tree_.Kind(opening) != TokenKind::Operator_OpenBrace ||
                Tree_.Kind(close) != TokenKind::Operator_CloseBrace ||
                Tree_.MatchingDelimiter(opening) != close)
        {
            return;
        }
        const bool canonical = policy.Layout == LayoutMode::Canonical;
        const bool namespaced =
            Syntax_[id].TreeNode->Kind == NodeKind::NamespaceDeclaration;
        int32_t offset = record ? BodyOffset(Policies_.Indentation.Members,
                                             int32_t(policy.IndentWidth))
                                : int32_t(policy.IndentWidth);
        if (namespaced) {
            offset = BodyOffset(Policies_.Indentation.Namespaces, 0);
            if (
                Policies_.Indentation.Namespaces.Mode ==
                    BodyIndentation::Preserve)
            {
                const auto outer =
                    SourceIndent(Syntax_[id].TreeNode->Tokens.Begin);
                const auto inner = SourceIndent(opening + 1);
                if (
                    inner != None && inner >= (outer == None ? 0 : outer) &&
                        inner - (outer == None ? 0 : outer) <=
                            uint32_t(INT32_MAX))
                {
                    offset = int32_t(inner - (outer == None ? 0 : outer));
                }
            }
        }
        const auto previous = Syntax_[id].PreviousSibling;
        const bool caseBody =
            previous != None &&
                Syntax_[previous].TreeNode->Kind == NodeKind::CaseLabel &&
                Syntax_[previous].TreeNode->Tokens.End == opening;
        if (
            caseBody &&
                Policies_.Indentation.CaseLabels.Mode !=
                    BodyIndentation::Preserve)
        {
            // Explicit case indentation also reindents the compound opener
            // to its structural base, as its closing brace already does.
            Gaps_[opening].Reindent = true;
        }
        const auto shape =
            opening + 1 == close ? policy.EmptyShape : policy.Shape;
        const bool independentIndent =
            record
                ? Policies_.Indentation.Members.Mode !=
                    BodyIndentation::Preserve
                : namespaced &&
                    Policies_.Indentation.Namespaces.Mode !=
                        BodyIndentation::Preserve;
        const auto group =
            GroupAt(GapPosition(opening + 1), GapPosition(close + 1),
                    layout::GroupMode::Consistent, offset,
                    IndentationPolicy{ContinuationAlignment::Block,
                                      size_t(offset), size_t(offset)});
        Groups_[group].Spec.StructuralBase = true;
        BlockOf_[id] = group;
        if (canonical) {
            const bool expanded = shape == BodyShape::Expanded;
            Own(opening + 1, group, expanded ? Boundary::Hard : Boundary::Soft);
            // Explicit body controls demand indentation independently of
            // topology and the general IndentationKind::Preserve policy.
            Gaps_[opening + 1].Reindent |= independentIndent;
            Own(
                close,
                group,
                expanded ? Boundary::Hard : Boundary::Soft,
                layout::GapKind::Flat,
                true);
        } else {
            Own(opening + 1, group, Boundary::Preserve);
            Own(close, group, Boundary::Preserve, layout::GapKind::Flat, true);
            Reindent(opening + 1, group, false, independentIndent);
            Reindent(close, group, true);
        }
        bool inCase = false;
        Index contents = None;
        for (auto child = Syntax_[id].FirstChild;
            child != None;
            child = Syntax_[child].NextSibling)
        {
            const auto& part = *Syntax_[child].TreeNode;
            if (
                part.Tokens.Empty() || part.Tokens.Begin <= opening ||
                    part.Tokens.End > close)
            {
                continue;
            }
            // Labels delimit direct-child case regions. A real compound body
            // supplies its own level; statements after it start another region.
            if (part.Kind == NodeKind::CaseLabel) {
                inCase = true;
                contents = None;
            } else if (part.Kind == NodeKind::CompoundStatement) {
                contents = None;
            } else if (inCase) {
                if (contents == None) {
                    contents =
                        GroupAt(
                            GapPosition(part.Tokens.Begin),
                            GapPosition(part.Tokens.End),
                            layout::GroupMode::Consistent,
                            int32_t(policy.IndentWidth));
                    Groups_[contents].Spec.StructuralBase = true;
                } else {
                    Groups_[contents].End = GapPosition(part.Tokens.End);
                }
            }
            const auto owner = contents == None ? group : contents;
            if (!Syntax_[child].Safe) {
                continue;
            }
            if (canonical) {
                Own(
                    part.Tokens.Begin,
                    owner,
                    shape == BodyShape::Expanded
                        ? Boundary::Hard
                        : Boundary::Soft);
                Gaps_[part.Tokens.Begin].Reindent |= independentIndent;
            } else {
                Own(part.Tokens.Begin, owner, Boundary::Preserve);
                Reindent(part.Tokens.Begin, owner, false, independentIndent);
            }
            // Indentation controls are structural subgroups, not a text pass.
            const auto special =
                part.Kind == NodeKind::AccessSpecifier
                    ? &Policies_.Indentation.AccessModifiers
                    : part.Kind == NodeKind::CaseLabel
                        ? &Policies_.Indentation.CaseLabels
                        : nullptr;
            if (
                part.Kind == NodeKind::CaseLabel && special &&
                    special->Mode == BodyIndentation::Preserve &&
                    SourceIndent(part.Tokens.Begin) != None)
            {
                // Preserve the source indentation, not the body's topology.
                auto& gap = Gaps_[part.Tokens.Begin];
                gap.PreserveIndent = true;
                gap.Reindent = false;
            }
            if (special && special->Mode != BodyIndentation::Preserve) {
                const auto target = BodyOffset(*special, offset);
                auto& gap = Gaps_[part.Tokens.Begin];
                gap.OverrideIndent = true;
                gap.ExtraIndent = target - offset;
                gap.Reindent = true;
            }
        }
    }
    void PlanAttribute(Index id)
    {
        const auto& node = *Syntax_[id].TreeNode;
        const auto& control = Policies_.Indentation.StatementAttributes;
        if (
            node.Kind != NodeKind::AttributeSpecifier || node.Tokens.Empty() ||
                control.Mode == StatementAttributeIndent::Preserve)
        {
            return;
        }
        const auto parent = Syntax_[id].Parent;
        if (parent == None) {
            return;
        }
        const auto kind = Syntax_[parent].TreeNode->Kind;
        auto block = None;
        if (kind == NodeKind::DeclarationSpecifiers) {
            const auto declaration = Syntax_[parent].Parent;
            if (
                declaration == None ||
                    Syntax_[declaration].TreeNode->Kind !=
                        NodeKind::DeclarationStatement ||
                    Syntax_[declaration].TreeNode->Tokens.Begin !=
                        node.Tokens.Begin)
            {
                return;
            }
            const auto blockParent = Syntax_[declaration].Parent;
            if (
                blockParent == None ||
                    (Syntax_[blockParent].TreeNode->Kind !=
                                NodeKind::CompoundStatement &&
                            Syntax_[blockParent].TreeNode->Kind !=
                                NodeKind::LambdaBody))
            {
                return;
            }
            block = BlockOf_[blockParent];
        } else if (!IsControl(kind)) {
            return;
        }
        auto& gap = Gaps_[node.Tokens.Begin];
        gap.OverrideIndent = true;
        gap.ExtraIndent =
            control.Mode == StatementAttributeIndent::Indent
                ? control.Offset
                : 0;
        if (block != None && gap.Owner != None && gap.Owner != block) {
            // Attribute controls are relative to the ordinary block level,
            // independently of the additional case-content body group.
            gap.ExtraIndent -= Groups_[gap.Owner].Spec.Indent;
        }
        gap.Reindent = true;
    }
    void PlanChain(Index id)
    {
        const auto& entry = Syntax_[id];
        if (
            !IsSpine(entry.TreeNode->Kind) ||
                Policies_.Chains.Layout != LayoutMode::Canonical)
        {
            return;
        }
        if (
            entry.Parent != None &&
                IsSpine(Syntax_[entry.Parent].TreeNode->Kind) &&
                Syntax_[entry.Parent].FirstChild == id)
        {
            return; // Only the maximal callee/member spine owns link gaps.
        }
        const auto root = Syntax_[entry.Root].TreeNode->Tokens;
        if (Annotations_.Chains() == ChainParticipation::AnnotatedRoots) {
            // One reusable scratch spelling per maximal spine, never per-token
            // strings. Qualified identity ignores source whitespace and uses
            // the annotation snapshot's sorted lookup rather than a name scan.
            RootName_.clear();
            for (Index t = root.Begin; t < root.End; ++t) {
                RootName_.append(Tree_.TokenText(t));
            }
            if (!Annotations_.IsAnnotatedChainRoot(RootName_)) {
                return;
            }
        }
        Index links = 0;
        for (Index spine = id; spine != entry.Root;) {
            const auto callee = Syntax_[spine].FirstChild;
            if (callee == None) {
                break;
            }
            const auto& part = *Syntax_[spine].TreeNode;
            const auto t = Syntax_[callee].TreeNode->Tokens.End;
            if (
                t < part.Tokens.End &&
                    ((part.Kind == NodeKind::MemberExpression &&
                                    Tree_.Kind(t) !=
                                        TokenKind::Operator_ScopeResolution) ||
                            (part.Kind == NodeKind::CallExpression &&
                                    Syntax_[callee].TreeNode->Kind ==
                                        NodeKind::CallExpression)))
            {
                ++links;
            }
            spine = callee;
        }
        if (!links) {
            return;
        }
        const auto& policy = Policies_.Chains;
        const auto group =
            GroupAt(
                TokenPosition(entry.TreeNode->Tokens.Begin),
                GapPosition(entry.TreeNode->Tokens.End),
                layout::GroupMode::Fill,
                int32_t(policy.Indent.ContinuationOffset),
                policy.Indent);
        // A chain owns continuation independently of pending expression
        // offsets, but retains enclosing list and realized-line indentation.
        Groups_[group].Spec.StructuralBase = true;
        Groups_[group].Spec.ForceBreak =
            policy.Break == BreakRequirement::Always;
        for (Index spine = id; spine != entry.Root;) {
            const auto callee = Syntax_[spine].FirstChild;
            if (callee == None) {
                break;
            }
            const auto& part = *Syntax_[spine].TreeNode;
            const auto t = Syntax_[callee].TreeNode->Tokens.End;
            if (
                t < part.Tokens.End &&
                    ((part.Kind == NodeKind::MemberExpression &&
                                    Tree_.Kind(t) !=
                                        TokenKind::Operator_ScopeResolution) ||
                            (part.Kind == NodeKind::CallExpression &&
                                    Syntax_[callee].TreeNode->Kind ==
                                        NodeKind::CallExpression)))
            {
                Own(
                    t,
                    group,
                    Boundary::Conditional,
                    policy.Packing == ListPacking::VerticalWhenBroken
                        ? layout::GapKind::Hard
                        : layout::GapKind::Soft);
            }
            spine = callee;
        }
    }
    struct Endpoint
    {
        Index Statement;
        Index Name;
        Index Pair;
        bool Begin;
    };
    enum class Discovery : uint8_t { None, Unique, Ambiguous };
    struct DiscoveredEndpoint
    {
        Discovery Kind = Discovery::None;
        Endpoint Value = {};
    };
    DiscoveredEndpoint ScopeEndpoint(Index statement)
    {
        if (
            !Syntax_[statement].Safe ||
                Syntax_[statement].TreeNode->Kind !=
                    NodeKind::ExpressionStatement)
        {
            return {};
        }
        DiscoveredEndpoint result;
        Stack_.clear();
        if (Syntax_[statement].FirstChild != None) {
            Stack_.push_back(Syntax_[statement].FirstChild);
        }
        while (!Stack_.empty()) {
            const auto expression = Stack_.back();
            Stack_.pop_back();
            const auto& node = *Syntax_[expression].TreeNode;
            if (!Syntax_[expression].Safe) {
                continue;
            }
            if (
                node.Kind == NodeKind::Argument ||
                    node.Kind == NodeKind::ParenthesizedExpression)
            {
                for (auto child = Syntax_[expression].FirstChild;
                    child != None;
                    child = Syntax_[child].NextSibling)
                {
                    Stack_.push_back(child);
                }
                continue;
            }
            // Only complete call wrappers and their arguments are transparent.
            // Receivers, lambdas, unary/unevaluated and other expression forms
            // are execution/context barriers, not wrapper search subtrees.
            if (node.Kind != NodeKind::CallExpression) {
                continue;
            }
            const auto callee = Syntax_[expression].FirstChild;
            if (
                callee == None ||
                    Syntax_[callee].TreeNode->Kind == NodeKind::Expression ||
                    Syntax_[callee].TreeNode->Kind ==
                        NodeKind::LambdaExpression ||
                    Syntax_[callee].TreeNode->Kind ==
                        NodeKind::StatementExpression)
            {
                continue;
            }
            auto name = callee;
            if (Syntax_[callee].TreeNode->Kind == NodeKind::MemberExpression) {
                const auto receiver = Syntax_[callee].FirstChild;
                name = receiver == None ? None : Syntax_[receiver].NextSibling;
            }
            if (name != None && Syntax_[name].TreeNode->Tokens.Size() == 1) {
                const auto token = Syntax_[name].TreeNode->Tokens.Begin;
                const auto spelling = Tree_.TokenText(token);
                const auto found =
                    std::lower_bound(
                        ScopeNames_.begin(),
                        ScopeNames_.end(),
                        spelling,
                        [] (
                            const ScopeName& item,
                            std::string_view key) { return item.Name < key; });
                if (found != ScopeNames_.end() && found->Name == spelling) {
                    if (result.Kind != Discovery::None) {
                        return {Discovery::Ambiguous, {}};
                    }
                    result =
                        {
                            Discovery::Unique,
                            {statement, token, found->Pair, found->Begin},
                        };
                }
            }
            const auto args = Syntax_[callee].NextSibling;
            if (
                args == None ||
                    Syntax_[args].TreeNode->Kind != NodeKind::ArgumentList)
            {
                continue;
            }
            for (auto arg = Syntax_[args].FirstChild;
                arg != None;
                arg = Syntax_[arg].NextSibling)
            {
                if (Syntax_[arg].TreeNode->Kind == NodeKind::Argument) {
                    Stack_.push_back(arg);
                }
            }
        }
        return result;
    }
    bool SameContext(const Endpoint& begin, const Endpoint& end) const
    {
        const auto a = Syntax_[begin.Statement].TreeNode->Tokens;
        const auto b = Syntax_[end.Statement].TreeNode->Tokens;
        if (a.Size() != b.Size() || begin.Name - a.Begin != end.Name - b.Begin)
        {
            return false;
        }
        for (Index i = 0; i < a.Size(); ++i) {
            if (a.Begin + i == begin.Name) {
                continue;
            }
            if (
                Tree_.Kind(a.Begin + i) != Tree_.Kind(b.Begin + i) ||
                    Tree_.TokenText(a.Begin + i) !=
                        Tree_.TokenText(b.Begin + i))
            {
                return false;
            }
        }
        return true;
    }
    Index SourceIndent(Index token) const
    {
        const auto& gap = Safety_.Gaps()[token];
        const auto lf = gap.Original.rfind('\n');
        if (lf == std::string_view::npos) {
            return None;
        }
        Index column = 0;
        for (const char c : gap.Original.substr(lf + 1)) {
            if (c == '\t') {
                column =
                    AddWidth(
                        column,
                        Options_.TabWidth - column % Options_.TabWidth);
            } else if (c == ' ') {
                column = AddWidth(column, 1);
            } else {
                return None;
            }
        }
        return column;
    }

    void AnalyzeVisualScopes()
    {
        if (Annotations_.VisualScopes().empty()) {
            return;
        }
        const auto annotations = Annotations_.VisualScopes();
        ScopeNames_.reserve(annotations.size() * 2);
        for (Index i = 0; i < annotations.size(); ++i) {
            ScopeNames_.push_back({annotations[i].Begin, i, true});
            ScopeNames_.push_back({annotations[i].End, i, false});
        }
        std::sort(
            ScopeNames_.begin(),
            ScopeNames_.end(),
            [] (
                const ScopeName& a,
                const ScopeName& b) { return a.Name < b.Name; });
        std::pmr::vector<Endpoint> pairs(Syntax_.get_allocator().resource());
        std::pmr::vector<int64_t> frozen(Tree_.TokenCount() + size_t(2), 0,
                                         Syntax_.get_allocator().resource());
        const auto freeze = [&] (Index begin, Index end) {
            if (begin < end) {
                ++frozen[begin];
                --frozen[end];
            }
        };
        for (Index id = 0; id < Syntax_.size(); ++id) {
            const auto kind = Syntax_[id].TreeNode->Kind;
            if (
                kind != NodeKind::CompoundStatement &&
                    kind != NodeKind::LambdaBody)
            {
                continue;
            }
            pairs.clear();
            for (auto child = Syntax_[id].FirstChild;
                child != None;
                child = Syntax_[child].NextSibling)
            {
                if (
                    !Syntax_[child].Safe ||
                        Syntax_[child].TreeNode->Kind ==
                            NodeKind::PreprocessingDirective ||
                        Syntax_[child].TreeNode->Kind ==
                            NodeKind::PreprocessingConditional ||
                        Syntax_[child].TreeNode->Kind == NodeKind::CaseLabel ||
                        Syntax_[child].TreeNode->Kind ==
                            NodeKind::LabelStatement)
                {
                    if (!pairs.empty()) {
                        freeze(
                            Syntax_[pairs.front().Statement].TreeNode->Tokens
                                .Begin,
                            Syntax_[id].TreeNode->Tokens.End - 1);
                    }
                    pairs.clear();
                    continue;
                }
                const auto discovered = ScopeEndpoint(child);
                if (discovered.Kind == Discovery::Ambiguous) {
                    // Like an unmatched begin, uncertainty is owned by this
                    // compound's remaining region, not the file. Do not erase
                    // existing annotated indentation or infer a pairing.
                    freeze(
                        pairs.empty()
                            ? Syntax_[child].TreeNode->Tokens.Begin
                            : Syntax_[pairs.front().Statement].TreeNode->Tokens
                                .Begin,
                        Syntax_[id].TreeNode->Tokens.End - 1);
                    pairs.clear();
                    continue;
                }
                if (discovered.Kind == Discovery::None) {
                    continue;
                }
                const auto* endpoint = &discovered.Value;
                if (endpoint->Begin) {
                    pairs.push_back(*endpoint);
                    continue;
                }
                if (
                    pairs.empty() || pairs.back().Pair != endpoint->Pair ||
                        !SameContext(pairs.back(), *endpoint))
                {
                    freeze(
                        pairs.empty()
                            ? Syntax_[id].TreeNode->Tokens.Begin + 1
                            : Syntax_[pairs.front().Statement].TreeNode->Tokens
                                .Begin,
                        Syntax_[child].TreeNode->Tokens.End);
                    pairs.clear();
                    continue;
                }
                const auto begin = pairs.back();
                pairs.pop_back();
                const auto first = Syntax_[begin.Statement].NextSibling;
                if (first == child || first == None) {
                    continue;
                }
                const auto beginIndent =
                    SourceIndent(
                        Syntax_[begin.Statement].TreeNode->Tokens.Begin);
                const auto interiorIndent =
                    SourceIndent(Syntax_[first].TreeNode->Tokens.Begin);
                if (
                    beginIndent == None || interiorIndent == None ||
                        interiorIndent <= beginIndent)
                {
                    continue; // Flat nested pairs never invent an extra level.
                }
                Visual_.push_back({first, child});
            }
            if (!pairs.empty()) {
                freeze(
                    Syntax_[pairs.front().Statement].TreeNode->Tokens.Begin,
                    Syntax_[id].TreeNode->Tokens.End - 1);
            }
        }
        int64_t level = 0;
        for (Index i = 0; i < Tree_.TokenCount(); ++i) {
            level += frozen[i];
            Gaps_[i].Frozen = level != 0;
            if (level != 0) {
                ++PrefixProtection_[i + 1];
            }
        }
        // Rebuild the prefix, retaining actual protection and annotation
        // islands.
        for (Index i = 0, sum = 0; i < Tree_.TokenCount(); ++i) {
            const auto token = Tree_.Token(i).Text;
            PrefixProtection_[i] = sum;
            sum +=
                Gaps_[i].Frozen || ContextProtected({token.Begin, token.End});
            PrefixProtection_[i + 1] = sum;
        }
        for (auto& entry : Syntax_) {
            const auto range = entry.TreeNode->Tokens;
            entry.Safe =
                entry.Safe &&
                    PrefixProtection_[range.Begin] ==
                        PrefixProtection_[range.End];
        }
    }
    void PlanVisualScopes()
    {
        if (Policies_.IndentationKind == IndentationKind::Preserve) {
            return;
        }
        for (const auto range : Visual_) {
            const auto beginToken = Syntax_[range.First].TreeNode->Tokens.Begin;
            const auto endToken = Syntax_[range.End].TreeNode->Tokens.Begin;
            if (PrefixProtection_[beginToken] != PrefixProtection_[endToken]) {
                continue;
            }
            const auto owner = BlockOf_[Syntax_[range.First].Parent];
            const auto group =
                GroupAt(
                    GapPosition(beginToken),
                    GapPosition(endToken),
                    layout::GroupMode::Consistent,
                    int32_t(Policies_.IndentWidth));
            for (auto interior = range.First;
                interior != range.End;
                interior = Syntax_[interior].NextSibling)
            {
                auto& gap = Gaps_[Syntax_[interior].TreeNode->Tokens.Begin];
                if (gap.Owner == owner) {
                    gap.Owner = group;
                    gap.Reindent = true;
                }
            }
        }
    }
    static bool DeclarationItem(NodeKind kind)
    {
        switch (kind) {
            case NodeKind::EmptyDeclaration:
            case NodeKind::Declaration:
            case NodeKind::FunctionDeclaration:
            case NodeKind::FunctionDefinition:
            case NodeKind::FunctionTryBlock:
            case NodeKind::NamespaceDeclaration:
            case NodeKind::RecordDeclaration:
            case NodeKind::EnumDeclaration:
            case NodeKind::TemplateDeclaration:
            case NodeKind::UsingDeclaration:
            case NodeKind::TypedefDeclaration:
            case NodeKind::StaticAssertDeclaration:
            case NodeKind::ConceptDeclaration:
            case NodeKind::ModuleDeclaration:
            case NodeKind::ImportDeclaration:
            case NodeKind::ExportDeclaration:
            case NodeKind::AsmDeclaration:
            case NodeKind::LinkageSpecification:
                return true;
            default:
                return false;
        }
    }
    void ExactVertical(
        Index gap,
        VerticalGapPolicy policy,
        uint8_t priority = 0)
    {
        if (
            policy.Mode != VerticalGapMode::Exact || !GapMutable(gap) ||
                Gaps_[gap].VerticalPriority > priority)
        {
            return;
        }
        auto& plan = Gaps_[gap];
        plan.PreserveIndent |=
            plan.Shape == Boundary::Preserve && !plan.Reindent &&
                !plan.OverrideIndent;
        if (!plan.ContextKnown) {
            plan.ContextKnown = true;
            plan.Context = GapContext::Preserved;
        }
        plan.Vertical = true;
        plan.VerticalPriority = priority;
        plan.NewlineCount = uint16_t(policy.EmptyLines + 1);
        if (policy.EmptyLines != 0) {
            plan.Shape = Boundary::Hard;
        }
    }
    void PlanStandaloneCommentGaps(Index id)
    {
        if (!Syntax_[id].ItemSequence) {
            return;
        }
        const auto& policy = Policies_.VerticalGaps;
        for (auto child = Syntax_[id].FirstChild; child != None;) {
            const auto& entry = Syntax_[child];
            if (
                entry.TreeNode->Kind != NodeKind::Comment || !entry.Safe ||
                    entry.TreeNode->Tokens.Begin <
                        Syntax_[id].SequenceBody.Begin ||
                    entry.TreeNode->Tokens.End > Syntax_[id].SequenceBody.End)
            {
                child = entry.NextSibling;
                continue;
            }
            auto begin = entry.TreeNode->Tokens.Begin;
            auto end = entry.TreeNode->Tokens.End;
            child = entry.NextSibling;
            while (
                child != None && Syntax_[child].Safe &&
                    Syntax_[child].TreeNode->Kind == NodeKind::Comment &&
                    Syntax_[child].TreeNode->Tokens.Begin == end &&
                    Syntax_[child].TreeNode->Tokens.End <=
                        Syntax_[id].SequenceBody.End && !Gaps_[end].Frozen &&
                    !ContextProtected(Safety_.Gaps()[end].Source))
            {
                end = Syntax_[child].TreeNode->Tokens.End;
                child = Syntax_[child].NextSibling;
            }
            // Trim comments sharing a physical line with noncomment syntax.
            // Internal comment newlines are token bytes, never owned gaps.
            bool line = begin == 0 || HasLine(Safety_.Gaps()[begin].Original);
            auto first = end;
            for (auto token = begin; token < end; ++token) {
                line = line || HasLine(Safety_.Gaps()[token].Original);
                if (line && first == end) {
                    first = token;
                }
                line = line || HasLine(Tree_.TokenText(token));
            }
            line =
                end == Tree_.TokenCount() ||
                    HasLine(Safety_.Gaps()[end].Original);
            auto last = begin;
            for (auto token = end; token-- > begin;) {
                line = line || HasLine(Safety_.Gaps()[token + 1].Original);
                if (line && last == begin) {
                    last = token + 1;
                }
                line = line || HasLine(Tree_.TokenText(token));
            }
            if (first >= last) {
                continue;
            }
            if (first != 0) {
                ExactVertical(
                    first,
                    policy.StandaloneCommentBlankLinesBefore,
                    1);
            }
            if (last != Tree_.TokenCount()) {
                ExactVertical(last, policy.StandaloneCommentBlankLinesAfter, 2);
            }
        }
    }

    void PlanVerticalGaps()
    {
        const auto& policy = Policies_.VerticalGaps;
        const bool commentRules =
            policy.StandaloneCommentBlankLinesBefore.Mode ==
                    VerticalGapMode::Exact ||
                policy.StandaloneCommentBlankLinesAfter.Mode ==
                    VerticalGapMode::Exact;
        const bool sequenceRules =
            commentRules ||
                policy.DefinitionBlankLines.Mode == VerticalGapMode::Exact ||
                policy.MemberDefinitionBlankLines.Mode ==
                    VerticalGapMode::Exact ||
                policy.NamespaceBlankLinesAfterOpen.Mode ==
                    VerticalGapMode::Exact ||
                policy.NamespaceBlankLinesBeforeClose.Mode ==
                    VerticalGapMode::Exact ||
                policy.AccessSectionBlankLinesBefore.Mode ==
                    VerticalGapMode::Exact ||
                policy.AccessSectionBlankLinesAfterLabel.Mode ==
                    VerticalGapMode::Exact;
        if (sequenceRules) {
            std::pmr::vector<Index> children(
                Syntax_.get_allocator().resource());
            std::pmr::vector<Index> following(
                Syntax_.get_allocator().resource());
            for (Index id = 0; id < Syntax_.size(); ++id) {
                const auto& node = *Syntax_[id].TreeNode;
                if (commentRules) {
                    if (
                        node.Kind == NodeKind::PreprocessingConditional ||
                            node.Kind == NodeKind::PreprocessingBranch)
                    {
                        auto& entry = Syntax_[id];
                        const auto parent = entry.Parent;
                        entry.ItemSequence =
                            parent != None && Syntax_[parent].ItemSequence;
                        if (entry.ItemSequence) {
                            entry.SequenceBody.Begin =
                                std::max(
                                    entry.SequenceBody.Begin,
                                    Syntax_[parent].SequenceBody.Begin);
                            entry.SequenceBody.End =
                                std::min(
                                    entry.SequenceBody.End,
                                    Syntax_[parent].SequenceBody.End);
                        }
                    }
                    PlanStandaloneCommentGaps(id);
                }
                if (
                    node.Kind != NodeKind::TranslationUnit &&
                        node.Kind != NodeKind::NamespaceDeclaration &&
                        node.Kind != NodeKind::RecordDeclaration)
                {
                    continue;
                }
                children.clear();
                for (auto child = Syntax_[id].FirstChild;
                    child != None;
                    child = Syntax_[child].NextSibling)
                {
                    children.push_back(child);
                }
                following.resize(children.size());
                Index nextItem = None;
                for (size_t p = children.size(); p-- > 0;)
                {
                    following[p] = nextItem;
                    if (
                        Syntax_[children[p]].TreeNode->Kind !=
                            NodeKind::Comment)
                    {
                        nextItem =
                            DeclarationItem(Syntax_[children[p]].TreeNode->Kind)
                                ? children[p]
                                : None;
                    }
                }
                const auto body = BlockOf_[id];
                const auto opening =
                    body == None ? None : Index(Groups_[body].Begin / 2) - 1;
                const auto close =
                    body == None ? None : Index(Groups_[body].End / 2) - 1;
                Index previous = None;
                Index access = None;
                bool seenAccess = false;
                bool content = false;
                for (size_t p = 0; p < children.size(); ++p)
                {
                    const auto child = children[p];
                    const auto& entry = Syntax_[child];
                    const auto& part = *entry.TreeNode;
                    if (
                        body != None &&
                            (part.Tokens.Begin <= opening ||
                                    part.Tokens.End > close))
                    {
                        continue;
                    }
                    if (
                        !content &&
                            node.Kind == NodeKind::NamespaceDeclaration &&
                            !part.Tokens.Empty())
                    {
                        ExactVertical(
                            entry.Attached.Begin,
                            policy.NamespaceBlankLinesAfterOpen);
                    }
                    content = content || !part.Tokens.Empty();
                    if (
                        !entry.Safe ||
                            part.Kind == NodeKind::PreprocessingDirective ||
                            part.Kind == NodeKind::PreprocessingConditional)
                    {
                        previous = access = None;
                        continue;
                    }
                    if (part.Kind == NodeKind::Comment) {
                        // Attached trivia is not a new sequence item. A
                        // genuinely unattached comment is a barrier, never
                        // moved to invent declaration adjacency or an
                        // access-content boundary.
                        const auto preceding =
                            access != None ? access : previous;
                        const bool trailing =
                            preceding != None &&
                                part.Tokens.End <=
                                    Syntax_[preceding].Attached.End;
                        const bool leading =
                            following[p] != None &&
                                part.Tokens.Begin >=
                                    Syntax_[following[p]].Attached.Begin;
                        if (!trailing && !leading) {
                            previous = access = None;
                        }
                        continue;
                    }
                    if (
                        part.Kind == NodeKind::AccessSpecifier &&
                            node.Kind == NodeKind::RecordDeclaration)
                    {
                        if (seenAccess) {
                            ExactVertical(
                                entry.Attached.Begin,
                                policy.AccessSectionBlankLinesBefore);
                        }
                        seenAccess = true;
                        access = child;
                        previous = None;
                        continue;
                    }
                    if (!DeclarationItem(part.Kind)) {
                        previous = access = None;
                        continue;
                    }
                    if (access != None) {
                        ExactVertical(
                            entry.Attached.Begin,
                            policy.AccessSectionBlankLinesAfterLabel);
                        access = None;
                    }
                    if (
                        previous != None &&
                            (entry.Definition || Syntax_[previous].Definition))
                    {
                        ExactVertical(
                            entry.Attached.Begin,
                            node.Kind == NodeKind::RecordDeclaration
                                ? policy.MemberDefinitionBlankLines
                                : policy.DefinitionBlankLines);
                    }
                    previous = child;
                }
                if (
                    content && node.Kind == NodeKind::NamespaceDeclaration &&
                        close != None)
                {
                    ExactVertical(close, policy.NamespaceBlankLinesBeforeClose);
                }
            }
        }
        if (policy.MaxEmptyLines.Enabled) {
            for (Index i = 0; i < Gaps_.size(); ++i) {
                if (!GapMutable(i) || Gaps_[i].Vertical) {
                    continue;
                }
                const auto lines =
                    std::count(
                        Safety_.Gaps()[i].Original.begin(),
                        Safety_.Gaps()[i].Original.end(),
                        '\n');
                if (lines > policy.MaxEmptyLines.EmptyLines + 1) {
                    auto& gap = Gaps_[i];
                    gap.PreserveIndent |=
                        gap.Shape == Boundary::Preserve && !gap.Reindent &&
                            !gap.OverrideIndent;
                    if (!gap.ContextKnown) {
                        gap.ContextKnown = true;
                        gap.Context = GapContext::Preserved;
                    }
                    gap.Vertical = true;
                    gap.NewlineCount =
                        uint16_t(policy.MaxEmptyLines.EmptyLines + 1);
                }
            }
        }
    }
    void ResolveSafety()
    {
        for (Index i = 0; i < Gaps_.size(); ++i) {
            auto& gap = Gaps_[i];
            if (RetainsBareCR(i)) {
                // Retain source topology before summaries or emission; do not
                // freeze safe atom spelling or invent an owned break.
                gap.Shape = Boundary::Preserve;
                gap.Reindent = false;
                gap.Vertical = false;
                gap.PartialIndent = false;
                gap.CanonicalFlat = false;
                gap.PreserveIndent = true;
                if (gap.MarkDelimiter != None) {
                    Groups_[gap.MarkDelimiter].AnchorAfterGap = true;
                }
                continue;
            }
            gap.PartialIndent =
                gap.Reindent ||
                    (!gap.PreserveIndent &&
                            Policies_.IndentationKind !=
                                IndentationKind::Preserve &&
                            (gap.Shape == Boundary::Hard ||
                                    gap.Shape == Boundary::Soft ||
                                    (gap.Shape == Boundary::Conditional &&
                                            gap.Broken !=
                                                layout::GapKind::Flat)));
            if (gap.Shape == Boundary::Preserve) {
                continue;
            }
            const bool flat = Allows(i, layout::GapKind::Flat);
            const bool line = Allows(i, layout::GapKind::Hard);
            if (!flat && !line) {
                gap.Shape = Boundary::Preserve;
            } else if (!flat) {
                gap.Shape = Boundary::Hard;
            } else if (!line) {
                gap.Shape = Boundary::Flat;
            }
        }
    }
    void SummarizeSegments()
    {
        // One reverse pass computes next indivisible segment plus mandatory
        // same-line suffix. Delimiter/semicolon/comma suffixes contribute their
        // actual token widths; optional commas contribute one independent of
        // source comma presence. A future break boundary terminates a segment.
        Index width = 0;
        for (Index i = Tree_.TokenCount(); i-- > 0;) {
            if (!Removed_[i]) {
                const auto tokenWidth =
                    HorizontalColumns(Tree_.TokenText(i), Options_.TabWidth);
                width = AddWidth(tokenWidth, width);
            }
            if (Commas_[i + 1].Owner != None) {
                width = AddWidth(width, 1);
            }
            Gaps_[i].RequiredWidth = width;
            const auto& gap = Gaps_[i];
            const bool boundary =
                gap.Shape == Boundary::Soft || gap.Shape == Boundary::Hard ||
                    (gap.Shape == Boundary::Conditional &&
                            gap.Broken != layout::GapKind::Flat) ||
                    (gap.Shape == Boundary::Preserve &&
                            HasLine(Safety_.Gaps()[i].Original));
            if (boundary) {
                width = 0;
            } else {
                width = AddWidth(width, FlatWidth(i));
            }
        }
    }
    GapContext ContextFor(Index i) const
    {
        const auto& gap = Gaps_[i];
        if (gap.Generated) {
            return GapContext::Rebuilt;
        }
        if (gap.ContextKnown) {
            return gap.Context;
        }
        return SpacingOwnerContext(Spacing_.Gaps()[i].Category, Policies_);
    }
    bool RebuildsTopology(Index i) const
    {
        const auto& gap = Gaps_[i];
        return gap.CanonicalFlat || gap.Shape == Boundary::Soft ||
            gap.Shape == Boundary::Hard ||
            (gap.Shape == Boundary::Conditional &&
                    gap.Broken != layout::GapKind::Flat);
    }
    Index FlatWidth(Index i) const
    {
        if (Removed_.size() > i && Removed_[i]) {
            return 0;
        }
        if (RetainsBareCR(i)) {
            return HorizontalColumns(
                Safety_.Gaps()[i].Original,
                Options_.TabWidth);
        }
        if (Gaps_[i].ControlSeparator) {
            return 1;
        }
        if (const auto count = Spacing_.TrailingCommentCount(i, Safety_)) {
            return *count;
        }
        const auto preference =
            Spacing_.SpacePreference(i, ContextFor(i), Safety_);
        if (preference) {
            return *preference ? 1 : 0;
        }
        const auto& source = Safety_.Gaps()[i];
        if (!RebuildsTopology(i) && !HasLine(source.Original)) {
            return HorizontalColumns(source.Original, Options_.TabWidth);
        }
        return Spacing_.Gaps()[i].FallbackSpace ||
                source.Separation == GapSeparation::Required
            ? 1
            : 0;
    }

    void SortForest()
    {
        Order_.resize(Groups_.size());
        std::iota(Order_.begin(), Order_.end(), 0);
        std::sort(
            Order_.begin(),
            Order_.end(),
            [&] (Index a, Index b) {
                const auto& x = Groups_[a];
                const auto& y = Groups_[b];
                if (x.Begin != y.Begin) {
                    return x.Begin < y.Begin;
                }
                if (x.End != y.End) {
                    return x.End > y.End;
                }
                return a < b;
            });
        Stack_.clear();
        for (const auto id : Order_) {
            auto& group = Groups_[id];
            while (!Stack_.empty() && Groups_[Stack_.back()].End <= group.Begin)
            {
                Stack_.pop_back();
            }
            if (!Stack_.empty() && Groups_[Stack_.back()].End < group.End) {
                Fail(SyntaxLayoutError::CrossingGroups, Index(group.Begin / 2));
                return;
            }
            const auto parent =
                Stack_.empty()
                    ? IndentBasis{0, false}
                    : IndentBasis{Groups_[Stack_.back()].LogicalIndent,
                                  Groups_[Stack_.back()].SaturatedIndent};
            const auto basis = AccumulateIndent(parent, group.Spec.Indent);
            group.LogicalIndent = basis.Column;
            group.SaturatedIndent = basis.Saturated;
            if (group.PrefixGap != None && group.SaturatedIndent) {
                // Rebased fallback cancellation is exact only before core's
                // saturating indentation bound, including every ancestor.
                // Reject even if later negative offsets return below the bound;
                // do not claim equivalence or guess a block anchor.
                Fail(SyntaxLayoutError::IndentationOverflow, group.PrefixGap);
                return;
            }
            Stack_.push_back(id);
        }
        Stack_.clear();
    }

    layout::GapSpec Spec(
        Index i,
        layout::GapKind kind,
        bool sourceHorizontal = true) const
    {
        layout::GapSpec spec;
        spec.Kind = kind;
        spec.Spaces = uint16_t(std::min<Index>(FlatWidth(i), UINT16_MAX));
        spec.RequiredWidth = Gaps_[i].RequiredWidth;
        spec.NewlineCount = Gaps_[i].NewlineCount;
        if (Gaps_[i].PreserveIndent && !Gaps_[i].Reindent) {
            const auto& source = Safety_.Gaps()[i];
            const auto lf = source.Original.rfind('\n');
            if (
                lf != std::string_view::npos &&
                    source.Original.substr(lf + 1).find_first_not_of(" \t") ==
                        std::string_view::npos)
            {
                spec.IndentSourceBegin = source.Source.Begin + Index(lf + 1);
                spec.IndentSourceEnd = source.Source.End;
            }
        }
        if (Safety_.Gaps()[i].MustStartWithLineFeed) {
            spec.LineEnding = layout::GapNewline::LF;
        }
        const auto context = ContextFor(i);
        if (
            sourceHorizontal && !RebuildsTopology(i) &&
                Safety_.Gaps()[i].Original.find_first_not_of(" \t") ==
                    std::string_view::npos &&
                !Spacing_.TrailingCommentCount(i, Safety_) &&
                !Spacing_.SpacePreference(i, context, Safety_))
        {
            spec.FlatSourceBegin = Safety_.Gaps()[i].Source.Begin;
            spec.FlatSourceEnd = Safety_.Gaps()[i].Source.End;
        }
        return spec;
    }
    layout::Provenance GapOrigin(Index i) const
    {
        const auto range = Safety_.Gaps()[i].Source;
        return {range.Begin, range.End, layout::NoIdentity};
    }
    bool Allows(Index i, layout::GapKind kind) const
    {
        const auto& gap = Safety_.Gaps()[i];
        if (
            Gaps_[i].Frozen || Uncertain(i) || gap.Preserve ||
                gap.ContextSensitive || RetainsBareCR(i))
        {
            return false;
        }
        if (kind == layout::GapKind::Flat) {
            const bool space = FlatWidth(i) != 0;
            return Safety_.CanReplaceGap(i, space ? " " : "");
        }
        const auto line =
            gap.MustStartWithLineFeed ||
                    Options_.LineEnding == layout::Newline::LF
                ? "\n"
                : "\r\n";
        return Safety_.CanReplaceGap(i, line);
    }
    bool ExpressionGap(Index i) const
    {
        const auto owner = Gaps_[i].Owner;
        return owner != None && Groups_[owner].Spec.ExpressionContinuation &&
            !Stack_.empty() && Stack_.back() == owner;
    }
    void RecordFixedExpressionLine(Index i, Index begin, Index end)
    {
        // Protection retains bytes, not the fact that a safe current-expression
        // whitespace line realized continuation. Tokens never enter this path.
        if (
            Gaps_[i].Frozen || Uncertain(i) ||
                Safety_.Gaps()[i].ContextSensitive || !ExpressionGap(i))
        {
            return;
        }
        const auto text = Tree_.Source().substr(begin, end - begin);
        if (
            HasLine(text) &&
                text.find_first_not_of(" \t\r\n") == std::string_view::npos)
        {
            Document_.ExpressionLine();
        }
    }
    void FixedGapSlice(Index i, Index begin, Index end)
    {
        Document_.SourceText(begin, end, layout::NoIdentity, true);
        RecordFixedExpressionLine(i, begin, end);
    }
    void EditableGapSlice(Index i, Index begin, Index end)
    {
        if (Policies_.LineEnding == LineEnding::Preserve || begin == end) {
            FixedGapSlice(i, begin, end);
            return;
        }
        // A CR belonging to the previous token and this gap's first LF form
        // one ineligible atom. The existing line-comment LF guard also prevents
        // inserting CR into that token on retokenization. No atom is split.
        if (
            begin == Safety_.Gaps()[i].Source.Begin && begin &&
                Tree_.Source()[begin] == '\n' &&
                (Tree_.Source()[begin - 1] == '\r' ||
                        (Safety_.Gaps()[i].MustStartWithLineFeed &&
                                Policies_.LineEnding == LineEnding::CRLF)))
        {
            FixedGapSlice(i, begin, begin + 1);
            ++begin;
        }
        if (begin < end) {
            Document_.SourceWhitespace(begin, end, Options_.LineEnding);
            RecordFixedExpressionLine(i, begin, end);
        }
    }
    void PreservedSlice(
        Index i,
        Index begin,
        Index end,
        bool trim,
        bool trimTail = false)
    {
        if (!trim) {
            EditableGapSlice(i, begin, end);
            return;
        }
        const auto source = Tree_.Source();
        auto run = begin;
        for (auto cursor = begin; cursor < end; ++cursor) {
            if (source[cursor] != '\n') {
                continue;
            }
            auto newline = cursor;
            if (newline > run && source[newline - 1] == '\r') {
                --newline;
            }
            auto space = newline;
            while (
                space > run &&
                    (source[space - 1] == ' ' || source[space - 1] == '\t'))
            {
                --space;
            }
            if (
                RetainsBareCR(i) && space < newline &&
                    space > Safety_.Gaps()[i].Source.Begin &&
                    source[space - 1] == '\r')
                space = newline; // Keep the complete original separator run.
            EditableGapSlice(i, run, space);
            run = newline;
        }
        if (trimTail) {
            const auto originalEnd = end;
            while (
                end > run &&
                    (source[end - 1] == ' ' || source[end - 1] == '\t'))
            {
                --end;
            }
            if (
                RetainsBareCR(i) && end < originalEnd &&
                    end > Safety_.Gaps()[i].Source.Begin &&
                    source[end - 1] == '\r')
            {
                end = originalEnd;
            }
        }
        EditableGapSlice(i, run, end);
    }
    void PreserveGap(Index i)
    {
        const auto& source = Safety_.Gaps()[i];
        const auto& plan = Gaps_[i];
        if (plan.Frozen || Uncertain(i) || source.ContextSensitive) {
            FixedGapSlice(i, source.Source.Begin, source.Source.End);
            return;
        }
        if (source.Preserve) {
            // Full existing bytes need no authority to insert indentation at
            // End/EOF. This grants spelling only; generic gap guards stay put.
            if (
                Policies_.LineEnding != LineEnding::Preserve &&
                    Safety_.CanSpellGapNewlines(i))
            {
                EditableGapSlice(i, source.Source.Begin, source.Source.End);
            } else {
                FixedGapSlice(i, source.Source.Begin, source.Source.End);
            }
            return;
        }
        if (RetainsBareCR(i)) {
            PreservedSlice(
                i,
                source.Source.Begin,
                source.Source.End,
                Policies_.TrailingWhitespace == TrailingWhitespace::Trim &&
                    Safety_.CanReplaceGap(i, source.Original),
                i == Tree_.TokenCount());
            return;
        }
        const auto lf = source.Original.rfind('\n');
        if (lf != std::string_view::npos) {
            if (plan.Vertical && Allows(i, layout::GapKind::Hard)) {
                auto anchor =
                    plan.Owner == None
                        ? layout::InvalidId
                        : plan.Closing
                            ? Groups_[plan.Owner].Block
                            : Groups_[plan.Owner].Anchor;
                if (plan.OverrideIndent) {
                    anchor =
                        Document_
                            .MarkAnchor(
                                {
                                    layout::AnchorKind::BlockRelative,
                                    plan.ExtraIndent,
                                });
                }
                Document_
                    .Gap(Spec(i, layout::GapKind::Hard), anchor, GapOrigin(i));
            } else if (plan.Reindent && Allows(i, layout::GapKind::Hard)) {
                const bool crlf = lf > 0 && source.Original[lf - 1] == '\r';
                const auto newlineBegin =
                    source.Source.Begin + Index(lf) - (crlf ? 1 : 0);
                PreservedSlice(
                    i,
                    source.Source.Begin,
                    newlineBegin,
                    Policies_.TrailingWhitespace == TrailingWhitespace::Trim);
                auto spec = Spec(i, layout::GapKind::Hard, false);
                if (Policies_.LineEnding == LineEnding::Preserve) {
                    spec.LineEnding =
                        crlf
                            ? layout::GapNewline::CRLF
                            : layout::GapNewline::LF;
                } else if (!source.MustStartWithLineFeed || lf != 0) {
                    spec.LineEnding = layout::GapNewline::Inherit;
                }
                auto anchor =
                    plan.Owner == None
                        ? layout::InvalidId
                        : plan.Closing
                            ? Groups_[plan.Owner].Block
                            : Groups_[plan.Owner].Anchor;
                if (plan.OverrideIndent) {
                    anchor =
                        Document_
                            .MarkAnchor(
                                {
                                    layout::AnchorKind::BlockRelative,
                                    plan.ExtraIndent,
                                });
                }
                Document_
                    .Gap(
                        spec,
                        anchor,
                        {newlineBegin, source.Source.End, layout::NoIdentity});
            } else {
                PreservedSlice(
                    i,
                    source.Source.Begin,
                    source.Source.End,
                    Policies_.TrailingWhitespace == TrailingWhitespace::Trim &&
                        Safety_.CanReplaceGap(i, source.Original),
                    i == Tree_.TokenCount());
            }
            return;
        }
        if (
            i == Tree_.TokenCount() &&
                Policies_.TrailingWhitespace == TrailingWhitespace::Trim &&
                Safety_.CanReplaceGap(i, ""))
        {
            FixedGapSlice(i, source.Source.Begin, source.Source.Begin);
            return;
        }
        const auto context = ContextFor(i);
        const auto preference = Spacing_.SpacePreference(i, context, Safety_);
        const auto count = Spacing_.TrailingCommentCount(i, Safety_);
        const bool space = count ? *count != 0 : preference.value_or(false);
        if ((count || preference) && Safety_.CanReplaceGap(i, space ? " " : ""))
        {
            auto spec = Spec(i, layout::GapKind::Flat, false);
            Document_.Gap(spec, layout::InvalidId, GapOrigin(i));
        } else {
            FixedGapSlice(i, source.Source.Begin, source.Source.End);
        }
    }
    void EmitPartialGap(
        Index i,
        std::span<const PartialGapPermission> permissions)
    {
        const auto& source = Safety_.Gaps()[i];
        const auto& plan = Gaps_[i];
        // Accepted descriptors authorize only their slices, even though this
        // whole gap remains Preserve. Annotation/spacing uncertainty wins too.
        if (plan.Frozen || Uncertain(i) || source.ContextSensitive) {
            FixedGapSlice(i, source.Source.Begin, source.Source.End);
            return;
        }
        const bool spelling =
            Policies_.LineEnding != LineEnding::Preserve &&
                Safety_.CanSpellGapNewlines(i);
        const auto copied = [&] (Index begin, Index end) {
            if (spelling) {
                EditableGapSlice(i, begin, end);
            } else {
                FixedGapSlice(i, begin, end);
            }
        };
        auto cursor = source.Source.Begin;
        for (const auto& permission : permissions) {
            if (permission.Kind == PartialGapPermissionKind::TrimHorizontal) {
                if (Policies_.TrailingWhitespace == TrailingWhitespace::Trim) {
                    if (cursor < permission.Source.Begin) {
                        copied(cursor, permission.Source.Begin);
                    }
                    cursor = permission.Source.End;
                }
            } else if (plan.PartialIndent) {
                auto anchor =
                    plan.Owner == None
                        ? layout::InvalidId
                        : plan.Closing
                            ? Groups_[plan.Owner].Block
                            : Groups_[plan.Owner].Anchor;
                if (plan.OverrideIndent) {
                    anchor =
                        Document_
                            .MarkAnchor(
                                {
                                    layout::AnchorKind::BlockRelative,
                                    plan.ExtraIndent,
                                });
                }
                // Any trimmed runs were before terminators. The remaining
                // contiguous prefix still ends in the original final LF.
                // No fixed newline becomes an editable owned boundary.
                Document_
                    .SourceLineIndent(
                        cursor,
                        permission.Source.Begin,
                        permission.Source.Begin,
                        permission.Source.End,
                        anchor,
                        plan.RequiredWidth);
                RecordFixedExpressionLine(i, cursor, permission.Source.Begin);
                cursor = permission.Source.End;
            }
        }
        if (cursor < source.Source.End) {
            copied(cursor, source.Source.End);
        }
    }
    void EmitGap(Index i)
    {
        const auto permissions = Safety_.PartialGapPermissions();
        const auto begin = NextPermission_;
        while (
            NextPermission_ < permissions.size() &&
                permissions[NextPermission_].Gap == i)
        {
            ++NextPermission_;
        }
        const auto& gap = Gaps_[i];
        if (i < Removed_.size() && Removed_[i]) {
            return;
        }
        if (NextPermission_ != begin) {
            EmitPartialGap(
                i,
                permissions.subspan(begin, NextPermission_ - begin));
            return;
        }
        if (gap.Shape == Boundary::Preserve) {
            PreserveGap(i);
            return;
        }
        const auto original = Safety_.Gaps()[i].Original;
        if (
            !RebuildsTopology(i) && !HasLine(original) &&
                original.find_first_not_of(" \t") != std::string_view::npos &&
                !Spacing_.SpacePreference(i, GapContext::Rebuilt, Safety_))
        {
            PreserveGap(i);
            return;
        }
        auto anchor =
            gap.Owner == None
                ? layout::InvalidId
                : gap.Closing
                    ? Groups_[gap.Owner].Block
                    : Groups_[gap.Owner].Anchor;
        if (gap.OverrideIndent) {
            anchor =
                Document_
                    .MarkAnchor(
                        {layout::AnchorKind::BlockRelative, gap.ExtraIndent});
        }
        const auto flat = Spec(i, layout::GapKind::Flat);
        const bool flatOK = Allows(i, layout::GapKind::Flat);
        const bool lineOK = Allows(i, layout::GapKind::Hard);
        if (!flatOK && !lineOK) {
            PreserveGap(i);
            return;
        }
        // Safety converts required/forbidden boundaries before shape selection.
        // This is not byte-diff activation and never weakens topology barriers.
        if (!flatOK) {
            Document_.Gap(Spec(i, layout::GapKind::Hard), anchor, GapOrigin(i));
        } else if (!lineOK || gap.Shape == Boundary::Flat) {
            Document_.Gap(flat, anchor, GapOrigin(i));
        } else if (gap.Shape == Boundary::Conditional) {
            if (Stack_.empty() || Stack_.back() != gap.Owner) {
                Fail(SyntaxLayoutError::ConflictingGapOwners, i);
                return;
            }
            Document_
                .ConditionalGap(
                    Groups_[gap.Owner].Id,
                    flat,
                    Spec(i, gap.Broken),
                    anchor,
                    GapOrigin(i));
        } else {
            Document_
                .Gap(
                    Spec(
                        i,
                        gap.Shape == Boundary::Hard
                            ? layout::GapKind::Hard
                            : layout::GapKind::Soft),
                    anchor,
                    GapOrigin(i));
        }
    }
    void MarkPrefixAnchor(Index id)
    {
        auto& group = Groups_[id];
        if (Stack_.empty() || Stack_.back() != id) {
            Fail(SyntaxLayoutError::ConflictingGapOwners, group.PrefixGap);
            return;
        }
        // Capture the actual emitted colon/flat-spacing column inside B+C.
        // Minimum0 floors at B+C; signed (F-C) recovers B+F. SortForest proves
        // B+C has not saturated. The closer's original B anchor is unchanged.
        const auto fallback =
            int64_t(group.Indent.FallbackOffset) -
                int64_t(group.Indent.ContinuationOffset);
        const auto offset =
            group.AnchorAfterGap ? 0 : int32_t(FlatWidth(group.PrefixGap));
        group.Anchor =
            Document_
                .MarkAnchor(
                    {
                        layout::AnchorKind::DelimiterColumn,
                        offset,
                        int32_t(fallback),
                        0,
                    });
    }

    void Emit()
    {
        Document_.Reserve(uint32_t(std::min<uint64_t>(
            None,
            uint64_t(Tree_.TokenCount()) * 3 + uint64_t(Groups_.size()) * 5 +
                1)),
                          Index(Groups_.size()));
        size_t next = 0;
        for (Position p = 0; p <= GapPosition(Tree_.TokenCount()); ++p) {
            while (!Stack_.empty() && Groups_[Stack_.back()].End == p) {
                Document_.EndGroup(Groups_[Stack_.back()].Id);
                Stack_.pop_back();
            }
            while (next < Order_.size() && Groups_[Order_[next]].Begin == p) {
                const auto id = Order_[next++];
                auto& group = Groups_[id];
                group.Block =
                    Document_
                        .MarkAnchor(
                            {
                                layout::AnchorKind::BlockRelative,
                                0,
                                0,
                                0,
                                group.Spec.StructuralBase,
                            });
                if (group.PrefixGap != None) {
                    group.Anchor =
                        Document_
                            .MarkAnchor(
                                {
                                    layout::AnchorKind::BlockRelative,
                                    int32_t(group.Indent.ContinuationOffset),
                                    0,
                                    0,
                                    group.Spec.StructuralBase,
                                });
                } else if (group.HasAnchor) {
                    group.Anchor =
                        Document_
                            .MarkAnchor(
                                {
                                    group.Indent.Alignment ==
                                            ContinuationAlignment::Delimiter
                                        ? layout::AnchorKind::DelimiterColumn
                                        : layout::AnchorKind::BlockRelative,
                                    group.Indent.Alignment ==
                                            ContinuationAlignment::Delimiter
                                        ? 0
                                        : int32_t(
                                            group.Indent.ContinuationOffset),
                                    int32_t(group.Indent.FallbackOffset),
                                    uint32_t(group.Indent.ContinuationOffset),
                                    group.Spec.StructuralBase,
                                });
                }
                group.Id = Document_.BeginGroup(group.Spec);
                Stack_.push_back(id);
            }
            const auto i = Index(p / 2);
            if ((p & 1) == 0) {
                if (Commas_[i].Owner != None) {
                    const auto owner = Commas_[i].Owner;
                    if (Stack_.empty() || Stack_.back() != owner) {
                        Fail(SyntaxLayoutError::ConflictingGapOwners, i);
                        return;
                    }
                    if (i == 0) {
                        Fail(SyntaxLayoutError::MismatchedInputs, i);
                        return;
                    }
                    // Syntax-selected insertion point after the last item,
                    // not an identity for a surviving source comma.
                    const auto end = Tree_.Token(i - 1).Text.End;
                    Document_
                        .ConditionalText(
                            Groups_[owner].Id,
                            "",
                            ",",
                            {end, end, layout::NoIdentity},
                            1);
                }
                const auto delayed = Gaps_[i].MarkDelimiter;
                if (delayed != None && !Groups_[delayed].AnchorAfterGap) {
                    MarkPrefixAnchor(delayed);
                }
                EmitGap(i);
                if (delayed != None && Groups_[delayed].AnchorAfterGap) {
                    MarkPrefixAnchor(delayed);
                }
            } else if (!Removed_[i]) {
                const auto token = Tree_.Token(i).Text;
                const bool opaque =
                    Tree_.Kind(i) == TokenKind::Comment ||
                        Tree_.Kind(i) == TokenKind::PreprocessingDirective ||
                        HasLine(Tree_.TokenText(i)) ||
                        Safety_.IntersectsImmutable({token.Begin, token.End});
                Document_.SourceText(token.Begin, token.End, i, opaque);
            }
            if (!State_) {
                return;
            }
        }
        while (!Stack_.empty()) {
            Document_.EndGroup(Groups_[Stack_.back()].Id);
            Stack_.pop_back();
        }
    }
};
} // namespace
SyntaxLayoutStatus BuildSyntaxDocument(
    const parser::SyntaxTree& tree,
    const CompiledPolicies& policies,
    const LibraryAnnotations& annotations,
    const SourceAnalysis& safety,
    const SyntaxSpacing& spacing,
    layout::Document& document,
    std::pmr::memory_resource& scratch,
    const SyntaxLayoutOptions& options)
{
    if (options.TabWidth == 0 || options.TabWidth > 32) {
        return {SyntaxLayoutError::MismatchedInputs};
    }
    return Planner(
            tree,
            policies,
            annotations,
            safety,
            spacing,
            document,
            scratch,
            options).Build();
}

layout::Policy SyntaxRenderPolicy(
    const CompiledPolicies& policies,
    const SyntaxLayoutOptions& options)
{
    layout::Policy result;
    result.Width = uint32_t(policies.ColumnLimit);
    result.TabWidth = options.TabWidth;
    result.LineEnding = options.LineEnding;
    result.Indentation =
        policies.IndentationKind == IndentationKind::Tabs
            ? layout::IndentationKind::Tabs
            : layout::IndentationKind::Spaces;
    return result;
}
} // namespace formatter
