#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace formatter {

void ValidateIncludeGroupPrefixes(std::span<const std::string> prefixes);

// Configuration-lifetime owning metadata, not a formatter feature yet. Compile
// after final overrides, then reuse read-only. No Style/input/scratch views are
// retained; copies and moves keep index-based edges valid.
class PreparedIncludePrefixes
{
  public:
    explicit PreparedIncludePrefixes(std::span<const std::string> prefixes);

    // Input must already be lexically normalized relative slash syntax. Invalid
    // syntax is unmatched; this lookup neither normalizes nor rewrites paths.
    // Backslashes in input names are ordinary bytes, not slash separators.
    // First configured match wins, not the longest. RuleCount is unmatched.
    [[nodiscard]] std::size_t
    ClassifyNormalizedRelativePath(std::string_view path) const noexcept;
    [[nodiscard]] std::size_t RuleCount() const noexcept
    {
        return Prefixes_.size();
    }

  private:
    struct Node
    {
        std::size_t FirstEdge = 0;
        std::size_t EdgeCount = 0;
        std::size_t Rule;
    };
    struct Edge
    {
        unsigned char Byte;
        std::size_t Child;
    };
    std::vector<std::string> Prefixes_;
    std::vector<Node> Nodes_;
    std::vector<Edge> Edges_;
};

} // namespace formatter
