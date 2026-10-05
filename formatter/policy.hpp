#pragma once

#include "config_enum.hpp"
#include "config_options.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <type_traits>

namespace formatter {

struct Style;

// Lowering supplies the owning property's layout context, independently of
// token provenance. A canonical combinator rebuilds its own gaps, including
// existing adjacencies; unrelated nested edits do not rebuild preserved owners.
// Inserted gaps are always Rebuilt. Never infer context from before/after text.
enum class GapContext : std::uint8_t
{
    Preserved,
    Rebuilt
};

// nullopt delegates spelling to lowering: rebuilt, topology-changing gaps use
// syntax-safe flat fallback before fitting, even if they stay flat. Preserved
// and flat-only gaps retain source bytes. Layout owns newlines; safety wins.
[[nodiscard]] constexpr std::optional<bool> GapUsesSpace(
    GapPolicy policy,
    GapContext context) noexcept
{
    switch (policy) {
        case GapPolicy::Preserve:
            return std::nullopt;
        case GapPolicy::Space:
            return true;
        case GapPolicy::NoSpace:
            return false;
        case GapPolicy::GeneratedSpace:
            return context == GapContext::Rebuilt
                ? std::optional<bool>{true}
                : std::nullopt;
        case GapPolicy::GeneratedNoSpace:
            return context == GapContext::Rebuilt
                ? std::optional<bool>{false}
                : std::nullopt;
    }
    return std::nullopt;
}

struct IndentationPolicy
{
    ContinuationAlignment Alignment;
    // Block: base + ContinuationOffset. Delimiter: column after the opener
    // (chain root for chains), floored at base + ContinuationOffset. If an atom
    // and suffix cannot fit, use base + FallbackOffset. Offsets are 1..32.
    std::uint8_t ContinuationOffset;
    std::uint8_t FallbackOffset;
};

static_assert(sizeof(IndentationPolicy) == 3);

struct ListPolicy
{
    LayoutMode Layout;
    ListPacking Packing;
    // First/Closer govern broken groups only; flat groups stay flat.
    FirstItemPlacement FirstItem;
    CloserPlacement Closer;
    IndentationPolicy Indent;
    // Always requires an available legal owned break even if a child breaks;
    // empty/indivisible/protected or Preserve groups cannot
    // fabricate brokenness.
    BreakRequirement Break = BreakRequirement::IfNeeded;
};

struct ExpressionPolicy
{
    LayoutMode Layout;
    OperatorSide Binary;
    OperatorSide Assignment;
    OperatorSide Shift;
    OperatorSide Ternary;
    // ThreePart couples both ternary breaks when the group breaks; it alone
    // never forces a fitting flat expression to split (Break remains separate).
    TernaryLayout TernaryShape;
    // Indent adds a level for the delimiters when their condition starts on
    // an already-continued line; the condition keeps its ordinary anchors.
    TernaryOperatorIndentation TernaryIndentation;
    IndentationPolicy Indent;
    BreakRequirement Break = BreakRequirement::IfNeeded;
    ListPacking Packing = ListPacking::Fill;
    ExpressionAlignment Alignment = ExpressionAlignment::Block;
    FirstItemPlacement AssignmentRhs = FirstItemPlacement::InlineIfFits;
};

struct ChainPolicy
{
    LayoutMode Layout;
    ListPacking Packing;
    IndentationPolicy Indent;
    BreakRequirement Break = BreakRequirement::IfNeeded;
};

struct BodyPolicy
{
    LayoutMode Layout;
    BodyShape Shape;
    std::uint8_t IndentWidth;
    // Separate from nonempty Shape; excludes enums/lists/requirement bodies.
    BodyShape EmptyShape = BodyShape::Expanded;
};

static_assert(sizeof(BodyPolicy) == 4);

// Offsets are 1..32 after validation/resolution and fit in one byte. The mode
// decides whether to apply the offset, align to the block, or retain source.
struct BodyIndentControl
{
    BodyIndentation Mode;
    std::uint8_t Offset;
};

struct StandaloneBraceIndentControl
{
    StandaloneBlockBraceIndentation Mode;
    std::uint8_t Offset;
};

struct AttributeIndentControl
{
    StatementAttributeIndent Mode;
    std::uint8_t Offset;
};

struct IndentationControls
{
    BodyIndentControl AccessModifiers;
    BodyIndentControl Members;
    BodyIndentControl CaseLabels;
    StandaloneBraceIndentControl StandaloneBraces;
    AttributeIndentControl StatementAttributes;
    BodyIndentControl Namespaces;
};

struct BracePolicy
{
    BracePlacement Class;
    BracePlacement Struct;
    BracePlacement Union;
    BracePlacement Function;
    BracePlacement Lambda;
    BracePlacement Namespace;
    BracePlacement Control;
    BracePlacement Case;
    BracePlacement Enum = BracePlacement::Attached;
    BracePlacement Constructor = BracePlacement::Attached;
    BracePlacement Destructor = BracePlacement::Attached;
};

struct HeaderListPolicy
{
    ListPolicy Items;
    OperatorSide Colon;
    OperatorSide Comma;
};

struct IncludePolicy
{
    IncludeMatchingHeader MatchingHeader;
    IncludeGrouping Grouping;
    IncludeDirectoryOrder DirectoryOrder;
    IncludePathBase PathBase;
};

// An engaged outer option with no Count means explicit Preserve, not omission.
struct TrailingCommentSpaceSetting
{
    std::optional<std::size_t> Count;
    friend bool operator==(
        const TrailingCommentSpaceSetting&,
        const TrailingCommentSpaceSetting&) = default;
};

[[nodiscard]] constexpr GapPolicy ResolveGapPolicy(
    GapPolicy policy,
    GapPolicy) noexcept
{
    return policy;
}

[[nodiscard]] constexpr GapPolicy ResolveGapPolicy(
    std::optional<GapPolicy> policy,
    GapPolicy fallback) noexcept
{
    return policy.value_or(fallback);
}

struct GapPolicies
{
    // Raw-style optional overrides are resolved before syntax lowering.
    #define FORMATTER_GAP_FIELD(type, name, initial) \
        GapPolicy name = ResolveGapPolicy(type(initial), GapPolicy::Preserve);
    FORMATTER_GAP_OPTIONS(FORMATTER_GAP_FIELD)
#undef FORMATTER_GAP_FIELD
};

// An immutable, allocation-free snapshot, independent of parser/engine types.
// Inherit and zero-offset sentinels cannot reach syntax lowering.
struct CompiledPolicies
{
    const LayoutMode Layout;
    const std::uint8_t IndentWidth;
    const std::uint16_t ColumnLimit;
    const WidthDiagnosticPolicy WidthDiagnostics;
    const formatter::IndentationKind IndentationKind;
    const formatter::LineEnding LineEnding;
    const formatter::TrailingWhitespace TrailingWhitespace;
    const formatter::PointerReferenceDeclaratorLocation
        PointerReferenceDeclaratorLocation;
    const IndentationControls Indentation;
    const ListPolicy Calls;
    // Resolved from Calls.FirstItem unless explicitly overridden. Applies to
    // exactly one parsed argument, independently of its expression kind.
    const FirstItemPlacement CallsSoleArgumentFirstItem;
    const ListPolicy Parameters;
    const ListPolicy TemplateParameters;
    const ListPolicy TemplateArguments;
    const ListPolicy Initializers;
    const ListPolicy Captures;
    const ListPolicy ControlHeaders;
    const HeaderListPolicy BaseLists;
    const HeaderListPolicy ConstructorLists;
    const ExpressionPolicy Expressions;
    const ChainPolicy Chains;
    const LayoutMode DeclarationHeaders;
    const BodyPolicy Bodies;
    const BodyPolicy LambdaBodies;
    const BracePolicy Braces;
    // Applied only to grammar-approved trailing-comma sites after selecting
    // shape. Reserve the optional comma's width while fitting; never derive
    // brokenness or closer placement from source comma presence.
    const TrailingCommaPolicy InitializersTrailingComma;
    const GapPolicies Spacing;
    const std::optional<TrailingCommentSpaceSetting> TrailingCommentSpaces;
    // Enum commas are unchanged; InitializersTrailingComma never applies here.
    const ListPolicy Enumerators;
    // Syntax-owned exact counts/cap, not a post-layout cleanup stage.
    const VerticalGapPolicies VerticalGaps;
    const bool BreakAfterTemplateHeader;
    const bool GenerateCallScopeIndentation;
    const VerticalGapPolicy RulerBlankLinesBefore;
    const VerticalGapPolicy RulerBlankLinesAfter;
    const IncludePolicy Includes;
};

static_assert(std::is_trivially_copyable_v<CompiledPolicies>);

// Throws invalid_argument for out-of-range widths/offsets, including those in
// programmatic Styles. Compile once per style; reuse the snapshot across files.
[[nodiscard]] CompiledPolicies CompilePolicies(const Style& style);

void ValidatePolicyStyle(const Style& style);

} // namespace formatter
