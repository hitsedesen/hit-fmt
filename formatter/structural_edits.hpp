#pragma once

#include <formatter/config_enum.hpp>
#include <formatter/include_policy.hpp>
#include <formatter/source_analysis.hpp>

#include <memory_resource>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace formatter {

// Independent of renderer fitting and configuration storage. Include sorting
// moves exact directive atoms unless opt-in directive whitespace also applies.
// Text policies never grant authority over other protected interiors.
enum class StructuralAction : uint8_t
{
    AddMissingControlBraces,
    EmptyControlBody,
    RemoveNullStatements,
    RemoveNullDeclarations,
    IncludeOrder,
    NamespaceComments,
    DirectiveWhitespace,
    RulerLength,
};

enum class StructuralSkip : uint8_t
{
    Incomplete,
    Protected,
    CommentAttached,
    Attributes,
    LabelBody,
    DirectiveBoundary,
    InvalidMapping,
    FollowingText,
    ShortNamespace,
    RequiredBody,
};

struct StructuralDiagnostic
{
    StructuralAction Action;
    parser::TokenRange Tokens;
    StructuralSkip Reason;
};

struct StructuralOptions
{
    bool AddMissingControlBraces = false;
    bool EmptyControlBody = false; // true means EmptyBlock, not null removal
    bool RemoveNullStatements = false;
    bool RemoveNullDeclarations = false;
    bool SortIncludes = false;
    // Explicit ending selection retains original whitespace bare-CR membership.
    bool PreserveBareCR = false;
    bool IndentPreprocessingDirectives = false;
    bool AlignMacroBackslashes = false;
    std::size_t RulerLength = 0;
    std::uint8_t IndentWidth = 4;
    IncludeMatchingHeader MatchingHeader = IncludeMatchingHeader::Off;
    IncludeGrouping Grouping = IncludeGrouping::Preserve;
    IncludeDirectoryOrder DirectoryOrder = IncludeDirectoryOrder::NearestFirst;
    IncludePathBase PathBase = IncludePathBase::SourceDirectory;
};

struct IncludeSortContext
{
    std::string_view InputFilePath;
    const PreparedIncludePrefixes* Prefixes = nullptr;
};

using StructuralActionId = uint32_t;

inline constexpr StructuralActionId NoStructuralAction = UINT32_MAX;

struct StructuralOperation
{
    StructuralAction Kind;
    parser::TokenRange Owner;
};

// Source pieces borrow original bytes; literal pieces borrow static strings
// or original namespace name views (the original source must survive).
// Each operation owns all its edits atomically. Equal-offset insertions have
// explicit nesting order; no two edits consume the same original byte.
struct StructuralPiece
{
    parser::SourceRange Source;
    std::string_view Literal;
    bool FromSource = true;
};

struct StructuralEdit
{
    parser::SourceRange Source;
    uint32_t PieceBegin;
    uint32_t PieceEnd;
    StructuralActionId Operation;
    uint32_t Depth = 0;

    enum class Edge : uint8_t
    {
        Close,
        Open,
        Replace
    } Boundary = Edge::Replace;
};
// Planner-created records are exposed for inspection, not permission to
// fabricate semantic edits. Materialization validates ownership, not C++
// semantics or protection anew; do not mutate an accepted plan.
class StructuralPlan
{
public:
    explicit StructuralPlan(std::pmr::memory_resource& memory);
    StructuralPlan(StructuralPlan&&) = default;
    StructuralPlan(const StructuralPlan&) = delete;
    std::pmr::vector<StructuralOperation> Operations;
    std::pmr::vector<StructuralEdit> Edits;
    std::pmr::vector<StructuralPiece> Pieces;
    std::pmr::vector<StructuralDiagnostic> Skipped;
};
struct TokenOutputOffset
{
    parser::SourceRange Output;
    bool Present = false;
};

struct StructuralProvenance
{
    parser::SourceRange Output;
    parser::SourceRange Original;
    StructuralActionId Operation = NoStructuralAction;
    bool Generated = false;
};

struct GeneratedAdjacency
{
    // Includes the two surrounding gaps, or a zero-width deletion boundary.
    // Normalized-token lowering must mark gaps intersecting/touching this
    // range Rebuilt, including adjacencies with generated brace tokens.
    parser::SourceRange Output;
    StructuralActionId Operation;
};

struct StructuralMaterialization
{
    explicit StructuralMaterialization(std::pmr::memory_resource& memory);
    std::pmr::string Text;
    std::pmr::vector<TokenOutputOffset> OriginalTokens;
    std::pmr::vector<StructuralProvenance> Provenance;
    std::pmr::vector<GeneratedAdjacency> GeneratedAdjacencies;
};

struct IncludeGroup
{
    parser::TokenRange Tokens;
    // Complete exact line atoms, including indentation and terminating LF.
    parser::SourceRange Source;
};

// Discover maximal safe include blocks in source order, including sorted
// blocks; omit singletons. Regroup/Directory also span blank lines.
// Uses planner attachment/line-ending rules;
// additional protection excludes the whole group. For hunk eligibility, use
// unrestricted original analysis; scoped planning still authorizes each edit.
// Tree/analysis must match, otherwise throws invalid_argument. Results retain
// no input or scratch views; fileMemory must outlive them.
[[nodiscard]] std::pmr::vector<IncludeGroup> DiscoverIncludeGroups(
    const parser::SyntaxTree& tree,
    const SourceAnalysis& safety,
    std::pmr::memory_resource& fileMemory,
    std::pmr::memory_resource& scratchMemory,
    IncludeGrouping grouping = IncludeGrouping::Preserve);

// One iterative traversal of original parsed syntax, with a single postorder
// completeness annotation for colon-only labels and their enclosing controls.
// Required labeled statements may be siblings of those controls. Unrelated
// complete subregions remain editable. Scratch is not retained.
[[nodiscard]] StructuralPlan PlanStructuralEdits(
    const parser::SyntaxTree& tree,
    const SourceAnalysis& safety,
    StructuralOptions options,
    std::pmr::memory_resource& fileMemory,
    std::pmr::memory_resource& scratchMemory,
    IncludeSortContext includes = {});

// Validates ownership/overlaps before emission, reserves then copies once.
// Throws invalid_argument for malformed plans; length_error for oversized
// output. No parser or renderer is invoked. Original tree/source must survive.
[[nodiscard]] StructuralMaterialization MaterializeStructuralEdits(
    const parser::SyntaxTree& tree,
    const StructuralPlan& plan,
    std::pmr::memory_resource& fileMemory,
    std::pmr::memory_resource& scratchMemory);

enum class NamespaceCommentPolicy : uint8_t
{
    Preserve,
    AddMissing,
    Fix,
    Remove
};

struct NamespaceCommentOptions
{
    NamespaceCommentPolicy Policy = NamespaceCommentPolicy::Preserve;
    // Strictly interior physical output lines: max(0, closeLine-openLine-1).
    // Zero disables exemption. Fix/Remove of existing comments ignores it.
    size_t ShortNamespaceLines = 0;
    size_t Spaces = 1; // 0..32, after the caller resolves trailing gap policy
    bool PreserveBareCR =
        false; // Do not capture EOF whitespace in a NEW comment.
};

struct NamespaceIdentity
{
    parser::TokenRange Owner;
    parser::TokenRange Header;
    parser::TokenIndex Open;
    parser::TokenIndex Close;
    parser::TokenIndex Comment; // TokenCount() if absent
};

struct NamespaceCommentPlan
{
    explicit NamespaceCommentPlan(std::pmr::memory_resource& memory);
    NamespaceCommentOptions Options;
    std::pmr::vector<NamespaceIdentity> Namespaces;
    std::pmr::vector<StructuralDiagnostic> Skipped;
};

[[nodiscard]] NamespaceCommentPlan PlanNamespaceComments(
    const parser::SyntaxTree& original,
    const SourceAnalysis& safety,
    NamespaceCommentOptions options,
    std::pmr::memory_resource& fileMemory,
    std::pmr::memory_resource& scratchMemory);

// Mapping is indexed by ORIGINAL token identity, composed by the caller
// through normalization AND final layout. Mapped namespace/comment atoms must
// retain exact spelling and enclose surviving original interior identities in
// order relative to outside neighbors. Interior envelopes are computed once;
// authorized include permutations and absent deleted tokens are supported.
// OutputProtection contains sorted disjoint immutable final-output ranges,
// including exclusions introduced after normalization.
// This is one semantic materialization after layout, never a reflow or parse.
// Unsafe following text is skipped rather than moved onto a new line.
[[nodiscard]] StructuralPlan ResolveNamespaceComments(
    const parser::SyntaxTree& original,
    const NamespaceCommentPlan& plan,
    std::string_view output,
    std::span<const TokenOutputOffset> mapping,
    std::span<const parser::SourceRange> outputProtection,
    std::pmr::memory_resource& fileMemory,
    std::pmr::memory_resource& scratchMemory);

// Apply the resolved output-coordinate comment plan once, without token map
// reconstruction. Literal/name views borrow static storage/original source.
[[nodiscard]] std::pmr::string MaterializeNamespaceComments(
    std::string_view output,
    const StructuralPlan& resolved,
    std::pmr::memory_resource& fileMemory);

} // namespace formatter
