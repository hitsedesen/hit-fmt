#pragma once

#include "diagnostic_detail.hpp"
#include "path_rules.hpp"
#include "terminal.hpp"

#include <util/types.hpp>

#include <formatter/config.hpp>
#include <formatter/diagnostics.hpp>
#include <formatter/formatter.hpp>
#include <formatter/hunks.hpp>

#include <optional>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace cli {

struct PreparedConfiguration
{
    formatter::Configuration Settings;
    formatter::PreparedFormat Formatting{Settings.FormattingStyle};
    std::optional<PathRegex> Exclusions;
    fs::Path RepositoryRoot;
};

Result<PreparedConfiguration> PrepareConfiguration(formatter::Configuration configuration);

struct SemanticDiagnostic
{
    util::Position Original;
    util::Position Emitted;
    std::string Message;
    formatter::TokenAnchor Anchor;
    bool BlockedByHunks = false;
};

struct DebugDiagnostic
{
    util::Position Begin;
    util::Position End;
    std::string Message;
};

struct CheckedFile
{
    formatter::FormatResult Formatting;
    std::vector<SemanticDiagnostic> Semantic;
    // Confirmed required edits that cannot be applied automatically.
    std::vector<SemanticDiagnostic> RequiredChanges;
    // Original byte ranges that were not fully checked.
    std::vector<formatter::HunkCoverage> Incomplete;
    // Informational only; positions always refer to the original input.
    std::vector<DebugDiagnostic> Debug;
    u16 ColumnLimit = 0;
    bool FormattingMismatch = false;
    bool QuickFailure = false;

    // Only confirmed style violations fail; fix mode ignores applied formatting.
    bool HasFailures(bool fix) const;
};

// One-based inclusive lines in the original source.
struct ChangedFileScope
{
    std::span<const formatter::Hunk> Hunks;
    // Allow bounded spillover into enclosing lists and include groups.
    bool Spillover = true;
};

// Absent scope covers the whole file; an empty scope selects nothing.
// Summary and Detailed include formatting replacements for diagnostic rendering.
// Quick checks may return before formatting after a confirmed failure.
// Fix requests always format completely and collect written-file locations.
Result<CheckedFile> FormatAndCheckFile(
    util::PaddedSourceView source,
    const PreparedConfiguration& configuration,
    std::string_view inputFilePath = {},
    std::optional<ChangedFileScope> changed = std::nullopt,
    DiagnosticDetail diagnostics = DiagnosticDetail::Summary,
    bool debug = false,
    std::pmr::memory_resource* memory = nullptr,
    bool fix = false);

void PrintFileDebug(std::ostream& output, std::string_view path, const CheckedFile& checked);

// Check uses original coordinates; fix uses formatted coordinates.
void PrintFileIssues(
    std::ostream& output,
    std::string_view path,
    std::string_view originalSource,
    const CheckedFile& checked,
    bool fix,
    const Terminal& terminal = Terminal{});

void PrintFileSummaryIssues(std::ostream& output, const CheckedFile& checked, bool fix);

Error PrintCheckResult(
    std::ostream& output,
    std::string_view path,
    std::string_view source,
    const CheckedFile& checked,
    DiagnosticDetail diagnostics,
    bool fix,
    const Terminal& terminal = Terminal{},
    bool debug = false);

void PrintQuickHint(std::ostream& output, bool fix);

} // namespace cli
