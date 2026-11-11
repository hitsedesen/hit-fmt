#pragma once

#include "config_annotations.hpp"
#include "layout_measure.hpp"
#include "syntax_spacing.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <optional>

namespace formatter::detail {

struct FlatSpanRules
{
    using Index = parser::TokenIndex;
    using NK = parser::NodeKind;
    using TK = tokenizer::TokenKind;
    static constexpr Index None = UINT32_MAX;

    static bool Canonical(const ListPolicy& policy)
    {
        return policy.Layout == LayoutMode::Canonical && policy.Break == BreakRequirement::IfNeeded;
    }

    static bool Expanded(const BodyPolicy& policy)
    {
        return policy.Layout == LayoutMode::Canonical && policy.Shape == BodyShape::Expanded;
    }

    static bool StylesAllow(const CompiledPolicies& policies)
    {
        return policies.IndentationKind == IndentationKind::Spaces &&
            policies.DeclarationHeaders == LayoutMode::Canonical &&
            policies.Expressions.Layout == LayoutMode::Canonical &&
            policies.Expressions.Break == BreakRequirement::IfNeeded &&
            policies.Expressions.AssignmentRhs == FirstItemPlacement::InlineIfFits &&
            policies.Chains.Layout == LayoutMode::Canonical && policies.Chains.Break == BreakRequirement::IfNeeded &&
            Canonical(policies.Calls) && Canonical(policies.Parameters) && Canonical(policies.TemplateArguments) &&
            Expanded(policies.Bodies) && Expanded(policies.FunctionBodies) && Expanded(policies.ControlBodies) &&
            policies.Indentation.StandaloneBraces.Mode != StandaloneBlockBraceIndentation::Indent;
    }

    static constexpr bool ScalarGrammar(NK kind)
    {
        switch (kind) {
            case NK::DeclarationSpecifiers:
            case NK::InitDeclaratorList:
            case NK::InitDeclarator:
            case NK::Initializer:
            case NK::Declarator:
            case NK::AbstractDeclarator:
            case NK::DeclaratorName:
            case NK::DestructorName:
            case NK::OperatorFunctionName:
            case NK::PointerOperator:
            case NK::MemberPointerOperator:
            case NK::NestedDeclarator:
            case NK::ArrayDeclarator:
            case NK::FunctionDeclarator:
            case NK::ParameterList:
            case NK::ParameterDeclaration:
            case NK::FunctionQualifier:
            case NK::ExceptionSpecification:
            case NK::TrailingReturnType:
            case NK::TypeId:
            case NK::TemplateArgument:
            case NK::TemplateArgumentList:
            case NK::TemplateIdExpression:
            case NK::CastExpression:
            case NK::Expression:
            case NK::IdentifierExpression:
            case NK::Argument:
            case NK::ArgumentList:
            case NK::ListSeparator:
            case NK::UnaryExpression:
            case NK::BinaryExpression:
            case NK::AssignmentExpression:
            case NK::ConditionalExpression:
            case NK::ParenthesizedExpression:
            case NK::SubscriptExpression:
            case NK::LiteralExpression:
            case NK::CallExpression:
            case NK::MemberExpression:
                return true;
            default:
                return false;
        }
    }

    static bool CategorySpine(NK parent, NK child)
    {
        switch (parent) {
            case NK::Declaration:
            case NK::DeclarationStatement:
            case NK::FunctionDeclaration:
                return child == NK::InitDeclaratorList;
            case NK::InitDeclaratorList:
                return child == NK::InitDeclarator;
            case NK::InitDeclarator:
                return child == NK::Declarator;
            case NK::Declarator:
            case NK::NestedDeclarator:
                return child == NK::NestedDeclarator || child == NK::FunctionDeclarator ||
                    child == NK::PointerOperator || child == NK::MemberPointerOperator || child == NK::ArrayDeclarator;
            default:
                return false;
        }
    }

    static uint32_t SourceColumn(const parser::SyntaxTree& tree, Index token, uint8_t tabWidth)
    {
        const auto prefix = tree.Source().substr(0, tree.Token(token).Text.Begin);
        const auto line = prefix.rfind('\n');
        return layout::detail::SourceColumn(prefix.substr(line == std::string_view::npos ? 0 : line + 1), tabWidth);
    }

    static std::optional<uint32_t> Indentation(
        const parser::SyntaxTree& tree,
        RegionId root,
        std::span<const SourceRegion> regions,
        const CompiledPolicies& policies,
        uint8_t tabWidth,
        std::span<const uint32_t> maximumIndent)
    {
        uint64_t indent = 0;
        uint64_t fixed = 0;
        auto child = root;

        for (auto parent = regions[child].Parent; parent != NoRegion; parent = regions[parent].Parent) {
            const auto& region = regions[parent];
            switch (region.Kind) {
                case NK::TranslationUnit:
                    break;
                case NK::TemplateDeclaration:
                    if (!policies.BreakAfterTemplateHeader) {
                        return std::nullopt;
                    }

                    // A positioned prefix constraint can join the following
                    // header to its own line and continuation base.
                    for (auto part = region.FirstChild; part != NoRegion; part = regions[part].NextSibling) {
                        if (regions[part].Kind == NK::RequiresClause) {
                            return std::nullopt;
                        }
                    }

                    break;
                case NK::FunctionDefinition:
                case NK::MacroFunctionDefinition:
                    break;
                case NK::NamespaceDeclaration:
                case NK::RecordDeclaration: {
                    fixed = std::max(fixed, uint64_t(maximumIndent[parent]) + indent);
                    const auto& control = region.Kind == NK::NamespaceDeclaration
                        ? policies.Indentation.Namespaces
                        : policies.Indentation.Members;

                    if (control.Mode == BodyIndentation::Preserve) {
                        return std::nullopt;
                    }

                    if (control.Mode == BodyIndentation::Indent) {
                        indent += control.Offset;
                    }

                    break;
                }
                case NK::CompoundStatement: {
                    fixed = std::max(fixed, uint64_t(maximumIndent[parent]) + indent);
                    const auto outer = region.Parent == NoRegion ? NK::TranslationUnit : regions[region.Parent].Kind;
                    const auto& body = outer == NK::FunctionDefinition || outer == NK::MacroFunctionDefinition
                        ? policies.FunctionBodies
                        : outer == NK::IfStatement || outer == NK::ForStatement || outer == NK::WhileStatement ||
                            outer == NK::DoWhileStatement || outer == NK::TryStatement || outer == NK::CatchClause
                            ? policies.ControlBodies
                            : policies.Bodies;
                    indent += body.IndentWidth;
                    break;
                }
                case NK::IfStatement:
                case NK::ForStatement:
                case NK::WhileStatement:
                case NK::DoWhileStatement:
                case NK::TryStatement:
                case NK::CatchClause:
                    if (regions[child].Kind != NK::CompoundStatement) {
                        return std::nullopt;
                    }

                    break;
                default:
                    return std::nullopt;
            }

            child = parent;
        }

        auto outer = root;

        while (regions[outer].Parent != NoRegion && regions[regions[outer].Parent].Kind != NK::TranslationUnit) {
            outer = regions[outer].Parent;
        }

        if (!regions[outer].Tokens.Empty()) {
            indent += SourceColumn(tree, regions[outer].Tokens.Begin, tabWidth);
        }

        indent = std::max(indent, fixed);

        if (indent > uint32_t(INT32_MAX)) {
            return std::nullopt;
        }

        return static_cast<uint32_t>(indent);
    }

};

class FlatSpanCollector
    : public FlatSpanRules
{
public:
    FlatSpanCollector(
        const parser::SyntaxTree& tree,
        const SourceAnalysis& safety,
        const CompiledPolicies& policies,
        const LibraryAnnotations& annotations,
        uint8_t tabWidth,
        std::pmr::memory_resource& scratch)
        : Tree_(tree)
        , Safety_(safety)
        , Policies_(policies)
        , Annotations_(annotations)
        , TabWidth_(tabWidth)
        , Candidates_(&scratch)
        , MaximumIndent_(&scratch)
        , Forbidden_(&scratch)
    {
        constexpr uint64_t maximumItems = 65536;
        constexpr uint64_t maximumBytes = 16 * 1024 * 1024;
        constexpr uint64_t maximumGroups = 64 * maximumItems;
        static_assert(maximumGroups < 0x04000000);
        static_assert(32 * (maximumBytes + maximumGroups) < INT32_MAX);
        Enabled_ = StylesAllow(policies) && uint64_t(tree.TokenCount()) + safety.Regions().size() <= maximumItems &&
            tree.Source().size() <= maximumBytes;

        if (!Enabled_) {
            return;
        }

        MaximumIndent_.resize(safety.Regions().size(), 0);
        const auto add = [&] (std::string_view name) {
            const auto qualifier = name.rfind("::");
            const auto terminal = qualifier == std::string_view::npos ? name : name.substr(qualifier + 2);
            Forbidden_.push_back(terminal);
            ForbiddenNames_.Add(terminal);
        };

        for (const auto& pin : annotations.MessagePins()) {
            add(pin.Name);
        }

        for (const auto& name : annotations.ChainRoots()) {
            add(name);
        }

        for (const auto& name : annotations.AlwaysBreakChainRoots()) {
            add(name);
        }

        for (const auto& name : annotations.AlwaysBreakChainCalls()) {
            add(name);
        }

        for (const auto& pair : annotations.ChainScopes()) {
            add(pair.First);
            add(pair.Second);
        }

        for (const auto& pair : annotations.StickyChainPairs()) {
            add(pair.First);
            add(pair.Second);
        }

        std::sort(Forbidden_.begin(), Forbidden_.end());
        Forbidden_.erase(std::unique(Forbidden_.begin(), Forbidden_.end()), Forbidden_.end());
        HasVisualScopes_ = !annotations.VisualScopes().empty();

        for (const auto& scope : annotations.VisualScopes()) {
            VisualNames_.Add(scope.Begin);
            VisualNames_.Add(scope.End);
        }
    }

    bool Enabled() const
    {
        return Enabled_;
    }

    static constexpr bool CandidateKind(NK kind)
    {
        return kind == NK::FunctionDefinition || kind == NK::Declaration || kind == NK::FunctionDeclaration ||
            kind == NK::ExpressionStatement || kind == NK::DeclarationStatement || kind == NK::ReturnStatement ||
            kind == NK::ThrowStatement || kind == NK::CoYieldStatement;
    }

    static bool RecoveryBody(const SourceRegion& node)
    {
        return node.Status != RegionStatus::Complete &&
            (node.Kind == NK::NamespaceDeclaration || node.Kind == NK::RecordDeclaration ||
                node.Kind == NK::CompoundStatement);
    }

    void BodyChild(RegionId parent, const SourceRegion& node)
    {
        if (node.Tokens.Empty()) {
            return;
        }

        const auto& gap = Safety_.Gaps()[node.Tokens.Begin];

        if (!gap.HasLineFeed) {
            return;
        }

        const auto original = Safety_.Original(gap);
        const auto indent = original.substr(original.rfind('\n') + 1);

        if (indent.find_first_not_of(" \t") == std::string_view::npos) {
            MaximumIndent_[parent] = std::max(MaximumIndent_[parent], layout::detail::SourceColumn(indent, TabWidth_));
        }
    }

    void Call(const SourceRegion& node)
    {
        if (!HasVisualScopes_ || Visual_) {
            return;
        }

        const auto regions = Safety_.Regions();
        const auto callee = node.FirstChild;

        if (callee == NoRegion) {
            return;
        }

        auto name = callee;

        if (regions[callee].Kind == NK::MemberExpression) {
            const auto receiver = regions[callee].FirstChild;
            name = receiver == NoRegion ? NoRegion : regions[receiver].NextSibling;
        }

        if (name == NoRegion || regions[name].Tokens.Size() != 1) {
            return;
        }

        // ScopeEndpoint only accepts these call/member terminal-name shapes.
        // Observing every call is conservative across its statement, wrapper,
        // argument and source-safety barriers.
        const auto spelling = Tree_.TokenText(regions[name].Tokens.Begin);

        if (VisualNames_.MayContain(spelling)) {
            Visual_ =
                Annotations_.FindScopeBegin(spelling) != nullptr || Annotations_.FindScopeEnd(spelling) != nullptr;
        }
    }

    Index Enter(RegionId id, const SourceRegion& node, Index enclosing)
    {
        if (enclosing != None) {
            auto& candidate = Candidates_[enclosing];

            if (candidate.Valid && node.Tokens.Begin < candidate.Span.Tokens.End) {
                candidate.Valid = node.Status == RegionStatus::Complete && ScalarGrammar(node.Kind) &&
                    node.Tokens.End <= candidate.Span.Tokens.End &&
                    (node.Kind != NK::LiteralExpression || node.Tokens.Size() == 1);
                candidate.Span.RegionEnd = id + 1;

                if (candidate.Valid) {
                    return enclosing;
                }
            }
        }

        return CandidateKind(node.Kind) ? Start(id, node) : None;
    }

    Index Start(RegionId id, const SourceRegion& node)
    {
        const auto regions = Safety_.Regions();
        auto range = node.Tokens;
        const bool header = node.Kind == NK::FunctionDefinition;
        const bool declaration = node.Kind == NK::Declaration || node.Kind == NK::FunctionDeclaration;
        const bool statement = node.Kind == NK::ExpressionStatement || node.Kind == NK::DeclarationStatement ||
            node.Kind == NK::ReturnStatement || node.Kind == NK::ThrowStatement || node.Kind == NK::CoYieldStatement;

        if ((!header && !declaration && !statement) || range.Size() < 5 || node.Parent == NoRegion ||
            (node.Status != RegionStatus::Complete && !header))
        {
            return None;
        }

        const auto parent = regions[node.Parent].Kind;

        if (statement && parent != NK::CompoundStatement) {
            return None;
        }

        // StatementMacroName only classifies a one-token identifier callee
        // root. In this comment-free scalar grammar that root starts the
        // expression statement; nested arguments cannot affect this rule.
        if (node.Kind == NK::ExpressionStatement && Tree_.Kind(range.Begin) == TK::Identifier &&
            Annotations_.IsBlankLineMacro(Tree_.TokenText(range.Begin)))
        {
            return None;
        }

        if (declaration && parent != NK::TranslationUnit && parent != NK::NamespaceDeclaration &&
            parent != NK::RecordDeclaration && parent != NK::TemplateDeclaration)
        {
            return None;
        }

        RegionId body = NoRegion;

        if (header) {
            const auto brace = node.Function == parser::FunctionKind::Constructor
                ? Policies_.Braces.Constructor
                : node.Function == parser::FunctionKind::Destructor
                    ? Policies_.Braces.Destructor
                    : Policies_.Braces.Function;

            if (brace != BracePlacement::NextLine) {
                return None;
            }

            body = node.FirstChild;

            while (body != NoRegion && regions[body].Kind != NK::CompoundStatement) {
                body = regions[body].NextSibling;
            }

            if (body == NoRegion) {
                return None;
            }

            range.End = regions[body].Tokens.Begin;
        } else {
            if (Tree_.Kind(range.End - 1) != TK::Operator_Semicolon) {
                return None;
            }

            --range.End;
        }

        if (range.Size() < 4) {
            return None;
        }

        const auto end = header ? range.End : range.End + 1;

        if ((range.Begin &&
            (!Safety_.Gaps()[range.Begin].HasLineFeed ||
                Safety_.Gaps()[range.Begin].LineBoundary == GapLineBoundary::Forbidden)) ||
            (end != Tree_.TokenCount() && !Safety_.Gaps()[end].HasLineFeed) ||
            (header && !Safety_.Gaps()[range.End].HasLineFeed))
        {
            return None;
        }

        const auto beginOffset = Tree_.Token(range.Begin).Text.Begin;
        const auto endOffset = Tree_.Token(range.End - 1).Text.End;

        if (Policies_.ColumnLimit && endOffset - beginOffset + (header ? 0 : 2) > Policies_.ColumnLimit) {
            return None;
        }

        const auto original = Tree_.Source().substr(beginOffset, endOffset - beginOffset);

        if (util::PlainAsciiPrefix(original) != original.size() || original.find('\x7f') != std::string_view::npos) {
            return None;
        }

        const auto result = Index(Candidates_.size());
        Candidates_.push_back({{range, id, id + 1}, body, true});
        return result;
    }

    void Token(Index candidateId, Index id, TK kind)
    {
        if (candidateId == None) {
            return;
        }

        auto& candidate = Candidates_[candidateId];
        const auto range = candidate.Span.Tokens;

        if (!candidate.Valid || id < range.Begin || id >= range.End) {
            return;
        }

        if (kind == TK::Comment || kind == TK::PreprocessingDirective || kind == TK::Invalid ||
            kind == TK::Operator_OpenBrace || kind == TK::Operator_CloseBrace)
        {
            candidate.Valid = false;
            return;
        }

        if (kind == TK::Identifier || kind >= TK::Keyword_alignas) {
            const auto name = Tree_.TokenText(id);

            if (ForbiddenNames_.MayContain(name) && std::binary_search(Forbidden_.begin(), Forbidden_.end(), name)) {
                candidate.Valid = false;
                return;
            }
        }

        // The parser only assigns matching links to physical delimiters.
        if (kind == TK::Operator_OpenParenthesis || kind == TK::Operator_CloseParenthesis ||
            kind == TK::Operator_OpenBracket || kind == TK::Operator_CloseBracket)
        {
            const auto matching = Tree_.MatchingDelimiter(id);

            if (matching != None && (matching < range.Begin || matching >= range.End)) {
                candidate.Valid = false;
            }
        }
    }

    Index BeforeGaps()
    {
        size_t retained = 0;

        for (const auto& candidate : Candidates_) {
            if (candidate.Valid) {
                Candidates_[retained++] = candidate;
            }
        }

        Candidates_.resize(retained);

        // Candidate kinds are disjoint statement/declaration ranges or a
        // function's pre-body header. Invalid enclosing syntax was discarded.
        for (size_t i = 1; i < Candidates_.size(); ++i) {
            if (Candidates_[i - 1].Span.Tokens.End > Candidates_[i].Span.Tokens.Begin) {
                Candidates_.clear();
                return None;
            }
        }

        return Candidates_.empty() ? None : Candidates_[0].Span.Tokens.Begin + 1;
    }

    Index Gap(Index id, const SourceGap& source, const SyntaxGapPreference& rule)
    {
        auto& candidate = Candidates_[GapCandidate_];
        const bool safe = source.Horizontal && !source.Preserve && !source.ContextSensitive &&
            !source.MustStartWithLineFeed && source.LineBoundary != GapLineBoundary::Required &&
            !(uint8_t(rule.Constraint) & uint8_t(SpacingConstraint::UncertainSyntax));
        candidate.Valid = safe && Matches(source, rule);

        if (candidate.Valid && id + 1 < candidate.Span.Tokens.End) {
            return id + 1;
        }

        ++GapCandidate_;
        return GapCandidate_ == Candidates_.size() ? None : Candidates_[GapCandidate_].Span.Tokens.Begin + 1;
    }

    void Finish(const SyntaxSpacing& spacing, std::pmr::vector<FlatSpan>& spans)
    {
        if (Visual_) {
            return;
        }

        const auto boundary = [&] (Index id, bool line) {
            const auto& source = Safety_.Gaps()[id];
            const auto& rule = spacing.Gaps()[id];

            if (source.Preserve || source.ContextSensitive || source.HasBareCR || source.MustStartWithLineFeed ||
                (uint8_t(rule.Constraint) & uint8_t(SpacingConstraint::UncertainSyntax)))
            {
                return false;
            }

            const auto text = Safety_.Original(source);

            if (!line) {
                return source.Horizontal && source.LineBoundary != GapLineBoundary::Required &&
                    (text.empty() || text == " ");
            }

            if (source.LineBoundary == GapLineBoundary::Forbidden) {
                return false;
            }

            const auto first = text.find('\n');
            const auto prefix = text.substr(0, first);
            return prefix.empty() || (first != std::string_view::npos && prefix == "\r");
        };
        size_t protection = 0, permission = 0;
        const auto protectedRanges = Safety_.Protection();
        const auto permissions = Safety_.PartialGapPermissions();

        for (const auto& candidate : Candidates_) {
            if (!candidate.Valid) {
                continue;
            }

            const auto range = candidate.Span.Tokens;
            const bool header = candidate.Body != NoRegion;
            const auto end = header ? range.End : range.End + 1;

            if (!boundary(range.End, header) || (!header && !boundary(end, true))) {
                continue;
            }

            const auto sourceBegin = Tree_.Token(range.Begin).Text.Begin;
            const auto sourceEnd = Tree_.Token(range.End - 1).Text.End;

            while (protection < protectedRanges.size() && protectedRanges[protection].Source.End <= sourceBegin) {
                ++protection;
            }

            if (protection < protectedRanges.size() && protectedRanges[protection].Source.Begin < sourceEnd) {
                continue;
            }

            while (permission < permissions.size() && permissions[permission].Gap <= range.Begin) {
                ++permission;
            }

            if (permission < permissions.size() && permissions[permission].Gap <= end) {
                continue;
            }

            const auto indent =
                Indentation(Tree_, candidate.Span.Root, Safety_.Regions(), Policies_, TabWidth_, MaximumIndent_);

            if (!indent) {
                continue;
            }

            const auto authored = SourceColumn(Tree_, range.Begin, TabWidth_);
            const auto headerBase = header ? MaximumIndent_[candidate.Body] : 0;
            const uint64_t width =
                std::max({*indent, authored, headerBase}) + uint64_t(sourceEnd - sourceBegin) + (header ? 0 : 2);

            if (Policies_.ColumnLimit && width > Policies_.ColumnLimit) {
                continue;
            }

            spans.push_back(candidate.Span);
        }
    }

private:
    struct NameFilter
    {
        // A negative answer is exact; collisions only perform the existing
        // annotation lookup. Configuration size does not affect lookup cost.
        std::array<uint64_t, 16> Bits{};
        size_t Maximum = 0;

        static size_t Key(std::string_view name)
        {
            return (name.size() * 131 + uint8_t(name.front()) * 17 + uint8_t(name.back()) * 7 +
                (name.size() > 1 ? uint8_t(name[1]) * 31 : 0)) & 1023;
        }

        void Add(std::string_view name)
        {
            if (name.empty()) {
                return;
            }

            Maximum = std::max(Maximum, name.size());
            const auto key = Key(name);
            Bits[key >> 6] |= uint64_t{1} << (key & 63);
        }

        bool MayContain(std::string_view name) const
        {
            if (name.empty() || name.size() > Maximum) {
                return false;
            }

            const auto key = Key(name);
            return (Bits[key >> 6] & (uint64_t{1} << (key & 63))) != 0;
        }
    };

    bool Matches(const SourceGap& source, const SyntaxGapPreference& rule) const
    {
        const auto text = Safety_.Original(source);

        if (text.size() > 1 || (!text.empty() && text.front() != ' ')) {
            return false;
        }

        const bool space = !text.empty();

        if (space && source.Separation == GapSeparation::Required) {
            return true;
        }

        // These are already finalized, safe, original-adjacency preferences.
        // The shared scalar policy is all SpacePreference can still change.
        const bool fallback = rule.FallbackSpace;

        if (Policies_.TrailingCommentSpaces && rule.Field == SpacingField::TrailingCommentSpacing) {
            return space == fallback;
        }

        return GapUsesSpace(rule.Policy, GapContext::Preserved).value_or(fallback) == space &&
            GapUsesSpace(rule.Policy, GapContext::Rebuilt).value_or(fallback) == space;
    }

    struct Candidate
    {
        FlatSpan Span;
        RegionId Body;
        bool Valid;
    };

    const parser::SyntaxTree& Tree_;
    const SourceAnalysis& Safety_;
    const CompiledPolicies& Policies_;
    const LibraryAnnotations& Annotations_;
    uint8_t TabWidth_;
    bool Enabled_ = false, Visual_ = false, HasVisualScopes_ = false;
    NameFilter ForbiddenNames_, VisualNames_;
    std::pmr::vector<Candidate> Candidates_;
    std::pmr::vector<uint32_t> MaximumIndent_;
    std::pmr::vector<std::string_view> Forbidden_;
    size_t GapCandidate_ = 0;
};

struct FlatSpanAccess
{
    template <bool CollectFlat>
    static SyntaxSpacing BuildSpacing(
        const parser::SyntaxTree& tree,
        const SourceAnalysis& safety,
        const GapPolicies& spacing,
        PointerReferenceDeclaratorLocation pointerBinding,
        std::pmr::memory_resource& fileMemory,
        std::pmr::memory_resource& scratchMemory,
        std::optional<TrailingCommentSpaceSetting> trailingCommentSpaces,
        FlatSpanCollector* flat);

    static std::span<const FlatSpan> Spans(const SyntaxSpacing& spacing)
    {
        return spacing.FlatSpans_;
    }
};

SyntaxSpacing BuildFlatSyntaxSpacing(
    const parser::SyntaxTree& tree,
    const SourceAnalysis& safety,
    const CompiledPolicies& policies,
    const LibraryAnnotations& annotations,
    uint8_t tabWidth,
    std::pmr::memory_resource& fileMemory,
    std::pmr::memory_resource& scratchMemory);

} // namespace formatter::detail
