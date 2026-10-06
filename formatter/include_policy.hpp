#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace formatter {

struct IncludeGroupRule
{
    std::vector<std::string> Prefixes;
    // Zero keeps the rule together; -1 uses the full directory suffix.
    // Positive values cap the suffix's directory components.
    std::int8_t Depth = 0;
    friend bool operator==(
        const IncludeGroupRule&,
        const IncludeGroupRule&) = default;
};

void ValidateIncludeGroups(std::span<const IncludeGroupRule> groups);

// Owns grouping metadata for read-only reuse across files. Rule order controls
// matching, not sorting. The first prefix represents a group's ordering path.
class PreparedIncludeGroups
{
public:
    struct Match
    {
        std::size_t Group;
        std::size_t Prefix;
    };

    explicit PreparedIncludeGroups(std::span<const IncludeGroupRule> groups);

    // Explicit prefixes match normalized relative literal spelling only.
    // An empty-prefix fallback matches any spelling. First rule/prefix wins.
    [[nodiscard]] Match Classify(std::string_view path) const noexcept;

    [[nodiscard]] std::size_t GroupCount() const noexcept
    {
        return Groups_.size();
    }

    [[nodiscard]] std::int8_t Depth(std::size_t group) const noexcept;
    [[nodiscard]] std::size_t PrefixDepth(std::size_t prefix) const noexcept;
    [[nodiscard]] std::string_view Prefix(std::size_t prefix) const noexcept;
    [[nodiscard]] std::string_view CanonicalPrefix(
        std::size_t group) const noexcept;

private:
    struct Group
    {
        std::size_t FirstPrefix;
        std::int8_t Depth;
    };

    struct PrefixEntry
    {
        std::string Text;
        std::size_t Depth;
        std::size_t Group;
    };

    struct Node
    {
        std::size_t FirstEdge = 0;
        std::size_t EdgeCount = 0;
        std::size_t Prefix;
    };

    struct Edge
    {
        unsigned char Byte;
        std::size_t Child;
    };

    std::vector<Group> Groups_;
    std::vector<PrefixEntry> Prefixes_;
    std::vector<Node> Nodes_;
    std::vector<Edge> Edges_;
};

} // namespace formatter
