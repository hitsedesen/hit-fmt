#pragma once

#include <parser/syntax_tree.hpp>

#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace formatter {

enum class StructuralAction : uint8_t;

// Physical LF-delimited lines, with an exclusive EOF boundary. A final LF
// adds no phantom line; empty input has no physical lines. No text is retained.
// splitBareCR supports the existing emitted-width coordinate convention.
class SourceLines
{
public:
    SourceLines() = default;
    explicit SourceLines(std::string_view text, bool splitBareCR = false);
    // Reuse sorted [0, ..., EOF] boundaries from the same source buffer.
    SourceLines(std::span<const parser::SourceOffset> boundaries, bool containsBareCR);

    [[nodiscard]] size_t Count() const;
    [[nodiscard]] size_t LineAt(parser::SourceOffset offset) const;
    [[nodiscard]] parser::SourceRange Range(size_t line) const;
    [[nodiscard]] parser::SourceRange Cover(parser::SourceRange range) const;
    [[nodiscard]] std::string_view Text(std::string_view text, size_t line) const;
    [[nodiscard]] util::Position Position(std::string_view text, parser::SourceOffset offset) const;
    [[nodiscard]] std::span<const parser::SourceOffset> Boundaries() const;
    [[nodiscard]] bool ContainsBareCR() const;

private:
    std::vector<parser::SourceOffset> Boundaries_;
    bool BareCR_ = false;
};

enum class FormatChangeKind : uint8_t
{
    Whitespace,
    Content,
    IncludeOrder,
};

struct IncludeOrderChange
{
    // Whole-line ranges in original source order.
    parser::SourceRange Original;
    parser::SourceRange Output;
    util::Position Position;
};

struct FormatChangeSummary
{
    std::vector<IncludeOrderChange> IncludeOrderChanges;
    bool OtherChanges = false;
};

struct FormatChange
{
    parser::SourceRange Original;
    parser::SourceRange Output;
    FormatChangeKind Kind;
    // A contributing structural operation, when generated-byte provenance
    // is available. No borrowed plan/action indices survive the call.
    std::optional<StructuralAction> Action;
};

struct FormatChanges
{
    SourceLines OriginalLines;
    SourceLines OutputLines;
    // Ordered, nonoverlapping replacements. Applying Output slices from the
    // final Text to Original ranges reconstructs that Text exactly. Empty
    // ranges represent insertions/deletions; include permutations stay atomic.
    // These descriptions do not grant hunk or protected-source edit authority.
    std::vector<FormatChange> Edits;
};

} // namespace formatter
