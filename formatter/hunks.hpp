#pragma once

#include "formatter.hpp"

#include <filesystem>
#include <istream>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace formatter {

struct Hunk
{
    size_t Begin;
    size_t End;

    bool operator==(const Hunk& other) const = default;
};

struct ChangedHunkFiles
{
    std::vector<std::filesystem::path> Paths;
    std::unordered_map<std::string, std::vector<Hunk>> Hunks;
};

// Paths in file records are resolved against directory, not the input stream.
[[nodiscard]] ChangedHunkFiles ParseChangedHunks(std::istream& input, const std::filesystem::path& directory);

enum class HunkCoverageReason { LimitedLayout, SemanticProtection, StructuralEligibility, };

struct HunkCoverage
{
    parser::SourceRange Original;
    HunkCoverageReason Reason;
};

struct ChangedFormatResult
{
    // Same owned pipeline result. Its raw diagnostics cover the pipeline input;
    // consumers must not treat broad/out-of-scope skips as requested failures.
    FormatResult Result;
    std::vector<Hunk> SelectedLines;
    // Spill envelope before the caller's optional further byte restriction.
    std::vector<Hunk> PermittedLines;
    std::vector<parser::SourceRange> EditScope;
    // Incomplete checking is NOT evidence of a formatting mismatch, nor may a
    // consumer call it verified-clean. Owners/coordinates are original input.
    std::vector<HunkCoverage> Incomplete;
    // Only action-specific proof of a required, scope-blocked edit belongs
    // here. Generic Protected skips (notably namespace/include skips) are
    // insufficient.
    std::vector<StructuralDiagnostic> RequiredSkips;
};

// One-based inclusive ORIGINAL physical lines. Empty source has no lines;
// final LF adds no phantom line, and an unterminated last line is selectable.
// Zero, reversed and out-of-file ranges throw invalid_argument. Unsorted,
// adjacent and overlapping ranges coalesce deterministically. Spill promotes
// only complete safe parsed lists/include groups of at most 200 lines.
// The caller's EditScope, if present, further limits this authority; stronger
// immutable/semantic protections always win. Results own their storage.
[[nodiscard]] ChangedFormatResult FormatChanged(
    const parser::SyntaxTree& tree,
    const PreparedFormat& prepared,
    std::span<const Hunk> hunks,
    bool spillover,
    const FormatRequest& request = {});

// Apply all fixable rules to selected hunks; preserve whole units when the
// allowed spill policy cannot accommodate their complete edit.
[[nodiscard]] std::string FormatChanged(
    const parser::SyntaxTree& tree,
    const Style& style,
    std::span<const Hunk> hunks,
    bool spillover);

} // namespace formatter
