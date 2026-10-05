#pragma once

#include "config_annotations.hpp"
#include "config_enum.hpp"
#include "config_options.hpp"
#include "policy.hpp"
#include "structural_edits.hpp"

#include <document/document.hpp>
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
    // Columns count Unicode code points, with tabs expanded to tab stops.
    util::Position Position;
    std::string Message;
    LayoutDiagnosticKind Kind;
};

// Immutable, owning configuration snapshot. Preparation throws invalid_argument
// for invalid or unsupported settings, including nonempty CallLikeMacroNames
// and non-Preserve ambiguous template spacing.
class PreparedFormat
{
public:
    explicit PreparedFormat(const Style& style);
    const CompiledPolicies Policies;
    const LibraryAnnotations Annotations;
    const PreparedIncludePrefixes IncludePrefixes;
    const StructuralOptions Structural;
    const NamespaceCommentOptions NamespaceComments;
    const bool HonorClangFormatDirectives;
    const std::vector<std::string> WhitespaceSensitiveMacros;
    const std::vector<std::string> FunctionBodyMacroNames;

    [[nodiscard]] parser::ParseOptions ParsingOptions() const
    {
        return {.FunctionBodyMacroNames = FunctionBodyMacroNames};
    }
};

struct FormatRequest
{
    // Original source coordinates; constrain both planning and layout.
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
    // Empty disables matching-header selection. Directory distances then use
    // an empty source directory; ProjectRoot paths must share the same root
    // as include paths (normally a project-relative input filename).
    std::string_view InputFilePath = {};
};

struct FormatMapping
{
    parser::SourceRange Output;
    // Exact copied range, or a non-exact source association/insertion anchor.
    // Zero-width syntax anchors have affinity to the preceding source piece.
    parser::SourceRange Original;
    document::Identity OriginalToken = document::NoIdentity;
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

static_assert(sizeof(FormatMapping) <= 28);

struct FormatResult
{
    std::string Text;
    std::vector<LayoutDiagnostic> Diagnostics;
    // Structural skip owners always refer to the original tree.
    std::vector<StructuralDiagnostic> StructuralDiagnostics;
    // Maps describe final output, including namespace comment edits.
    // Source associations may overlap; they are not independent edits.
    // Generated commas have no identity; deleted or rewritten original atoms
    // have Present=false.
    std::vector<FormatMapping> Map;
    std::vector<StructuralOperation> StructuralOperations;
    std::vector<TokenOutputOffset> OriginalTokens;
    std::vector<ProtectedSourceRange> OutputProtection;
};

// Normalize, lower and render once; failures throw without returning source.
// Tree overloads require parsing with prepared.ParsingOptions() for macro-body
// hints; source overloads apply them automatically. Inputs must survive this
// call; results own their storage.
[[nodiscard]] FormatResult Format(
    const parser::SyntaxTree& tree,
    const PreparedFormat& prepared,
    const FormatRequest& request = {});

[[nodiscard]] FormatResult Format(
    util::PaddedSourceView source,
    const PreparedFormat& prepared,
    const FormatRequest& request = {});

// Convenience entrypoints compile the style once per call.
[[nodiscard]] std::string Format(
    const parser::SyntaxTree& tree,
    const Style& style);

[[nodiscard]] std::string Format(const parser::SyntaxTree& tree);

[[nodiscard]] std::string Format(
    util::PaddedSourceView source,
    const Style& style);

[[nodiscard]] std::string Format(util::PaddedSourceView source);

} // namespace formatter
