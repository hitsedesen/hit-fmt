#pragma once

#include <formatter/config.hpp>
#include <formatter/formatter.hpp>

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
    bool FormattingMismatch = false;

    // Applied formatting is not a remaining failure in fix mode. Naming,
    // parameter names and actual emitted width failures are never erased.
    [[nodiscard]] bool HasFailures(bool fix) const;
};

// One original parse and one public pipeline invocation, mode-independent.
// Original slash-separated inputFilePath is borrowed only during this call.
// Result owns all data; no references into input/path/configuration/parser arenas.
[[nodiscard]] CheckedFile CheckFile(
    util::PaddedSourceView source,
    const PreparedFileConfiguration& configuration,
    std::string_view inputFilePath = {});

// Width locations are explicitly emitted-output coordinates even in check
// mode. Semantic locations use original source in check, final mapped output
// in fix. This printer has no layout/feasibility logic or parser invocation.
void PrintFileIssues(
    std::ostream& output,
    std::string_view path,
    const CheckedFile& checked,
    bool fix);

// Summary preserves all failure categories, not only the first one.
void PrintFileSummaryIssues(
    std::ostream& output,
    const CheckedFile& checked,
    bool fix);

// Reject unsupported options before discovery, formatting or writes.
void ValidatePreviewOption(std::string_view argument);

} // namespace hit_fmt
