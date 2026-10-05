#pragma once

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

struct CheckedFile
{
    formatter::FormatResult Formatting;
    std::vector<SemanticDiagnostic> Semantic;
    // Definite required edits blocked by the original hunk boundary.
    std::vector<SemanticDiagnostic> RequiredChanges;
    // Missing check coverage is not proof of a violation or verified-clean.
    std::vector<formatter::HunkCoverage> Incomplete;
    // Original token anchors, indexed like Formatting.Diagnostics.
    std::vector<util::Position> OriginalWidthPositions;
    std::uint16_t ColumnLimit = 0;
    bool FormattingMismatch = false;

    // Applied formatting is not a remaining failure in fix mode. Naming,
    // parameter names, required blocked edits and in-scope emitted width
    // failures are never erased. Incomplete coverage alone is not a failure.
    [[nodiscard]] bool HasFailures(bool fix) const;
};

// One-based inclusive original physical lines. Spill authorizes only bounded
// safe lists/include groups, not whole functions. An engaged empty selection
// selects nothing; absent scope checks the whole file. Borrowed only during
// CheckFile.
struct ChangedFileScope
{
    std::span<const formatter::Hunk> Hunks;
    bool Spillover = true;
};

// One original parse and one public pipeline invocation, mode-independent.
// Original slash-separated inputFilePath is borrowed only during this call.
// Result owns all data; no references into input/path/configuration/parser
// arenas.
[[nodiscard]] CheckedFile CheckFile(
    util::PaddedSourceView source,
    const PreparedFileConfiguration& configuration,
    std::string_view inputFilePath = {},
    std::optional<ChangedFileScope> changed = std::nullopt);

// Check diagnostics point into the original source. Fix diagnostics point
// into the written output. Incomplete coverage notes always refer to the
// original source. Width failures state that automatic formatting
// cannot satisfy the limit and show the offending line. originalSource is
// borrowed only while printing; fix excerpts use Formatting.Text instead.
// Printing does not parse or run the formatter.
void PrintFileIssues(
    std::ostream& output,
    std::string_view path,
    std::string_view originalSource,
    const CheckedFile& checked,
    bool fix,
    size_t terminalWidth = 80);

// Summary preserves all failure categories, not only the first one.
void PrintFileSummaryIssues(
    std::ostream& output,
    const CheckedFile& checked,
    bool fix);

} // namespace hit_fmt
