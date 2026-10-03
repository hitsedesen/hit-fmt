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

// nullopt means preserve/fallback, not unconditional source spelling: lowering
// uses syntax-safe flat fallback BEFORE fitting canonical
// topology-changing gaps,
// even if finally flat. Preserved/genuinely flat-only gaps retain source bytes.
// Topology ownership is structural, not final brokenness or token provenance.
// Layout owns newlines; hard boundaries and opaque bytes always win.
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
            return context == GapContext::Rebuilt ? std::optional<bool>{true}
                                                  : std::nullopt;
        case GapPolicy::GeneratedNoSpace:
            return context == GapContext::Rebuilt ? std::optional<bool>{false}
                                                  : std::nullopt;
    }
    return std::nullopt;
}

struct IndentationPolicy
{
    ContinuationAlignment Alignment;
    // Block alignment: block column + ContinuationOffset. Delimiter alignment:
    // column after the opener (chain root for chains), with the same offset as
    // a minimum relative to the block column. If this leaves no room for the
    // next indivisible atom and required suffix, use block + FallbackOffset.
    // This is the lowering contract; anchor>=Width alone is insufficient.
    std::size_t ContinuationOffset;
    std::size_t FallbackOffset;
    // Omission inherits the nearest syntax context, then the global default.
    std::optional<ContinuationNesting> Nesting = std::nullopt;
};

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
    IndentationPolicy Indent;
    BreakRequirement Break = BreakRequirement::IfNeeded;
    ListPacking Packing = ListPacking::Fill;
    ExpressionAlignment Alignment = ExpressionAlignment::Block;
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
    std::size_t IndentWidth;
    // Separate from nonempty Shape; excludes enums/lists/requirement bodies.
    BodyShape EmptyShape = BodyShape::Expanded;
};

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
};

// An engaged outer option with no Count means explicit Preserve, not omission.
struct TrailingCommentSpaceSetting
{
    std::optional<std::size_t> Count;
    friend bool operator==(
        const TrailingCommentSpaceSetting&,
        const TrailingCommentSpaceSetting&) = default;
};

struct GapPolicies
{
    #define FORMATTER_GAP_FIELD(type, name, initial) type name = initial;
    FORMATTER_GAP_OPTIONS(FORMATTER_GAP_FIELD)
#undef FORMATTER_GAP_FIELD
};

// An immutable, allocation-free snapshot, independent of parser/engine types.
// Inherit and zero-offset sentinels cannot reach syntax lowering.
struct CompiledPolicies
{
    const LayoutMode Layout;
    const std::size_t IndentWidth;
    const std::size_t ColumnLimit;
    const WidthDiagnosticPolicy WidthDiagnostics;
    const formatter::IndentationKind IndentationKind;
    const formatter::LineEnding LineEnding;
    const formatter::TrailingWhitespace TrailingWhitespace;
    const formatter::PointerReferenceDeclaratorLocation
        PointerReferenceDeclaratorLocation;
    const IndentationControls Indentation;
    const formatter::ContinuationNesting ContinuationNesting;
    const std::optional<formatter::ContinuationNesting>
        ControlBodiesContinuationNesting;
    const ListPolicy Calls;
    const ListPolicy Parameters;
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
    // Scalar metadata does not enable unsupported public-pipeline behavior.
    const bool BreakAfterTemplateHeader;
    const IncludePolicy Includes;
};

static_assert(std::is_trivially_copyable_v<CompiledPolicies>);

// Throws invalid_argument for out-of-range widths/offsets, including those in
// programmatic Styles. Compile once per style; reuse the snapshot across files.
[[nodiscard]] CompiledPolicies CompilePolicies(const Style& style);

void ValidatePolicyStyle(const Style& style);

} // namespace formatter
