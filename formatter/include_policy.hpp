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
    static constexpr std::int8_t MaxDepth = 127;

    std::vector<std::string> Prefixes;
    // Zero keeps the rule together; MaxDepth uses the full directory suffix.
    // Intermediate values cap the suffix's directory components.
    std::int8_t Depth = MaxDepth;
    // Lower priorities sort first; matching precedence is unchanged.
    std::int8_t Priority = 0;
    bool StandardHeaders = false;
    friend bool operator==(const IncludeGroupRule& lhs, const IncludeGroupRule& rhs) = default;
};

void ValidateIncludeGroups(std::span<const IncludeGroupRule> groups);

// Angled names with neither / nor .h; no catalog or filesystem inference.
bool IsStandardInclude(std::string_view path, bool angled) noexcept;

// Owns grouping metadata for read-only reuse across files. Rule order controls
// matching and optional configured sorting within each priority tier. The
// first prefix represents a group's ordering path for directory sorting.
class PreparedIncludeGroups
{
public:
    struct Match
    {
        std::size_t Group;
        std::size_t Prefix;
        bool StandardHeaders = false;
    };

    explicit PreparedIncludeGroups(std::span<const IncludeGroupRule> groups);

    // Explicit prefixes match normalized relative literal spelling only.
    // An empty-prefix fallback matches any spelling. Standard rules require
    // an angled include. First matching rule/prefix wins.
    [[nodiscard]] Match Classify(std::string_view path, bool angled = false) const noexcept;

    [[nodiscard]] std::size_t GroupCount() const noexcept
    {
        return Groups_.size();
    }

    [[nodiscard]] std::int8_t Depth(std::size_t group) const noexcept;
    [[nodiscard]] std::int8_t Priority(std::size_t group) const noexcept;
    [[nodiscard]] std::size_t PrefixDepth(std::size_t prefix) const noexcept;
    [[nodiscard]] std::string_view Prefix(std::size_t prefix) const noexcept;
    [[nodiscard]] std::string_view CanonicalPrefix(std::size_t group) const noexcept;

private:
    struct Group
    {
        std::size_t FirstPrefix;
        std::int8_t Depth;
        std::int8_t Priority;
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
    std::size_t StandardGroup_;
};

} // namespace formatter
