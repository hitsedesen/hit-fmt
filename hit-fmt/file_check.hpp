#pragma once

#include "terminal.hpp"

#include <formatter/config.hpp>
#include <formatter/diagnostics.hpp>
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

struct PreparedConfiguration
{
    explicit PreparedConfiguration(formatter::Configuration configuration);
    const formatter::Configuration Settings;
    const formatter::PreparedFormat Formatting;
};

struct SemanticDiagnostic
{
    util::Position Original;
    util::Position Emitted;
    std::string Message;
    formatter::TokenAnchor Anchor;
    bool BlockedByHunks = false;
};

struct CheckedFile
{
    formatter::FormatResult Formatting;
    std::vector<SemanticDiagnostic> Semantic;
    // Required edits that cannot be applied automatically.
    std::vector<SemanticDiagnostic> RequiredChanges;
    // Original byte ranges that were not fully checked.
    std::vector<formatter::HunkCoverage> Incomplete;
    std::uint16_t ColumnLimit = 0;
    bool FormattingMismatch = false;

    // Fix mode ignores applied formatting changes.
    [[nodiscard]] bool HasFailures(bool fix) const;
};

// One-based inclusive lines in the original source.
struct ChangedFileScope
{
    std::span<const formatter::Hunk> Hunks;
    // Allow bounded spillover into enclosing lists and include groups.
    bool Spillover = true;
};

enum class DiagnosticDetail
{
    // Keep failure categories; hunk filtering still needs original locations.
    Summary,
    Locations,
    Full,
};

// Absent scope covers the whole file; an empty scope selects nothing.
// Full includes formatting replacements for diagnostic rendering.
[[nodiscard]] CheckedFile FormatAndCheckFile(
    util::PaddedSourceView source,
    const PreparedConfiguration& configuration,
    std::string_view inputFilePath = {},
    std::optional<ChangedFileScope> changed = std::nullopt,
    DiagnosticDetail diagnostics = DiagnosticDetail::Locations);

// Check uses original coordinates; fix uses formatted coordinates.
void PrintFileIssues(
    std::ostream& output,
    std::string_view path,
    std::string_view originalSource,
    const CheckedFile& checked,
    bool fix,
    const Terminal& terminal = Terminal{});

void PrintFileSummaryIssues(std::ostream& output, const CheckedFile& checked, bool fix);

} // namespace hit_fmt
