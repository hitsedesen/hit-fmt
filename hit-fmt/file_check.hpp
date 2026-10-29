#pragma once

#include "terminal.hpp"

#include <formatter/config.hpp>
#include <formatter/formatter.hpp>
#include <formatter/hunks.hpp>

#include <cstdint>
#include <optional>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hit_fmt {

// Configuration-lifetime owning snapshot, prepared after final settings.
// Each resolver/worker owns its snapshots; callers reuse them read-only.
struct PreparedFileConfiguration
{
    explicit PreparedFileConfiguration(formatter::Configuration configuration);
    const formatter::Configuration Settings;
    const formatter::PreparedFormat Formatting;
};

struct SemanticDiagnostic
{
    util::Position Original;
    util::Position Emitted;
    std::string Message;
};

struct IncludeOrderChange
{
    // Complete original/emitted line blocks, including generated separators.
    parser::SourceRange Original;
    parser::SourceRange Emitted;
    util::Position Position;
};

struct CheckedFile
{
    formatter::FormatResult Formatting;
    std::vector<SemanticDiagnostic> Semantic;
    // Definite required edits blocked by the original hunk boundary.
    std::vector<SemanticDiagnostic> RequiredChanges;
    // Original byte ranges that were not fully checked.
    std::vector<formatter::HunkCoverage> Incomplete;
    // Original token anchors, indexed like Formatting.Diagnostics.
    std::vector<util::Position> OriginalWidthPositions;
    std::uint16_t ColumnLimit = 0;
    bool FormattingMismatch = false;
    bool OtherFormattingMismatch = false;
    // Accepted include-sort operations in original source order. Detailed
    // line diffs omit these blocks; check mode reports one include-order
    // failure for the file, not one failure for every moved directive.
    std::vector<IncludeOrderChange> IncludeOrderChanges;

    // Applied formatting is not a failure in fix mode. Remaining semantic,
    // structural and width violations are failures in both modes.
    [[nodiscard]] bool HasFailures(bool fix) const;
};

// One-based inclusive original physical lines. Spill authorizes only bounded
// safe lists/include groups, not whole functions. An engaged empty selection
// selects nothing; absent scope checks the whole file. Borrowed only during
// FormatAndCheckFile.
struct ChangedFileScope
{
    std::span<const formatter::Hunk> Hunks;
    bool Spillover = true;
};

// One original parse and one public pipeline invocation, mode-independent.
// Original slash-separated inputFilePath is borrowed only during this call.
// Result owns all data; no references into input/path/configuration/parser
// arenas.
// detailedDiagnostics requests change metadata for PrintFormattingDiagnostics.
[[nodiscard]] CheckedFile FormatAndCheckFile(
    util::PaddedSourceView source,
    const PreparedFileConfiguration& configuration,
    std::string_view inputFilePath = {},
    std::optional<ChangedFileScope> changed = std::nullopt,
    bool detailedDiagnostics = false);

// Check diagnostics use original coordinates; fix diagnostics use emitted
// coordinates. originalSource is borrowed only while printing; fix excerpts
// use Formatting.Text.
// Printing does not parse or run the formatter.
void PrintFileIssues(
    std::ostream& output,
    std::string_view path,
    std::string_view originalSource,
    const CheckedFile& checked,
    bool fix,
    const Terminal& terminal = Terminal{});

// Summary preserves all failure categories, not only the first one.
void PrintFileSummaryIssues(std::ostream& output, const CheckedFile& checked, bool fix);

} // namespace hit_fmt
