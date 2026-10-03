#pragma once

#include "config_annotations.hpp"
#include "config_enum.hpp"
#include "config_options.hpp"
#include "layout.hpp"
#include "policy.hpp"
#include "structural_edits.hpp"

#include <parser/parser.hpp>
#include <util/input.hpp>

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace formatter {

struct Style
{
    #define FORMATTER_STYLE_FIELD(type, name, initial) type name = initial;
    FORMATTER_STYLE_OPTIONS(FORMATTER_STYLE_FIELD)
#undef FORMATTER_STYLE_FIELD
};

enum class LayoutDiagnosticKind
{
    LineWidth
};

struct LayoutDiagnostic
{
    // Final emitted coordinates, not an independent source feasibility check.
    util::Position Position;
    std::string Message;
    LayoutDiagnosticKind Kind;
};

// Immutable configuration-lifetime snapshot. Owns all metadata; no raw Style
// views or mutable derived cache. Preparation throws invalid_argument,
// including for currently unsupported nonempty CallLikeMacroNames and ambiguous
// spacing.
class PreparedFormat
{
public:
    explicit PreparedFormat(const Style& style);
    const CompiledPolicies Policies;
    const LibraryAnnotations Annotations;
    const StructuralOptions Structural;
    const NamespaceCommentOptions NamespaceComments;
    const bool HonorClangFormatDirectives;
    const std::vector<std::string> WhitespaceSensitiveMacros;
};

struct FormatRequest
{
    // Original source coordinates. Constrain planning AND layout from the
    // start.
    std::span<const parser::SourceRange> ImmutableRanges = {};
    bool SourceMap = false;
    // Optional ORIGINAL-input byte authority, borrowed only during Format.
    // Absent is unrestricted; engaged empty permits no edits or insertions.
    // Sorted/disjoint nonempty ranges must be in bounds; touching coalesces.
    // Insertion owns [Begin, End), never End/EOF. Normalization projects this
    // authority through accepted atomic operations, not source-map anchors.
    std::optional<std::span<const parser::SourceRange>> EditScope = std::
        nullopt;
    // Original input file, not normalized text or a temporary filename.
    // Slash-separated lexical path; borrowed for this synchronous Format or
    // FormatChanged call only. Never retained by prepared metadata or results.
    // Empty preserves existing behavior when matching-header selection is Off.
    std::string_view InputFilePath = {};
};

struct FormatMapping
{
    parser::SourceRange Output;
    // Exact copied range, or a non-exact source association/insertion anchor.
    // Zero-width syntax anchors have affinity to the preceding source piece.
    parser::SourceRange Original;
    uint64_t OriginalToken = layout::NoIdentity;
    // Index into FormatResult::StructuralOperations for normalization/comment
    // associations, including exact moved include bytes.
    StructuralActionId Action = NoStructuralAction;
    bool HasOriginal = false;
    bool Exact = false;
    // Renderer verbatim flag, not hunk authorization. Actual immutable reason
    // unions are returned separately in FormatResult::OutputProtection.
    bool Protected = false;
    // Generated text may have an original anchor but never an original token
    // ID.
    bool Generated = false;
};

struct FormatResult
{
    std::string Text;
    std::vector<LayoutDiagnostic> Diagnostics;
    // Structural skip owners always refer to the original tree.
    std::vector<StructuralDiagnostic> StructuralDiagnostics;
    // Requested maps refer to FINAL output, after namespace comment edits.
    // Associations are NOT independent edits: generated-prefix/borrowed-indent
    // records may overlap in their source association. Generated commas have no
    // original identity; deleted original tokens have Present=false.
    std::vector<FormatMapping> Map;
    std::vector<StructuralOperation> StructuralOperations;
    std::vector<TokenOutputOffset> OriginalTokens;
    std::vector<ProtectedSourceRange> OutputProtection;
};

// One optional atomic normalization, one syntax document, one render. Failures
// throw; no original-text fallback, retry, repair, or alternate pipeline.
// Tree/source need survive only this call. Results own storage independently of
// source, configuration, tree and the operation's file/scratch arenas.
[[nodiscard]] FormatResult Format(
    const parser::SyntaxTree& tree,
    const PreparedFormat& prepared,
    const FormatRequest& request = {});

[[nodiscard]] FormatResult Format(
    util::PaddedSourceView source,
    const PreparedFormat& prepared,
    const FormatRequest& request = {});

// Convenience entrypoints compile once per call over the SAME pipeline.
[[nodiscard]] std::string Format(
    const parser::SyntaxTree& tree,
    const Style& style);

[[nodiscard]] std::string Format(const parser::SyntaxTree& tree);

[[nodiscard]] std::string Format(
    util::PaddedSourceView source,
    const Style& style);

[[nodiscard]] std::string Format(util::PaddedSourceView source);

} // namespace formatter
