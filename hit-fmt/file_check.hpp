#pragma once

#include <formatter/config.hpp>
#include <formatter/formatter.hpp>

#include <cstdint>
#include <ostream>
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
    // Original token anchors, indexed like Formatting.Diagnostics.
    std::vector<util::Position> OriginalWidthPositions;
    std::uint16_t ColumnLimit = 0;
    bool FormattingMismatch = false;

    // Applied formatting is not a remaining failure in fix mode. Naming,
    // parameter names and actual emitted width failures are never erased.
    [[nodiscard]] bool HasFailures(bool fix) const;
};

// One original parse and one public pipeline invocation, mode-independent.
// Original slash-separated inputFilePath is borrowed only during this call.
// Result owns all data; no references into input/path/configuration/parser
// arenas.
[[nodiscard]] CheckedFile CheckFile(
    util::PaddedSourceView source,
    const PreparedFileConfiguration& configuration,
    std::string_view inputFilePath = {});

// Check diagnostics point into the original source. Fix diagnostics point
// into the written output. Width failures state that automatic formatting
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

// Reject unsupported options before discovery, formatting or writes.
void ValidatePreviewOption(std::string_view argument);

} // namespace hit_fmt
