#pragma once

#include "config_enum.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory_resource>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
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

// Lexical normalization, retaining unresolved leading parents for relative paths.
void AppendIncludePathComponents(std::pmr::vector<std::string_view>& parts, std::string_view path, bool absolute);

// Caches classifications for one structural plan in scratch memory.
// The source directory view borrows the input filename.
class IncludeDelimiterResolver
{
public:
    IncludeDelimiterResolver(
        IncludeDelimiters policy,
        IncludePathBase base,
        std::string_view inputFilePath,
        std::pmr::memory_resource& scratch);
    ~IncludeDelimiterResolver();
    IncludeDelimiterResolver(const IncludeDelimiterResolver& other) = delete;
    IncludeDelimiterResolver& operator=(const IncludeDelimiterResolver& other) = delete;

    [[nodiscard]] bool Angled(std::string_view path, bool original);

private:
    struct PathHash
    {
        using is_transparent = void;

        size_t operator()(std::string_view path) const noexcept
        {
            return std::hash<std::string_view>{}(path);
        }
    };

    using Cache = std::pmr::unordered_map<std::pmr::string, bool, PathHash, std::equal_to<>>;

    bool InitializeDirectory();
    bool Resolve(std::string_view path);
#ifndef _WIN32
    bool SameDirectory(std::string_view path, bool sourceRelative);
    int DirectoryFd_ = -1;
    std::uintmax_t Device_ = 0;
    std::uintmax_t Inode_ = 0;
#else
    std::filesystem::path CurrentDirectory_;
#endif
    IncludeDelimiters Policy_;
    IncludePathBase Base_;
    std::string_view InputDirectory_;
    bool Initialized_ = false;
    std::pmr::string Buffer_;
    Cache Headers_;
    Cache SourceDirectories_;
    Cache RootDirectories_;
};

// Owns grouping metadata for read-only reuse across files. Rule order controls
// matching and optional configured sorting within each priority tier. The
// first selector's directory represents a group's ordering path for directory sorting.
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

    // Selectors ending in / match directory prefixes; other nonempty selectors
    // match exact normalized relative header spelling, quoted or angled.
    // An empty fallback matches any spelling. StandardHeaders adds angled names
    // from IsStandardInclude. First matching rule/selector wins.
    [[nodiscard]] Match Classify(std::string_view path, bool angled = false) const noexcept;

    [[nodiscard]] std::size_t GroupCount() const noexcept
    {
        return Groups_.size();
    }

    [[nodiscard]] std::int8_t Depth(std::size_t group) const noexcept;
    [[nodiscard]] std::int8_t Priority(std::size_t group) const noexcept;
    [[nodiscard]] std::size_t PrefixDepth(std::size_t prefix) const noexcept;
    [[nodiscard]] std::string_view Prefix(std::size_t prefix) const noexcept;
    // Ordering directory only; exact header selectors omit their filename.
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
        std::size_t DirectoryLength;
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
