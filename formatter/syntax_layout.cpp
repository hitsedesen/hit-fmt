#include "syntax_layout.hpp"

#include "declaration_kind.hpp"
#include "text_policies.hpp"

#include <util/ascii.hpp>

#include <parser/operator_info.hpp>

#include <unicode/utf8.hpp>

#include <algorithm>
#include <limits>
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

// Two ordered positions per token require 33 bits for a 32-bit token index.
using Position = uint64_t;

constexpr Position GapPosition(Index i)
{
    return Position(i) * 2;
}

constexpr Position TokenPosition(Index i)
{
    return GapPosition(i) + 1;
}

enum class Boundary : uint8_t
{
    Preserve,
    Flat,
    Soft,
    Hard,
    Conditional
};

struct Gap
{
    Index Owner = None;
    Index RelativeGroup = None;
    Index BraceIndentOffset = None;
    Boundary Shape = Boundary::Preserve;
    document::GapKind Broken = document::GapKind::Flat;
    GapAdjacency BrokenAdjacency = GapAdjacency::Source;
    bool Closing = false;
    bool Generated = false;
    bool Reindent = false;
    // Style-owned indentation demand survives a scope fence on topology.
    bool PartialIndent = false;
    bool ParagraphBoundary = false;
    bool Reassembled = false;
    int32_t ExtraIndent = 0;
    bool OverrideIndent = false;
    // Only final line indentation may change in otherwise frozen syntax.
    bool RelativeIndent = false;
    bool Frozen = false;
    uint16_t NewlineCount = 1;
    bool Vertical = false;
    bool VerticalMinimum = false;
    bool MemberGroup = false;
    bool PreserveIndent = false;
    uint8_t VerticalPriority = 0;
    Index MarkDelimiter = None;
    // Fit a complete syntax prefix before considering optional child breaks.
    Index FitThrough = None;
    GapContext Context = GapContext::Preserved;
    bool ContextKnown = false;
    // A flat header boundary still rebuilds source topology, so horizontal
    // Preserve spelling must use the same safe fallback before fitting.
    bool CanonicalFlat = false;
    bool ControlSeparator = false;
    bool FitNextSegment = false;
    bool FitNextSegmentIfOverflow = false;
    bool FitFlatTail = false;
    bool KeepFlatSpacing = false;
    bool SpaceFallback = false;
    uint16_t FixedSpaces = UINT16_MAX;
};

// Compact, densely accessed finalized facts, separate from planning metadata.
// Preferences depend only on immutable spacing/source analysis. Widths and
// permissions are cached after planning and invalidated when safety changes
// the gap shape.
struct GapFacts
{
    Index RequiredWidth = 0;
    mutable Index CachedWidth = 0;
    mutable uint16_t CachedCommentCount = UINT16_MAX;
    // 0 unknown, 1 preserve, 2 no space, 3 space.
    mutable uint16_t PreservedPreference : 2 = 0;
    mutable uint16_t RebuiltPreference : 2 = 0;
    mutable uint16_t WidthKnown : 1 = false;
    mutable uint16_t FlatKnown : 1 = false;
    mutable uint16_t LineKnown : 1 = false;
    mutable uint16_t FlatAllowed : 1 = false;
    mutable uint16_t LineAllowed : 1 = false;
};

static_assert(sizeof(GapFacts) <= 12);

// Style offsets fit in a byte, but preserved source indentation can be much
// wider. Keep that distinct from the validated compiled policy representation.
struct LayoutIndentation
{
    ContinuationAlignment Alignment;
    uint32_t ContinuationOffset;
    uint32_t FallbackOffset;

    LayoutIndentation(ContinuationAlignment alignment, uint32_t continuation, uint32_t fallback)
        : Alignment(alignment)
        , ContinuationOffset(continuation)
        , FallbackOffset(fallback)
    { }

    LayoutIndentation(IndentationPolicy policy)
        : LayoutIndentation(policy.Alignment, policy.ContinuationOffset, policy.FallbackOffset)
    { }
};

struct Group
{
    Position Begin;
    Position End;
    document::GroupSpec Spec;
    LayoutIndentation Indent;
    bool HasAnchor = false;
    bool SyntaxBody = false;
    document::GroupId Id = document::InvalidId;
    document::AnchorId Anchor = document::InvalidId;
    // Delimiter placement may differ from the level shared by the condition.
    document::AnchorId ContinuationAnchor = document::InvalidId;
    bool IndentTernaryOperators = false;
    bool ShareBranchContinuation = false;
    document::AnchorId Block = document::InvalidId;
    Index PrefixGap = None;
    bool AnchorAfterGap = false;
    uint32_t LogicalIndent = 0;
    bool SaturatedIndent = false;
    Index PreviousContinuation = None;
    Index IndentGroup = None;
    bool NeedsBlock = false;
    Index PreservedSourceIndent = None;
};

struct IndentBasis
{
    uint32_t Column;
    bool Saturated;
};

constexpr IndentBasis AccumulateIndent(IndentBasis parent, int32_t delta)
{
    const auto logical = int64_t(parent.Column) + delta;
    return {
        uint32_t(std::clamp<int64_t>(logical, 0, int64_t(UINT32_MAX) - 1)),
        parent.Saturated || logical >= int64_t(UINT32_MAX),
    };
}

// A later negative offset cannot undo information lost by an ancestor clamp.
static_assert(AccumulateIndent(AccumulateIndent({UINT32_MAX - 2, false}, 3), -32).Saturated);

static_assert(!AccumulateIndent({UINT32_MAX - 2, false}, 1).Saturated);

enum class DeclarationCategory : uint8_t
{
    None,
    Type,
    Function,
    Variable
};

struct Syntax
{
    RegionId Region = NoRegion;
    Index Root = None;
    Index PreviousSibling = None;
    Index Operator = None;
    Index ListGroup = None;
    Index PreservedIndentGroup = None;
    bool Safe = true;
    bool IndentSafe = true;
    bool ChainSafe = true;
    // Opaque syntax in independently delimited bodies does not invalidate
    // the enclosing expression's proven boundaries.
    bool OpaqueSafe = true;
    bool String = false;
    bool Definition = false;
    DeclarationCategory Category = DeclarationCategory::None;
    bool ItemSequence : 1 = false;
    bool ControlHeader : 1 = false;
    bool NamespaceSuffix : 1 = false;
    bool Preprocessing : 1 = false;
    parser::TokenRange SequenceBody = {};
};

struct ScopeName
{
    std::string_view Name;
    Index Pair;
    bool Begin;
};

struct ChainLink
{
    Index Gap;
    Index End;
    std::string_view Name;
    Index ScopeEnd = None;
    Index Owner = None;
    Index ScopeGroup = None;
    bool Sticky = false;
    bool RootReceiver = false;
};

struct VisualRange
{
    Index First;
    Index End;
};

struct FrozenRange
{
    Index Begin;
    Index End;
    Index Body;
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
    // Tree source ranges have 32-bit byte offsets, so even all tabs fit
    // this accumulator. Clamp the complete width rather than each byte.
    const auto ascii = util::PlainAsciiPrefix(text);

    if (ascii == text.size()) {
        return Index(ascii);
    }

    uint64_t width = ascii;

    for (std::size_t i = ascii; i < text.size(); ++i) {
        const auto c = static_cast<unsigned char>(text[i]);

        if (c >= 0x80) {
            i += unicode::utf8::GetFirstCodePointSize(text.substr(i)).value_or(1) - 1;
        }

        if (c == '\n' || c == '\r') {
            return None;
        }

        width += c == '\t' ? tabWidth : 1;
    }
    return Index(std::min<uint64_t>(width, None));
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
    return kind == NodeKind::CallExpression || kind == NodeKind::MemberExpression;
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
        document::Document& document,
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
        , Facts_(&scratch)
        , Commas_(&scratch)
        , Removed_(&scratch)
        , Order_(&scratch)
        , Stack_(&scratch)
        , LastChild_(&scratch)
        , BlockOf_(&scratch)
        , PrefixProtection_(&scratch)
        , PrefixOpaqueFences_(&scratch)
        , Visual_(&scratch)
        , Frozen_(&scratch)
        , RootName_(&scratch)
        , ScopeNames_(&scratch)
        , ChainLinks_(&scratch)
        , ChainScopeNames_(&scratch)
    { }

    SyntaxLayoutStatus Build()
    {
        const auto count = Tree_.TokenCount();

        if (Safety_.Gaps().size() != size_t(count) + 1 || Spacing_.Gaps().size() != size_t(count) + 1 ||
            Safety_.Source().data() != Tree_.Source().data() || Safety_.Source().size() != Tree_.Source().size())
        {
            return {SyntaxLayoutError::MismatchedInputs};
        }

        Gaps_.resize(size_t(count) + 1);
        Facts_.resize(size_t(count) + 1);
        Commas_.resize(size_t(count) + 1);
        Removed_.resize(count);
        PrefixProtection_.resize(size_t(count) + 1);
        PrefixOpaqueFences_.resize(size_t(count) + 1);
        const auto protection = Safety_.Protection();
        std::pmr::vector<uint8_t> opaqueFences(
            PrefixOpaqueFences_.get_allocator().resource());
        opaqueFences.reserve(protection.size());

        for (const auto& range : protection) {
            const auto reasons = uint16_t(range.Reasons) & ~uint16_t(ProtectionReason::OutsideEditScope);
            const bool contextual = reasons != 0;
            const bool opaque = contextual &&
                (reasons != uint16_t(ProtectionReason::Recovery) || HasLine(Tree_.SourceText(range.Source)));
            opaqueFences.push_back(uint8_t(contextual) | (uint8_t(opaque) << 1));
        }

        size_t firstProtection = 0;

        for (Index i = 0; i < count; ++i) {
            const auto token = Tree_.Token(i).Text;

            while (firstProtection < protection.size() && protection[firstProtection].Source.End <= token.Begin) {
                ++firstProtection;
            }

            bool contextual = false;
            bool fenced = false;

            for (auto p = firstProtection; p < protection.size() && protection[p].Source.Begin < token.End; ++p) {
                contextual |= (opaqueFences[p] & 1) != 0;
                fenced |= (opaqueFences[p] & 2) != 0;
            }

            PrefixProtection_[i + 1] = PrefixProtection_[i] + contextual;
            PrefixOpaqueFences_[i + 1] = PrefixOpaqueFences_[i] + fenced;
        }

        for (auto id : Options_.GeneratedAdjacencies) {
            if (id > count) {
                return {SyntaxLayoutError::InvalidGeneratedAdjacency, id};
            }

            Gaps_[id].Generated = true;
        }

        for (auto id : Options_.ReassembledGaps) {
            if (id > count) {
                return {SyntaxLayoutError::InvalidGeneratedAdjacency, id};
            }

            Gaps_[id].Reassembled = true;
        }

        ProjectSyntax();
        AnalyzeVisualScopes();
        const auto chainScopes = Annotations_.ChainScopes();
        ChainScopeNames_.reserve(chainScopes.size() * 2);

        for (Index i = 0; i < chainScopes.size(); ++i) {
            ChainScopeNames_.push_back({chainScopes[i].First, i, true});
            ChainScopeNames_.push_back({chainScopes[i].Second, i, false});
        }

        std::sort(
            ChainScopeNames_.begin(),
            ChainScopeNames_.end(),
            [] (const auto& a, const auto& b) {
                return a.Name < b.Name;
            });

        for (Index i = 0; i < Syntax_.size(); ++i) {
            auto& entry = Syntax_[i];
            const auto& node = Region(entry);

            // Uncertain annotation islands retain their complete topology.
            // Plan their relative indentation once, not nested syntax layouts.
            if (!node.Tokens.Empty() && Gaps_[node.Tokens.Begin].Frozen) {
                continue;
            }

            if (entry.ItemSequence && entry.SequenceBody.Begin < entry.SequenceBody.End) {
                Gaps_[entry.SequenceBody.Begin].ParagraphBoundary = true;
                Gaps_[entry.SequenceBody.End].ParagraphBoundary = true;
            }

            if (Region(entry).Parent != None) {
                const auto& parent = Syntax_[Region(entry).Parent];

                if (parent.ItemSequence && !node.Tokens.Empty() && node.Tokens.Begin >= parent.SequenceBody.Begin &&
                    node.Tokens.End <= parent.SequenceBody.End)
                {
                    Gaps_[node.Tokens.Begin].ParagraphBoundary = true;
                    Gaps_[node.Tokens.End].ParagraphBoundary = true;
                }
            }

            // Only direct logical operands inherit the header owner. Calls,
            // parentheses, init-statements and lambda bodies stop propagation.
            if (node.Kind == NodeKind::BinaryExpression && entry.Operator != None && node.Parent != None &&
                (Tree_.Kind(entry.Operator) == TokenKind::Operator_And ||
                    Tree_.Kind(entry.Operator) == TokenKind::Operator_Or))
            {
                entry.ListGroup = Syntax_[node.Parent].ListGroup;
            }

            switch (node.Kind) {
                case NodeKind::TranslationUnit:
                    PlanRootIndentation(i);
                    break;
                case NodeKind::MacroEnumDeclaration:
                    PlanEnumMacro(i);
                    break;
                case NodeKind::AttributedStatement:
                    PlanAttributedStatement(i);
                    break;
                case NodeKind::LinkageSpecification:
                    if (entry.ItemSequence) {
                        // Linkage keeps its authored body shape; explicit
                        // indentation controls only the contents' level.
                        auto policy = Policies_.Bodies;
                        policy.Layout = LayoutMode::Preserve;
                        PlanBody(i, entry.SequenceBody.Begin - 1, entry.SequenceBody.End, policy, false);
                        PlanPreservedIndentation(i, node.Tokens.Begin);
                    }

                    break;
                case NodeKind::FunctionDeclaration:
                case NodeKind::FunctionDefinition:
                case NodeKind::MacroFunctionDefinition:
                case NodeKind::FunctionTryBlock:
                case NodeKind::CompoundStatement:
                case NodeKind::LambdaBody:
                case NodeKind::RequirementBody:
                case NodeKind::NamespaceDeclaration:
                case NodeKind::RecordDeclaration:
                case NodeKind::MacroRecordDeclaration:
                case NodeKind::EnumDeclaration:
                case NodeKind::LambdaExpression:
                case NodeKind::IfStatement:
                case NodeKind::ForStatement:
                case NodeKind::WhileStatement:
                case NodeKind::DoWhileStatement:
                case NodeKind::SwitchStatement:
                case NodeKind::TryStatement:
                case NodeKind::CatchClause:
                case NodeKind::CaseLabel:
                    PlanHeaderAndBody(i);
                    break;
                default:
                    break;
            }

            if (node.Kind == NodeKind::TemplateDeclaration) {
                PlanTemplateHeader(i);
            }

            if ((node.Kind == NodeKind::AmbiguousStatement || node.Kind == NodeKind::UnparsedSyntax) &&
                node.StatementBoundary)
            {
                PlanPreservedIndentation(i, node.Tokens.Begin);
            }

            if (!entry.Safe && !(IsSpine(node.Kind) ? entry.ChainSafe : CanPlanAroundOpaqueChildren(i))) {
                if (ListFor(node.Kind)) {
                    PreserveUnplannedListContext(i);
                }

                continue;
            }

            if (const auto* list = ListFor(node.Kind)) {
                PlanList(i, *list);
            }

            switch (node.Kind) {
                case NodeKind::InitDeclarator:
                    PlanInitializedDeclarator(i);
                    break;
                case NodeKind::AmbiguousDeclaratorSuffix:
                    PlanPreservedIndentation(
                        i,
                        node.Parent == None ? node.Tokens.Begin : Region(Syntax_[node.Parent]).Tokens.Begin);
                    break;
                case NodeKind::RequiresClause:
                    PlanConstraintHeader(i);
                    break;
                case NodeKind::CompoundRequirement:
                    PlanCompoundRequirement(i);
                    break;
                case NodeKind::TypeRequirement:
                case NodeKind::NestedRequirement:
                case NodeKind::ReturnStatement:
                case NodeKind::ThrowStatement:
                case NodeKind::CoYieldStatement:
                    PlanKeywordExpression(i);
                    break;
                case NodeKind::Initializer:
                case NodeKind::Enumerator:
                case NodeKind::BinaryExpression:
                case NodeKind::AssignmentExpression:
                case NodeKind::ConditionalExpression:
                case NodeKind::ParenthesizedExpression:
                case NodeKind::RequirementExpression:
                case NodeKind::SubscriptExpression:
                case NodeKind::CastExpression:
                case NodeKind::TypeConstructionExpression:
                    PlanExpression(i);
                    break;
                case NodeKind::LiteralExpression:
                    if (entry.String) {
                        PlanExpression(i);
                    }

                    break;
                case NodeKind::CallExpression:
                case NodeKind::MemberExpression:
                    PlanChain(i);
                    break;
                case NodeKind::AttributeSpecifier:
                    PlanAttribute(i);
                    break;
                default:
                    break;
            }
        }

        // Classified children plan first. Only unowned continuation gaps in
        // a bounded opaque statement need the authored relative indentation.
        for (Index i = 0; i < Syntax_.size(); ++i) {
            const auto& node = Region(Syntax_[i]);

            if (Syntax_[i].Preprocessing && node.StatementBoundary) {
                PlanPreservedIndentation(i, node.Tokens.Begin, true);
            }
        }

        PlanFrozenScopes();
        PlanVisualScopes();
        PlanVerticalGaps();
        PlanRulerGaps();
        PlanEmptyLineCaps();
        PlanRelativeIndentation();

        if (!State_) {
            return State_;
        }

        GapCacheReady_ = true;
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
    document::Document& Document_;
    const SyntaxLayoutOptions& Options_;
    std::pmr::vector<Syntax> Syntax_;
    std::pmr::vector<Group> Groups_;
    std::pmr::vector<Gap> Gaps_;
    std::pmr::vector<GapFacts> Facts_;
    std::pmr::vector<Comma> Commas_;
    std::pmr::vector<uint8_t> Removed_;
    std::pmr::vector<Index> Order_;
    std::pmr::vector<Index> Stack_;
    std::pmr::vector<Index> LastChild_;
    std::pmr::vector<Index> BlockOf_;
    std::pmr::vector<Index> PrefixProtection_;
    std::pmr::vector<Index> PrefixOpaqueFences_;
    std::pmr::vector<VisualRange> Visual_;
    std::pmr::vector<FrozenRange> Frozen_;
    std::pmr::string RootName_;
    std::pmr::vector<ScopeName> ScopeNames_;
    std::pmr::vector<ChainLink> ChainLinks_;
    std::pmr::vector<ScopeName> ChainScopeNames_;
    SyntaxLayoutStatus State_;
    size_t NextPermission_ = 0;
    bool GapCacheReady_ = false;
    bool NaturalBodies_ = true;
    bool HasPreservedIndentGroups_ = false;
    Index LastRelativeGroup_ = None;
    int32_t LastRelativeOffset_ = 0;
    document::AnchorId LastRelativeAnchor_ = document::InvalidId;

    // Scope exclusions prohibit edits, not understanding the surrounding
    // grammar. Keep real reason unions (including scope + semantic reasons)
    // stronger. Edit queries continue to use the unfiltered safety analysis.
    bool ContextProtected(parser::SourceRange source) const
    {
        const auto protection = Safety_.Protection();
        auto first = std::lower_bound(
            protection.begin(),
            protection.end(),
            source.Begin,
            [] (const ProtectedSourceRange& range, parser::SourceOffset point) {
                return range.Source.End <= point;
            });

        for (; first != protection.end() && first->Source.Begin < source.End; ++first) {
            if ((uint16_t(first->Reasons) & ~uint16_t(ProtectionReason::OutsideEditScope)) != 0) {
                return true;
            }
        }

        return false;
    }

    const SourceRegion& Region(const Syntax& syntax) const
    {
        return Safety_.Regions()[syntax.Region];
    }

    void Fail(SyntaxLayoutError error, Index token)
    {
        if (State_) {
            State_ = {error, token};
        }
    }

    // Preorder projection visits each node once. Reverse order computes shared
    // callee roots/string eligibility and safety. No overlapping range scans.
    void ProjectSyntax()
    {
        const auto regions = Safety_.Regions();
        Syntax_.resize(regions.size());
        LastChild_.resize(regions.size(), None);
        BlockOf_.resize(regions.size(), None);

        for (Index id = 0; id < regions.size(); ++id) {
            auto& entry = Syntax_[id];
            const auto& region = regions[id];
            entry.Region = id;
            switch (region.Kind) {
                case NodeKind::TranslationUnit:
                case NodeKind::NamespaceDeclaration:
                case NodeKind::RecordDeclaration:
                case NodeKind::MacroRecordDeclaration:
                case NodeKind::CompoundStatement:
                case NodeKind::LambdaBody:
                case NodeKind::RequirementBody:
                case NodeKind::LinkageSpecification:
                case NodeKind::ExportDeclaration:
                    entry.ItemSequence = true;
                    break;
                case NodeKind::PreprocessingConditional:
                case NodeKind::PreprocessingBranch:
                    entry.ItemSequence = region.Parent != None && Syntax_[region.Parent].ItemSequence;
                    break;
                default:
                    break;
            }

            // Carry header context through expressions and arguments, but
            // never into a lambda or other independently formatted body.
            entry.ControlHeader = region.Kind == NodeKind::Condition || region.Kind == NodeKind::ForHeader ||
                (!entry.ItemSequence && region.Parent != None && Syntax_[region.Parent].ControlHeader);

            if (region.Parent != None) {
                entry.PreviousSibling = LastChild_[region.Parent];
                LastChild_[region.Parent] = id;
            }

            if (region.Kind == NodeKind::EmptyDeclaration && region.Tokens.Size() == 1 &&
                entry.PreviousSibling != None && Safety_.Gaps()[region.Tokens.Begin].Horizontal)
            {
                const auto& previous = Syntax_[entry.PreviousSibling];
                const auto& preceding = Region(previous);
                // Same-line null declarations after a namespace body are a
                // closing suffix, not a new paragraph. Carry this through a
                // run of semicolons so its trailing comment stays attached.
                entry.NamespaceSuffix = preceding.Tokens.End == region.Tokens.Begin &&
                    (previous.NamespaceSuffix ||
                        (preceding.Kind == NodeKind::NamespaceDeclaration && !preceding.Tokens.Empty() &&
                            Tree_.Kind(preceding.Tokens.End - 1) == TokenKind::Operator_CloseBrace));
            }
        }

        for (Index i = Index(Syntax_.size()); i-- > 0;) {
            auto& entry = Syntax_[i];
            const auto& node = Region(entry);
            entry.Safe = node.Tokens.Begin <= node.Tokens.End && node.Tokens.End <= Tree_.TokenCount();
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
                entry.Safe = PrefixProtection_[node.Tokens.Begin] == PrefixProtection_[node.Tokens.End];
            }

            if (entry.ItemSequence) {
                entry.SequenceBody = node.Tokens;

                if (node.Kind == NodeKind::MacroRecordDeclaration) {
                    const auto first = node.FirstChild;
                    const auto last = LastChild_[i];
                    entry.ItemSequence =
                        first != None && last != None && Region(Syntax_[first]).Kind == NodeKind::MacroRecordBegin &&
                            Region(Syntax_[last]).Kind == NodeKind::MacroRecordEnd;

                    if (entry.ItemSequence) {
                        entry.SequenceBody = {Region(Syntax_[first]).Tokens.End, Region(Syntax_[last]).Tokens.Begin};
                    }
                } else if (node.Kind != NodeKind::TranslationUnit && node.Kind != NodeKind::PreprocessingConditional &&
                    node.Kind != NodeKind::PreprocessingBranch)
                {
                    Index opening = None;
                    Index closing = None;
                    DirectTokens(
                        i,
                        [&] (Index token) {
                            if (opening == None && Tree_.Kind(token) == TokenKind::Operator_OpenBrace) {
                                opening = token;
                            } else if (Tree_.Kind(token) == TokenKind::Operator_CloseBrace) {
                                closing = token;
                            }
                        });
                    // Only real sequence bodies own these gaps. In particular,
                    // an unbraced export wrapper and pre-brace trivia do not.
                    entry.ItemSequence = opening != None && closing != None && opening < closing;

                    if (entry.ItemSequence) {
                        entry.SequenceBody = {opening + 1, closing};
                    }
                }
            }

            const auto body = entry.SequenceBody;
            // A matched body isolates its preceding header's indentation.
            // Plain blocks still inherit their contents' indentation constraints.
            const bool boundedBody = entry.ItemSequence && body.Begin != 0 && body.Begin - 1 > node.Tokens.Begin &&
                body.End < Tree_.TokenCount() && Tree_.Kind(body.Begin - 1) == TokenKind::Operator_OpenBrace &&
                Tree_.MatchingDelimiter(body.Begin - 1) == body.End &&
                PrefixProtection_[body.Begin - 1] == PrefixProtection_[body.Begin] &&
                PrefixProtection_[body.End] == PrefixProtection_[body.End + 1];
            // Bounded opaque statements and conditions permit leading
            // indentation without rewriting their uncertain interiors.
            const bool statementBoundary = node.StatementBoundary;

            if (PrefixOpaqueFences_[node.Tokens.Begin] != PrefixOpaqueFences_[node.Tokens.End]) {
                DirectTokens(
                    i,
                    [&] (Index token) {
                        const bool safe = PrefixOpaqueFences_[token] == PrefixOpaqueFences_[token + 1];
                        entry.OpaqueSafe &= safe;
                        entry.Preprocessing |= Tree_.Kind(token) == TokenKind::PreprocessingDirective;

                        if (!statementBoundary && !(boundedBody && token >= body.Begin && token < body.End)) {
                            // Directives keep their own indentation, but do
                            // not anchor the surrounding C++ container.
                            entry.IndentSafe &= safe || IndentTransparentDirective(token);
                        }
                    });
            }

            for (auto child = Region(entry).FirstChild; child != None; child = Region(Syntax_[child]).NextSibling) {
                entry.Safe = entry.Safe && Syntax_[child].Safe;
                entry.Preprocessing |= Syntax_[child].Preprocessing;
                entry.OpaqueSafe &= Syntax_[child].OpaqueSafe || HasIndependentBodyBoundary(Region(Syntax_[child]));
                const auto childTokens = Region(Syntax_[child]).Tokens;

                if (!statementBoundary &&
                    !(boundedBody && childTokens.Begin >= body.Begin && childTokens.End <= body.End))
                {
                    entry.IndentSafe &= Syntax_[child].IndentSafe;
                }
            }

            // Opaque arguments do not invalidate a proven callee/member spine.
            // Cache this separately: other planners still need subtree safety.
            entry.ChainSafe = entry.Safe || (IsSpine(node.Kind) && CanPlanAroundOpaqueChildren(i));

            if (node.Kind == NodeKind::BinaryExpression || node.Kind == NodeKind::AssignmentExpression) {
                DirectTokens(
                    i,
                    [&] (Index token) {
                        const auto role = Tree_.Role(token);

                        if (role == PunctuationRole::BinaryOperator || role == PunctuationRole::ComparisonOperator ||
                            role == PunctuationRole::ShiftOperator || role == PunctuationRole::AssignmentOperator)
                        {
                            entry.Operator = token;
                        }
                    });
            }

            entry.Definition = node.Kind == NodeKind::FunctionDefinition ||
                node.Kind == NodeKind::MacroFunctionDefinition || node.Kind == NodeKind::FunctionTryBlock ||
                (node.Kind == NodeKind::MacroRecordDeclaration && entry.ItemSequence);

            if (node.Kind == NodeKind::RecordDeclaration || node.Kind == NodeKind::EnumDeclaration) {
                DirectTokens(
                    i,
                    [&] (Index token) {
                        entry.Definition = entry.Definition || Tree_.Kind(token) == TokenKind::Operator_OpenBrace;
                    });

                for (auto child = Region(entry).FirstChild; child != None; child = Region(Syntax_[child]).NextSibling) {
                    entry.Definition = entry.Definition || Region(Syntax_[child]).Kind == NodeKind::EnumeratorList;
                }
            }

            if (node.Kind == NodeKind::TemplateDeclaration || node.Kind == NodeKind::ExportDeclaration) {
                for (auto child = Region(entry).FirstChild; child != None; child = Region(Syntax_[child]).NextSibling) {
                    entry.Definition = entry.Definition || Syntax_[child].Definition;
                }
            }

            if (Policies_.VerticalGaps.DeclarationCategoryBlankLines.Mode != VerticalGapMode::Preserve ||
                Policies_.VerticalGaps.MemberGroupMaxEmptyLines.Enabled)
            {
                entry.Category = ClassifyDeclaration(i);
            }

            entry.Root = i;

            if (IsSpine(node.Kind) && Region(entry).FirstChild != None) {
                entry.Root = Syntax_[Region(entry).FirstChild].Root;

                // Qualified names belong to the root identity, not a fluent
                // member boundary. Keep their complete parsed token range.
                if (node.Kind == NodeKind::MemberExpression) {
                    auto second = Region(Syntax_[Region(entry).FirstChild]).NextSibling;

                    if (second != None) {
                        const auto op = NextNonComment(Region(Syntax_[Region(entry).FirstChild]).Tokens.End);

                        if (op < node.Tokens.End && Tree_.Kind(op) == TokenKind::Operator_ScopeResolution) {
                            entry.Root = i;
                        }
                    }
                }
            }

            if (node.Kind == NodeKind::LiteralExpression && !node.Tokens.Empty()) {
                entry.String = true;

                for (Index t = node.Tokens.Begin; t < node.Tokens.End; ++t) {
                    entry.String = entry.String &&
                        (Tree_.Kind(t) == TokenKind::Literal_String || Tree_.Kind(t) == TokenKind::Comment);
                }
            }
        }
    }

    Index GroupAt(
        Position begin,
        Position end,
        document::GroupMode mode,
        int32_t indent = 0,
        std::optional<LayoutIndentation> anchor = {})
    {
        if (begin >= end) {
            return None;
        }

        const auto id = Index(Groups_.size());
        Groups_.push_back({
            begin,
            end,
            {mode, indent},
            anchor.value_or(LayoutIndentation{ContinuationAlignment::Block, 0, 0}),
            anchor.has_value(),
        });
        return id;
    }

    Index PackingGroup(Index parent, Position begin, Position end)
    {
        const auto group = GroupAt(begin, end, document::GroupMode::Consistent);

        if (group != None) {
            Groups_[group].IndentGroup = parent;
            Groups_[group].Spec.BreakPenalty = Groups_[parent].Spec.BreakPenalty;
        }

        return group;
    }

    void Own(
        Index gap,
        Index owner,
        Boundary shape,
        document::GapKind broken = document::GapKind::Flat,
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
        plan.Context = shape == Boundary::Preserve ? GapContext::Preserved : GapContext::Rebuilt;
        plan.ContextKnown = true;
        plan.Shape = shape;
        plan.Broken = broken;
        plan.Closing = closing;
    }

    bool Uncertain(Index gap) const
    {
        return (uint8_t(Spacing_.Gaps()[gap].Constraint) & uint8_t(SpacingConstraint::UncertainSyntax)) != 0;
    }

    bool RetainsBareCR(Index gap) const
    {
        return Policies_.LineEnding != LineEnding::Preserve && Safety_.Gaps()[gap].HasBareCR;
    }

    bool GapMutable(Index gap) const
    {
        return gap < Gaps_.size() && !RetainsBareCR(gap) && !Uncertain(gap) && !Gaps_[gap].Frozen &&
            !Safety_.Gaps()[gap].Preserve && !Safety_.Gaps()[gap].ContextSensitive;
    }

    const ListPolicy* ListFor(NodeKind kind) const
    {
        switch (kind) {
            case NodeKind::ArgumentList:
                return &Policies_.Calls;
            case NodeKind::ParameterList:
                return &Policies_.Parameters;
            case NodeKind::TemplateParameterList:
                return &Policies_.TemplateParameters;
            case NodeKind::TemplateArgumentList:
                return &Policies_.TemplateArguments;
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

    bool IndentTransparentDirective(Index token) const
    {
        if (Tree_.Kind(token) != TokenKind::PreprocessingDirective) {
            return false;
        }

        const auto source = Tree_.Token(token).Text;
        const auto protection = Safety_.Protection();
        const auto range = std::lower_bound(
            protection.begin(),
            protection.end(),
            source.Begin,
            [] (const auto& range, auto begin) {
                return range.Source.End <= begin;
            });
        return range != protection.end() && range->Source.Begin <= source.Begin && range->Source.End >= source.End &&
            (uint16_t(range->Reasons) & ~uint16_t(ProtectionReason::Directive | ProtectionReason::OutsideEditScope)) ==
                0;
    }

    bool HasIndependentBodyBoundary(const SourceRegion& node) const
    {
        if ((node.Kind != NodeKind::LambdaBody && node.Kind != NodeKind::RequirementBody) || node.Tokens.Size() < 2) {
            return false;
        }

        const auto opening = node.Tokens.Begin;
        const auto close = node.Tokens.End - 1;
        return Tree_.Kind(opening) == TokenKind::Operator_OpenBrace && Tree_.MatchingDelimiter(opening) == close &&
            PrefixProtection_[opening] == PrefixProtection_[opening + 1] &&
            PrefixProtection_[close] == PrefixProtection_[close + 1];
    }

    bool CanPlanAroundOpaqueChildren(Index id) const
    {
        const auto& node = Region(Syntax_[id]);
        switch (node.Kind) {
            case NodeKind::CallExpression:
            case NodeKind::MemberExpression: {
                const auto callee = node.FirstChild;

                if (callee == None || !Syntax_[callee].ChainSafe) {
                    return false;
                }

                for (auto child = Region(Syntax_[callee]).NextSibling;
                    child != None;
                    child = Region(Syntax_[child]).NextSibling)
                {
                    const auto& part = Region(Syntax_[child]);

                    if (node.Kind == NodeKind::CallExpression && part.Kind == NodeKind::ArgumentList) {
                        // Recovery may live inside arguments, not in the
                        // delimiters proving where this invocation ends.
                        if (part.Tokens.Size() < 2 ||
                            Tree_.Kind(part.Tokens.Begin) != TokenKind::Operator_OpenParenthesis ||
                            Tree_.MatchingDelimiter(part.Tokens.Begin) != part.Tokens.End - 1 ||
                            PrefixProtection_[part.Tokens.Begin] != PrefixProtection_[part.Tokens.Begin + 1] ||
                            PrefixProtection_[part.Tokens.End - 1] != PrefixProtection_[part.Tokens.End])
                        {
                            return false;
                        }
                    } else if (!Syntax_[child].Safe) {
                        return false;
                    }
                }

                break;
            }
            case NodeKind::TemplateParameterList:
            case NodeKind::TemplateArgumentList:
                // An incomplete template grammar must not acquire new breaks.
                return false;
            case NodeKind::ArgumentList:
            case NodeKind::Condition:
            case NodeKind::InitializerList:
                if (node.Tokens.Size() < 2 ||
                    Tree_.Kind(node.Tokens.Begin) !=
                        (node.Kind == NodeKind::InitializerList
                            ? TokenKind::Operator_OpenBrace
                            : TokenKind::Operator_OpenParenthesis) ||
                    Tree_.MatchingDelimiter(node.Tokens.Begin) != node.Tokens.End - 1)
                {
                    return false;
                }

                break;
            case NodeKind::ConstructorInitializerList:
                if (node.Tokens.Empty() || Tree_.Kind(node.Tokens.Begin) != TokenKind::Operator_Colon) {
                    return false;
                }

                break;
            case NodeKind::Initializer:
            case NodeKind::BinaryExpression:
            case NodeKind::AssignmentExpression:
            case NodeKind::ConditionalExpression:
            case NodeKind::ParenthesizedExpression:
            case NodeKind::RequirementExpression:
            case NodeKind::RequiresClause:
                break;
            case NodeKind::CastExpression: {
                const auto opening = NamedCastOpening(id);

                if (opening == None || !Safety_.Gaps()[opening + 1].HasLineFeed) {
                    return false;
                }

                break;
            }
            default:
                return false;
        }

        if (node.FirstChild == None) {
            return false;
        }

        if (!Syntax_[id].OpaqueSafe) {
            return false;
        }

        for (auto child = node.FirstChild; child != None; child = Region(Syntax_[child]).NextSibling) {
            const auto& part = Region(Syntax_[child]);

            if (part.Kind == NodeKind::UnparsedSyntax || part.Kind == NodeKind::MissingToken ||
                part.Kind == NodeKind::UnexpectedToken || part.Kind == NodeKind::AmbiguousStatement ||
                part.Kind == NodeKind::PreprocessingDirective || part.Kind == NodeKind::PreprocessingConditional)
            {
                return false;
            }
        }

        // Only the proven enclosing syntax owns breaks. Recovered descendants
        // remain exact atoms; their adjacent protected gaps remain fenced by
        // the normal edit-permission checks. Do not plan recovered punctuation.
        bool directSafe = true;
        DirectTokens(
            id,
            [&] (Index token) {
                directSafe &= PrefixProtection_[token] == PrefixProtection_[token + 1];
            });
        return directSafe;
    }

    struct PinnedArgument
    {
        const MessagePinAnnotation* Annotation = nullptr;
        Index Argument = None;
    };

    PinnedArgument PinArgument(Index id) const
    {
        const auto parent = Region(Syntax_[id]).Parent;

        if (parent == None || Region(Syntax_[parent]).Kind != NodeKind::CallExpression) {
            return {};
        }

        auto callee = Region(Syntax_[parent]).FirstChild;

        if (callee == None) {
            return {};
        }

        if (Region(Syntax_[callee]).Kind == NodeKind::MemberExpression) {
            callee = LastChild_[callee];
        }

        const auto name = Region(Syntax_[callee]).Tokens;

        if (name.Size() != 1) {
            return {};
        }

        const auto* pin = Annotations_.FindMessagePin(Tree_.TokenText(name.Begin));

        if (!pin) {
            return {};
        }

        auto first = Region(Syntax_[id]).FirstChild;

        while (pin->MessageLayout && first != None && Region(Syntax_[first]).Kind == NodeKind::Comment) {
            first = Region(Syntax_[first]).NextSibling;
        }

        if (first == None || Region(Syntax_[first]).Kind != NodeKind::Argument) {
            return {};
        }

        if (pin->FirstArgument == FirstArgumentAnnotation::PinAnyArgument) {
            return {pin, first};
        }

        for (auto argument = first; argument != None; argument = Region(Syntax_[argument]).NextSibling) {
            const auto& part = Region(Syntax_[argument]);

            if (part.Kind != NodeKind::Argument) {
                continue;
            }

            const auto expression = part.FirstChild;

            if (expression != None && Syntax_[expression].String &&
                Region(Syntax_[expression]).Tokens.Begin == part.Tokens.Begin &&
                Region(Syntax_[expression]).Tokens.End == part.Tokens.End)
            {
                return {pin, argument};
            }

            if (!pin->MessageLayout) {
                break;
            }
        }

        return {};
    }

    void PreserveUnplannedListContext(Index id)
    {
        const auto& entry = Syntax_[id];
        const auto& node = Region(entry);

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

            const auto owner = Spacing_.Gaps()[gap].Owner;

            if ((owner == entry.Region || (Region(entry).Parent != None && owner == Region(entry).Parent)) &&
                !Gaps_[gap].ContextKnown)
            {
                Gaps_[gap].Context = GapContext::Preserved;
                Gaps_[gap].ContextKnown = true;
            }
        };
        const bool prefixed = node.Kind == NodeKind::BaseClause || node.Kind == NodeKind::ConstructorInitializerList;
        preserve(node.Tokens.Begin + 1);

        if (prefixed) {
            preserve(node.Tokens.Begin);
        } else {
            preserve(node.Tokens.End - 1);
        }

        for (auto child = Region(entry).FirstChild; child != None; child = Region(Syntax_[child]).NextSibling) {
            const auto& part = Region(Syntax_[child]);

            if (part.Kind == NodeKind::ListSeparator || part.Kind == NodeKind::RangeForSeparator) {
                preserve(part.Tokens.Begin);
                preserve(part.Tokens.Begin + 1);
            }
        }

        if (node.Kind == NodeKind::ForHeader || node.Kind == NodeKind::Condition) {
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

    // Named casts have a single operand rather than an ArgumentList node.
    // Its parentheses still own an independent delimiter continuation.
    Index NamedCastOpening(Index id) const
    {
        const auto& node = Region(Syntax_[id]);

        if (node.Kind != NodeKind::CastExpression || node.FirstChild == None ||
            Region(Syntax_[node.FirstChild]).Kind != NodeKind::TemplateArgumentList)
        {
            return None;
        }

        const auto opening = NextNonComment(Region(Syntax_[node.FirstChild]).Tokens.End);
        return opening < node.Tokens.End && Tree_.Kind(opening) == TokenKind::Operator_OpenParenthesis &&
            Tree_.MatchingDelimiter(opening) == node.Tokens.End - 1
            ? opening
            : None;
    }

    void PlanEnumMacro(Index id)
    {
        const auto& node = Region(Syntax_[id]);

        if (!Syntax_[id].Safe || node.FirstChild == None) {
            return;
        }

        const auto& header = Region(Syntax_[node.FirstChild]);
        auto close = node.Tokens.End - 1;

        if (Tree_.Kind(close) == TokenKind::Operator_Semicolon) {
            --close;
        }

        Index width = 0;

        for (auto child = header.NextSibling; child != None; child = Region(Syntax_[child]).NextSibling) {
            const auto& entry = Region(Syntax_[child]);

            if (entry.Kind == NodeKind::MacroEnumEntry &&
                Tree_.Kind(entry.Tokens.Begin + 1) == TokenKind::Operator_OpenParenthesis)
            {
                width = std::max(width, HorizontalColumns(Tree_.TokenText(entry.Tokens.Begin + 2), Options_.TabWidth));
            }
        }

        // GapSpec uses a bounded space count. An unusually long identifier
        // must not truncate alignment or introduce input-sized scratch storage.
        if (width >= UINT16_MAX) {
            return;
        }

        const auto indent = Policies_.IndentWidth;
        const auto group = GroupAt(
            TokenPosition(node.Tokens.Begin),
            GapPosition(node.Tokens.End),
            document::GroupMode::Consistent,
            indent,
            LayoutIndentation{ContinuationAlignment::Block, indent, indent});
        Groups_[group].Spec.StructuralBase = true;
        Groups_[group].Spec.LineBase = true;
        const auto flat = [&] (Index gap, Index spaces) {
            Own(gap, group, Boundary::Flat);
            Gaps_[gap].FixedSpaces = uint16_t(spaces);
        };
        const auto opening = NextNonComment(header.Tokens.Begin + 1);
        flat(opening, 0);
        flat(opening + 1, 0);

        for (auto token = opening + 1; token < header.Tokens.End; ++token) {
            if (Tree_.Kind(token) == TokenKind::Operator_Comma) {
                flat(token, 0);

                if (token + 1 < header.Tokens.End) {
                    flat(token + 1, 1);
                }
            } else if (Tree_.MatchingDelimiter(token) > token && Tree_.MatchingDelimiter(token) < header.Tokens.End) {
                token = Tree_.MatchingDelimiter(token);
            }
        }

        for (auto child = header.NextSibling; child != None; child = Region(Syntax_[child]).NextSibling) {
            const auto& entry = Region(Syntax_[child]);
            const auto begin = entry.Tokens.Begin;
            const auto end = entry.Tokens.End;

            if (entry.Kind == NodeKind::Comment) {
                if (Safety_.Gaps()[begin].HasLineFeed) {
                    Own(begin, group, Boundary::Hard);
                    Gaps_[begin].Reindent = true;
                }

                continue;
            }

            Own(begin, group, Boundary::Hard);
            Gaps_[begin].Reindent = true;
            flat(begin + 1, 0);
            flat(end - 1, 0);

            if (Tree_.Kind(begin + 1) == TokenKind::Operator_OpenParenthesis) {
                flat(begin + 2, 0);
                flat(begin + 3, 0);
                const auto nameWidth = HorizontalColumns(Tree_.TokenText(begin + 2), Options_.TabWidth);
                flat(begin + 4, width - nameWidth + 1);
            }
        }

        Own(close, group, Boundary::Hard, document::GapKind::Flat, true);
        Gaps_[close].Reindent = true;

        if (close + 1 < node.Tokens.End) {
            flat(close + 1, 0);
        }
    }

    void PlanList(Index id, const ListPolicy& policy)
    {
        const auto& node = Region(Syntax_[id]);

        if (node.Tokens.Size() < 2) {
            return;
        }

        const bool prefixed = node.Kind == NodeKind::BaseClause || node.Kind == NodeKind::ConstructorInitializerList;
        const auto begin = node.Tokens.Begin;
        const auto end = node.Tokens.End;
        const auto closer = prefixed ? end : end - 1;
        bool sharedCloser = false;

        if ((node.Kind == NodeKind::TemplateArgumentList || node.Kind == NodeKind::TemplateParameterList) &&
            Tree_.Kind(closer) == TokenKind::Operator_RightShift)
        {
            for (auto parent = node.Parent; parent != None; parent = Region(Syntax_[parent]).Parent) {
                const auto& outer = Region(Syntax_[parent]);

                if ((outer.Kind == NodeKind::TemplateArgumentList || outer.Kind == NodeKind::TemplateParameterList) &&
                    outer.Tokens.End == end)
                {
                    sharedCloser = true;
                    break;
                }
            }
        }

        const bool canonical = policy.Layout == LayoutMode::Canonical;
        const auto pinned = canonical ? PinArgument(id) : PinnedArgument{};
        const auto* pin = pinned.Annotation;
        const bool messageLayout = pin && pin->MessageLayout;
        const auto messageEnd = messageLayout ? Region(Syntax_[pinned.Argument]).Tokens.End : None;

        if (sharedCloser && node.Tokens.Size() == 2) {
            return;
        }

        const auto group = GroupAt(
            prefixed ? GapPosition(begin) : GapPosition(begin + 1),
            GapPosition(sharedCloser ? closer : end),
            document::GroupMode::Fill,
            int32_t(policy.Indent.ContinuationOffset),
            policy.Indent);

        if (group == None) {
            return;
        }

        Groups_[group].Spec.StructuralBase = true;
        // An opaque callee spine may have no chain group to provide its level.
        // Arguments still continue from the call's physical opening line.
        Groups_[group].Spec.LineBase = node.Kind == NodeKind::ArgumentList;

        if (node.Kind == NodeKind::TemplateParameterList || node.Kind == NodeKind::TemplateArgumentList ||
            node.Kind == NodeKind::LambdaCaptureList)
        {
            // Prefer surrounding declaration, call or initializer breaks,
            // including the lambda's independent parameter list.
            Groups_[group].Spec.BreakPenalty = 3;
        }

        if (node.Kind == NodeKind::ParameterList && node.Parent != None &&
            (Region(Syntax_[node.Parent]).Kind == NodeKind::FunctionDeclarator ||
                (Region(Syntax_[node.Parent]).Kind == NodeKind::LambdaExpression &&
                    HasPositionedConstraint(node.Parent))) && Policies_.DeclarationHeaders == LayoutMode::Canonical)
        {
            const auto& parent = Region(Syntax_[node.Parent]);

            for (auto child = parent.FirstChild; child != None; child = Region(Syntax_[child]).NextSibling) {
                if (Region(Syntax_[child]).Kind == NodeKind::TrailingReturnType ||
                    (Region(Syntax_[child]).Kind == NodeKind::RequiresClause &&
                        Policies_.RequiresClausePosition != RequiresClausePosition::Preserve))
                {
                    // Prefer moving a trailing header part over splitting an
                    // otherwise fitting parameter list.
                    Groups_[group].Spec.BreakPenalty = 1;
                    break;
                }
            }
        }

        // Semicolon-separated control clauses choose their own boundaries,
        // rather than splitting an initializer to keep every clause inline.
        Groups_[group].Spec.ChildBreaks = node.Kind != NodeKind::ForHeader;

        if (node.Kind != NodeKind::InitializerList && node.Kind != NodeKind::EnumeratorList) {
            Groups_[group].Spec.Continuation = true;
        }

        if (node.Kind == NodeKind::ArgumentList && node.Parent != None &&
            Region(Syntax_[node.Parent]).Kind == NodeKind::CallExpression)
        {
            const auto callee = Region(Syntax_[node.Parent]).FirstChild;

            if (callee != None && !Syntax_[callee].Safe) {
                const auto& leading = Safety_.Gaps()[Region(Syntax_[callee]).Tokens.Begin];

                if (leading.Preserve && leading.HasLineFeed) {
                    // A protected incoming line can leave surrounding lists
                    // unplanned. Use the actual opening line instead of the
                    // enclosing block's unrealized continuation level.
                    Groups_[group].Spec.LineBase = true;
                    Groups_[group].Spec.Continuation = false;
                }
            }
        }

        if (canonical && node.Kind == NodeKind::Condition && node.Parent != None &&
            Region(Syntax_[node.Parent]).Kind == NodeKind::IfStatement &&
            Policies_.Expressions.Layout == LayoutMode::Canonical && Policies_.IfConditionsPacking != ListPacking::Fill)
        {
            Syntax_[id].ListGroup = group;
        }

        const bool empty = !prefixed && node.Tokens.Size() == 2;
        Groups_[group].Spec.ForceBreak = canonical && !empty && policy.Break == BreakRequirement::Always;

        if (empty && canonical) {
            Own(closer, group, Boundary::Flat, document::GapKind::Flat, true);
            return;
        }

        if (canonical) {
            // Fill is used even for vertical lists: a broken first-item Soft
            // boundary can stay inline, while conditional separator Hard gaps
            // couple every subsequent item to the list's actual shape.
            auto firstItem = policy.FirstItem;

            if (node.Kind == NodeKind::ArgumentList && Policies_.CallsSoleArgumentFirstItem != firstItem) {
                // Count syntax arguments, not commas inside their expressions
                // or trivia. Stop as soon as the sole-argument override cannot
                // apply. A string's multiple fragments keep the ordinary
                // first-item placement without changing semantic arity.
                unsigned argumentCount = 0;
                bool stringSequence = false;

                for (auto child = Region(Syntax_[id]).FirstChild;
                    child != None;
                    child = Region(Syntax_[child]).NextSibling)
                {
                    if (Region(Syntax_[child]).Kind == NodeKind::Argument) {
                        if (++argumentCount == 2) {
                            break;
                        }

                        const auto expression = Region(Syntax_[child]).FirstChild;
                        stringSequence = Policies_.StringLiteralFragmentsAsArguments && expression != None &&
                            Syntax_[expression].String && Region(Syntax_[expression]).Tokens.Size() > 1;
                    }
                }

                if (argumentCount == 1 && !stringSequence) {
                    firstItem = Policies_.CallsSoleArgumentFirstItem;
                }
            }

            const auto pinnedExpression = pin ? Region(Syntax_[pinned.Argument]).FirstChild : None;
            const bool forcePinnedFragments = Policies_.StringLiteralFragmentsAsArguments && pinnedExpression != None &&
                Syntax_[pinnedExpression].String && Region(Syntax_[pinnedExpression]).Tokens.Size() > 1 &&
                policy.Break == BreakRequirement::Always;
            auto firstKind = forcePinnedFragments || (firstItem == FirstItemPlacement::NextLine && !pin)
                ? document::GapKind::Hard
                : document::GapKind::Soft;

            if (pin && pin->Overflow == AttachedStringOverflow::KeepInline && policy.Break != BreakRequirement::Always)
            {
                // Attach the first argument's opening piece; its nested
                // syntax (including literal concatenation) may still wrap.
                firstKind = document::GapKind::Flat;
            }

            if (node.Kind == NodeKind::TemplateArgumentList && policy.Break == BreakRequirement::IfNeeded &&
                policy.Closer == CloserPlacement::SameLine && node.FirstChild != None &&
                Region(Syntax_[node.FirstChild]).Kind == NodeKind::TemplateArgument &&
                Region(Syntax_[node.FirstChild]).NextSibling == None)
            {
                // A sole template argument has no separators to couple.
                // Keep its opener flat when cheaper nested syntax can break;
                // an owned break still places the sole item on the next line.
                firstKind = document::GapKind::Soft;
            }

            Own(begin + 1, group, Boundary::Conditional, firstKind);

            if (pin && pin->Overflow == AttachedStringOverflow::BreakBefore && Policies_.ColumnLimit != 0) {
                // Demand the complete argument, not a prefix shortened by
                // optional nested breaks. FitThrough also includes its
                // mandatory same-line punctuation suffix.
                const auto first = node.FirstChild;
                // Child-only trials keep the opener flat without consulting
                // its fit demand; the list must own this overflow decision.
                Groups_[group].Spec.ChildBreaks = false;
                Gaps_[begin + 1].FitNextSegment = true;
                Gaps_[begin + 1].FitThrough = Region(Syntax_[first]).Tokens.End;
            }

            if (node.Kind == NodeKind::ArgumentList && firstKind == document::GapKind::Soft &&
                !(pin && pin->Overflow == AttachedStringOverflow::BreakBefore))
            {
                for (auto child = node.FirstChild; child != None; child = Region(Syntax_[child]).NextSibling) {
                    if (Region(Syntax_[child]).Kind != NodeKind::Argument) {
                        continue;
                    }

                    const auto expression = Region(Syntax_[child]).FirstChild;
                    auto body = None;

                    if (expression != None) {
                        const auto kind = Region(Syntax_[expression]).Kind;

                        if (kind == NodeKind::BracedInitializer) {
                            body = expression;
                        } else if (kind == NodeKind::LambdaExpression || kind == NodeKind::TypeConstructionExpression) {
                            const auto bodyKind = kind == NodeKind::LambdaExpression
                                ? NodeKind::LambdaBody
                                : NodeKind::BracedInitializer;

                            for (auto part = Region(Syntax_[expression]).FirstChild;
                                part != None;
                                part = Region(Syntax_[part]).NextSibling)
                            {
                                if (Region(Syntax_[part]).Kind == bodyKind) {
                                    body = part;
                                    break;
                                }
                            }
                        }
                    }

                    if (body != None) {
                        // Fit through the opening brace. Optional breaks in
                        // the body must not push a fitting argument header
                        // onto the next line or hide an overflowing header.
                        Gaps_[begin + 1].FitNextSegment = true;
                        Gaps_[begin + 1].FitThrough = Region(Syntax_[body]).Tokens.Begin;
                        Groups_[group].Spec.ChildBreaks = false;
                    }

                    break;
                }
            }

            if (firstKind == document::GapKind::Soft &&
                (node.Kind == NodeKind::Condition || node.Kind == NodeKind::ForHeader))
            {
                // Let the expression select its own breaks. A fitting first
                // segment need not move just because the whole header breaks.
                Gaps_[begin + 1].FitNextSegment = true;
            }

            if (!prefixed && !sharedCloser) {
                Own(
                    closer,
                    group,
                    Boundary::Conditional,
                    policy.Closer == CloserPlacement::OwnLine ? document::GapKind::Hard : document::GapKind::Flat,
                    true);
            }
        } else {
            Own(begin + 1, group, Boundary::Preserve);
            Reindent(begin + 1, group);

            if (!prefixed && !sharedCloser) {
                Own(closer, group, Boundary::Preserve, document::GapKind::Flat, true);
                Reindent(closer, group, true);
            }
        }

        OwnCommentLinks(begin + 1, group, closer);
        const auto* header = node.Kind == NodeKind::BaseClause ? &Policies_.BaseLists : &Policies_.ConstructorLists;

        if (prefixed && policy.Indent.Alignment == ContinuationAlignment::Delimiter) {
            Groups_[group].PrefixGap = begin + 1;
            Groups_[group].AnchorAfterGap =
                canonical ? header->Colon == OperatorSide::Leading : !Safety_.Gaps()[begin + 1].HasLineFeed;
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
                header->Colon == OperatorSide::Leading ? firstKindFor(policy) : document::GapKind::Flat);
            Own(
                begin + 1,
                group,
                Boundary::Conditional,
                header->Colon == OperatorSide::Trailing ? firstKindFor(policy) : document::GapKind::Flat);
        }

        // Delimiter edges can break independently of the items. Consistent
        // item gaps keep the complete row together or split every separator.
        const auto items = canonical && !empty && !messageLayout && policy.Packing == ListPacking::AllOrVertical
            ? PackingGroup(group, TokenPosition(begin + 1), GapPosition(closer))
            : group;

        if (node.Kind == NodeKind::ArgumentList) {
            Syntax_[id].ListGroup = items;
        }

        Index lastItem = None;
        Index lastSeparator = None;

        for (auto child = Region(Syntax_[id]).FirstChild; child != None; child = Region(Syntax_[child]).NextSibling) {
            const auto& part = Region(Syntax_[child]);
            const auto t = part.Tokens.Begin;
            const bool separator = part.Kind == NodeKind::ListSeparator || part.Kind == NodeKind::RangeForSeparator;

            if (!separator) {
                if (canonical && part.Kind == NodeKind::InitializerElement && Syntax_[child].Safe) {
                    // Designator assignments belong to the list, while the
                    // value's nested expression plans its own breaks.
                    DirectTokens(
                        child,
                        [&] (Index token) {
                            if (Tree_.Kind(token) != TokenKind::Operator_Assign) {
                                return;
                            }

                            for (auto gap : {token, token + 1}) {
                                if (Tree_.Kind(gap - 1) != TokenKind::Comment && Tree_.Kind(gap) != TokenKind::Comment)
                                {
                                    Own(gap, group, Boundary::Flat);
                                    Gaps_[gap].SpaceFallback = Safety_.Gaps()[gap].HasLineFeed;
                                }
                            }
                        });
                }

                if (canonical && part.Kind == NodeKind::ParameterDeclaration && Syntax_[child].Safe &&
                    part.FirstChild != None && Region(Syntax_[part.FirstChild]).Kind == NodeKind::DeclarationSpecifiers)
                {
                    // The list owns breaks between parameters; each complete
                    // declaration keeps its type and declarator together.
                    // Nested lists and default expressions plan independently.
                    ProjectHeaderGaps(child, t, part.Tokens.End, group);
                }

                if (part.Kind != NodeKind::Comment && part.Kind != NodeKind::PreprocessingDirective &&
                    part.Tokens.Begin > begin && part.Tokens.End <= closer)
                {
                    lastItem = child;
                    // Comment topology stays preserved, but spacing owned by
                    // this list follows its layout throughout attached runs.
                    const auto spacingContext = [&] (Index gap) {
                        if (Spacing_.Gaps()[gap].Owner == Syntax_[id].Region) {
                            Gaps_[gap].Context = canonical ? GapContext::Rebuilt : GapContext::Preserved;
                            Gaps_[gap].ContextKnown = true;
                        }
                    };
                    auto gap = part.Tokens.Begin;
                    spacingContext(gap);

                    while (gap > begin + 1 && Tree_.Kind(gap - 1) == TokenKind::Comment) {
                        spacingContext(--gap);
                    }

                    for (gap = part.Tokens.End; gap < closer && Tree_.Kind(gap) == TokenKind::Comment; ++gap) {
                        spacingContext(gap);
                    }
                }

                continue;
            }

            lastSeparator = t;
            const auto side = prefixed ? header->Comma : OperatorSide::Trailing;

            if (canonical) {
                const auto split = side == OperatorSide::Leading ? t : t + 1;

                if (split < closer) {
                    const bool formatArgument = messageLayout && t >= messageEnd;
                    const bool messagePrefix =
                        messageLayout && t < messageEnd && policy.Break != BreakRequirement::Always;

                    if (formatArgument) {
                        Groups_[group].Spec.ForceBreak = true;
                    }

                    Own(
                        split,
                        items,
                        Boundary::Conditional,
                        messagePrefix
                        ? document::GapKind::Flat
                        : formatArgument || policy.Packing == ListPacking::VerticalWhenBroken
                            ? document::GapKind::Hard
                            : document::GapKind::Soft);
                }

                Own(side == OperatorSide::Leading ? t + 1 : t, group, Boundary::Flat);
            } else {
                Own(t, group, Boundary::Preserve);

                if (t + 1 < closer) {
                    Own(t + 1, group, Boundary::Preserve);
                    Reindent(t + 1, group);
                }

                Reindent(t, group);
            }

            OwnCommentLinks(t + 1, items, closer);
        }

        // Control headers can represent their semicolons as direct punctuation,
        // not ListSeparator children. Walk only disjoint direct-token spans.
        if (node.Kind == NodeKind::ForHeader || node.Kind == NodeKind::Condition) {
            DirectTokens(
                id,
                [&] (Index t) {
                    if (Tree_.Kind(t) == TokenKind::Operator_Semicolon && t + 1 < closer) {
                        if (canonical) {
                            Own(t, group, Boundary::Flat);
                            Own(
                                t + 1,
                                items,
                                Boundary::Conditional,
                                policy.Packing == ListPacking::VerticalWhenBroken
                                ? document::GapKind::Hard
                                : document::GapKind::Soft);
                        } else {
                            Reindent(t + 1, group);
                        }

                        OwnCommentLinks(t + 1, items, closer);
                    }
                });
        }

        if (node.Kind == NodeKind::InitializerList && lastItem != None &&
            Policies_.InitializersTrailingComma != TrailingCommaPolicy::Preserve)
        {
            const auto& item = Region(Syntax_[lastItem]);
            const auto after = item.Tokens.End;

            // A recovered list may have only a parsed prefix of its last
            // element (for example, a type name before an unparsed braced
            // expression). Only a complete element followed by comments and
            // an optional owned trailing comma authorizes punctuation edits.
            if (item.Kind != NodeKind::InitializerElement || item.Tokens.Empty()) {
                return;
            }

            bool existing = false;

            for (auto t = after; t < closer; ++t) {
                if (Tree_.Kind(t) == TokenKind::Comment) {
                    continue;
                }

                if (t == lastSeparator && Tree_.Kind(t) == TokenKind::Operator_Comma && !existing) {
                    existing = true;
                    continue;
                }

                return;
            }

            const auto insertion = Tree_.Token(after - 1).Text.End;

            if (!GapMutable(closer) ||
                // A trailing comment can separate the closer gap from the
                // actual insertion point. Check before removing any comma.
                (Policies_.InitializersTrailingComma == TrailingCommaPolicy::WhenMultiline &&
                    Safety_.IntersectsImmutable({insertion, insertion})) ||
                (existing &&
                    (Safety_.IntersectsImmutable({
                        Tree_.Token(lastSeparator).Text.Begin,
                        Tree_.Token(lastSeparator).Text.End,
                    }) || !GapMutable(lastSeparator) || !Safety_.CanReplaceGap(lastSeparator, "") ||
                        (!canonical && Safety_.Gaps()[lastSeparator].HasLineFeed))))
            {
                // Removing the comma also removes its preceding gap. Keep it
                // if removal would discard a terminator or preserved break.
                return;
            }

            if (existing) {
                Removed_[lastSeparator] = true;
                // The deleted comma's preceding whitespace must not survive as
                // an orphan. Its following closer gap retains the edge rule.
                Gaps_[lastSeparator].Owner = group;
                Gaps_[lastSeparator].Shape = Boundary::Flat;
            }

            if (Policies_.InitializersTrailingComma == TrailingCommaPolicy::WhenMultiline) {
                if (items != group) {
                    // The comma follows the complete list's multiline shape,
                    // including delimiter-only breaks, rather than the row's.
                    Groups_[items].End = GapPosition(after);

                    for (auto gap = after; gap < closer; ++gap) {
                        if (Gaps_[gap].Owner == items) {
                            Gaps_[gap].Owner = group;
                        }
                    }
                }

                Commas_[after] = {group, existing ? lastSeparator : None};
                const auto context = ContextFor(after);

                if (Safety_.Gaps()[after].Horizontal && GapMutable(after) &&
                    Spacing_.SpacePreference(after, context, Safety_, GapAdjacency::AfterComma) !=
                        SpacePreference(after, context))
                {
                    auto& gap = Gaps_[after];
                    gap.Owner = group;
                    gap.Shape = Boundary::Conditional;
                    gap.Broken = document::GapKind::Flat;
                    gap.BrokenAdjacency = GapAdjacency::AfterComma;
                    gap.Context = context;
                    gap.ContextKnown = true;
                }
            }
        }
    }

    static document::GapKind firstKindFor(const ListPolicy& policy)
    {
        return policy.FirstItem == FirstItemPlacement::NextLine ? document::GapKind::Hard : document::GapKind::Soft;
    }

    template <class Function>
    void DirectTokens(Index id, Function function) const
    {
        const auto& node = Region(Syntax_[id]);
        auto cursor = node.Tokens.Begin;

        for (auto child = Region(Syntax_[id]).FirstChild; child != None; child = Region(Syntax_[child]).NextSibling) {
            const auto range = Region(Syntax_[child]).Tokens;

            for (; cursor < std::min(range.Begin, node.Tokens.End); ++cursor) {
                function(cursor);
            }

            cursor = std::max(cursor, range.End);
        }

        for (; cursor < node.Tokens.End; ++cursor) {
            function(cursor);
        }
    }

    Index NextNonComment(Index token) const
    {
        while (token < Tree_.TokenCount() && Tree_.Kind(token) == TokenKind::Comment) {
            ++token;
        }

        return token;
    }

    // Comment attachment fixes topology, not the continuation's indentation.
    // Use one anchor for the comment prefix and its following link, including
    // a line-comment's required terminator.
    void OwnContinuationBoundary(Index begin, Index owner, document::GapKind broken, bool force = false)
    {
        Own(begin, owner, force ? Boundary::Hard : Boundary::Conditional, broken);
        OwnCommentLinks(begin, owner, Tree_.TokenCount() + 1);
    }

    // Carry the boundary's indentation through trivia without reflowing its
    // links. The end gap belongs to the list closer, not its next item.
    void OwnCommentLinks(Index begin, Index owner, Index end)
    {
        for (auto token = begin;
            token < Tree_.TokenCount() && token + 1 < end && Tree_.Kind(token) == TokenKind::Comment;
            ++token)
        {
            Own(token + 1, owner, Boundary::Preserve);
            Reindent(token + 1, owner);
        }
    }

    void PlanTemplateHeader(Index id)
    {
        if (!Policies_.BreakAfterTemplateHeader || Policies_.DeclarationHeaders != LayoutMode::Canonical) {
            return;
        }

        for (auto child = Region(Syntax_[id]).FirstChild; child != None; child = Region(Syntax_[child]).NextSibling) {
            const auto& header = Region(Syntax_[child]);

            if (header.Kind != NodeKind::TemplateParameterList || !Syntax_[child].Safe || header.Tokens.Empty()) {
                continue;
            }

            const auto gap = header.Tokens.End;

            if (gap >= Region(Syntax_[id]).Tokens.End) {
                continue;
            }

            const auto next = NextNonComment(gap);
            const bool constraint =
                next < Region(Syntax_[id]).Tokens.End && Tree_.Kind(next) == TokenKind::Keyword_requires;

            if (constraint && Policies_.RequiresClausePosition != RequiresClausePosition::Preserve) {
                continue;
            }

            // A prefix constraint continues the declaration header; the
            // declaration following that constraint still starts at its base.
            const auto offset = constraint ? Policies_.Parameters.Indent.ContinuationOffset : 0;
            const auto group = GroupAt(
                GapPosition(gap),
                TokenPosition(constraint ? next : gap),
                document::GroupMode::Consistent,
                int32_t(offset));
            Groups_[group].Spec.StructuralBase = true;
            Own(gap, group, Boundary::Hard);

            if (constraint) {
                OwnCommentLinks(gap, group, next + 1);
            }
        }
    }

    void PlanConstraintHeader(Index id)
    {
        const auto& node = Region(Syntax_[id]);
        const auto position = Policies_.RequiresClausePosition;
        const bool canonical =
            Policies_.DeclarationHeaders == LayoutMode::Canonical && position != RequiresClausePosition::Preserve;

        if (canonical) {
            const bool prefix =
                node.Parent != None && Region(Syntax_[node.Parent]).Kind == NodeKind::TemplateDeclaration;
            const bool ownLine =
                position == RequiresClausePosition::OwnLine || position == RequiresClausePosition::OwnLineWithBrace ||
                    position == RequiresClausePosition::WithFollowing;
            const auto offset = ownLine
                ? (Policies_.IndentRequiresClause ? Policies_.IndentWidth : 0)
                : Policies_.Parameters.Indent.ContinuationOffset;
            const auto begin = node.Tokens.Begin;
            const auto leading =
                GroupAt(GapPosition(begin), TokenPosition(begin), document::GroupMode::Fill, int32_t(offset));
            Groups_[leading].Spec.StructuralBase = true;
            Own(begin, leading, ownLine ? Boundary::Hard : Boundary::Soft);
            Gaps_[begin].SpaceFallback = true;
            Gaps_[begin].FitThrough = node.Tokens.End;
            Gaps_[begin].FitNextSegment = true;

            if (!prefix && position == RequiresClausePosition::SingleLine) {
                auto parent = node.Parent;

                while (parent != None && !Syntax_[parent].ItemSequence &&
                    Region(Syntax_[parent]).Kind != NodeKind::LambdaExpression &&
                    Region(Syntax_[parent]).Kind != NodeKind::TemplateDeclaration)
                {
                    parent = Region(Syntax_[parent]).Parent;
                }

                if (parent != None && Region(Syntax_[parent]).Kind == NodeKind::TemplateDeclaration) {
                    const auto header = Region(Syntax_[parent]).FirstChild;

                    if (header != None && Region(Syntax_[header]).Kind == NodeKind::TemplateParameterList &&
                        Syntax_[header].Safe)
                    {
                        const auto gap = Region(Syntax_[header]).Tokens.End;

                        if (Gaps_[gap].Owner == None) {
                            const auto owner = GroupAt(
                                GapPosition(gap),
                                TokenPosition(NextNonComment(gap)),
                                document::GroupMode::Fill);
                            Groups_[owner].Spec.StructuralBase = true;
                            Own(gap, owner, Boundary::Soft);
                            Gaps_[gap].SpaceFallback = true;
                            OwnCommentLinks(gap, owner, NextNonComment(gap) + 1);
                        } else {
                            Gaps_[gap].Shape = Boundary::Soft;
                            Gaps_[gap].SpaceFallback = true;
                        }
                    }
                }
            }

            const auto end = node.Tokens.End;
            const auto next = NextNonComment(end);

            if (next < Tree_.TokenCount() && Tree_.Kind(next) != TokenKind::Operator_Semicolon) {
                if (prefix) {
                    const auto following = GroupAt(GapPosition(end), TokenPosition(next), document::GroupMode::Fill);
                    Groups_[following].Spec.StructuralBase = true;
                    const bool separate = position == RequiresClausePosition::OwnLine ||
                        position == RequiresClausePosition::OwnLineWithBrace ||
                        position == RequiresClausePosition::WithPreceding;
                    Own(end, following, separate ? Boundary::Hard : Boundary::Soft);
                    Gaps_[end].SpaceFallback = true;
                    OwnCommentLinks(end, following, next + 1);
                } else if (Tree_.Kind(next) == TokenKind::Operator_OpenBrace) {
                    // The enclosing function owns its brace and base indentation.
                    // Only its shape changes; explicit brace policies still win.
                    auto parent = node.Parent;

                    while (parent != None && !BraceFor(parent)) {
                        parent = Region(Syntax_[parent]).Parent;
                    }

                    auto& gap = Gaps_[next];

                    if (parent != None && gap.Owner != None && BraceFor(parent) == BracePlacement::Attached) {
                        gap.Shape = position == RequiresClausePosition::OwnLine ||
                            position == RequiresClausePosition::WithPreceding
                            ? Boundary::Hard
                            : Boundary::Flat;
                    }
                }
            }
        }

        const auto group =
            GroupAt(TokenPosition(node.Tokens.Begin), GapPosition(node.Tokens.End), document::GroupMode::Fill);

        if (canonical) {
            Groups_[group].Spec.BreakPenalty = 2;
        }

        // Constraint operands continue from the clause's line, including when
        // that line has its own indentation independent of the declaration.
        Groups_[group].Spec.StructuralBase = true;
        Groups_[group].Spec.LineBase = true;
    }

    void PlanKeywordExpression(Index id)
    {
        const auto& entry = Syntax_[id];
        const auto& node = Region(entry);

        if (Region(entry).FirstChild == None || Policies_.Expressions.Layout != LayoutMode::Canonical) {
            return;
        }

        const auto& indent = Policies_.Expressions.Indent;
        const auto group = GroupAt(
            TokenPosition(node.Tokens.Begin),
            GapPosition(node.Tokens.End),
            document::GroupMode::Fill,
            int32_t(indent.ContinuationOffset),
            indent);
        Groups_[group].Spec.StructuralBase = true;
        Groups_[group].Spec.Continuation = true;
        Groups_[group].Spec.BreakPenalty = 2;
        OwnContinuationBoundary(node.Tokens.Begin + 1, group, document::GapKind::Soft);
        Gaps_[node.Tokens.Begin + 1].SpaceFallback = true;
        // Keep the keyword beside a fitting first segment even when later
        // operators or a nested call need independent breaks.
        Gaps_[node.Tokens.Begin + 1].FitNextSegment = true;
    }

    void PlanCompoundRequirement(Index id)
    {
        const auto& node = Region(Syntax_[id]);
        const auto& policy = Policies_.Expressions;
        const auto& indent = policy.Indent;
        const auto group = GroupAt(
            TokenPosition(node.Tokens.Begin),
            GapPosition(node.Tokens.End),
            document::GroupMode::Fill,
            int32_t(indent.ContinuationOffset),
            indent);
        Groups_[group].Spec.StructuralBase = true;
        Groups_[group].Spec.Continuation = true;

        for (auto child = node.FirstChild; child != None; child = Region(Syntax_[child]).NextSibling) {
            const auto& part = Region(Syntax_[child]);

            if (part.Kind != NodeKind::ExceptionSpecification && part.Kind != NodeKind::TypeConstraint) {
                continue;
            }

            Own(part.Tokens.Begin, group, policy.Layout == LayoutMode::Canonical ? Boundary::Soft : Boundary::Preserve);
            Reindent(part.Tokens.Begin, group);
        }
    }

    bool SameOperatorSequence(Index parent, Index child) const
    {
        const auto& a = Syntax_[parent];
        const auto& b = Syntax_[child];

        if (!a.Safe || !b.Safe || a.Operator == None || b.Operator == None || Region(a).Kind != Region(b).Kind) {
            return false;
        }

        const auto outer = parser::GetBinaryOperatorInfo(Tree_.Kind(a.Operator));
        const auto inner = parser::GetBinaryOperatorInfo(Tree_.Kind(b.Operator));
        return outer.Precedence != 0 && outer.Precedence == inner.Precedence &&
            outer.RightAssociative == inner.RightAssociative &&
            child == (outer.RightAssociative ? LastChild_[parent] : Region(a).FirstChild);
    }

    void PlanStringSequence(Index id)
    {
        const auto& node = Region(Syntax_[id]);
        const auto& policy = Policies_.Calls;
        const bool canonical = policy.Layout == LayoutMode::Canonical;
        const auto parent = node.Parent;
        const bool argument = parent != None && Region(Syntax_[parent]).Kind == NodeKind::Argument;
        const bool initializer = parent != None && Region(Syntax_[parent]).Kind == NodeKind::Initializer;
        const auto enclosing = argument && Region(Syntax_[parent]).Parent != None
            ? Syntax_[Region(Syntax_[parent]).Parent].ListGroup
            : initializer ? Gaps_[node.Tokens.Begin].Owner : None;

        if (enclosing != None && (canonical || argument)) {
            if (initializer) {
                // The initializer owns the assignment boundary, but its string
                // fragments continue with the Calls indentation policy.
                Groups_[enclosing].Spec.Indent = int32_t(policy.Indent.ContinuationOffset);
                Groups_[enclosing].Indent = policy.Indent;
                Groups_[enclosing].Spec.ForceBreak |= policy.Break == BreakRequirement::Always;

                if (Gaps_[node.Tokens.Begin].Shape != Boundary::Hard) {
                    Own(node.Tokens.Begin, enclosing, Boundary::Conditional, document::GapKind::Hard);
                }
            }

            DirectTokens(
                id,
                [&] (Index token) {
                    if (token > node.Tokens.Begin && Tree_.Kind(token) == TokenKind::Literal_String) {
                        if (canonical) {
                            Own(
                                token,
                                enclosing,
                                policy.Packing == ListPacking::Fill ? Boundary::Soft : Boundary::Conditional,
                                document::GapKind::Hard);
                        } else {
                            Own(token, enclosing, Boundary::Preserve);
                            Reindent(token, enclosing);
                        }
                    }
                });
            return;
        }

        const auto group = GroupAt(
            TokenPosition(node.Tokens.Begin),
            GapPosition(node.Tokens.End),
            canonical && policy.Packing == ListPacking::AllOrVertical
            ? document::GroupMode::Consistent
            : document::GroupMode::Fill,
            int32_t(policy.Indent.ContinuationOffset),
            policy.Indent);
        // Like call arguments, fragments continue from their opening line
        // independently of the enclosing expression's continuation offset.
        Groups_[group].Spec.StructuralBase = true;
        Groups_[group].Spec.LineBase = true;
        Groups_[group].Spec.Continuation = true;
        Groups_[group].Spec.ForceBreak = canonical && policy.Break == BreakRequirement::Always;
        DirectTokens(
            id,
            [&] (Index token) {
                if (token == node.Tokens.Begin || Tree_.Kind(token) != TokenKind::Literal_String) {
                    return;
                }

                if (canonical) {
                    Own(
                        token,
                        group,
                        policy.Packing == ListPacking::Fill ? Boundary::Soft : Boundary::Conditional,
                        document::GapKind::Hard);
                } else {
                    Own(token, group, Boundary::Preserve);
                    Reindent(token, group);
                }
            });
    }

    void PlanExpression(Index id)
    {
        const auto& node = Region(Syntax_[id]);

        if (const auto opening = NamedCastOpening(id); opening != None && Safety_.Gaps()[opening + 1].HasLineFeed) {
            // Keep an existing operand line, but give it its own structural
            // level instead of sharing the enclosing call's continuation.
            const auto& indent = Policies_.Expressions.Indent;
            const auto group = GroupAt(
                GapPosition(opening + 1),
                GapPosition(node.Tokens.End),
                document::GroupMode::Fill,
                int32_t(indent.ContinuationOffset),
                indent);

            if (group != None) {
                Groups_[group].Spec.StructuralBase = true;
                Groups_[group].Spec.LineBase = true;
                Groups_[group].Spec.Continuation = true;
                Reindent(opening + 1, group);
                Reindent(node.Tokens.End - 1, group, true);
                OwnCommentLinks(opening + 1, group, node.Tokens.End - 1);
            }

            return;
        }

        const auto kind = node.Kind;
        const bool declarationAssignment = !node.Tokens.Empty() &&
            ((kind == NodeKind::Initializer && Tree_.Kind(node.Tokens.Begin) == TokenKind::Operator_Assign) ||
                (kind == NodeKind::Enumerator && LastChild_[id] != None &&
                    Region(Syntax_[LastChild_[id]]).Kind == NodeKind::EnumeratorValue));

        // Build dispatches expression kinds and string sequences directly;
        // initializers/enumerators participate only through actual assignment.
        if ((kind == NodeKind::Initializer || kind == NodeKind::Enumerator) && !declarationAssignment) {
            return;
        }

        if (node.Tokens.Size() < 2) {
            return;
        }

        if (Syntax_[id].String && Policies_.StringLiteralFragmentsAsArguments) {
            PlanStringSequence(id);
            return;
        }

        if (const auto group = Syntax_[id].ListGroup; group != None) {
            Groups_[group].Spec.ForceBreak |= Policies_.Expressions.Break == BreakRequirement::Always;
            // The header owns these boundaries, not independent precedence
            // groups. Any header break therefore splits all logical operands,
            // even when only a call or a different precedence level wraps.
            DirectTokens(
                id,
                [&] (Index t) {
                    if (Tree_.Kind(t) != TokenKind::Operator_And && Tree_.Kind(t) != TokenKind::Operator_Or) {
                        return;
                    }

                    const auto side = Policies_.Expressions.Binary;
                    Own(
                        side == OperatorSide::Leading ? t : t + 1,
                        group,
                        Boundary::Conditional,
                        document::GapKind::Hard);
                    Own(side == OperatorSide::Leading ? t + 1 : t, group, Boundary::Flat);
                });
            return;
        }

        const auto parent = Region(Syntax_[id]).Parent;

        if (parent != None && SameOperatorSequence(parent, id)) {
            return;
        }

        const auto& policy = Policies_.Expressions;
        const auto& indentation = policy.Indent;
        const bool canonical = policy.Layout == LayoutMode::Canonical;
        const bool threePart =
            kind == NodeKind::ConditionalExpression && policy.TernaryShape == TernaryLayout::ThreePart;
        const auto group = GroupAt(
            kind == NodeKind::Initializer ? GapPosition(node.Tokens.Begin) : TokenPosition(node.Tokens.Begin),
            GapPosition(node.Tokens.End),
            threePart ? document::GroupMode::Consistent : document::GroupMode::Fill,
            int32_t(indentation.ContinuationOffset),
            indentation);

        if (policy.Alignment == ExpressionAlignment::Operand && !declarationAssignment &&
            kind != NodeKind::AssignmentExpression)
        {
            Groups_[group].Indent.Alignment = ContinuationAlignment::Delimiter;
            Groups_[group].Indent.ContinuationOffset = 0;
        }

        auto declaration = parent;

        while (declaration != None &&
            (Region(Syntax_[declaration]).Kind == NodeKind::InitDeclarator ||
                Region(Syntax_[declaration]).Kind == NodeKind::InitDeclaratorList))
        {
            declaration = Region(Syntax_[declaration]).Parent;
        }

        const bool defaultArgument = kind == NodeKind::Initializer && declaration != None &&
            Region(Syntax_[declaration]).Kind == NodeKind::ParameterDeclaration;
        // A default starts a continuation from its parameter's physical line,
        // not the parameter list's already-realized continuation level.
        Groups_[group].Spec.ExpressionContinuation = !defaultArgument;
        Groups_[group].Spec.StructuralBase = defaultArgument;
        Groups_[group].Spec.LineBase = defaultArgument;
        Groups_[group].Spec.Continuation = true;

        if (canonical && threePart && parent != None && Region(Syntax_[parent]).Kind == NodeKind::Initializer &&
            Tree_.Kind(Region(Syntax_[parent]).Tokens.Begin) == TokenKind::Operator_Assign)
        {
            // Prefer the requested branch shape when an initializer wraps,
            // but let cheaper layouts win and keep fitting ternaries flat.
            Groups_[group].Spec.FlatInBrokenParentPenalty = 2;
        }

        // Comparisons may keep their operand/operator prefix while call or
        // list operands break. Other binary sequences retain their own
        // precedence-level breaks; conditionals retain their branch layout.
        const auto expressionOperator = Syntax_[id].Operator;
        const bool comparison = kind == NodeKind::BinaryExpression && expressionOperator != None &&
            (Tree_.Role(expressionOperator) == PunctuationRole::ComparisonOperator ||
                Tree_.Kind(expressionOperator) == TokenKind::Operator_Equals ||
                Tree_.Kind(expressionOperator) == TokenKind::Operator_NotEquals);
        Groups_[group].Spec.ChildBreaks =
            (kind != NodeKind::BinaryExpression || comparison) && kind != NodeKind::ConditionalExpression;
        Groups_[group].Spec.ForceBreak = canonical && policy.Break == BreakRequirement::Always &&
            (declarationAssignment || kind == NodeKind::BinaryExpression || kind == NodeKind::AssignmentExpression ||
                kind == NodeKind::ConditionalExpression || Syntax_[id].String);

        if (kind == NodeKind::ConditionalExpression) {
            Groups_[group].IndentTernaryOperators = policy.TernaryIndentation == TernaryOperatorIndentation::Indent;
            Groups_[group].ShareBranchContinuation = policy.TernaryIndentation == TernaryOperatorIndentation::Align;
            // Arms share the delimiter's realized continuation, rather than
            // adding another level. Capture after its gap so protected or
            // trailing delimiters contribute their actual emitted indentation.
            const auto condition = Region(Syntax_[id]).FirstChild;

            for (auto branch = condition == None ? None : Region(Syntax_[condition]).NextSibling;
                branch != None;
                branch = Region(Syntax_[branch]).NextSibling)
            {
                const auto& arm = Syntax_[branch];

                if (!arm.Safe || Region(arm).Kind == NodeKind::Comment ||
                    Region(arm).Kind == NodeKind::PreprocessingDirective || Region(arm).Tokens.Size() < 2)
                {
                    continue;
                }

                const auto scope = GroupAt(
                    TokenPosition(Region(arm).Tokens.Begin),
                    GapPosition(Region(arm).Tokens.End),
                    document::GroupMode::Fill,
                    int32_t(indentation.ContinuationOffset),
                    indentation);
                Groups_[scope].Spec.ExpressionContinuation = true;
                Groups_[scope].Spec.Continuation = true;
                Groups_[scope].ShareBranchContinuation = true;
            }
        }

        const auto packing = declarationAssignment || kind == NodeKind::AssignmentExpression
            ? policy.AssignmentPacking
            : policy.Packing;
        const auto boundary = packing != ListPacking::Fill && !threePart ? Boundary::Conditional : Boundary::Soft;
        auto segment = id;
        const auto planToken = [&] (Index t) {
            const auto role = declarationAssignment && Tree_.Kind(t) == TokenKind::Operator_Assign
                ? PunctuationRole::AssignmentOperator
                : Tree_.Role(t);

            if (canonical &&
                (kind == NodeKind::ParenthesizedExpression || kind == NodeKind::RequirementExpression ||
                    kind == NodeKind::SubscriptExpression || kind == NodeKind::CastExpression ||
                    kind == NodeKind::TypeConstructionExpression) && Tree_.Kind(t) != TokenKind::Comment)
            {
                if (t > node.Tokens.Begin) {
                    Own(t, group, Boundary::Flat);
                }

                if (t + 1 < node.Tokens.End) {
                    Own(t + 1, group, Boundary::Flat);
                }
            }

            const bool op = role == PunctuationRole::BinaryOperator || role == PunctuationRole::ComparisonOperator ||
                role == PunctuationRole::ShiftOperator || role == PunctuationRole::AssignmentOperator ||
                role == PunctuationRole::ConditionalDelimiter;

            if (op && (t > node.Tokens.Begin || declarationAssignment) && t + 1 < node.Tokens.End) {
                const auto side = role == PunctuationRole::AssignmentOperator
                    ? policy.Assignment
                    : role == PunctuationRole::ShiftOperator
                        ? policy.Shift
                        : role == PunctuationRole::ConditionalDelimiter ? policy.Ternary : policy.Binary;

                if (canonical) {
                    if (role == PunctuationRole::AssignmentOperator &&
                        policy.AssignmentRhs == FirstItemPlacement::NextLine)
                    {
                        Own(t + 1, group, Boundary::Hard);
                        Own(t, group, Boundary::Flat);
                        return;
                    }

                    const auto split = side == OperatorSide::Leading ? t : t + 1;
                    Own(split, group, boundary, document::GapKind::Hard);

                    if (role == PunctuationRole::AssignmentOperator && boundary == Boundary::Soft) {
                        // Moving an RHS that cannot fit intact adds a parent
                        // break without avoiding its independent child breaks.
                        Gaps_[split].FitNextSegmentIfOverflow = true;
                    }

                    if (kind == NodeKind::BinaryExpression && !comparison && boundary == Boundary::Soft &&
                        Policies_.ColumnLimit != 0 && LastChild_[segment] != None &&
                        Region(Syntax_[LastChild_[segment]]).Tokens.Size() > 1)
                    {
                        // Prefer the operator boundary over splitting the next
                        // operand merely to keep its opening fragment inline.
                        Gaps_[split].FitNextSegment = true;
                        Gaps_[split].FitThrough = Region(Syntax_[LastChild_[segment]]).Tokens.End;
                    }

                    Own(side == OperatorSide::Leading ? t + 1 : t, group, Boundary::Flat);
                } else {
                    Own(t, group, Boundary::Preserve);
                    Own(t + 1, group, Boundary::Preserve);
                    Reindent(t, group);
                    Reindent(t + 1, group);
                }
            }

            if (Syntax_[id].String && t > node.Tokens.Begin && Tree_.Kind(t) == TokenKind::Literal_String) {
                if (canonical) {
                    Own(t, group, boundary, document::GapKind::Hard);
                } else {
                    Reindent(t, group);
                }
            }
        };

        while (true) {
            DirectTokens(segment, planToken);

            if (Syntax_[segment].Operator == None) {
                break;
            }

            const auto info = parser::GetBinaryOperatorInfo(Tree_.Kind(Syntax_[segment].Operator));
            const auto next = info.RightAssociative ? LastChild_[segment] : Region(Syntax_[segment]).FirstChild;

            if (next == None || !SameOperatorSequence(segment, next)) {
                break;
            }

            segment = next;
        }
    }

    void Reindent(Index gap, Index owner, bool closing = false, bool force = false)
    {
        if ((force || Policies_.IndentationKind != IndentationKind::Preserve) &&
            (Safety_.Gaps()[gap].HasLineFeed || (gap == 0 && Safety_.Gaps()[gap].Horizontal)))
        {
            // Indentation must not turn an already planned boundary into a
            // source-dependent hard line on the next formatting pass.
            if (Gaps_[gap].Owner != owner || Gaps_[gap].Shape == Boundary::Preserve) {
                Own(gap, owner, Boundary::Preserve, document::GapKind::Flat, closing);
            }

            Gaps_[gap].Reindent = true;
        }
    }

    bool IsCaseLabel(Index id) const
    {
        // Statement attributes wrap the label, not its following body items.
        while (id != None && Region(Syntax_[id]).Kind == NodeKind::AttributedStatement) {
            id = LastChild_[id];
        }

        return id != None && Region(Syntax_[id]).Kind == NodeKind::CaseLabel;
    }

    bool IsSoleCaseBlock(Index id) const
    {
        const auto& block = Region(Syntax_[id]);
        const auto previous = Syntax_[id].PreviousSibling;

        if (block.Kind != NodeKind::CompoundStatement || previous == None || !IsCaseLabel(previous) ||
            Region(Syntax_[previous]).Tokens.End != block.Tokens.Begin)
        {
            return false;
        }

        auto next = block.NextSibling;

        // Comments and directives are not statements in the case body.
        while (next != None &&
            (Region(Syntax_[next]).Kind == NodeKind::Comment ||
                Region(Syntax_[next]).Kind == NodeKind::PreprocessingDirective))
        {
            next = Region(Syntax_[next]).NextSibling;
        }

        // A case consisting solely of one block can attach it to the label.
        // Mixed cases instead give every statement the same body level.
        // An opaque run may introduce new labels, so it cannot establish
        // that the preceding block belongs to a mixed case.
        return next == None || IsCaseLabel(next) || Region(Syntax_[next]).Kind == NodeKind::UnparsedSyntax;
    }

    std::optional<BracePlacement> BraceFor(Index id) const
    {
        const auto& node = Region(Syntax_[id]);
        switch (node.Kind) {
            case NodeKind::FunctionDefinition:
            case NodeKind::MacroFunctionDefinition:
            case NodeKind::FunctionTryBlock: {
                auto function = node.Function;

                if (node.Kind == NodeKind::FunctionTryBlock && node.Parent != None) {
                    function = Region(Syntax_[node.Parent]).Function;
                }

                if (function == parser::FunctionKind::Constructor) {
                    return Policies_.Braces.Constructor;
                }

                if (function == parser::FunctionKind::Destructor) {
                    return Policies_.Braces.Destructor;
                }

                return Policies_.Braces.Function;
            }
            case NodeKind::NamespaceDeclaration:
                return Policies_.Braces.Namespace;
            case NodeKind::EnumDeclaration:
                return Policies_.Braces.Enum;
            case NodeKind::RecordDeclaration:
                if (Tree_.Kind(node.Tokens.Begin) == TokenKind::Keyword_struct) {
                    return Policies_.Braces.Struct;
                }

                if (Tree_.Kind(node.Tokens.Begin) == TokenKind::Keyword_union) {
                    return Policies_.Braces.Union;
                }

                return Policies_.Braces.Class;
            case NodeKind::LambdaExpression:
                return Policies_.Braces.Lambda;
            default:
                if (IsControl(node.Kind) || node.Kind == NodeKind::TryStatement) {
                    return node.Kind == NodeKind::CaseLabel ? Policies_.Braces.Case : Policies_.Braces.Control;
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
            case NodeKind::ClassVirtSpecifier:
            case NodeKind::ExceptionSpecification:
            case NodeKind::TrailingReturnType:
            case NodeKind::TypeId:
            case NodeKind::CallingConventionSpecifier:
                return true;
            default:
                return false;
        }
    }

    static bool HeaderBreakPart(NodeKind kind)
    {
        switch (kind) {
            case NodeKind::DeclaratorName:
            case NodeKind::DestructorName:
            case NodeKind::OperatorFunctionName:
            case NodeKind::FunctionQualifier:
            case NodeKind::ExceptionSpecification:
            case NodeKind::TrailingReturnType:
                return true;
            default:
                return false;
        }
    }

    void PlanInitializedDeclarator(Index id)
    {
        const auto& node = Region(Syntax_[id]);
        const auto initializer = LastChild_[id];
        const auto list = node.Parent;
        const auto declaration = list == None ? None : Region(Syntax_[list]).Parent;

        // Without a type prefix, the leading gap belongs to the enclosing
        // statement sequence, not a declarator continuation.
        if (initializer == None || Region(Syntax_[initializer]).Kind != NodeKind::Initializer || declaration == None ||
            node.Tokens.Begin != Region(Syntax_[list]).Tokens.Begin ||
            (Region(Syntax_[declaration]).Kind != NodeKind::Declaration &&
                Region(Syntax_[declaration]).Kind != NodeKind::DeclarationStatement) ||
            node.Tokens.Begin == Region(Syntax_[declaration]).Tokens.Begin ||
            Policies_.DeclarationHeaders != LayoutMode::Canonical ||
            PrefixProtection_[node.Tokens.Begin] != PrefixProtection_[node.Tokens.End])
        {
            return;
        }

        // Keep both the type and initializer outside the declarator group:
        // adding a parent around the RHS changes its bounded layout search.
        const auto end = Region(Syntax_[initializer]).Tokens.Begin;

        if (end <= node.Tokens.Begin) {
            return;
        }

        const auto& indent = Policies_.Expressions.Indent;
        const auto group = GroupAt(
            GapPosition(node.Tokens.Begin),
            GapPosition(end),
            document::GroupMode::Fill,
            int32_t(indent.ContinuationOffset),
            indent);
        Groups_[group].Spec.StructuralBase = true;
        Groups_[group].Spec.Continuation = true;
        const bool leading = Policies_.Expressions.Layout == LayoutMode::Canonical
            ? Tree_.Kind(end) == TokenKind::Operator_Assign &&
                Policies_.Expressions.Assignment == OperatorSide::Leading &&
                Policies_.Expressions.AssignmentRhs != FirstItemPlacement::NextLine
            : Safety_.Gaps()[end].HasLineFeed;
        // Only the declarator and an attached initializer opener must fit.
        // An unbreakable or preserved RHS must not move a fitting prefix.
        Groups_[group].Spec.FitSuffixWidth =
            leading ? 0 : AddWidth(FlatWidth(end), HorizontalColumns(Tree_.TokenText(end), Options_.TabWidth));
        ProjectHeaderGaps(id, node.Tokens.Begin, end, group);
    }

    void ProjectHeaderGaps(Index id, Index begin, Index end, Index group)
    {
        const auto kind = Region(Syntax_[id]).Kind;
        const bool function = kind == NodeKind::FunctionDeclaration || kind == NodeKind::FunctionDefinition ||
            kind == NodeKind::FunctionTryBlock;
        const bool declaration = function || kind == NodeKind::InitDeclarator;

        if (declaration) {
            // Prefer list/initializer breaks, but allow a long type to
            // separate from the complete declarator name when needed.
            Groups_[group].Spec.BreakPenalty = 2;
        }

        Stack_.clear();
        Stack_.push_back(id);

        while (!Stack_.empty()) {
            const auto current = Stack_.back();
            Stack_.pop_back();
            const auto& part = Region(Syntax_[current]);
            const bool suffix = function && !part.Tokens.Empty() &&
                (part.Kind == NodeKind::FunctionQualifier || part.Kind == NodeKind::ExceptionSpecification ||
                    part.Kind == NodeKind::TrailingReturnType);
            const auto owner = suffix
                ? GroupAt(GapPosition(part.Tokens.Begin), GapPosition(part.Tokens.End), document::GroupMode::Fill)
                : group;

            if (suffix) {
                // Keep qualifiers beside the parameter closer; a return type
                // can instead move intact before breaking its template lists.
                const bool trailing = part.Kind == NodeKind::TrailingReturnType;
                Groups_[owner].Spec.BreakPenalty = trailing ? 0 : 2;

                if (trailing) {
                    const auto offset = Policies_.Parameters.Indent.ContinuationOffset;
                    Groups_[owner].Spec.Indent = int32_t(offset);
                    Groups_[owner].Spec.StructuralBase = true;
                    Groups_[owner].Spec.Continuation = true;
                    Groups_[owner].Indent = {ContinuationAlignment::Block, offset, offset};
                    Groups_[owner].HasAnchor = true;
                    Gaps_[part.Tokens.Begin].FitThrough = part.Tokens.End;
                }
            }

            Index nameBegin = None;

            if (declaration && part.Kind == NodeKind::Declarator) {
                auto prefix = part.Tokens.Begin;

                for (auto child = part.FirstChild; child != None; child = Region(Syntax_[child]).NextSibling) {
                    const auto& node = Region(Syntax_[child]);

                    if ((node.Kind == NodeKind::PointerOperator || node.Kind == NodeKind::MemberPointerOperator) &&
                        node.Tokens.Begin == prefix)
                    {
                        prefix = node.Tokens.End;

                        // Pointer cv-qualifiers are direct declarator tokens.
                        while (prefix < part.Tokens.End &&
                            (Tree_.Kind(prefix) == TokenKind::Keyword_const ||
                                Tree_.Kind(prefix) == TokenKind::Keyword_volatile))
                        {
                            ++prefix;
                        }
                    } else if (node.Kind == NodeKind::DeclaratorName || node.Kind == NodeKind::DestructorName ||
                        node.Kind == NodeKind::OperatorFunctionName)
                    {
                        // Qualified prefixes are direct declarator tokens;
                        // the name child may contain only the terminal name.
                        nameBegin = prefix;

                        if (prefix < node.Tokens.Begin) {
                            Gaps_[prefix].FitThrough = node.Tokens.End;
                        }

                        break;
                    }
                }
            }

            Index operandEnd = 0;
            DirectTokens(
                current,
                [&] (Index t) {
                    const auto& node = Region(Syntax_[current]);
                    // decltype and extension operands can be direct tokens,
                    // not expression children. Keep those balanced units flat.
                    const bool operandOpening = Tree_.Kind(t) == TokenKind::Operator_OpenParenthesis ||
                        Tree_.Kind(t) == TokenKind::Operator_OpenBracket;
                    const bool operand = t < operandEnd || operandOpening;

                    if (node.Kind == NodeKind::DeclarationSpecifiers && operandOpening) {
                        const auto close = Tree_.MatchingDelimiter(t);

                        if (close != None) {
                            operandEnd = std::max(operandEnd, close + 1);
                        }
                    }

                    const bool opaqueExceptionOperand =
                        node.Kind == NodeKind::ExceptionSpecification && t > node.Tokens.Begin + 1;

                    if (!opaqueExceptionOperand && (t > begin || (declaration && !function && t == begin)) && t < end &&
                        GapMutable(t) && Tree_.Kind(t) != TokenKind::Comment &&
                        (kind != NodeKind::ParameterDeclaration || Tree_.Kind(t - 1) != TokenKind::Comment) &&
                        (Spacing_.Gaps()[t].Category == SpacingOwner::DeclarationHeaders ||
                            Spacing_.Gaps()[t].Category == SpacingOwner::ControlHeaders ||
                            (kind == NodeKind::ParameterDeclaration &&
                                Spacing_.Gaps()[t].Category == SpacingOwner::Parameters)))
                    {
                        const bool separated =
                            Spacing_.Gaps()[t].FallbackSpace || Safety_.Gaps()[t].Separation == GapSeparation::Required;
                        // A qualified name breaks before its complete prefix,
                        // never between the final :: and terminal identifier.
                        const bool qualifiedTerminal = (node.Kind == NodeKind::DeclaratorName ||
                            node.Kind == NodeKind::DestructorName || node.Kind == NodeKind::OperatorFunctionName) &&
                            Tree_.Kind(t - 1) == TokenKind::Operator_ScopeResolution;
                        // Keep a grouped pointer/name spine intact; either
                        // function signature can break its own parameter list.
                        const bool groupedName = (node.Kind == NodeKind::DeclaratorName ||
                            node.Kind == NodeKind::DestructorName || node.Kind == NodeKind::OperatorFunctionName) &&
                            node.Parent != None && Region(Syntax_[node.Parent]).Kind == NodeKind::NestedDeclarator;
                        const bool headerPart = (node.Kind == NodeKind::Declarator && separated) ||
                            (HeaderBreakPart(node.Kind) && !qualifiedTerminal && !groupedName);
                        const bool breakable = declaration && !operand && Tree_.Kind(t - 1) != TokenKind::Comment &&
                            ((node.Kind == NodeKind::DeclarationSpecifiers && separated) || t == nameBegin ||
                                (t == node.Tokens.Begin && headerPart));

                        if ((kind == NodeKind::FunctionDeclaration || kind == NodeKind::InitDeclarator) && !breakable) {
                            return;
                        }

                        // Extension attributes can overlap the declarator's
                        // suffix span. Its specific owner replaces an earlier
                        // transparent-header projection, never another owner.
                        if (suffix && Gaps_[t].Owner == group) {
                            Gaps_[t].Owner = None;
                        }

                        Own(t, owner, breakable ? Boundary::Soft : Boundary::Flat);
                        Gaps_[t].CanonicalFlat = true;
                        Gaps_[t].FitNextSegment = breakable;
                        Gaps_[t].KeepFlatSpacing =
                            kind == NodeKind::FunctionDeclaration || kind == NodeKind::ParameterDeclaration ||
                                kind == NodeKind::InitDeclarator || node.Kind == NodeKind::FunctionQualifier;
                    }
                });

            for (auto child = Region(Syntax_[current]).FirstChild;
                child != None;
                child = Region(Syntax_[child]).NextSibling)
            {
                const auto& node = Region(Syntax_[child]);

                if (!Syntax_[child].Safe || node.Tokens.End <= begin || node.Tokens.Begin >= end) {
                    continue;
                }

                if (declaration &&
                    (node.Kind == NodeKind::AttributeSpecifier || node.Kind == NodeKind::ExtensionAttributeSpecifier) &&
                    node.Tokens.Begin > begin && GapMutable(node.Tokens.Begin) &&
                    Tree_.Kind(node.Tokens.Begin - 1) != TokenKind::Comment)
                {
                    Own(node.Tokens.Begin, group, Boundary::Soft);
                    Gaps_[node.Tokens.Begin].CanonicalFlat = true;
                    Gaps_[node.Tokens.Begin].FitNextSegment = true;
                    Gaps_[node.Tokens.Begin].KeepFlatSpacing = true;
                }

                if (TransparentHeader(node.Kind)) {
                    Stack_.push_back(child);
                } else if ((node.Kind == NodeKind::ParameterList || node.Kind == NodeKind::Condition ||
                    node.Kind == NodeKind::ForHeader) && node.Tokens.Begin > begin && GapMutable(node.Tokens.Begin))
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

    bool SimpleStatement(Index id) const
    {
        if (id == None || !Syntax_[id].Safe) {
            return false;
        }

        while (Region(Syntax_[id]).Kind == NodeKind::AttributedStatement) {
            id = LastChild_[id];

            if (id == None) {
                return false;
            }
        }

        switch (Region(Syntax_[id]).Kind) {
            case NodeKind::DeclarationStatement:
            case NodeKind::UsingDeclaration:
            case NodeKind::TypedefDeclaration:
            case NodeKind::StaticAssertDeclaration:
            case NodeKind::AsmDeclaration:
            case NodeKind::ExpressionStatement:
            case NodeKind::ReturnStatement:
            case NodeKind::BreakStatement:
            case NodeKind::ContinueStatement:
            case NodeKind::GotoStatement:
            case NodeKind::ThrowStatement:
            case NodeKind::CoYieldStatement:
                return true;
            default:
                return false;
        }
    }

    void PlanControlledArms(Index id, BracePlacement brace)
    {
        const auto& node = Region(Syntax_[id]);
        const auto& policy = Policies_.ControlBodies;
        Index previousEnd = node.Tokens.Begin;
        Index previousBody = None;
        bool first = true;
        Index compactSeparators = None;
        for (auto child = Region(Syntax_[id]).FirstChild; child != None; child = Region(Syntax_[child]).NextSibling) {
            const auto& body = Region(Syntax_[child]);
            const bool suffix =
                node.Kind == NodeKind::DoWhileStatement && previousBody != None && body.Kind == NodeKind::Condition;

            if ((!ControlledBody(body.Kind) && !suffix) || body.Tokens.Empty()) {
                continue;
            }

            const auto precedingBody = previousBody;
            Index begin = node.Tokens.Begin;

            if (!first) {
                begin = None;
                auto part = Region(Syntax_[previousBody]).NextSibling;
                const auto end = std::min(body.Tokens.Begin, node.Tokens.End);

                for (Index t = previousEnd; t < end;) {
                    while (part != None && part != child && Region(Syntax_[part]).Tokens.End <= t) {
                        part = Region(Syntax_[part]).NextSibling;
                    }

                    if (part != None && part != child && Region(Syntax_[part]).Tokens.Begin <= t) {
                        // A descendant's else belongs to that child, not this
                        // arm. Half-open ranges retain a direct else at End.
                        t = std::min(Region(Syntax_[part]).Tokens.End, end);
                        part = Region(Syntax_[part]).NextSibling;
                        continue;
                    }

                    if (Tree_.Kind(t) == (suffix ? TokenKind::Keyword_while : TokenKind::Keyword_else)) {
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
            bool safeHeader = PrefixProtection_[begin] == PrefixProtection_[opening + (braced ? 1 : 0)] &&
                (!suffix || Syntax_[child].Safe);

            for (auto part = Region(Syntax_[id]).FirstChild; part != None; part = Region(Syntax_[part]).NextSibling) {
                const auto range = Region(Syntax_[part]).Tokens;

                if (range.Begin >= begin && range.End <= opening && !Syntax_[part].Safe) {
                    safeHeader = false;
                }
            }

            const bool canonical = safeHeader && Policies_.ControlHeaders.Layout == LayoutMode::Canonical;

            if (begin != node.Tokens.Begin && GapMutable(begin)) {
                const auto& preceding = Region(Syntax_[precedingBody]);
                // Only an adjacent, editable direct-parent boundary may join.
                // Trivia children and protected headers retain their topology.
                const bool adjacent = preceding.Tokens.End == begin && Tree_.Kind(begin - 1) != TokenKind::Comment;
                const bool precedingBraced = preceding.Kind == NodeKind::CompoundStatement;
                Boundary boundary = Boundary::Preserve;

                if (canonical && adjacent) {
                    if (precedingBraced) {
                        boundary = Boundary::Flat;
                    } else if (policy.Layout == LayoutMode::Canonical) {
                        boundary = policy.Shape == BodyShape::CompactIfFits ? Boundary::Soft : Boundary::Hard;
                    }
                }

                Index prefix;

                if (boundary == Boundary::Soft) {
                    if (compactSeparators == None) {
                        compactSeparators = GroupAt(
                            TokenPosition(node.Tokens.Begin),
                            GapPosition(node.Tokens.End),
                            document::GroupMode::Consistent);
                        Groups_[compactSeparators].Spec.StructuralBase = true;
                    }

                    prefix = compactSeparators;
                } else {
                    prefix = GroupAt(GapPosition(begin), TokenPosition(begin), document::GroupMode::Consistent);
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
                    GroupAt(TokenPosition(begin), GapPosition(node.Tokens.End), document::GroupMode::Fill);
                Groups_[header].Spec.StructuralBase = true;

                if (canonical) {
                    ProjectHeaderGaps(id, begin, node.Tokens.End, header);
                }

                continue;
            }

            const auto header = GroupAt(
                TokenPosition(begin),
                braced ? GapPosition(opening + 1) : elseIf ? TokenPosition(opening) : GapPosition(opening),
                document::GroupMode::Fill);
            Groups_[header].Spec.StructuralBase = true;

            if (braced) {
                // Prefer the body's independent break over splitting a fitting header.
                Groups_[header].Spec.BreakPenalty = 1;
            }

            if (canonical) {
                ProjectHeaderGaps(id, begin, opening, header);
            }
            if (braced) {
                // Opaque header descendants prevent reflow, not placement of
                // a separately proven, editable body delimiter boundary.
                const bool canonicalBrace =
                    Policies_.ControlHeaders.Layout == LayoutMode::Canonical && GapMutable(opening);
                Own(
                    opening,
                    header,
                    !canonicalBrace
                    ? Boundary::Preserve
                    : brace == BracePlacement::NextLine
                        ? Boundary::Hard
                        : brace == BracePlacement::Attached ? Boundary::Flat : Boundary::Conditional,
                    document::GapKind::Hard,
                    true);
                if (canonicalBrace && brace == BracePlacement::NextLineIfHeaderIndented) {
                    Gaps_[opening].BraceIndentOffset = std::max<uint8_t>(1, policy.IndentWidth);
                }
                if (!canonicalBrace) {
                    Reindent(opening, header, true);
                }
            } else if (elseIf) {
                Own(opening, header, canonical ? Boundary::Flat : Boundary::Preserve);
            } else {
                const auto group = GroupAt(
                    GapPosition(opening),
                    GapPosition(body.Tokens.End),
                    document::GroupMode::Consistent,
                    int32_t(policy.IndentWidth));
                Groups_[group].Spec.StructuralBase = true;

                if (policy.Layout == LayoutMode::Canonical) {
                    Own(
                        opening,
                        group,
                        policy.Shape == BodyShape::Expanded ||
                            (policy.Shape == BodyShape::CompactSingleStatementIfFits && !SimpleStatement(child))
                        ? Boundary::Hard
                        : Boundary::Soft);
                } else {
                    Reindent(opening, group);
                }
            }
        }
    }

    Index PreservedBodyIndent(Index id, Index opening, Index close) const
    {
        constexpr auto uncertain = uint8_t(RegionStatus::Ambiguous) | uint8_t(RegionStatus::Recovery);

        if ((uint8_t(Region(Syntax_[id]).Status) & uncertain) == 0) {
            return None;
        }

        const auto parent = Region(Syntax_[id]).Parent;
        const bool switchBody = parent != None && Region(Syntax_[parent]).Kind == NodeKind::SwitchStatement;
        bool labeled = false;

        for (auto child = Region(Syntax_[id]).FirstChild; child != None; child = Region(Syntax_[child]).NextSibling) {
            const auto& part = Region(Syntax_[child]);
            labeled |= IsCaseLabel(child) || part.Kind == NodeKind::LabelStatement;

            if (Syntax_[child].Safe || part.Tokens.Empty() || part.Tokens.Begin <= opening || part.Tokens.End > close) {
                continue;
            }

            const auto indent = SourceIndent(part.Tokens.Begin);

            if (indent == None || indent > uint32_t(INT32_MAX)) {
                continue;
            }

            // A nested control/body can freeze interior indentation even
            // when its leading gap is editable. Its authored level constrains
            // this body too. Expressions containing independently anchored
            // lambdas do not constrain their enclosing statement's level.
            const bool nestedBody =
                IsControl(part.Kind) || part.Kind == NodeKind::TryStatement || part.Kind == NodeKind::CompoundStatement;
            const auto reasons = uint16_t(Safety_.Gaps()[part.Tokens.Begin].Reasons);

            // An opaque switch run before any explicit label may contain
            // generated labels. Its fixed indentation anchors this body, not
            // an inferred case-content level. Recovery inside a known labeled
            // region or another body does not establish this level.
            if ((reasons & uint16_t(ProtectionReason::Ambiguous)) != 0 ||
                (switchBody && !labeled && (reasons & uint16_t(ProtectionReason::Recovery)) != 0) ||
                (nestedBody && !Syntax_[child].IndentSafe &&
                    (uint8_t(part.Status) & uint8_t(RegionStatus::Ambiguous)) != 0))
            {
                return indent;
            }
        }

        return None;
    }

    const BodyPolicy& PolicyForBody(Index id) const
    {
        auto kind = Region(Syntax_[id]).Kind;

        if (kind == NodeKind::LambdaBody || kind == NodeKind::LambdaExpression) {
            return Policies_.LambdaBodies;
        }

        if (kind == NodeKind::CompoundStatement) {
            if (IsSoleCaseBlock(id)) {
                return Policies_.ControlBodies;
            }

            auto parent = Region(Syntax_[id]).Parent;

            while (parent != None && Region(Syntax_[parent]).Kind == NodeKind::AttributedStatement) {
                parent = Region(Syntax_[parent]).Parent;
            }

            kind = parent == None ? kind : Region(Syntax_[parent]).Kind;
        }

        switch (kind) {
            case NodeKind::FunctionDefinition:
            case NodeKind::MacroFunctionDefinition:
            case NodeKind::FunctionTryBlock:
                return Policies_.FunctionBodies;
            case NodeKind::LambdaExpression:
                return Policies_.LambdaBodies;
            case NodeKind::TryStatement:
                return Policies_.ControlBodies;
            default:
                return IsControl(kind) ? Policies_.ControlBodies : Policies_.Bodies;
        }
    }

    bool HasPositionedConstraint(Index id) const
    {
        if (Policies_.DeclarationHeaders != LayoutMode::Canonical ||
            Policies_.RequiresClausePosition == RequiresClausePosition::Preserve)
        {
            return false;
        }

        for (auto child = Region(Syntax_[id]).FirstChild; child != None; child = Region(Syntax_[child]).NextSibling) {
            if (Region(Syntax_[child]).Kind == NodeKind::RequiresClause && Syntax_[child].Safe) {
                return true;
            }
        }

        return false;
    }

    void PlanHeaderAndBody(Index id)
    {
        const auto& node = Region(Syntax_[id]);

        if (node.Kind == NodeKind::MacroRecordDeclaration) {
            if (Syntax_[id].ItemSequence) {
                const auto body = Syntax_[id].SequenceBody;
                PlanBody(id, body.Begin - 1, body.End, Policies_.Bodies, true);
            }

            return;
        }

        if (node.Kind == NodeKind::FunctionDeclaration) {
            if (!node.Tokens.Empty() && Syntax_[id].Safe && Policies_.DeclarationHeaders == LayoutMode::Canonical &&
                PrefixProtection_[node.Tokens.Begin] == PrefixProtection_[node.Tokens.End])
            {
                const auto group =
                    GroupAt(TokenPosition(node.Tokens.Begin), GapPosition(node.Tokens.End), document::GroupMode::Fill);
                Groups_[group].Spec.StructuralBase = true;
                ProjectHeaderGaps(id, node.Tokens.Begin, node.Tokens.End, group);
            }

            return;
        }

        if (node.Kind == NodeKind::CompoundStatement || node.Kind == NodeKind::LambdaBody ||
            node.Kind == NodeKind::RequirementBody)
        {
            const auto parent = Region(Syntax_[id]).Parent;
            const bool lambda = node.Kind == NodeKind::LambdaBody ||
                (parent != None && Region(Syntax_[parent]).Kind == NodeKind::LambdaExpression);
            const auto previous = Syntax_[id].PreviousSibling;
            const bool labeled = IsSoleCaseBlock(id) ||
                (previous != None && Region(Syntax_[previous]).Kind == NodeKind::LabelStatement &&
                    Region(Syntax_[previous]).Tokens.End == node.Tokens.Begin);

            if (!lambda && !labeled &&
                (parent == None || (!BraceFor(parent) && Region(Syntax_[parent]).Kind != NodeKind::FunctionTryBlock)) &&
                Policies_.Indentation.StandaloneBraces.Mode == StandaloneBlockBraceIndentation::Indent)
            {
                const auto scope = GroupAt(
                    GapPosition(node.Tokens.Begin),
                    GapPosition(node.Tokens.End),
                    document::GroupMode::Consistent,
                    Policies_.Indentation.StandaloneBraces.Offset);
                auto& gap = Gaps_[node.Tokens.Begin];
                gap.Owner = scope;
                gap.Reindent = true;
            }

            PlanBody(id, node.Tokens.Begin, node.Tokens.End - 1, PolicyForBody(id), false);
            return;
        }

        // The parser stores a function-try body in a separate child. Its
        // enclosing definition owns the complete declaration header once.
        if (node.Kind == NodeKind::FunctionTryBlock && Region(Syntax_[id]).Parent != None &&
            Region(Syntax_[Region(Syntax_[id]).Parent]).Kind == NodeKind::FunctionDefinition)
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
                if (opening == None && Tree_.Kind(t) == TokenKind::Operator_OpenBrace &&
                    (node.Kind == NodeKind::NamespaceDeclaration || Tree_.Role(t) == PunctuationRole::RecordDelimiter ||
                        Tree_.Role(t) == PunctuationRole::BlockDelimiter ||
                        Tree_.Role(t) == PunctuationRole::LambdaDelimiter))
                {
                    opening = t;
                }
            });
        Index bodyChild = None;

        for (auto child = Region(Syntax_[id]).FirstChild; child != None; child = Region(Syntax_[child]).NextSibling) {
            const auto& part = Region(Syntax_[child]);

            if (part.Kind == NodeKind::FunctionTryBlock && node.Kind == NodeKind::FunctionDefinition) {
                const auto body = Region(Syntax_[child]).FirstChild;

                if (body != None && Region(Syntax_[body]).Kind == NodeKind::CompoundStatement) {
                    opening = Region(Syntax_[body]).Tokens.Begin;
                    bodyChild = body;
                    break;
                }
            }

            if (part.Kind == NodeKind::CompoundStatement || part.Kind == NodeKind::LambdaBody ||
                (node.Kind == NodeKind::EnumDeclaration && part.Kind == NodeKind::EnumeratorList))
            {
                opening = part.Tokens.Begin;
                bodyChild = child;
                break;
            }
        }

        auto labelItem = id;

        if (node.Kind == NodeKind::CaseLabel) {
            while (Region(Syntax_[labelItem]).Parent != None &&
                Region(Syntax_[Region(Syntax_[labelItem]).Parent]).Kind == NodeKind::AttributedStatement)
            {
                labelItem = Region(Syntax_[labelItem]).Parent;
            }
        }

        if (opening == None && node.Kind == NodeKind::CaseLabel && Region(Syntax_[labelItem]).NextSibling != None) {
            const auto next = Region(Syntax_[labelItem]).NextSibling;
            const auto& body = Region(Syntax_[next]);

            if (IsSoleCaseBlock(next) && Syntax_[next].Safe) {
                opening = body.Tokens.Begin;
                bodyChild = next;
                // Conditional sequences use their enclosing body's gap
                // owner, not a separate block for each directive branch.
                auto parent = Region(Syntax_[labelItem]).Parent;

                while (parent != None && PreprocessingSequence(parent)) {
                    parent = Region(Syntax_[parent]).Parent;
                }

                if (parent != None && Gaps_[opening].Owner == BlockOf_[parent]) {
                    Gaps_[opening].Owner = None;
                }
            }
        }

        if (opening == None || opening <= node.Tokens.Begin) {
            return;
        }

        const bool control = IsControl(node.Kind) || node.Kind == NodeKind::TryStatement;
        bool safeHeader = PrefixProtection_[node.Tokens.Begin] == PrefixProtection_[opening + 1];

        for (auto child = Region(Syntax_[id]).FirstChild; child != None; child = Region(Syntax_[child]).NextSibling) {
            if (Region(Syntax_[child]).Tokens.End <= opening && !Syntax_[child].Safe) {
                safeHeader = false;
            }
        }

        const bool macroFunction = node.Kind == NodeKind::MacroFunctionDefinition;
        // A configured macro owns only the brace outside its opaque header.
        // Gap safety still fences directives, disabled and excluded bytes.
        const bool canonical = (safeHeader || macroFunction) &&
            (control ? Policies_.ControlHeaders.Layout : Policies_.DeclarationHeaders) == LayoutMode::Canonical;
        const auto contentIndent =
            PreservedBodyIndent(bodyChild == None ? id : bodyChild, opening, Tree_.MatchingDelimiter(opening));
        const auto bodyWidth =
            BodyIndentWidth(id, opening, PolicyForBody(id), node.Kind == NodeKind::RecordDeclaration);
        const auto fixedBase = contentIndent == None ? None : contentIndent - std::min(contentIndent, Index(bodyWidth));
        const auto group =
            GroupAt(TokenPosition(node.Tokens.Begin), GapPosition(opening + 1), document::GroupMode::Fill);
        Groups_[group].Spec.StructuralBase = true;
        Groups_[group].Spec.FixedBase = fixedBase;
        Groups_[group].Spec.LineBase = fixedBase == None && node.Kind == NodeKind::LambdaExpression;

        if (node.Kind == NodeKind::LambdaExpression) {
            // Prefer an independent body break over splitting a fitting header.
            Groups_[group].Spec.BreakPenalty = 1;
        }

        // Opaque header descendants do not change a separately proven body
        // boundary. Keep their bytes exact without preserving its brace gap.
        const bool canonicalBrace = (control ? Policies_.ControlHeaders.Layout : Policies_.DeclarationHeaders) ==
            LayoutMode::Canonical && GapMutable(opening);
        Own(
            opening,
            group,
            !canonicalBrace
            ? Boundary::Preserve
            : *brace == BracePlacement::NextLine
                ? Boundary::Hard
                : *brace == BracePlacement::Attached && fixedBase == None ? Boundary::Flat : Boundary::Conditional,
            document::GapKind::Hard,
            true);

        if (canonicalBrace && *brace == BracePlacement::NextLineIfHeaderIndented) {
            Gaps_[opening].BraceIndentOffset = std::max<Index>(1, std::min(contentIndent, Index(bodyWidth)));
        }

        if (canonical && !macroFunction) {
            ProjectHeaderGaps(id, node.Tokens.Begin, opening, group);
        }

        if (!canonicalBrace) {
            Reindent(opening, group, true);
        }

        if (bodyChild == None) {
            const auto close = Tree_.MatchingDelimiter(opening);

            if (close != None && close < node.Tokens.End) {
                PlanBody(id, opening, close, PolicyForBody(id), node.Kind == NodeKind::RecordDeclaration);
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

    int32_t BodyIndentWidth(Index id, Index opening, const BodyPolicy& policy, bool record) const
    {
        const auto kind = Region(Syntax_[id]).Kind;

        if (kind == NodeKind::NamespaceDeclaration || kind == NodeKind::LinkageSpecification) {
            const bool linkage = kind == NodeKind::LinkageSpecification;
            const auto& control = linkage ? Policies_.Indentation.LinkageBlocks : Policies_.Indentation.Namespaces;

            if (control.Mode == BodyIndentation::Preserve) {
                const auto start = Region(Syntax_[id]).Tokens.Begin;
                const auto outer = linkage ? SourceLineIndent(start) : SourceIndent(start);
                const auto inner = SourceIndent(opening + 1);
                const auto base = outer == None ? 0 : outer;

                if (inner != None && inner >= base && inner - base <= uint32_t(INT32_MAX)) {
                    return int32_t(inner - base);
                }
            }

            return BodyOffset(control, 0);
        }

        return record
            ? BodyOffset(Policies_.Indentation.Members, int32_t(policy.IndentWidth))
            : int32_t(policy.IndentWidth);
    }

    bool PreprocessingSequence(Index id) const
    {
        const auto kind = Region(Syntax_[id]).Kind;
        return Syntax_[id].ItemSequence &&
            (kind == NodeKind::PreprocessingConditional || kind == NodeKind::PreprocessingBranch);
    }

    Index NextBodyItem(Index item, Index body) const
    {
        // Conditional statement/declaration sequences are transparent to the
        // surrounding body. Do not descend into expression fragments or real
        // nested bodies, which own their own layout and indentation.
        if (PreprocessingSequence(item) && Region(Syntax_[item]).FirstChild != None) {
            return Region(Syntax_[item]).FirstChild;
        }

        while (item != body) {
            const auto& node = Region(Syntax_[item]);

            if (node.NextSibling != None) {
                return node.NextSibling;
            }

            item = node.Parent;
        }

        return None;
    }

    void PlanRootIndentation(Index id)
    {
        for (auto child = Region(Syntax_[id]).FirstChild; child != None; child = NextBodyItem(child, id)) {
            const auto& part = Region(Syntax_[child]);

            if (PreprocessingSequence(child) || part.Tokens.Empty() || part.Kind == NodeKind::Comment ||
                part.Kind == NodeKind::PreprocessingDirective)
            {
                continue;
            }

            if (!Syntax_[child].Safe) {
                const auto last = LastChild_[child];

                if (!Syntax_[child].ItemSequence &&
                    !(last != None && Syntax_[last].ItemSequence &&
                        Region(Syntax_[last]).Kind == NodeKind::CompoundStatement))
                {
                    // An opaque top-level run may generate a scope. Without
                    // its grammar, later declarations have no known root level.
                    return;
                }
            }

            if (DeclarationItem(part.Kind) && Syntax_[child].IndentSafe) {
                // Recovery inside a bounded body does not change the root
                // level of its declaration or the declarations following it.
                Reindent(part.Tokens.Begin, None);
            }
        }
    }

    void PlanBody(Index id, Index opening, Index close, const BodyPolicy& policy, bool record)
    {
        const bool macroRecord = Region(Syntax_[id]).Kind == NodeKind::MacroRecordDeclaration;

        if (opening >= close || close >= Tree_.TokenCount() ||
            (!macroRecord &&
                (Tree_.Kind(opening) != TokenKind::Operator_OpenBrace ||
                    Tree_.Kind(close) != TokenKind::Operator_CloseBrace || Tree_.MatchingDelimiter(opening) != close)))
        {
            return;
        }

        const bool canonical = policy.Layout == LayoutMode::Canonical;
        const bool namespaced = Region(Syntax_[id]).Kind == NodeKind::NamespaceDeclaration;
        const bool linkage = Region(Syntax_[id]).Kind == NodeKind::LinkageSpecification;
        int32_t offset = BodyIndentWidth(id, opening, policy, record);
        const auto contentIndent = PreservedBodyIndent(id, opening, close);

        if (contentIndent != None) {
            offset = std::min(offset, int32_t(contentIndent));
        }

        if (IsSoleCaseBlock(id) && Policies_.Indentation.CaseLabels.Mode != BodyIndentation::Preserve) {
            // Explicit case indentation also reindents the compound opener
            // to its structural base, as its closing brace already does.
            Gaps_[opening].Reindent = true;
        }

        // Macro delimiters must remain separate invocations, not collapse
        // together under a compact/empty brace-body policy.
        auto shape = macroRecord ? BodyShape::Expanded : opening + 1 == close ? policy.EmptyShape : policy.Shape;

        if (shape == BodyShape::CompactSingleStatementIfFits) {
            const auto& body = Region(Syntax_[id]);
            const auto child = body.FirstChild;
            // A complete direct statement excludes sibling comments, directives
            // and additional statements without counting nested semicolons.
            const bool single =
                (body.Kind == NodeKind::CompoundStatement || body.Kind == NodeKind::LambdaBody) && child != None &&
                    Region(Syntax_[child]).NextSibling == None && Region(Syntax_[child]).Tokens.Begin == opening + 1 &&
                    Region(Syntax_[child]).Tokens.End == close && SimpleStatement(child);
            shape = single ? BodyShape::CompactIfFits : BodyShape::Expanded;
        }

        const bool independentIndent = record
            ? Policies_.Indentation.Members.Mode != BodyIndentation::Preserve
            : (namespaced && Policies_.Indentation.Namespaces.Mode != BodyIndentation::Preserve) ||
                (linkage && Policies_.Indentation.LinkageBlocks.Mode != BodyIndentation::Preserve);
        const auto group = GroupAt(
            GapPosition(opening + 1),
            GapPosition(close + 1),
            document::GroupMode::Consistent,
            offset,
            LayoutIndentation{ContinuationAlignment::Block, uint32_t(offset), uint32_t(offset)});
        Groups_[group].Spec.StructuralBase = true;
        // Canonical lambda headers can end on a parameter continuation line.
        // Preserved bodies retain the enclosing structural level in that case.
        // Other lambda bodies follow their opening line, including opaque calls
        // without a continuation frame.
        const auto parent = Region(Syntax_[id]).Parent;
        Groups_[group].Spec.FixedBase = contentIndent == None ? None : contentIndent - uint32_t(offset);
        // Inline linkage headers inherit their enclosing structural level
        // when indentation is explicit; own-line headers keep their line base.
        const bool linkageLineBase =
            linkage && (!independentIndent || StartsSourceLine(Region(Syntax_[id]).Tokens.Begin));
        Groups_[group].Spec.LineBase = contentIndent == None &&
            !(parent != None && Region(Syntax_[parent]).Kind == NodeKind::LambdaExpression &&
                Policies_.DeclarationHeaders == LayoutMode::Canonical && policy.Layout == LayoutMode::Preserve) &&
            (linkageLineBase || Region(Syntax_[id]).Kind == NodeKind::LambdaBody ||
                Region(Syntax_[id]).Kind == NodeKind::RequirementBody ||
                (parent != None && Region(Syntax_[parent]).Kind == NodeKind::LambdaExpression));
        BlockOf_[id] = group;
        Groups_[group].SyntaxBody = true;

        if (canonical) {
            const bool expanded = shape == BodyShape::Expanded;
            Own(opening + 1, group, expanded ? Boundary::Hard : Boundary::Soft);
            // Explicit body controls demand indentation independently of
            // topology and the general IndentationKind::Preserve policy.
            Gaps_[opening + 1].Reindent |= independentIndent;

            // Expanding a body must not indent its first directive as C++.
            if (Tree_.Kind(opening + 1) == TokenKind::PreprocessingDirective) {
                Gaps_[opening + 1].PreserveIndent = true;
            }

            Own(close, group, expanded ? Boundary::Hard : Boundary::Soft, document::GapKind::Flat, true);
        } else {
            Own(opening + 1, group, Boundary::Preserve);
            Own(close, group, Boundary::Preserve, document::GapKind::Flat, true);
            Reindent(opening + 1, group, false, independentIndent);
            Reindent(close, group, true, linkage && independentIndent);
        }

        bool inCase = false;
        Index contents = None;
        Index commentEnd = 0;
        bool commentsBeforeCase = false;
        Index sourceLine = None;
        Index sourceLineIndent = 0;

        for (auto child = Region(Syntax_[id]).FirstChild; child != None; child = NextBodyItem(child, id)) {
            const auto& part = Region(Syntax_[child]);

            if (PreprocessingSequence(child) || part.Tokens.Empty() || part.Tokens.Begin <= opening ||
                part.Tokens.End > close)
            {
                continue;
            }

            if (Region(Syntax_[id]).Kind == NodeKind::RequirementBody && part.Kind == NodeKind::ListSeparator) {
                if (canonical) {
                    Own(part.Tokens.Begin, group, Boundary::Flat);
                }

                continue;
            }

            const bool caseLabel = IsCaseLabel(child);

            if (part.Kind == NodeKind::Comment && part.Tokens.Begin >= commentEnd) {
                // Classify each consecutive comment run once. Only siblings
                // can document this body's next label; directives and nested
                // bodies are not transparent attachment boundaries.
                auto next = child;
                do {
                    commentEnd = Region(Syntax_[next]).Tokens.End;
                    next = Region(Syntax_[next]).NextSibling;
                } while (next != None && Region(Syntax_[next]).Kind == NodeKind::Comment);

                commentsBeforeCase = IsCaseLabel(next);
            }

            const bool caseComment =
                part.Kind == NodeKind::Comment && commentsBeforeCase && Safety_.Gaps()[part.Tokens.Begin].HasLineFeed;

            // Labels delimit direct-child case regions. A sole compound
            // supplies the case's body level; mixed cases share one level
            // across all their statements, including the first block.
            if (caseLabel || caseComment) {
                inCase |= caseLabel;
                // Standalone comments describing a label share its level,
                // not the preceding case's contents. Trailing comments keep
                // their existing statement attachment.
                contents = None;
            } else if (IsSoleCaseBlock(child)) {
                contents = None;
            } else if (inCase) {
                if (contents == None) {
                    contents = GroupAt(
                        GapPosition(part.Tokens.Begin),
                        GapPosition(part.Tokens.End),
                        document::GroupMode::Consistent,
                        int32_t(policy.IndentWidth) + BodyOffset(Policies_.Indentation.CaseLabels, offset) - offset);
                    Groups_[contents].Spec.StructuralBase = true;
                } else {
                    Groups_[contents].End = GapPosition(part.Tokens.End);
                }
            }

            const auto owner = contents == None ? group : contents;

            if (independentIndent && Policies_.IndentationKind == IndentationKind::Preserve && Syntax_[child].Safe &&
                part.Kind != NodeKind::PreprocessingDirective)
            {
                // Explicit body indentation moves complete items. Preserve
                // retains their authored geometry relative to the new line.
                const auto scope = GroupAt(
                    TokenPosition(part.Tokens.Begin),
                    GapPosition(part.Tokens.End),
                    document::GroupMode::Fill,
                    0,
                    LayoutIndentation{ContinuationAlignment::Block, 0, 0});
                Groups_[scope].Spec.StructuralBase = true;
                // Inline items already inherit their body's logical level,
                // even before any physical line break realizes that indent.
                Groups_[scope].Spec.LineBase = StartsSourceLine(part.Tokens.Begin);
                const auto line = Tree_.TokenPosition(part.Tokens.Begin).Line;

                if (line != sourceLine) {
                    sourceLine = line;
                    sourceLineIndent = SourceLineIndent(part.Tokens.Begin);
                }

                Groups_[scope].PreservedSourceIndent = sourceLineIndent;
                Syntax_[child].PreservedIndentGroup = scope;
                HasPreservedIndentGroups_ = true;
            }

            // Proven statement edges and hinted definitions retain their
            // sequence layout even when their interiors are opaque.
            if (!Syntax_[child].Safe && !(canonical && part.StatementBoundary) &&
                part.Kind != NodeKind::MacroRecordDeclaration && part.Kind != NodeKind::MacroFunctionDefinition)
            {
                // Uncertain interiors do not own an editable statement's
                // leading indentation. Retain its topology; final slice
                // permissions can indent after a protected directive LF.
                // All other protected bytes remain untouched.
                if (!Gaps_[part.Tokens.Begin].Frozen && Syntax_[child].IndentSafe) {
                    Reindent(part.Tokens.Begin, owner, false, independentIndent);
                }

                continue;
            }

            if (canonical) {
                Own(
                    part.Tokens.Begin,
                    owner,
                    Syntax_[child].NamespaceSuffix
                    ? Boundary::Preserve
                    : shape == BodyShape::Expanded ? Boundary::Hard : Boundary::Soft);
                Gaps_[part.Tokens.Begin].Reindent |= independentIndent;
            } else {
                Own(part.Tokens.Begin, owner, Boundary::Preserve);
                Reindent(part.Tokens.Begin, owner, false, independentIndent);
            }

            // Indentation controls are structural subgroups, not a text pass.
            const auto special = part.Kind == NodeKind::AccessSpecifier
                ? &Policies_.Indentation.AccessModifiers
                : caseLabel || caseComment ? &Policies_.Indentation.CaseLabels : nullptr;

            if ((caseLabel || caseComment) && special && special->Mode == BodyIndentation::Preserve &&
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

    void PlanAttributedStatement(Index id)
    {
        // Attributes and their statement share a level without changing
        // whether their existing boundaries are inline or multiline.
        const auto& node = Region(Syntax_[id]);
        const bool caseLabel = IsCaseLabel(id);
        auto end = node.Tokens.End;
        const auto next = node.NextSibling;

        if (caseLabel && next != None && IsSoleCaseBlock(next) && Syntax_[next].Safe) {
            // Include the following brace so the label's header group nests
            // inside the attributed group instead of crossing its boundary.
            end = Region(Syntax_[next]).Tokens.Begin + 1;
        }

        const auto group = GroupAt(GapPosition(node.Tokens.Begin), GapPosition(end), document::GroupMode::Fill);
        Groups_[group].Spec.StructuralBase = true;

        for (auto child = node.FirstChild; child != None; child = Region(Syntax_[child]).NextSibling) {
            const auto& part = Region(Syntax_[child]);
            const auto begin = part.Tokens.Begin;

            if (begin > node.Tokens.Begin) {
                const auto owner = !caseLabel && part.Kind == NodeKind::AttributeSpecifier
                    ? Gaps_[node.Tokens.Begin].Owner
                    : group;
                Own(begin, owner, Boundary::Preserve);
                Reindent(begin, owner);

                if (caseLabel) {
                    const auto& leading = Gaps_[node.Tokens.Begin];
                    auto& gap = Gaps_[begin];
                    gap.OverrideIndent = leading.OverrideIndent;
                    gap.ExtraIndent = leading.ExtraIndent;
                    gap.PreserveIndent = leading.PreserveIndent;
                    gap.Reindent = leading.Reindent;
                }
            }
        }
    }

    void PlanAttribute(Index id)
    {
        const auto& node = Region(Syntax_[id]);
        const auto& control = Policies_.Indentation.StatementAttributes;

        if (node.Tokens.Empty() || control.Mode == StatementAttributeIndent::Preserve) {
            return;
        }

        const auto parent = Region(Syntax_[id]).Parent;

        if (parent == None) {
            return;
        }

        const auto kind = Region(Syntax_[parent]).Kind;
        auto block = None;

        if (kind == NodeKind::DeclarationSpecifiers) {
            const auto declaration = Region(Syntax_[parent]).Parent;

            if (declaration == None || Region(Syntax_[declaration]).Kind != NodeKind::DeclarationStatement ||
                Region(Syntax_[declaration]).Tokens.Begin != node.Tokens.Begin)
            {
                return;
            }

            const auto blockParent = Region(Syntax_[declaration]).Parent;

            if (blockParent == None ||
                (Region(Syntax_[blockParent]).Kind != NodeKind::CompoundStatement &&
                    Region(Syntax_[blockParent]).Kind != NodeKind::LambdaBody))
            {
                return;
            }

            block = BlockOf_[blockParent];
        } else if (kind == NodeKind::AttributedStatement) {
            const auto blockParent = Region(Syntax_[parent]).Parent;

            if (blockParent != None &&
                (Region(Syntax_[blockParent]).Kind == NodeKind::CompoundStatement ||
                    Region(Syntax_[blockParent]).Kind == NodeKind::LambdaBody))
            {
                block = BlockOf_[blockParent];
            }
        } else if (!IsControl(kind)) {
            return;
        }

        auto& gap = Gaps_[node.Tokens.Begin];
        gap.OverrideIndent = true;
        gap.ExtraIndent = control.Mode == StatementAttributeIndent::Indent ? control.Offset : 0;

        if (block != None && gap.Owner != None && gap.Owner != block) {
            // Attribute controls are relative to the ordinary block level,
            // independently of the additional case-content body group.
            gap.ExtraIndent -= Groups_[gap.Owner].Spec.Indent;
        }

        gap.Reindent = true;
    }

    bool IsChainLink(Index spine) const
    {
        const auto callee = Region(Syntax_[spine]).FirstChild;

        if (callee == None) {
            return false;
        }

        const auto& part = Region(Syntax_[spine]);
        const auto token = NextNonComment(Region(Syntax_[callee]).Tokens.End);
        return token < part.Tokens.End &&
            ((part.Kind == NodeKind::MemberExpression && Tree_.Kind(token) != TokenKind::Operator_ScopeResolution) ||
                (part.Kind == NodeKind::CallExpression && Region(Syntax_[callee]).Kind == NodeKind::CallExpression));
    }

    // Only complete suffix calls participate; argument subtrees, fields,
    // qualified names and repeated operator() links are not named calls.
    std::string_view ChainCallName(Index member, Index& end) const
    {
        const auto parent = Region(Syntax_[member]).Parent;

        if (Region(Syntax_[member]).Kind != NodeKind::MemberExpression || parent == None ||
            Region(Syntax_[parent]).Kind != NodeKind::CallExpression || Region(Syntax_[parent]).FirstChild != member)
        {
            return {};
        }

        const auto receiver = Region(Syntax_[member]).FirstChild;
        auto name = receiver == None ? None : Region(Syntax_[receiver]).NextSibling;

        if (name != None && Region(Syntax_[name]).Kind == NodeKind::TemplateIdExpression) {
            name = Region(Syntax_[name]).FirstChild;
        }

        if (name == None || Region(Syntax_[name]).Kind != NodeKind::IdentifierExpression ||
            Region(Syntax_[name]).Tokens.Size() != 1)
        {
            return {};
        }

        end = Region(Syntax_[parent]).Tokens.End;
        return Tree_.TokenText(Region(Syntax_[name]).Tokens.Begin);
    }

    bool PlanAnnotatedChain(Index id, Index group, Index calls)
    {
        if (ChainScopeNames_.empty() && Annotations_.StickyChainPairs().empty() &&
            Annotations_.AlwaysBreakChainCalls().empty())
        {
            return false;
        }

        ChainLinks_.clear();
        const auto root = Syntax_[id].Root;

        for (Index spine = id; spine != root;) {
            const auto callee = Region(Syntax_[spine]).FirstChild;

            if (callee == None) {
                break;
            }

            const auto& part = Region(Syntax_[spine]);
            const auto t = Region(Syntax_[callee]).Tokens.End;

            if (IsChainLink(spine)) {
                auto end = part.Tokens.End;
                const auto name = ChainCallName(spine, end);
                ChainLinks_.push_back({t, end, name, None, group});
            }

            spine = callee;
        }

        const auto& rootNode = Region(Syntax_[root]);

        if (!ChainLinks_.empty() && rootNode.Kind == NodeKind::IdentifierExpression && rootNode.Tokens.Size() == 1) {
            const auto name = Tree_.TokenText(rootNode.Tokens.Begin);

            if (Annotations_.IsStickyChainPair(name, ChainLinks_.back().Name)) {
                ChainLinks_
                    .push_back({rootNode.Tokens.Begin, rootNode.Tokens.End, name, None, group, None, false, true});
            }
        }

        std::reverse(ChainLinks_.begin(), ChainLinks_.end());
        Stack_.clear();

        // Validate nesting before adding any indentation. Unmatched or crossed
        // endpoints never invent a scope; matched inner scopes remain local.
        for (Index i = 0; i < ChainLinks_.size(); ++i) {
            if (ChainLinks_[i].RootReceiver) {
                continue;
            }

            const auto name = ChainLinks_[i].Name;
            const auto endpoint = std::lower_bound(
                ChainScopeNames_.begin(),
                ChainScopeNames_.end(),
                name,
                [] (const auto& value, auto key) {
                    return value.Name < key;
                });

            if (endpoint == ChainScopeNames_.end() || endpoint->Name != name) {
                continue;
            }

            if (endpoint->Begin) {
                Stack_.push_back(i);
            } else {
                if (!Stack_.empty() &&
                    ChainLinks_[Stack_.back()].Name == Annotations_.ChainScopes()[endpoint->Pair].First)
                {
                    ChainLinks_[Stack_.back()].ScopeEnd = i;
                    Stack_.pop_back();
                } else {
                    Stack_.clear();
                }
            }
        }

        Stack_.clear();

        for (Index i = 0; i < ChainLinks_.size(); ++i) {
            while (!Stack_.empty() && ChainLinks_[Stack_.back()].ScopeEnd == i) {
                Stack_.pop_back();
            }

            auto& link = ChainLinks_[i];
            link.Owner = Stack_.empty() ? group : ChainLinks_[Stack_.back()].ScopeGroup;

            if (link.ScopeEnd != None && link.ScopeEnd > i + 1) {
                const auto begin = GapPosition(ChainLinks_[i + 1].Gap);
                const auto end = GapPosition(ChainLinks_[link.ScopeEnd].Gap);
                // Capture the chain's realized alignment (including delimiter
                // fallback), or its prospective level while still inline.
                // Then add an independent scope level without sharing it away.
                const auto base = GroupAt(begin, end, document::GroupMode::Fill);
                Groups_[base].Spec.StructuralBase = true;
                Groups_[base].Spec.Continuation = true;
                const auto scope = GroupAt(
                    begin,
                    end,
                    Policies_.Chains.Packing == ListPacking::AllOrVertical
                    ? document::GroupMode::Consistent
                    : document::GroupMode::Fill,
                    int32_t(Policies_.IndentWidth));
                Groups_[scope].Spec.ForceBreak = Groups_[group].Spec.ForceBreak;
                Groups_[scope].Spec.BreakPenalty = 2;
                // The opening call stays at the enclosing level.
                Stack_.push_back(i);
                link.ScopeGroup = scope;
            }
        }

        for (Index i = 0; i + 1 < ChainLinks_.size(); ++i) {
            const auto& first = ChainLinks_[i];
            auto last = i;

            while (last + 1 < ChainLinks_.size() && first.Owner == ChainLinks_[last + 1].Owner &&
                !ChainLinks_[last].Name.empty() && !ChainLinks_[last + 1].Name.empty() &&
                !Annotations_.AlwaysBreakChainCall(ChainLinks_[last + 1].Name) &&
                Annotations_.IsStickyChainPair(ChainLinks_[last].Name, ChainLinks_[last + 1].Name))
            {
                ++last;
            }

            if (last == i) {
                continue;
            }

            // Exempt these boundaries from the outer chain's forced/vertical
            // breaks, but retain independent width-driven escape hatches.
            const auto pair =
                GroupAt(TokenPosition(first.Gap), GapPosition(ChainLinks_[last].End), document::GroupMode::Fill);
            Groups_[pair].Spec.BreakPenalty = 2;

            for (auto j = i + 1; j <= last; ++j) {
                ChainLinks_[j].Owner = pair;
                ChainLinks_[j].Sticky = true;
            }

            i = last;
        }

        if (Policies_.Chains.Packing == ListPacking::AllOrVertical) {
            Index first = None;
            Index tail = group;

            for (auto& link : ChainLinks_) {
                if (link.RootReceiver || link.Sticky || link.Owner != group) {
                    continue;
                }

                if (first == None) {
                    // A sticky receiver prefix is indivisible. Start the
                    // independent tail only after that complete prefix.
                    first = link.Gap;
                    tail = PackingGroup(group, TokenPosition(first), Groups_[group].End);
                } else {
                    link.Owner = tail;
                }
            }
        }

        for (const auto& link : ChainLinks_) {
            if (link.RootReceiver) {
                continue;
            }

            OwnContinuationBoundary(
                link.Gap,
                link.Owner,
                Policies_.Chains.Packing == ListPacking::VerticalWhenBroken && calls > 1 && !link.Sticky
                ? document::GapKind::Hard
                : document::GapKind::Soft,
                Annotations_.AlwaysBreakChainCall(link.Name));
        }

        return true;
    }

    bool StartsSourceLine(Index token) const
    {
        const auto prefix = Tree_.Source().substr(0, Tree_.Token(token).Text.Begin);
        const auto previous = prefix.find_last_not_of(" \t");
        return previous == std::string_view::npos || prefix[previous] == '\n';
    }

    Index SourceLineIndent(Index token) const
    {
        const auto source = Tree_.Source();
        const auto offset = Tree_.Token(token).Text.Begin;
        const auto lf = source.rfind('\n', offset);
        const auto line = lf == std::string_view::npos ? 0 : lf + 1;
        const auto prefix = source.substr(line, offset - line);
        return HorizontalColumns(prefix.substr(0, prefix.find_first_not_of(" \t")), Options_.TabWidth);
    }

    bool CanReindentPreservedGap(Index gap) const
    {
        if (!Safety_.Gaps()[gap].Preserve) {
            return true;
        }

        const auto permissions = Safety_.PartialGapPermissions();
        auto permission = std::lower_bound(
            permissions.begin(),
            permissions.end(),
            gap,
            [] (const PartialGapPermission& permission, Index gap) {
                return permission.Gap < gap;
            });

        for (; permission != permissions.end() && permission->Gap == gap; ++permission) {
            if (permission->Kind == PartialGapPermissionKind::FinalIndentation) {
                return true;
            }
        }

        return false;
    }

    // Unclassified syntax has no layout authority. Its authored continuation
    // lines still move with the statement/declarator, without changing its
    // topology or horizontal spelling.
    void PlanPreservedIndentation(Index id, Index begin, bool descendants = false)
    {
        if (Policies_.IndentationKind == IndentationKind::Preserve) {
            return;
        }

        const auto range = Region(Syntax_[id]).Tokens;

        if (range.Empty()) {
            return;
        }

        Index base = None;
        Index group = None;
        auto plan = [&] (Index gap) {
            const auto indent = SourceIndent(gap);

            if (gap == begin || indent == None || Gaps_[gap].Frozen || !CanReindentPreservedGap(gap) ||
                Safety_.Gaps()[gap].ContextSensitive || RetainsBareCR(gap) || Gaps_[gap].Owner != None)
            {
                return;
            }

            if (base == None) {
                base = SourceLineIndent(begin);
            }

            const auto delta = int64_t(indent) - int64_t(base);

            if (delta < INT32_MIN || delta > INT32_MAX) {
                return;
            }

            if (group == None) {
                group = GroupAt(TokenPosition(begin), GapPosition(range.End), document::GroupMode::Fill);
                Groups_[group].Spec.StructuralBase = true;
                Groups_[group].Spec.LineBase = true;
            }

            Reindent(gap, group);
            Gaps_[gap].OverrideIndent = true;
            Gaps_[gap].RelativeIndent = true;
            Gaps_[gap].ExtraIndent = int32_t(delta);
        };

        if (!descendants) {
            DirectTokens(id, plan);
            return;
        }

        // Walk each token once, excluding independently delimited bodies.
        // Their own statements carry their own structural indentation.
        for (auto current = id;;) {
            const auto& node = Region(Syntax_[current]);
            plan(node.Tokens.Begin);
            const bool body = current != id &&
                (node.Kind == NodeKind::LambdaBody || node.Kind == NodeKind::RequirementBody ||
                    node.Kind == NodeKind::CompoundStatement);

            if (!body) {
                DirectTokens(current, plan);

                if (node.FirstChild != None) {
                    current = node.FirstChild;
                    continue;
                }
            }

            while (current != id && Region(Syntax_[current]).NextSibling == None) {
                current = Region(Syntax_[current]).Parent;
            }

            if (current == id) {
                break;
            }

            current = Region(Syntax_[current]).NextSibling;
        }
    }

    void PlanRelativeIndentation()
    {
        if (!HasPreservedIndentGroups_) {
            return;
        }

        for (Index id = 0; id < Syntax_.size(); ++id) {
            auto& entry = Syntax_[id];
            const auto& node = Region(entry);

            if (entry.PreservedIndentGroup == None && node.Parent != None) {
                entry.PreservedIndentGroup = Syntax_[node.Parent].PreservedIndentGroup;
            }

            const auto group = entry.PreservedIndentGroup;

            if (group == None) {
                continue;
            }

            DirectTokens(
                id,
                [&] (Index token) {
                    auto& gap = Gaps_[token];

                    if (gap.Shape != Boundary::Preserve || gap.Reindent || gap.OverrideIndent ||
                        !CanReindentPreservedGap(token) || gap.Frozen || Uncertain(token) || RetainsBareCR(token) ||
                        Safety_.Gaps()[token].ContextSensitive)
                    {
                        return;
                    }

                    const auto indent = SourceIndent(token);

                    if (indent == None) {
                        return;
                    }

                    const auto delta = int64_t(indent) - Groups_[group].PreservedSourceIndent;

                    if (delta < INT32_MIN || delta > INT32_MAX) {
                        return;
                    }

                    gap.Reindent = true;
                    gap.OverrideIndent = true;
                    gap.RelativeGroup = group;
                    gap.ExtraIndent = int32_t(delta);
                });
        }
    }

    // Unselected chains keep their breaks and relative indentation, but
    // their source-owned continuation lines still follow the realized root.
    void PlanPreservedChain(Index id)
    {
        if (Policies_.IndentationKind == IndentationKind::Preserve) {
            return;
        }

        const auto& entry = Syntax_[id];
        const auto begin = Region(entry).Tokens.Begin;
        const auto base = SourceLineIndent(begin);
        Index group = None;

        for (Index spine = id; spine != entry.Root;) {
            const auto callee = Region(Syntax_[spine]).FirstChild;

            if (callee == None) {
                break;
            }

            const auto gap = Region(Syntax_[callee]).Tokens.End;
            const auto indent = SourceIndent(gap);

            if (IsChainLink(spine) && GapMutable(gap) && indent != None && Gaps_[gap].Owner == None) {
                const auto delta = int64_t(indent) - int64_t(base);

                if (delta >= INT32_MIN && delta <= INT32_MAX) {
                    if (group == None) {
                        group = GroupAt(
                            TokenPosition(begin),
                            GapPosition(Region(entry).Tokens.End),
                            document::GroupMode::Fill);
                        Groups_[group].Spec.StructuralBase = true;
                        Groups_[group].Spec.LineBase = true;
                    }

                    Reindent(gap, group);
                    Gaps_[gap].OverrideIndent = true;
                    Gaps_[gap].ExtraIndent = int32_t(delta);
                }
            }

            spine = callee;
        }
    }

    void BuildChainRootName(Index root)
    {
        RootName_.clear();
        const auto range = Region(Syntax_[root]).Tokens;
        auto token = range.Begin;
        auto append = [&] (Index end) {
            for (; token < end; ++token) {
                const auto kind = Tree_.Kind(token);

                if (kind == TokenKind::Comment || kind == TokenKind::Keyword_template) {
                    continue;
                }

                if (kind != TokenKind::Identifier && kind != TokenKind::Operator_ScopeResolution) {
                    RootName_.clear();
                    return false;
                }

                RootName_.append(Tree_.TokenText(token));
            }

            return true;
        };

        // Skip parsed argument subtrees, not angle-bracket spellings: template
        // arguments can themselves contain comparisons, shifts and lambdas.
        for (auto current = root;;) {
            const auto& node = Region(Syntax_[current]);

            if (node.Kind == NodeKind::TemplateArgumentList) {
                if (!append(node.Tokens.Begin)) {
                    return;
                }

                token = node.Tokens.End;
            } else if (node.FirstChild != None) {
                current = node.FirstChild;
                continue;
            }

            while (current != root && Region(Syntax_[current]).NextSibling == None) {
                current = Region(Syntax_[current]).Parent;
            }

            if (current == root) {
                break;
            }

            current = Region(Syntax_[current]).NextSibling;
        }

        append(range.End);
    }

    void PlanChain(Index id)
    {
        const auto& entry = Syntax_[id];

        if (Region(entry).Parent != None && IsSpine(Region(Syntax_[Region(entry).Parent]).Kind) &&
            Region(Syntax_[Region(entry).Parent]).FirstChild == id)
        {
            return; // Only the maximal callee/member spine owns link gaps.
        }

        if (Policies_.Chains.Layout != LayoutMode::Canonical) {
            PlanPreservedChain(id);
            return;
        }

        const auto& rootNode = Region(Syntax_[entry.Root]);
        const bool namedRoot = rootNode.Kind == NodeKind::IdentifierExpression ||
            rootNode.Kind == NodeKind::MemberExpression || rootNode.Kind == NodeKind::TemplateIdExpression;
        const bool selectedRoots = Annotations_.Chains() == ChainParticipation::AnnotatedRoots;

        if (selectedRoots && !namedRoot) {
            PlanPreservedChain(id);
            return;
        }

        const bool conventionalRoots = namedRoot && !Annotations_.AlwaysBreakChainRoots().empty();

        if (selectedRoots || conventionalRoots) {
            // One reusable scratch spelling per maximal spine, never per-token
            // strings. Qualified identity ignores source whitespace and uses
            // the annotation snapshot's sorted lookup rather than a name scan.
            BuildChainRootName(entry.Root);

            if (selectedRoots && !Annotations_.IsAnnotatedChainRoot(RootName_)) {
                PlanPreservedChain(id);
                return;
            }
        }

        Index links = 0;
        Index calls = 0;
        Index firstLink = None;

        for (Index spine = id; spine != entry.Root;) {
            const auto callee = Region(Syntax_[spine]).FirstChild;

            if (callee == None) {
                break;
            }

            if (IsChainLink(spine)) {
                ++links;
                firstLink = Region(Syntax_[callee]).Tokens.End;
                const auto parent = Region(Syntax_[spine]).Parent;

                // Count suffix calls across the complete safe spine, including
                // mixed '.', '->' and repeated invocation links. Field paths
                // and receiver construction remain exempt.
                if (Region(Syntax_[spine]).Kind == NodeKind::CallExpression ||
                    (parent != None && Region(Syntax_[parent]).Kind == NodeKind::CallExpression &&
                        Region(Syntax_[parent]).FirstChild == spine))
                {
                    ++calls;
                }
            }

            spine = callee;
        }

        if (!links) {
            return;
        }

        const bool conventional = conventionalRoots && calls > 0 && Annotations_.AlwaysBreakChainRoot(RootName_);
        // Configured roots may force a break even with only one call.
        const auto packingCalls = conventional ? std::max(calls, Index(2)) : calls;
        const auto& policy = Policies_.Chains;
        const auto group = GroupAt(
            TokenPosition(Region(entry).Tokens.Begin),
            GapPosition(Region(entry).Tokens.End),
            document::GroupMode::Fill,
            int32_t(policy.Indent.ContinuationOffset),
            policy.Indent);
        // Prefer operator/list boundaries over splitting field paths or
        // multi-call chains. Outside control headers, a single suffix call
        // may split before its arguments; header chains prefer argument
        // breaks as well as operator and clause boundaries.
        Groups_[group].Spec.BreakPenalty =
            entry.ControlHeader ? 3 : packingCalls == 0 || (links > 1 && packingCalls != 1) ? 2 : 0;
        // A chain owns continuation independently of pending expression
        // offsets, but retains enclosing list and realized-line indentation.
        Groups_[group].Spec.StructuralBase = true;
        Groups_[group].Spec.Continuation = true;
        // Always counts suffix calls, not fields or receiver construction.
        // Control headers keep fitting chains inline and prefer other breaks.
        // Explicit conventional roots still require their annotated shape.
        Groups_[group].Spec.ForceBreak =
            conventional || (!entry.ControlHeader && policy.Break == BreakRequirement::Always && calls > 1);

        if (PlanAnnotatedChain(id, group, packingCalls)) {
            return;
        }

        // The receiver-to-tail break is independent of the tail's coupled
        // member boundaries, just like a list opener and its item row.
        const auto tail = policy.Packing == ListPacking::AllOrVertical && links > 1
            ? PackingGroup(group, TokenPosition(firstLink), GapPosition(Region(entry).Tokens.End))
            : group;

        for (Index spine = id; spine != entry.Root;) {
            const auto callee = Region(Syntax_[spine]).FirstChild;

            if (callee == None) {
                break;
            }

            const auto t = Region(Syntax_[callee]).Tokens.End;

            if (IsChainLink(spine)) {
                OwnContinuationBoundary(
                    t,
                    t == firstLink ? group : tail,
                    policy.Packing == ListPacking::VerticalWhenBroken && packingCalls > 1
                    ? document::GapKind::Hard
                    : document::GapKind::Soft);
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

    enum class Discovery : uint8_t
    {
        None,
        Unique,
        Ambiguous
    };

    struct DiscoveredEndpoint
    {
        Discovery Kind = Discovery::None;
        Endpoint Value = {};
    };

    DiscoveredEndpoint ScopeEndpoint(Index statement)
    {
        if (!Syntax_[statement].Safe || Region(Syntax_[statement]).Kind != NodeKind::ExpressionStatement) {
            return {};
        }

        DiscoveredEndpoint result;
        Stack_.clear();

        if (Region(Syntax_[statement]).FirstChild != None) {
            Stack_.push_back(Region(Syntax_[statement]).FirstChild);
        }

        while (!Stack_.empty()) {
            const auto expression = Stack_.back();
            Stack_.pop_back();
            const auto& node = Region(Syntax_[expression]);

            if (!Syntax_[expression].Safe) {
                continue;
            }

            if (node.Kind == NodeKind::Argument || node.Kind == NodeKind::ParenthesizedExpression) {
                for (auto child = Region(Syntax_[expression]).FirstChild;
                    child != None;
                    child = Region(Syntax_[child]).NextSibling)
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

            const auto callee = Region(Syntax_[expression]).FirstChild;

            if (callee == None || Region(Syntax_[callee]).Kind == NodeKind::Expression ||
                Region(Syntax_[callee]).Kind == NodeKind::LambdaExpression ||
                Region(Syntax_[callee]).Kind == NodeKind::StatementExpression)
            {
                continue;
            }

            auto name = callee;

            if (Region(Syntax_[callee]).Kind == NodeKind::MemberExpression) {
                const auto receiver = Region(Syntax_[callee]).FirstChild;
                name = receiver == None ? None : Region(Syntax_[receiver]).NextSibling;
            }

            if (name != None && Region(Syntax_[name]).Tokens.Size() == 1) {
                const auto token = Region(Syntax_[name]).Tokens.Begin;
                const auto spelling = Tree_.TokenText(token);
                const auto found = std::lower_bound(
                    ScopeNames_.begin(),
                    ScopeNames_.end(),
                    spelling,
                    [] (const ScopeName& item, std::string_view key) {
                        return item.Name < key;
                    });

                if (found != ScopeNames_.end() && found->Name == spelling) {
                    if (result.Kind != Discovery::None) {
                        return {Discovery::Ambiguous, {}};
                    }

                    result = {Discovery::Unique, {statement, token, found->Pair, found->Begin}};
                }
            }

            const auto args = Region(Syntax_[callee]).NextSibling;

            if (args == None || Region(Syntax_[args]).Kind != NodeKind::ArgumentList) {
                continue;
            }

            for (auto arg = Region(Syntax_[args]).FirstChild; arg != None; arg = Region(Syntax_[arg]).NextSibling) {
                if (Region(Syntax_[arg]).Kind == NodeKind::Argument) {
                    Stack_.push_back(arg);
                }
            }
        }

        return result;
    }

    bool SameContext(const Endpoint& begin, const Endpoint& end) const
    {
        const auto a = Region(Syntax_[begin.Statement]).Tokens;
        const auto b = Region(Syntax_[end.Statement]).Tokens;

        if (a.Size() != b.Size() || begin.Name - a.Begin != end.Name - b.Begin) {
            return false;
        }

        for (Index i = 0; i < a.Size(); ++i) {
            if (a.Begin + i == begin.Name) {
                continue;
            }

            if (Tree_.Kind(a.Begin + i) != Tree_.Kind(b.Begin + i) ||
                Tree_.TokenText(a.Begin + i) != Tree_.TokenText(b.Begin + i))
            {
                return false;
            }
        }

        return true;
    }

    Index SourceIndent(Index token) const
    {
        const auto& gap = Safety_.Gaps()[token];
        const auto lf = Safety_.Original(gap).rfind('\n');

        if (lf == std::string_view::npos) {
            return None;
        }

        Index column = 0;

        for (const char c : Safety_.Original(gap).substr(lf + 1)) {
            if (c == '\t') {
                column = AddWidth(column, Options_.TabWidth - column % Options_.TabWidth);
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
            [] (const ScopeName& a, const ScopeName& b) {
                return a.Name < b.Name;
            });
        std::pmr::vector<Endpoint> pairs(Syntax_.get_allocator().resource());
        std::pmr::vector<int64_t> frozen(Tree_.TokenCount() + size_t(2), 0,
                                         Syntax_.get_allocator().resource());
        Index frozenBody = None;
        bool labeledBody = false;
        const auto freeze = [&] (Index begin, Index end) {
            // Incomplete scopes in a labeled sequence must not preserve an
            // indentation region that could span independently entered cases.
            if (labeledBody) {
                return;
            }

            // Inline regions have no existing visual indentation to retain.
            // Freezing them would suppress ordinary body expansion, including
            // lambdas containing a single unmatched scope endpoint.
            if (begin < end && SourceIndent(begin) != None) {
                ++frozen[begin];
                --frozen[end];
                Frozen_.push_back({begin, end, frozenBody});
            }
        };

        for (Index id = 0; id < Syntax_.size(); ++id) {
            const auto kind = Region(Syntax_[id]).Kind;

            if (kind != NodeKind::CompoundStatement && kind != NodeKind::LambdaBody) {
                continue;
            }

            frozenBody = id;
            labeledBody = false;

            for (auto child = Region(Syntax_[id]).FirstChild; child != None; child = Region(Syntax_[child]).NextSibling)
            {
                const auto childKind = Region(Syntax_[child]).Kind;

                if (childKind == NodeKind::CaseLabel || childKind == NodeKind::LabelStatement) {
                    labeledBody = true;
                    break;
                }
            }

            pairs.clear();

            for (auto child = Region(Syntax_[id]).FirstChild; child != None; child = Region(Syntax_[child]).NextSibling)
            {
                if (!Syntax_[child].Safe || Region(Syntax_[child]).Kind == NodeKind::PreprocessingDirective ||
                    Region(Syntax_[child]).Kind == NodeKind::PreprocessingConditional || IsCaseLabel(child) ||
                    Region(Syntax_[child]).Kind == NodeKind::LabelStatement)
                {
                    if (!pairs.empty()) {
                        freeze(
                            Region(Syntax_[pairs.front().Statement]).Tokens.Begin,
                            Region(Syntax_[id]).Tokens.End - 1);
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
                        ? Region(Syntax_[child]).Tokens.Begin
                        : Region(Syntax_[pairs.front().Statement]).Tokens.Begin,
                        Region(Syntax_[id]).Tokens.End - 1);
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

                if (pairs.empty() || pairs.back().Pair != endpoint->Pair || !SameContext(pairs.back(), *endpoint)) {
                    freeze(
                        pairs.empty()
                        ? Region(Syntax_[id]).Tokens.Begin + 1
                        : Region(Syntax_[pairs.front().Statement]).Tokens.Begin,
                        Region(Syntax_[child]).Tokens.End);
                    pairs.clear();
                    continue;
                }

                const auto begin = pairs.back();
                pairs.pop_back();
                const auto first = Region(Syntax_[begin.Statement]).NextSibling;

                if (first == child || first == None) {
                    continue;
                }

                const auto beginIndent = SourceIndent(Region(Syntax_[begin.Statement]).Tokens.Begin);
                const auto interiorIndent = SourceIndent(Region(Syntax_[first]).Tokens.Begin);

                if (!Policies_.GenerateCallScopeIndentation &&
                    (beginIndent == None || interiorIndent == None || interiorIndent <= beginIndent))
                {
                    continue; // Flat nested pairs never invent an extra level.
                }

                Visual_.push_back({first, child});
            }

            if (!pairs.empty()) {
                freeze(Region(Syntax_[pairs.front().Statement]).Tokens.Begin, Region(Syntax_[id]).Tokens.End - 1);
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
            sum += Gaps_[i].Frozen || ContextProtected({token.Begin, token.End});
            PrefixProtection_[i + 1] = sum;
        }

        for (auto& entry : Syntax_) {
            const auto range = Region(entry).Tokens;
            entry.Safe = entry.Safe && PrefixProtection_[range.Begin] == PrefixProtection_[range.End];
        }
    }

    void PlanFrozenScopes()
    {
        if (Policies_.IndentationKind == IndentationKind::Preserve) {
            return;
        }

        std::sort(
            Frozen_.begin(),
            Frozen_.end(),
            [] (const FrozenRange& a, const FrozenRange& b) {
                return a.Begin != b.Begin ? a.Begin < b.Begin : a.End > b.End;
            });
        size_t merged = 0;

        for (const auto range : Frozen_) {
            if (merged && range.Begin < Frozen_[merged - 1].End) {
                Frozen_[merged - 1].End = std::max(Frozen_[merged - 1].End, range.End);
            } else {
                Frozen_[merged++] = range;
            }
        }

        Frozen_.resize(merged);

        for (const auto range : Frozen_) {
            if (range.Body == None || BlockOf_[range.Body] == None) {
                continue;
            }

            const auto first = Region(Syntax_[range.Body]).FirstChild;
            auto base = first == None ? None : SourceIndent(Region(Syntax_[first]).Tokens.Begin);

            if (base == None) {
                base = SourceIndent(range.Begin);
            }

            Index group = None;

            for (Index gap = range.Begin; gap < range.End; ++gap) {
                const auto indent = SourceIndent(gap);
                const auto& source = Safety_.Gaps()[gap];

                if (indent == None || (source.Preserve && !source.MovableStatementIndent) || source.ContextSensitive ||
                    RetainsBareCR(gap))
                {
                    continue;
                }

                const auto delta = int64_t(indent) - int64_t(base);

                if (delta < INT32_MIN || delta > INT32_MAX) {
                    continue;
                }

                if (group == None) {
                    group = GroupAt(GapPosition(range.Begin), GapPosition(range.End), document::GroupMode::Fill);
                    Groups_[group].Spec.StructuralBase = true;
                }

                auto& plan = Gaps_[gap];
                plan.Owner = group;
                plan.Shape = Boundary::Preserve;
                plan.Reindent = true;
                plan.OverrideIndent = true;
                plan.RelativeIndent = true;
                plan.ExtraIndent = int32_t(delta);
            }
        }
    }

    void PlanVisualScopes()
    {
        if (Policies_.IndentationKind == IndentationKind::Preserve) {
            return;
        }

        for (const auto range : Visual_) {
            const auto beginToken = Region(Syntax_[range.First]).Tokens.Begin;
            const auto endToken = Region(Syntax_[range.End]).Tokens.Begin;

            if (PrefixProtection_[beginToken] != PrefixProtection_[endToken]) {
                continue;
            }

            const auto owner = BlockOf_[Region(Syntax_[range.First]).Parent];
            const auto group = GroupAt(
                GapPosition(beginToken),
                GapPosition(endToken),
                document::GroupMode::Consistent,
                int32_t(Policies_.IndentWidth));

            for (auto interior = range.First; interior != range.End; interior = Region(Syntax_[interior]).NextSibling) {
                auto& gap = Gaps_[Region(Syntax_[interior]).Tokens.Begin];

                if (gap.Owner == owner) {
                    gap.Owner = group;
                    gap.Reindent = true;
                }
            }
        }
    }

    DeclarationCategory ClassifyDeclaration(Index id) const
    {
        const auto& entry = Syntax_[id];
        const auto& node = Region(entry);
        switch (node.Kind) {
            case NodeKind::MacroEnumDeclaration:
            case NodeKind::RecordDeclaration:
            case NodeKind::MacroRecordDeclaration:
            case NodeKind::EnumDeclaration:
            case NodeKind::TypedefDeclaration:
                return DeclarationCategory::Type;
            case NodeKind::FunctionDefinition:
            case NodeKind::MacroFunctionDefinition:
            case NodeKind::FunctionTryBlock:
                return DeclarationCategory::Function;
            case NodeKind::Declaration:
            case NodeKind::FunctionDeclaration:
            case NodeKind::UsingDeclaration:
                for (auto child = node.FirstChild; child != None; child = Region(Syntax_[child]).NextSibling) {
                    const auto kind = Region(Syntax_[child]).Kind;

                    if (kind == NodeKind::InitDeclaratorList) {
                        return Syntax_[child].Category;
                    }

                    if (node.Kind == NodeKind::UsingDeclaration && kind == NodeKind::TypeId) {
                        return DeclarationCategory::Type;
                    }
                }

                break;
            case NodeKind::Declarator:
            case NodeKind::NestedDeclarator: {
                bool pointer = false;
                auto suffix = DeclarationCategory::None;

                for (auto child = node.FirstChild; child != None; child = Region(Syntax_[child]).NextSibling) {
                    const auto& part = Syntax_[child];
                    const auto kind = Region(part).Kind;

                    // The operator nearest the name determines the entity.
                    // A nested pointer binds before an outer function suffix.
                    if (kind == NodeKind::NestedDeclarator && part.Category != DeclarationCategory::None) {
                        return part.Category;
                    }

                    pointer |= kind == NodeKind::PointerOperator || kind == NodeKind::MemberPointerOperator;

                    if (suffix == DeclarationCategory::None) {
                        if (kind == NodeKind::FunctionDeclarator) {
                            suffix = DeclarationCategory::Function;
                        } else if (kind == NodeKind::ArrayDeclarator) {
                            suffix = DeclarationCategory::Variable;
                        }
                    }
                }

                return suffix != DeclarationCategory::None
                    ? suffix
                    : pointer ? DeclarationCategory::Variable : DeclarationCategory::None;
            }
            case NodeKind::InitDeclarator:
                for (auto child = node.FirstChild; child != None; child = Region(Syntax_[child]).NextSibling) {
                    if (Region(Syntax_[child]).Kind == NodeKind::Declarator) {
                        const auto category = Syntax_[child].Category;
                        return category == DeclarationCategory::None ? DeclarationCategory::Variable : category;
                    }
                }

                break;
            case NodeKind::InitDeclaratorList: {
                auto category = DeclarationCategory::None;

                for (auto child = node.FirstChild; child != None; child = Region(Syntax_[child]).NextSibling) {
                    if (Region(Syntax_[child]).Kind != NodeKind::InitDeclarator) {
                        continue;
                    }

                    const auto next = Syntax_[child].Category;

                    if (next == DeclarationCategory::None ||
                        (category != DeclarationCategory::None && category != next))
                    {
                        return DeclarationCategory::None;
                    }

                    category = next;
                }

                return category;
            }
            case NodeKind::TemplateDeclaration:
            case NodeKind::ExportDeclaration:
                // Wrappers inherit the declared entity, never a braced sequence.
                if (!entry.ItemSequence) {
                    for (auto child = node.FirstChild; child != None; child = Region(Syntax_[child]).NextSibling) {
                        if (DeclarationItem(Region(Syntax_[child]).Kind)) {
                            return Syntax_[child].Category;
                        }
                    }
                }

                break;
            default:
                break;
        }

        return DeclarationCategory::None;
    }

    void ApplyVerticalGap(Index gap, VerticalGapPolicy policy, uint8_t priority = 0)
    {
        if (policy.Mode == VerticalGapMode::Preserve || !GapMutable(gap) || Gaps_[gap].VerticalPriority > priority) {
            return;
        }

        auto& plan = Gaps_[gap];
        plan.PreserveIndent |= plan.Shape == Boundary::Preserve && !plan.Reindent && !plan.OverrideIndent;

        if (!plan.ContextKnown) {
            plan.ContextKnown = true;
            plan.Context = GapContext::Preserved;
        }

        plan.Vertical = true;
        plan.VerticalPriority = priority;
        plan.VerticalMinimum = policy.Mode == VerticalGapMode::Minimum;
        const auto originalLines = plan.VerticalMinimum
            ? std::count(Safety_.Original(gap).begin(), Safety_.Original(gap).end(), '\n')
            : 0;

        if (originalLines > UINT16_MAX) {
            // An uncapped authored gap can exceed the document count width.
            // Preserve its bytes; the later cap pass can still reduce it.
            plan.Vertical = false;
            plan.Shape = Boundary::Preserve;
            return;
        }
        plan.NewlineCount =
            uint16_t(std::min<std::size_t>(UINT16_MAX, std::max<std::size_t>(policy.EmptyLines + 1, originalLines)));
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

        for (auto child = Region(Syntax_[id]).FirstChild; child != None;) {
            const auto& entry = Syntax_[child];

            if (Region(entry).Kind != NodeKind::Comment || !entry.Safe ||
                Region(entry).Tokens.Begin < Syntax_[id].SequenceBody.Begin ||
                Region(entry).Tokens.End > Syntax_[id].SequenceBody.End)
            {
                child = Region(entry).NextSibling;
                continue;
            }

            auto begin = Region(entry).Tokens.Begin;
            auto end = Region(entry).Tokens.End;
            child = Region(entry).NextSibling;

            while (child != None && Syntax_[child].Safe && Region(Syntax_[child]).Kind == NodeKind::Comment &&
                Region(Syntax_[child]).Tokens.Begin == end &&
                Region(Syntax_[child]).Tokens.End <= Syntax_[id].SequenceBody.End && !Gaps_[end].Frozen &&
                !ContextProtected(Safety_.Gaps()[end].Source))
            {
                end = Region(Syntax_[child]).Tokens.End;
                child = Region(Syntax_[child]).NextSibling;
            }

            // Trim comments sharing a physical line with noncomment syntax.
            // Internal comment newlines are token bytes, never owned gaps.
            bool line = begin == 0 || Safety_.Gaps()[begin].HasLineFeed;
            auto first = end;

            for (auto token = begin; token < end; ++token) {
                line = line || Safety_.Gaps()[token].HasLineFeed;

                if (line && first == end) {
                    first = token;
                }

                line = line || HasLine(Tree_.TokenText(token));
            }

            line = end == Tree_.TokenCount() || Safety_.Gaps()[end].HasLineFeed;
            auto last = begin;

            for (auto token = end; token-- > begin;) {
                line = line || Safety_.Gaps()[token + 1].HasLineFeed;

                if (line && last == begin) {
                    last = token + 1;
                }

                line = line || HasLine(Tree_.TokenText(token));
            }

            if (first >= last) {
                continue;
            }

            if (first != 0) {
                ApplyVerticalGap(first, policy.StandaloneCommentBlankLinesBefore, 1);
            }

            if (last != Tree_.TokenCount()) {
                ApplyVerticalGap(last, policy.StandaloneCommentBlankLinesAfter, 2);
            }
        }
    }

    void PlanRulerGaps()
    {
        if (Policies_.RulerBlankLinesBefore.Mode == VerticalGapMode::Preserve &&
            Policies_.RulerBlankLinesAfter.Mode == VerticalGapMode::Preserve)
        {
            return;
        }

        for (Index token = 0; token < Tree_.TokenCount(); ++token) {
            const auto range = Tree_.Token(token).Text;

            if (Tree_.Kind(token) != TokenKind::Comment || !IsRuler(Tree_.TokenText(token)) ||
                ContextProtected({range.Begin, range.End}) || (token != 0 && !Safety_.Gaps()[token].HasLineFeed) ||
                (token + 1 != Tree_.TokenCount() && !Safety_.Gaps()[token + 1].HasLineFeed))
            {
                continue;
            }

            if (token != 0) {
                ApplyVerticalGap(token, Policies_.RulerBlankLinesBefore, 3);
            }

            if (token + 1 != Tree_.TokenCount()) {
                ApplyVerticalGap(token + 1, Policies_.RulerBlankLinesAfter, 4);
            }
        }
    }

    Index UnwrapStatement(Index id) const
    {
        while (Region(Syntax_[id]).Kind == NodeKind::AttributedStatement && LastChild_[id] != None) {
            id = LastChild_[id];
        }

        return id;
    }

    std::string_view StatementMacroName(Index id) const
    {
        const auto& node = Region(Syntax_[UnwrapStatement(id)]);

        if (node.Kind != NodeKind::ExpressionStatement) {
            return {};
        }

        auto expression = node.FirstChild;

        if (expression != None && Region(Syntax_[expression]).Kind == NodeKind::Expression) {
            expression = Region(Syntax_[expression]).FirstChild;
        }

        if (expression == None || Region(Syntax_[expression]).Kind != NodeKind::CallExpression) {
            return {};
        }

        const auto root = Region(Syntax_[Syntax_[expression].Root]).Tokens;

        if (root.Size() != 1 || Tree_.Kind(root.Begin) != TokenKind::Identifier) {
            return {};
        }

        const auto name = Tree_.TokenText(root.Begin);
        return Annotations_.IsBlankLineMacro(name) ? name : std::string_view{};
    }

    bool EndsStatementBlock(Index id) const
    {
        const auto& entry = Syntax_[UnwrapStatement(id)];
        const auto& node = Region(entry);

        if (entry.Definition ||
            (entry.ItemSequence &&
                (node.Kind == NodeKind::NamespaceDeclaration || node.Kind == NodeKind::LinkageSpecification ||
                    node.Kind == NodeKind::ExportDeclaration)))
        {
            return true;
        }

        switch (node.Kind) {
            case NodeKind::CompoundStatement:
            case NodeKind::IfStatement:
            case NodeKind::ForStatement:
            case NodeKind::WhileStatement:
            case NodeKind::SwitchStatement:
            case NodeKind::TryStatement:
                return !node.Tokens.Empty() && Tree_.Kind(node.Tokens.End - 1) == TokenKind::Operator_CloseBrace;
            case NodeKind::DoWhileStatement:
                for (auto child = node.FirstChild; child != None; child = Region(Syntax_[child]).NextSibling) {
                    if (Region(Syntax_[child]).Kind == NodeKind::CompoundStatement) {
                        return true;
                    }
                }

                return false;
            default:
                return false;
        }
    }

    void PlanStatementGaps(Index id)
    {
        const auto& sequence = Syntax_[id];
        const auto kind = Region(sequence).Kind;

        if (!sequence.ItemSequence ||
            (kind != NodeKind::CompoundStatement && kind != NodeKind::LambdaBody &&
                kind != NodeKind::PreprocessingBranch))
        {
            return;
        }

        const auto& policy = Policies_.VerticalGaps;
        Index previous = None;
        Index commentBegin = None;
        std::string_view previousMacro;

        for (auto child = Region(sequence).FirstChild; child != None; child = Region(Syntax_[child]).NextSibling) {
            const auto& entry = Syntax_[child];
            const auto& part = Region(entry);

            if (part.Tokens.Empty() || part.Tokens.Begin < sequence.SequenceBody.Begin ||
                part.Tokens.End > sequence.SequenceBody.End)
            {
                continue;
            }

            if (!entry.Safe || part.Kind == NodeKind::PreprocessingDirective ||
                part.Kind == NodeKind::PreprocessingConditional || IsCaseLabel(child) ||
                part.Kind == NodeKind::LabelStatement)
            {
                previous = commentBegin = None;
                previousMacro = {};
                continue;
            }

            if (part.Kind == NodeKind::Comment) {
                if (previous == None || part.Tokens.End > Region(Syntax_[previous]).Attached.End) {
                    if (commentBegin == None) {
                        commentBegin = part.Tokens.Begin;
                    }
                }

                continue;
            }

            const auto macro = policy.MacroBlankLinesAround.Mode != VerticalGapMode::Preserve
                ? StatementMacroName(child)
                : std::string_view{};

            if (previous != None && (commentBegin == None || part.Attached.Begin <= commentBegin)) {
                const auto gap = part.Attached.Begin;
                // Library visual scopes can give sibling statements different
                // indentation. Only peers within the same scope need a gap.
                const bool sameScope = Gaps_[gap].Owner == Gaps_[Region(Syntax_[previous]).Attached.Begin].Owner;

                if (sameScope && EndsStatementBlock(previous)) {
                    ApplyVerticalGap(gap, policy.BlockBlankLinesAfter);
                }

                const auto statementKind = Region(Syntax_[UnwrapStatement(child)]).Kind;

                if (statementKind == NodeKind::IfStatement || statementKind == NodeKind::ForStatement ||
                    statementKind == NodeKind::WhileStatement)
                {
                    ApplyVerticalGap(gap, policy.ControlStatementBlankLinesBefore);
                }

                if (sameScope && (!macro.empty() || !previousMacro.empty())) {
                    // A run of the same macro is one logical group. The root
                    // of a fluent call identifies its complete statement.
                    ApplyVerticalGap(
                        gap,
                        macro == previousMacro
                        ? VerticalGapPolicy{VerticalGapMode::Exact, 0}
                        : policy.MacroBlankLinesAround);
                }
            }

            previous = child;
            previousMacro = macro;
            commentBegin = None;
        }
    }

    void PlanCaseGaps(Index id)
    {
        const auto& node = Region(Syntax_[id]);

        if (!Syntax_[id].ItemSequence ||
            (node.Kind != NodeKind::CompoundStatement && node.Kind != NodeKind::PreprocessingBranch))
        {
            return;
        }

        bool labeled = false;
        bool content = false;
        Index contentEnd = 0;

        for (auto child = node.FirstChild; child != None; child = Region(Syntax_[child]).NextSibling) {
            const auto& entry = Syntax_[child];
            const auto& part = Region(entry);

            if (part.Tokens.Empty()) {
                continue;
            }

            if (!entry.Safe || part.Kind == NodeKind::PreprocessingDirective ||
                part.Kind == NodeKind::PreprocessingConditional)
            {
                labeled = content = false;
                continue;
            }

            if (part.Kind == NodeKind::Comment) {
                continue;
            }

            if (IsCaseLabel(child)) {
                // Only the boundary before the next label separates cases.
                // Attributes on null statements (including fallthrough) are
                // part of the preceding body, never a boundary themselves.
                if (content && part.Attached.Begin >= contentEnd) {
                    ApplyVerticalGap(part.Attached.Begin, Policies_.VerticalGaps.CaseBlankLines);
                }

                labeled = true;
                content = false;
            } else if (labeled) {
                content = true;
                contentEnd = part.Tokens.End;
            }
        }
    }

    void PlanVerticalGaps()
    {
        const auto& policy = Policies_.VerticalGaps;
        const bool commentRules = policy.StandaloneCommentBlankLinesBefore.Mode != VerticalGapMode::Preserve ||
            policy.StandaloneCommentBlankLinesAfter.Mode != VerticalGapMode::Preserve;
        const bool caseRules = policy.CaseBlankLines.Mode != VerticalGapMode::Preserve;
        const bool statementRules = policy.BlockBlankLinesAfter.Mode != VerticalGapMode::Preserve ||
            policy.ControlStatementBlankLinesBefore.Mode != VerticalGapMode::Preserve ||
            policy.MacroBlankLinesAround.Mode != VerticalGapMode::Preserve;
        const bool sequenceRules = caseRules || commentRules || statementRules ||
            policy.MemberGroupMaxEmptyLines.Enabled || policy.DeclarationBlankLines.Mode != VerticalGapMode::Preserve ||
            policy.DeclarationCategoryBlankLines.Mode != VerticalGapMode::Preserve ||
            policy.MemberDeclarationBlankLines.Mode != VerticalGapMode::Preserve ||
            policy.DefinitionBlankLines.Mode != VerticalGapMode::Preserve ||
            policy.MemberDefinitionBlankLines.Mode != VerticalGapMode::Preserve ||
            policy.NamespaceBlankLinesAfterOpen.Mode != VerticalGapMode::Preserve ||
            policy.NamespaceBlankLinesBeforeClose.Mode != VerticalGapMode::Preserve ||
            policy.LinkageBlankLinesAfterOpen.Mode != VerticalGapMode::Preserve ||
            policy.LinkageBlankLinesBeforeClose.Mode != VerticalGapMode::Preserve ||
            policy.AccessSectionBlankLinesBefore.Mode != VerticalGapMode::Preserve ||
            policy.AccessSectionBlankLinesAfterLabel.Mode != VerticalGapMode::Preserve;

        if (sequenceRules) {
            std::pmr::vector<Index> children(Syntax_.get_allocator().resource());
            std::pmr::vector<Index> following(Syntax_.get_allocator().resource());

            for (Index id = 0; id < Syntax_.size(); ++id) {
                const auto& node = Region(Syntax_[id]);

                if (statementRules) {
                    PlanStatementGaps(id);
                }

                if (caseRules) {
                    PlanCaseGaps(id);
                }

                if (commentRules) {
                    if (node.Kind == NodeKind::PreprocessingConditional || node.Kind == NodeKind::PreprocessingBranch) {
                        auto& entry = Syntax_[id];
                        const auto parent = Region(entry).Parent;
                        entry.ItemSequence = parent != None && Syntax_[parent].ItemSequence;

                        if (entry.ItemSequence) {
                            entry.SequenceBody.Begin =
                                std::max(entry.SequenceBody.Begin, Syntax_[parent].SequenceBody.Begin);
                            entry.SequenceBody.End = std::min(entry.SequenceBody.End, Syntax_[parent].SequenceBody.End);
                        }
                    }

                    PlanStandaloneCommentGaps(id);
                }

                if (node.Kind != NodeKind::TranslationUnit && node.Kind != NodeKind::NamespaceDeclaration &&
                    node.Kind != NodeKind::RecordDeclaration && node.Kind != NodeKind::MacroRecordDeclaration &&
                    node.Kind != NodeKind::LinkageSpecification &&
                    !(node.Kind == NodeKind::ExportDeclaration && Syntax_[id].ItemSequence))
                {
                    continue;
                }

                children.clear();

                for (auto child = Region(Syntax_[id]).FirstChild;
                    child != None;
                    child = Region(Syntax_[child]).NextSibling)
                {
                    children.push_back(child);
                }

                following.resize(children.size());
                Index nextItem = None;

                for (size_t p = children.size(); p-- > 0;) {
                    following[p] = nextItem;

                    if (Region(Syntax_[children[p]]).Kind != NodeKind::Comment) {
                        nextItem = DeclarationItem(Region(Syntax_[children[p]]).Kind) ? children[p] : None;
                    }
                }

                // Sequence edges exist even when layout preserves the body
                // without creating a document group (for example, linkage).
                const bool body = node.Kind != NodeKind::TranslationUnit && Syntax_[id].ItemSequence;
                const auto opening = body ? Syntax_[id].SequenceBody.Begin - 1 : None;
                const auto close = body ? Syntax_[id].SequenceBody.End : None;
                Index previous = None;
                Index access = None;
                bool seenSection = false;
                bool content = false;

                for (size_t p = 0; p < children.size(); ++p) {
                    const auto child = children[p];
                    const auto& entry = Syntax_[child];
                    const auto& part = Region(entry);

                    if (body && (part.Tokens.Begin <= opening || part.Tokens.End > close)) {
                        continue;
                    }

                    if (!content && node.Kind == NodeKind::NamespaceDeclaration && !part.Tokens.Empty()) {
                        ApplyVerticalGap(Region(entry).Attached.Begin, policy.NamespaceBlankLinesAfterOpen);
                    }

                    if (!content && node.Kind == NodeKind::LinkageSpecification && !part.Tokens.Empty()) {
                        ApplyVerticalGap(Region(entry).Attached.Begin, policy.LinkageBlankLinesAfterOpen);
                    }

                    content = content || !part.Tokens.Empty();
                    const bool macroDefinition = part.Kind == NodeKind::MacroFunctionDefinition ||
                        (part.Kind == NodeKind::MacroRecordDeclaration && entry.ItemSequence);

                    if ((!entry.Safe && !macroDefinition) || part.Kind == NodeKind::PreprocessingDirective ||
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
                        const auto preceding = access != None ? access : previous;
                        const bool trailing =
                            preceding != None && part.Tokens.End <= Region(Syntax_[preceding]).Attached.End;
                        const bool leading =
                            following[p] != None && part.Tokens.Begin >= Region(Syntax_[following[p]]).Attached.Begin;

                        if (!trailing && !leading) {
                            previous = access = None;
                        }

                        continue;
                    }

                    if (part.Kind == NodeKind::AccessSpecifier &&
                        (node.Kind == NodeKind::RecordDeclaration || node.Kind == NodeKind::MacroRecordDeclaration))
                    {
                        if (seenSection) {
                            ApplyVerticalGap(Region(entry).Attached.Begin, policy.AccessSectionBlankLinesBefore);
                        }

                        seenSection = true;
                        access = child;
                        previous = None;
                        continue;
                    }

                    if (!DeclarationItem(part.Kind)) {
                        previous = access = None;
                        continue;
                    }

                    // Initial members form an implicit access section.
                    seenSection = true;

                    if (entry.NamespaceSuffix) {
                        previous = child;
                        continue;
                    }

                    if (access != None) {
                        ApplyVerticalGap(Region(entry).Attached.Begin, policy.AccessSectionBlankLinesAfterLabel);
                        access = None;
                    }

                    if (previous != None) {
                        if (EndsStatementBlock(previous)) {
                            ApplyVerticalGap(part.Attached.Begin, policy.BlockBlankLinesAfter);
                        }

                        const bool member =
                            node.Kind == NodeKind::RecordDeclaration || node.Kind == NodeKind::MacroRecordDeclaration;
                        const auto category = entry.Category;
                        const auto previousCategory = Syntax_[previous].Category;
                        Gaps_[part.Attached.Begin].MemberGroup = member &&
                            ((category == DeclarationCategory::Function &&
                                (previousCategory == DeclarationCategory::Variable ||
                                    (previousCategory == DeclarationCategory::Function && entry.Definition &&
                                        Syntax_[previous].Definition))) ||
                                (category == DeclarationCategory::Variable &&
                                    previousCategory == DeclarationCategory::Function));
                        ApplyVerticalGap(
                            Region(entry).Attached.Begin,
                            member ? policy.MemberDeclarationBlankLines : policy.DeclarationBlankLines);

                        if (entry.Definition || Syntax_[previous].Definition) {
                            ApplyVerticalGap(
                                Region(entry).Attached.Begin,
                                member ? policy.MemberDefinitionBlankLines : policy.DefinitionBlankLines);
                        }

                        if (entry.Category != DeclarationCategory::None &&
                            Syntax_[previous].Category != DeclarationCategory::None &&
                            entry.Category != Syntax_[previous].Category)
                        {
                            ApplyVerticalGap(Region(entry).Attached.Begin, policy.DeclarationCategoryBlankLines);
                        }
                    }

                    previous = child;
                }

                if (content && node.Kind == NodeKind::NamespaceDeclaration && close != None) {
                    ApplyVerticalGap(close, policy.NamespaceBlankLinesBeforeClose);
                }

                if (content && node.Kind == NodeKind::LinkageSpecification && close != None) {
                    ApplyVerticalGap(close, policy.LinkageBlankLinesBeforeClose);
                }
            }
        }
    }

    void PlanEmptyLineCaps()
    {
        const auto& policy = Policies_.VerticalGaps;

        if (policy.MaxEmptyLines.Enabled || policy.MemberGroupMaxEmptyLines.Enabled) {
            for (Index i = 0; i < Gaps_.size(); ++i) {
                const auto cap = Gaps_[i].MemberGroup && policy.MemberGroupMaxEmptyLines.Enabled
                    ? policy.MemberGroupMaxEmptyLines
                    : policy.MaxEmptyLines;

                if (!cap.Enabled || !GapMutable(i) || (Gaps_[i].Vertical && !Gaps_[i].VerticalMinimum)) {
                    continue;
                }

                const auto lines = Gaps_[i].Vertical
                    ? Gaps_[i].NewlineCount
                    : std::count(Safety_.Original(i).begin(), Safety_.Original(i).end(), '\n');

                if (lines > cap.EmptyLines + 1) {
                    auto& gap = Gaps_[i];
                    gap.PreserveIndent |= gap.Shape == Boundary::Preserve && !gap.Reindent && !gap.OverrideIndent;

                    if (!gap.ContextKnown) {
                        gap.ContextKnown = true;
                        gap.Context = GapContext::Preserved;
                    }

                    gap.Vertical = true;
                    gap.NewlineCount = uint16_t(cap.EmptyLines + 1);
                }
            }
        }
    }

    void ResolveSafety()
    {
        bool beforeDirective = false;

        for (Index i = Gaps_.size(); i-- > 0;) {
            auto& gap = Gaps_[i];
            const bool comment = i < Tree_.TokenCount() && Tree_.Kind(i) == TokenKind::Comment;

            if (!comment) {
                beforeDirective = i < Tree_.TokenCount() && Tree_.Kind(i) == TokenKind::PreprocessingDirective;
            }

            if (beforeDirective && (!comment || i == 0 || Safety_.Gaps()[i].HasLineFeed)) {
                // Directives retain authored indentation, including column
                // zero. Their leading standalone comments must retain it too,
                // rather than being captured by a preceding case/body scope.
                // The reverse pass carries attachment through comment runs
                // without changing trailing comments or allocating storage.
                gap.PreserveIndent = true;
                gap.Reindent = false;
                gap.OverrideIndent = false;
                gap.RelativeIndent = false;
            }

            if (RetainsBareCR(i)) {
                // Retain source topology before summaries or emission; do not
                // freeze safe atom spelling or invent an owned break.
                gap.Shape = Boundary::Preserve;
                gap.Reindent = false;
                gap.RelativeIndent = false;
                gap.Vertical = false;
                gap.PartialIndent = false;
                gap.CanonicalFlat = false;
                gap.PreserveIndent = true;

                if (gap.MarkDelimiter != None) {
                    Groups_[gap.MarkDelimiter].AnchorAfterGap = true;
                }

                continue;
            }

            // Canonical layout may reflow ordinary line breaks, not erase
            // authored paragraphs. Exact counts/caps have already won; retain
            // original newline spellings while allowing owned indentation.
            if (gap.ParagraphBoundary && !gap.Vertical && !gap.Reassembled && gap.Shape != Boundary::Flat &&
                GapMutable(i))
            {
                const auto original = Safety_.Original(i);
                const auto first = original.find('\n');

                if (first != std::string_view::npos && original.find('\n', first + 1) != std::string_view::npos) {
                    gap.Reindent |= gap.Shape != Boundary::Preserve && !gap.PreserveIndent;
                    gap.Shape = Boundary::Preserve;
                    gap.CanonicalFlat = false;
                }
            }

            gap.PartialIndent = gap.Reindent ||
                (!gap.PreserveIndent && Policies_.IndentationKind != IndentationKind::Preserve &&
                    (gap.Shape == Boundary::Hard || gap.Shape == Boundary::Soft ||
                        (gap.Shape == Boundary::Conditional && gap.Broken != document::GapKind::Flat)));

            if (gap.Shape == Boundary::Preserve) {
                continue;
            }

            const auto shape = gap.Shape;
            const bool flat = Allows(i, document::GapKind::Flat);
            const bool line = Allows(i, document::GapKind::Hard);

            if (!flat && !line) {
                gap.Shape = Boundary::Preserve;
            } else if (!flat) {
                gap.Shape = Boundary::Hard;
            } else if (!line && !(gap.Shape == Boundary::Conditional && gap.Broken == document::GapKind::Flat)) {
                gap.Shape = Boundary::Flat;
            }

            if (gap.Shape != shape) {
                auto& facts = Facts_[i];
                facts.WidthKnown = false;
                facts.FlatKnown = false;
                facts.LineKnown = false;
            }
        }
    }

    void RequireBlockAnchor(Index i)
    {
        const auto& gap = Gaps_[i];

        if (gap.Owner == None || !gap.Closing || gap.OverrideIndent) {
            return;
        }

        // Keep all potential partial-indent consumers conservatively. Fixed
        // horizontal copies and flat generated gaps cannot resolve an anchor.
        const bool lines = gap.Shape == Boundary::Hard || gap.Shape == Boundary::Soft ||
            (gap.Shape == Boundary::Conditional && gap.Broken != document::GapKind::Flat) ||
            (gap.Shape == Boundary::Preserve && Safety_.Gaps()[i].HasLineFeed && (gap.Reindent || gap.Vertical));
        Groups_[gap.Owner].NeedsBlock |= lines || gap.PartialIndent;
    }

    void SummarizeSegments()
    {
        // One reverse pass computes next indivisible segment plus mandatory
        // same-line suffix. Delimiter/semicolon/comma suffixes contribute their
        // actual token widths; optional commas contribute one independent of
        // source comma presence. A future break boundary terminates a segment.
        Index width = 0;
        RequireBlockAnchor(Tree_.TokenCount());

        for (Index i = Tree_.TokenCount(); i-- > 0;) {
            if (!Removed_[i]) {
                const auto tokenWidth = HorizontalColumns(Tree_.TokenText(i), Options_.TabWidth);
                width = AddWidth(tokenWidth, width);
            }

            if (Commas_[i + 1].Owner != None) {
                width = AddWidth(width, 1);
            }

            Facts_[i].RequiredWidth = width;
            const auto fitThrough = Gaps_[i].FitThrough;

            if (fitThrough != None && Gaps_[i].FitNextSegment) {
                // Fit operand/declaration prefixes across optional child
                // breaks. Stop beyond the line width to bound overlapping
                // demands from nested expressions.
                Index prefixWidth = 0;
                auto t = i;

                while (t < fitThrough && (Policies_.ColumnLimit == 0 || prefixWidth <= Policies_.ColumnLimit)) {
                    if (!Removed_[t]) {
                        prefixWidth = AddWidth(prefixWidth, HorizontalColumns(Tree_.TokenText(t), Options_.TabWidth));
                    }

                    ++t;
                    const auto& next = Gaps_[t];
                    const bool mandatory = next.Shape == Boundary::Hard ||
                        (next.Shape == Boundary::Preserve && Safety_.Gaps()[t].HasLineFeed) ||
                        (next.Shape == Boundary::Conditional && next.Broken != document::GapKind::Flat &&
                            next.Owner != None && Groups_[next.Owner].Spec.ForceBreak);

                    if (mandatory) {
                        break;
                    }

                    if (t == fitThrough &&
                        (next.Shape == Boundary::Soft ||
                            (next.Shape == Boundary::Conditional && next.Broken != document::GapKind::Flat)))
                    {
                        // An endpoint break does not require the following
                        // segment to fit on the same line.
                        break;
                    }

                    prefixWidth = AddWidth(prefixWidth, FlatWidth(t));

                    if (t == fitThrough) {
                        prefixWidth = AddWidth(prefixWidth, Facts_[t].RequiredWidth);
                    }
                }

                Facts_[i].RequiredWidth = std::max(width, prefixWidth);
            }

            RequireBlockAnchor(i);
            const auto& gap = Gaps_[i];
            const bool boundary = gap.Shape == Boundary::Soft || gap.Shape == Boundary::Hard ||
                (gap.Shape == Boundary::Conditional && gap.Broken != document::GapKind::Flat) ||
                (gap.Shape == Boundary::Preserve && Safety_.Gaps()[i].HasLineFeed);

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
        return gap.CanonicalFlat || gap.Shape == Boundary::Soft || gap.Shape == Boundary::Hard ||
            (gap.Shape == Boundary::Conditional && gap.Broken != document::GapKind::Flat);
    }

    bool PreservesBoundarySpacing(Index i) const
    {
        const auto& spacing = Spacing_.Gaps()[i];
        switch (spacing.Field) {
#define PRESERVED_BOUNDARY(type, name, initial) case SpacingField::name:
            FORMATTER_SYNTAX_BOUNDARY_GAP_OPTIONS(PRESERVED_BOUNDARY)
#undef PRESERVED_BOUNDARY
                return spacing.Policy == GapPolicy::Preserve;
            default:
                return false;
        }
    }

    bool KeepsFlatSpacing(Index i) const
    {
        return Gaps_[i].KeepFlatSpacing || PreservesBoundarySpacing(i);
    }

    std::optional<bool> SpacePreference(
        Index i,
        GapContext context,
        GapAdjacency adjacency = GapAdjacency::Source) const
    {
        if (adjacency == GapAdjacency::Source && Gaps_[i].FixedSpaces != UINT16_MAX) {
            return Gaps_[i].FixedSpaces != 0;
        }

        if (adjacency != GapAdjacency::Source) {
            return Spacing_.SpacePreference(i, context, Safety_, adjacency);
        }

        auto& facts = Facts_[i];
        auto cached = context == GapContext::Preserved ? facts.PreservedPreference : facts.RebuiltPreference;

        if (cached == 0) {
            const auto preference = Spacing_.SpacePreference(i, context, Safety_);
            cached = preference ? (*preference ? 3 : 2) : 1;

            if (context == GapContext::Preserved) {
                facts.PreservedPreference = cached;
            } else {
                facts.RebuiltPreference = cached;
            }
        }

        return cached < 2 ? std::nullopt : std::optional<bool>{cached == 3};
    }

    std::optional<uint16_t> TrailingCommentCount(Index i) const
    {
        auto& cached = Facts_[i].CachedCommentCount;

        if (cached == UINT16_MAX) {
            // BuildSyntaxSpacing validates counts at most 10000.
            cached = Spacing_.TrailingCommentCount(i, Safety_).value_or(UINT16_MAX - 1);
        }

        return cached == UINT16_MAX - 1 ? std::nullopt : std::optional<uint16_t>{cached};
    }

    Index FlatWidth(Index i, GapAdjacency adjacency = GapAdjacency::Source) const
    {
        if (adjacency != GapAdjacency::Source) {
            return ComputeFlatWidth(i, adjacency);
        }

        auto& gap = Facts_[i];

        if (!GapCacheReady_) {
            return ComputeFlatWidth(i);
        }

        if (!gap.WidthKnown) {
            gap.CachedWidth = ComputeFlatWidth(i);
            gap.WidthKnown = true;
        }

        return gap.CachedWidth;
    }

    Index ComputeFlatWidth(Index i, GapAdjacency adjacency = GapAdjacency::Source) const
    {
        if (adjacency == GapAdjacency::Source && Gaps_[i].FixedSpaces != UINT16_MAX) {
            return Gaps_[i].FixedSpaces;
        }

        if (Removed_.size() > i && Removed_[i]) {
            return 0;
        }

        if (RetainsBareCR(i)) {
            return HorizontalColumns(Safety_.Original(i), Options_.TabWidth);
        }

        if (const auto count = TrailingCommentCount(i)) {
            return *count;
        }

        const auto preference = SpacePreference(i, ContextFor(i), adjacency);

        if (preference) {
            return *preference ? 1 : 0;
        }

        if ((Gaps_[i].SpaceFallback || Gaps_[i].ControlSeparator) && !PreservesBoundarySpacing(i)) {
            return 1;
        }

        const auto& source = Safety_.Gaps()[i];

        if ((!RebuildsTopology(i) || KeepsFlatSpacing(i)) && !source.HasLineFeed) {
            return HorizontalColumns(Safety_.Original(source), Options_.TabWidth);
        }

        return Spacing_.Gaps()[i].FallbackSpace || source.Separation == GapSeparation::Required ? 1 : 0;
    }

    bool BeforeGroup(Index a, Index b) const
    {
        const auto& x = Groups_[a];
        const auto& y = Groups_[b];

        if (x.Begin != y.Begin) {
            return x.Begin < y.Begin;
        }

        if (x.End != y.End) {
            return x.End > y.End;
        }

        return a < b;
    }

    // Balanced syntax bodies already follow the region preorder. Walk their
    // existing projection iteratively and merge specialized interval owners;
    // neither validation nor emission needs a second list of body intervals.
    Index NextGroup(size_t& special, Index& syntax) const
    {
        if (NaturalBodies_) {
            while (syntax < BlockOf_.size() && BlockOf_[syntax] == None) {
                ++syntax;
            }

            if (syntax < BlockOf_.size()) {
                const auto body = BlockOf_[syntax];

                if (special == Order_.size() || BeforeGroup(body, Order_[special])) {
                    ++syntax;
                    return body;
                }
            }
        }

        return special < Order_.size() ? Order_[special++] : None;
    }

    void SortForest()
    {
        Index previous = None;
        size_t bodies = 0;

        for (const auto body : BlockOf_) {
            if (body == None) {
                continue;
            }

            ++bodies;

            if (previous != None && !BeforeGroup(previous, body)) {
                // Non-source-ordered projections still use the same interval
                // planner and emitter; never guess a different ownership.
                NaturalBodies_ = false;
            }

            previous = body;
        }

        Order_.reserve(Groups_.size() - (NaturalBodies_ ? bodies : 0));

        for (Index id = 0; id < Groups_.size(); ++id) {
            if (!NaturalBodies_ || !Groups_[id].SyntaxBody) {
                Order_.push_back(id);
            }
        }

        std::sort(
            Order_.begin(),
            Order_.end(),
            [&] (Index a, Index b) {
                return BeforeGroup(a, b);
            });
        Stack_.clear();
        size_t special = 0;
        Index syntax = 0;

        for (auto id = NextGroup(special, syntax); id != None; id = NextGroup(special, syntax)) {
            auto& group = Groups_[id];

            while (!Stack_.empty() && Groups_[Stack_.back()].End <= group.Begin) {
                Stack_.pop_back();
            }

            if (!Stack_.empty() && Groups_[Stack_.back()].End < group.End) {
                Fail(SyntaxLayoutError::CrossingGroups, Index(group.Begin / 2));
                return;
            }

            const auto parent = group.Spec.FixedBase != None
                ? IndentBasis{group.Spec.FixedBase, false}
                : Stack_.empty()
                    ? IndentBasis{0, false}
                    : IndentBasis{Groups_[Stack_.back()].LogicalIndent, Groups_[Stack_.back()].SaturatedIndent};
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

    document::GapSpec Spec(
        Index i,
        document::GapKind kind,
        bool sourceHorizontal = true,
        GapAdjacency adjacency = GapAdjacency::Source) const
    {
        document::GapSpec spec;
        spec.Kind = kind;
        spec.Spaces = uint16_t(std::min<Index>(FlatWidth(i, adjacency), UINT16_MAX));
        spec.RequiredWidth = Facts_[i].RequiredWidth;
        spec.FitNextSegment = Gaps_[i].FitNextSegment;
        spec.FitNextSegmentIfOverflow = Gaps_[i].FitNextSegmentIfOverflow;
        spec.FitFlatTail = Gaps_[i].FitFlatTail;
        spec.NewlineCount = Gaps_[i].NewlineCount;
        const bool relative = Gaps_[i].OverrideIndent && Gaps_[i].RelativeGroup != None;

        if ((Gaps_[i].PreserveIndent && !Gaps_[i].Reindent) || relative) {
            const auto& source = Safety_.Gaps()[i];
            const auto lf = Safety_.Original(source).rfind('\n');

            if (lf != std::string_view::npos &&
                Safety_.Original(source).substr(lf + 1).find_first_not_of(" \t") == std::string_view::npos)
            {
                spec.IndentSourceBegin = source.Source.Begin + Index(lf + 1);
                spec.IndentSourceEnd = source.Source.End;
                spec.PreserveIndentSpelling = relative;
            }
        }

        if (Safety_.Gaps()[i].MustStartWithLineFeed) {
            spec.LineEnding = document::GapNewline::LF;
        }

        const auto context = ContextFor(i);

        if (sourceHorizontal && (!RebuildsTopology(i) || KeepsFlatSpacing(i)) && Safety_.Gaps()[i].Horizontal &&
            !TrailingCommentCount(i) && !SpacePreference(i, context, adjacency))
        {
            spec.FlatSourceBegin = Safety_.Gaps()[i].Source.Begin;
            spec.FlatSourceEnd = Safety_.Gaps()[i].Source.End;
        }

        return spec;
    }

    document::Provenance GapOrigin(Index i) const
    {
        const auto range = Safety_.Gaps()[i].Source;
        return {range.Begin, range.End, document::NoIdentity};
    }

    bool Allows(Index i, document::GapKind kind) const
    {
        if (!GapCacheReady_) {
            return ComputeAllows(i, kind);
        }

        auto& gap = Facts_[i];

        if (kind == document::GapKind::Flat) {
            if (!gap.FlatKnown) {
                gap.FlatAllowed = ComputeAllows(i, kind);
                gap.FlatKnown = true;
            }

            return gap.FlatAllowed;
        }

        if (!gap.LineKnown) {
            gap.LineAllowed = ComputeAllows(i, kind);
            gap.LineKnown = true;
        }

        return gap.LineAllowed;
    }

    bool ComputeAllows(Index i, document::GapKind kind) const
    {
        const auto& gap = Safety_.Gaps()[i];

        if (Gaps_[i].Frozen || Uncertain(i) || gap.Preserve || gap.ContextSensitive || RetainsBareCR(i)) {
            return false;
        }

        if (kind == document::GapKind::Flat) {
            const bool space = FlatWidth(i) != 0;
            return Safety_.CanReplaceGap(i, space ? " " : "");
        }

        const auto line = gap.MustStartWithLineFeed || Options_.LineEnding == document::Newline::LF ? "\n" : "\r\n";
        return Safety_.CanReplaceGap(i, line);
    }

    bool ExpressionGap(Index i) const
    {
        const auto owner = Gaps_[i].Owner;
        return owner != None && Groups_[owner].Spec.ExpressionContinuation && !Stack_.empty() && Stack_.back() == owner;
    }

    void RecordFixedExpressionLine(Index i, Index begin, Index end)
    {
        // Protection retains bytes, not the fact that a safe current-expression
        // whitespace line realized continuation. Tokens never enter this path.
        if (Gaps_[i].Frozen || Uncertain(i) || Safety_.Gaps()[i].ContextSensitive || !ExpressionGap(i)) {
            return;
        }

        const auto text = Tree_.Source().substr(begin, end - begin);

        if (HasLine(text) && text.find_first_not_of(" \t\r\n") == std::string_view::npos) {
            Document_.ExpressionLine();
        }
    }

    void FixedGapSlice(Index i, Index begin, Index end)
    {
        Document_.AppendSourceText(begin, end, document::NoIdentity, true);
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
        if (begin == Safety_.Gaps()[i].Source.Begin && begin && Tree_.Source()[begin] == '\n' &&
            (Tree_.Source()[begin - 1] == '\r' ||
                (Safety_.Gaps()[i].MustStartWithLineFeed && Policies_.LineEnding == LineEnding::CRLF)))
        {
            FixedGapSlice(i, begin, begin + 1);
            ++begin;
        }

        if (begin < end) {
            Document_.SourceWhitespace(begin, end, Options_.LineEnding);
            RecordFixedExpressionLine(i, begin, end);
        }
    }

    void PreservedSlice(Index i, Index begin, Index end, bool trim, bool trimTail = false)
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

            while (space > run && (source[space - 1] == ' ' || source[space - 1] == '\t')) {
                --space;
            }

            if (RetainsBareCR(i) && space < newline && space > Safety_.Gaps()[i].Source.Begin &&
                source[space - 1] == '\r')
                space = newline; // Keep the complete original separator run.
            EditableGapSlice(i, run, space);
            run = newline;
        }

        if (trimTail) {
            const auto originalEnd = end;

            while (end > run && (source[end - 1] == ' ' || source[end - 1] == '\t')) {
                --end;
            }

            if (RetainsBareCR(i) && end < originalEnd && end > Safety_.Gaps()[i].Source.Begin &&
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

        if (((plan.Frozen || Uncertain(i)) && !plan.RelativeIndent) || source.ContextSensitive) {
            FixedGapSlice(i, source.Source.Begin, source.Source.End);
            return;
        }

        if (source.Preserve) {
            // Full existing bytes need no authority to insert indentation at
            // End/EOF. This grants spelling only; generic gap guards stay put.
            if (Policies_.LineEnding != LineEnding::Preserve && Safety_.CanSpellGapNewlines(i)) {
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
                Safety_.CanReplaceGap(i, Safety_.Original(source)),
                i == Tree_.TokenCount());
            return;
        }

        const auto lf = Safety_.Original(source).rfind('\n');

        if (lf != std::string_view::npos) {
            if (plan.RelativeIndent) {
                if (!Safety_.CanReplaceGap(i, Safety_.Original(source))) {
                    FixedGapSlice(i, source.Source.Begin, source.Source.End);
                    return;
                }

                const auto indentBegin = source.Source.Begin + Index(lf + 1);
                const auto anchor = IndentAnchor(plan);
                Document_.SourceLineIndent(
                    source.Source.Begin,
                    indentBegin,
                    indentBegin,
                    source.Source.End,
                    anchor,
                    Facts_[i].RequiredWidth,
                    document::NoIdentity,
                    plan.RelativeGroup != None);
                return;
            }

            if (plan.Vertical && Allows(i, document::GapKind::Hard)) {
                const auto anchor = IndentAnchor(plan);
                Document_.Gap(Spec(i, document::GapKind::Hard), anchor, GapOrigin(i));
            } else if (plan.Reindent && Allows(i, document::GapKind::Hard)) {
                const bool crlf = lf > 0 && Safety_.Original(source)[lf - 1] == '\r';
                const auto newlineBegin = source.Source.Begin + Index(lf) - (crlf ? 1 : 0);
                // The rebuilt final newline is outside this slice, so trim
                // its trailing whitespace at the slice boundary as well.
                PreservedSlice(
                    i,
                    source.Source.Begin,
                    newlineBegin,
                    Policies_.TrailingWhitespace == TrailingWhitespace::Trim,
                    true);
                auto spec = Spec(i, document::GapKind::Hard, false);

                if (Policies_.LineEnding == LineEnding::Preserve) {
                    spec.LineEnding = crlf ? document::GapNewline::CRLF : document::GapNewline::LF;
                } else if (!source.MustStartWithLineFeed || lf != 0) {
                    spec.LineEnding = document::GapNewline::Inherit;
                }

                const auto anchor = IndentAnchor(plan);
                Document_.Gap(spec, anchor, {newlineBegin, source.Source.End, document::NoIdentity});
            } else {
                PreservedSlice(
                    i,
                    source.Source.Begin,
                    source.Source.End,
                    Policies_.TrailingWhitespace == TrailingWhitespace::Trim &&
                    Safety_.CanReplaceGap(i, Safety_.Original(source)),
                    i == Tree_.TokenCount());
            }

            return;
        }

        if (i == 0 && plan.Reindent && !plan.PreserveIndent && source.Horizontal &&
            source.Source.Begin != source.Source.End && Safety_.CanReplaceGap(i, ""))
        {
            Document_.Gap({.Kind = document::GapKind::Flat, .Spaces = 0}, document::InvalidId, GapOrigin(i));
            return;
        }

        if (i == Tree_.TokenCount() && Policies_.TrailingWhitespace == TrailingWhitespace::Trim &&
            Safety_.CanReplaceGap(i, ""))
        {
            FixedGapSlice(i, source.Source.Begin, source.Source.Begin);
            return;
        }

        const auto context = ContextFor(i);
        const auto preference = SpacePreference(i, context);
        const auto count = TrailingCommentCount(i);
        const bool space = count ? *count != 0 : preference.value_or(false);

        if ((count || preference) && Safety_.CanReplaceGap(i, space ? " " : "")) {
            auto spec = Spec(i, document::GapKind::Flat, false);
            Document_.Gap(spec, document::InvalidId, GapOrigin(i));
        } else {
            FixedGapSlice(i, source.Source.Begin, source.Source.End);
        }
    }

    void EmitPartialGap(Index i, std::span<const PartialGapPermission> permissions)
    {
        const auto& source = Safety_.Gaps()[i];
        const auto& plan = Gaps_[i];

        // Accepted descriptors authorize only their slices, even though this
        // whole gap remains Preserve. Only final indentation may be rebased
        // inside uncertain syntax or a frozen annotation island.
        if (((plan.Frozen || Uncertain(i)) && !plan.RelativeIndent) || source.ContextSensitive) {
            FixedGapSlice(i, source.Source.Begin, source.Source.End);
            return;
        }

        const bool spelling =
            !plan.RelativeIndent && Policies_.LineEnding != LineEnding::Preserve && Safety_.CanSpellGapNewlines(i);
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
                if (!plan.RelativeIndent && Policies_.TrailingWhitespace == TrailingWhitespace::Trim) {
                    if (cursor < permission.Source.Begin) {
                        copied(cursor, permission.Source.Begin);
                    }

                    cursor = permission.Source.End;
                }
            } else if (plan.PartialIndent) {
                const auto anchor = IndentAnchor(plan);
                // Any trimmed runs were before terminators. The remaining
                // contiguous prefix still ends in the original final LF.
                // No fixed newline becomes an editable owned boundary.
                Document_.SourceLineIndent(
                    cursor,
                    permission.Source.Begin,
                    permission.Source.Begin,
                    permission.Source.End,
                    anchor,
                    Facts_[i].RequiredWidth,
                    document::NoIdentity,
                    plan.RelativeGroup != None);
                RecordFixedExpressionLine(i, cursor, permission.Source.Begin);
                cursor = permission.Source.End;
            }
        }

        if (cursor < source.Source.End) {
            copied(cursor, source.Source.End);
        }
    }

    document::AnchorId IndentAnchor(const Gap& gap)
    {
        if (gap.OverrideIndent && gap.RelativeGroup != None) {
            if (gap.ExtraIndent == 0) {
                return Groups_[gap.RelativeGroup].Anchor;
            }

            if (LastRelativeGroup_ != gap.RelativeGroup || LastRelativeOffset_ != gap.ExtraIndent) {
                LastRelativeGroup_ = gap.RelativeGroup;
                LastRelativeOffset_ = gap.ExtraIndent;
                LastRelativeAnchor_ =
                    Document_.MarkAnchor({.Offset = gap.ExtraIndent, .SharedWith = Groups_[gap.RelativeGroup].Anchor});
            }

            return LastRelativeAnchor_;
        }

        if (gap.OverrideIndent) {
            return Document_.MarkAnchor({.Offset = gap.ExtraIndent, .StructuralBase = gap.RelativeIndent});
        }

        return gap.Owner == None
            ? document::InvalidId
            : gap.Closing ? Groups_[gap.Owner].Block : Groups_[gap.Owner].Anchor;
    }

    void EmitGap(Index i)
    {
        const auto permissions = Safety_.PartialGapPermissions();
        const auto begin = NextPermission_;

        while (NextPermission_ < permissions.size() && permissions[NextPermission_].Gap == i) {
            ++NextPermission_;
        }

        const auto& gap = Gaps_[i];

        if (i < Removed_.size() && Removed_[i]) {
            return;
        }

        if (NextPermission_ != begin) {
            EmitPartialGap(i, permissions.subspan(begin, NextPermission_ - begin));
            return;
        }

        if (gap.Shape == Boundary::Preserve) {
            PreserveGap(i);
            return;
        }

        const auto& source = Safety_.Gaps()[i];

        if (!RebuildsTopology(i) && !source.HasLineFeed && !source.Horizontal &&
            !SpacePreference(i, GapContext::Rebuilt))
        {
            PreserveGap(i);
            return;
        }

        const auto anchor = IndentAnchor(gap);
        const bool flatOK = Allows(i, document::GapKind::Flat);
        const bool lineOK = Allows(i, document::GapKind::Hard);

        if (!flatOK && !lineOK) {
            PreserveGap(i);
            return;
        }

        // Safety converts required/forbidden boundaries before shape selection.
        // This is not byte-diff activation and never weakens topology barriers.
        if (!flatOK) {
            Document_.Gap(Spec(i, document::GapKind::Hard), anchor, GapOrigin(i));
        } else if (gap.Shape == Boundary::Conditional && (lineOK || gap.Broken == document::GapKind::Flat)) {
            if (Stack_.empty() || Stack_.back() != gap.Owner) {
                Fail(SyntaxLayoutError::ConflictingGapOwners, i);
                return;
            }

            Document_.ConditionalGap(
                Groups_[gap.Owner].Id,
                Spec(i, document::GapKind::Flat),
                Spec(i, gap.Broken, true, gap.BrokenAdjacency),
                anchor,
                GapOrigin(i),
                gap.BraceIndentOffset);
        } else if (!lineOK || gap.Shape == Boundary::Flat) {
            Document_.Gap(Spec(i, document::GapKind::Flat), anchor, GapOrigin(i));
        } else {
            Document_.Gap(
                Spec(i, gap.Shape == Boundary::Hard ? document::GapKind::Hard : document::GapKind::Soft),
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
        const auto fallback = int64_t(group.Indent.FallbackOffset) - int64_t(group.Indent.ContinuationOffset);
        const auto offset = group.AnchorAfterGap ? 0 : int32_t(FlatWidth(group.PrefixGap));
        group.Anchor = Document_.MarkAnchor({document::AnchorKind::DelimiterColumn, offset, int32_t(fallback), 0});
        group.ContinuationAnchor = group.Anchor;
    }

    void Emit()
    {
        Document_.Reserve(
            uint32_t(std::min<uint64_t>(None, uint64_t(Tree_.TokenCount()) * 2 + uint64_t(Groups_.size()) * 5 + 1)),
            Index(Groups_.size()),
            uint32_t(std::min<uint64_t>(None, uint64_t(Tree_.TokenCount()) * 2 + 1)));
        size_t special = 0;
        Index syntax = 0;
        auto next = NextGroup(special, syntax);
        Index continuation = None;

        for (Position p = 0; p <= GapPosition(Tree_.TokenCount()); ++p) {
            while (!Stack_.empty() && Groups_[Stack_.back()].End == p) {
                Document_.EndGroup(Groups_[Stack_.back()].Id);
                continuation = Groups_[Stack_.back()].PreviousContinuation;
                Stack_.pop_back();
            }

            while (next != None && Groups_[next].Begin == p) {
                const auto id = next;
                next = NextGroup(special, syntax);
                auto& group = Groups_[id];
                group.PreviousContinuation = continuation;
                const bool sharedAnchor = group.Spec.Continuation && continuation != None &&
                    group.Indent.Alignment == ContinuationAlignment::Block;

                if (group.NeedsBlock) {
                    group.Block = Document_.MarkAnchor({
                        document::AnchorKind::BlockRelative,
                        0,
                        0,
                        0,
                        group.Spec.StructuralBase,
                        group.Spec.Continuation,
                        document::InvalidId,
                        group.Spec.LineBase,
                        group.Spec.FixedBase,
                    });
                }

                // Begin repeats the unused marker's checks before emission.
                // Retain its search weight without emitting the marker.
                if (group.PrefixGap != None) {
                    group.Anchor = Document_.MarkAnchor({
                        document::AnchorKind::BlockRelative,
                        int32_t(group.Indent.ContinuationOffset),
                        0,
                        0,
                        group.Spec.StructuralBase,
                        group.Spec.Continuation,
                    });
                } else if (sharedAnchor && !group.Spec.StructuralBase) {
                    // Runtime line changes decide whether starters share.
                    // Alias the previous anchor only while still on that line;
                    // otherwise capture this scope's new continuation level.
                    group.Anchor = Document_.MarkAnchor({
                        .Kind = document::AnchorKind::BlockRelative,
                        .Offset = int32_t(group.Indent.ContinuationOffset),
                        .Continuation = true,
                        .SharedWith = Groups_[continuation].ContinuationAnchor,
                    });
                } else if (group.HasAnchor) {
                    group.Anchor = Document_
                        .MarkAnchor({
                            group.Indent.Alignment == ContinuationAlignment::Delimiter
                                ? document::AnchorKind::DelimiterColumn
                                : document::AnchorKind::BlockRelative,
                            group.Indent.Alignment == ContinuationAlignment::Delimiter
                                ? 0
                                : int32_t(group.Indent.ContinuationOffset),
                            int32_t(group.Indent.FallbackOffset),
                            uint32_t(group.Indent.ContinuationOffset),
                            group.Spec.StructuralBase,
                            group.Spec.Continuation,
                            sharedAnchor ? Groups_[continuation].ContinuationAnchor : document::InvalidId,
                            group.Spec.LineBase,
                            group.Spec.FixedBase,
                        });
                }

                if (group.IndentGroup != None) {
                    // Packing adds a shape choice, not a continuation level.
                    // Reuse delimiter alignment and its width fallback too.
                    group.Anchor = Groups_[group.IndentGroup].Anchor;
                }

                // Children share the prospective level, not the arm's
                // physical-line anchor used for its own preserved gaps.
                group.ContinuationAnchor = group.Anchor;

                if (group.ShareBranchContinuation && continuation != None) {
                    // Reuse the prospective anchor while inline, or the actual
                    // delimiter line's indentation once it has been emitted.
                    // Keep the shared frame's realization state so a call on
                    // this continued line can still indent its own arguments.
                    group.Anchor =
                        Document_.MarkAnchor({document::AnchorKind::BlockRelative, 0, 0, 0, true, true, group.Anchor});
                }

                if (group.IndentTernaryOperators) {
                    // Capture at the condition's start, not after its wrapped
                    // operands. Only the delimiter gaps use this new level;
                    // condition descendants keep the shared continuation.
                    group.Anchor = Document_.MarkAnchor({
                        document::AnchorKind::BlockRelative,
                        int32_t(group.Indent.ContinuationOffset),
                        0,
                        0,
                        true,
                        true,
                        group.ContinuationAnchor,
                    });
                }

                group.Id = Document_.BeginGroup(group.Spec, !group.NeedsBlock);

                if (group.Spec.Continuation) {
                    continuation = id;
                } else if (group.Spec.StructuralBase) {
                    continuation = None;
                }

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
                    // not an identity for a surviving source comma. The flat
                    // alternative has zero width: charging for the broken-only
                    // comma would spuriously break a fitting ancestor.
                    const auto end = Tree_.Token(i - 1).Text.End;
                    Document_.ConditionalText(Groups_[owner].Id, "", ",", {end, end, document::NoIdentity});
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
                const auto text = Tree_.TokenText(i);
                const bool immutable = Safety_.IntersectsImmutable({token.Begin, token.End});

                if (Tree_.Kind(i) == TokenKind::Comment && text.starts_with("/*") && HasLine(text) && !immutable) {
                    Document_.ReindentedSourceText(token.Begin, token.End, i);
                } else {
                    const bool opaque = Tree_.Kind(i) == TokenKind::Comment ||
                        Tree_.Kind(i) == TokenKind::PreprocessingDirective || HasLine(text) || immutable;
                    Document_.AppendSourceText(token.Begin, token.End, i, opaque);
                }
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
    document::Document& document,
    std::pmr::memory_resource& scratch,
    const SyntaxLayoutOptions& options)
{
    if (options.TabWidth == 0 || options.TabWidth > 32) {
        return {SyntaxLayoutError::MismatchedInputs};
    }

    return Planner(tree, policies, annotations, safety, spacing, document, scratch, options).Build();
}

document::Policy SyntaxRenderPolicy(const CompiledPolicies& policies, const SyntaxLayoutOptions& options)
{
    document::Policy result;
    result.Width = policies.ColumnLimit == 0 ? UINT32_MAX - 1 : uint32_t(policies.ColumnLimit);
    result.TabWidth = options.TabWidth;
    result.LineEnding = options.LineEnding;
    result.Indentation = policies.IndentationKind == IndentationKind::Tabs
        ? document::IndentationKind::Tabs
        : document::IndentationKind::Spaces;
    return result;
}

} // namespace formatter
