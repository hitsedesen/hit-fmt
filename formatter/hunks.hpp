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

    bool operator==(const Hunk&) const = default;
};

struct ChangedHunkFiles
{
    std::vector<std::filesystem::path> Paths;
    std::unordered_map<std::string, std::vector<Hunk>> Hunks;
};

// Paths in file records are resolved against directory, not the input stream.
[[nodiscard]] ChangedHunkFiles ParseChangedHunks(
    std::istream& input,
    const std::filesystem::path& directory);

[[nodiscard]] bool InHunks(
    size_t begin,
    size_t end,
    std::span<const Hunk> hunks);
[[nodiscard]] std::vector<Hunk> HunkWindows(
    const parser::SyntaxTree& tree,
    std::span<const Hunk> hunks,
    bool allowLong = false);
[[nodiscard]] std::string FormatHunks(
    const parser::SyntaxTree& tree,
    std::string_view candidate,
    std::span<const Hunk> windows,
    bool* unsupported = nullptr,
    bool allowLong = false);

// Apply all fixable rules to selected hunks; preserve whole units when the
// allowed spill policy cannot accommodate their complete edit.
[[nodiscard]] std::string FormatChanged(
    const parser::SyntaxTree& tree,
    const Style& style,
    std::span<const Hunk> hunks,
    bool spillover);

} // namespace formatter
