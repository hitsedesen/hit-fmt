#include "include_policy.hpp"

#include <algorithm>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace formatter {

namespace {

bool NormalizedRelativePath(std::string_view path) noexcept
{
    if (path.empty() || path.front() == '/') {
        return false;
    }

    while (!path.empty()) {
        const auto end = path.find('/');
        const auto component = path.substr(0, end);

        if (component.empty() || component == "." || component == "..") {
            return false;
        }

        if (end == std::string_view::npos) {
            break;
        }

        path.remove_prefix(end + 1);
    }

    return true;
}

} // namespace

void ValidateIncludeGroups(std::span<const IncludeGroupRule> groups)
{
    std::vector<std::string_view> prefixes;
    bool standardHeaders = false;

    for (const auto& group : groups) {
        if (group.Depth < 0) {
            throw std::invalid_argument("IncludeGroups Depth must be an integer from 0 to 127");
        }

        if (group.StandardHeaders) {
            if (standardHeaders) {
                throw std::invalid_argument("IncludeGroups has duplicate StandardHeaders rule");
            }

            standardHeaders = true;
        }

        if (group.Prefixes.empty() && !group.StandardHeaders) {
            throw std::invalid_argument("IncludeGroups requires Prefix, nonempty Prefixes or StandardHeaders = true");
        }

        for (const auto& prefix : group.Prefixes) {
            if (prefix.empty()) {
                if (group.Prefixes.size() != 1) {
                    throw std::invalid_argument(
                        "IncludeGroups empty fallback prefix must be " "the group's only prefix");
                }
            } else if (prefix.find('\\') != std::string::npos || !NormalizedRelativePath(prefix)) {
                throw std::invalid_argument("IncludeGroups requires normalized relative header names or directory "
                    "prefixes (no empty, . or .. components or backslashes)");
            }

            prefixes.push_back(prefix);
        }
    }

    std::sort(prefixes.begin(), prefixes.end());

    if (std::adjacent_find(prefixes.begin(), prefixes.end()) != prefixes.end()) {
        throw std::invalid_argument("IncludeGroups has duplicate prefix");
    }
}

bool IsStandardInclude(std::string_view path, bool angled) noexcept
{
    return angled && path.find('/') == std::string_view::npos && path.find(".h") == std::string_view::npos;
}

void AppendIncludePathComponents(std::pmr::vector<std::string_view>& parts, std::string_view path, bool absolute)
{
    while (!path.empty()) {
        const auto slash = path.find('/');
        const auto part = path.substr(0, slash);

        if (part == "..") {
            if (!parts.empty() && parts.back() != "..") {
                parts.pop_back();
            } else if (!absolute) {
                parts.push_back(part);
            }
        } else if (!part.empty() && part != ".") {
            parts.push_back(part);
        }

        if (slash == std::string_view::npos) {
            break;
        }

        path.remove_prefix(slash + 1);
    }
}

IncludeDelimiterResolver::IncludeDelimiterResolver(
    IncludeDelimiters policy,
    IncludePathBase base,
    std::string_view inputFilePath,
    std::pmr::memory_resource& scratch)
    : Policy_(policy)
    , Base_(base)
    , InputAbsolute_(inputFilePath.starts_with('/'))
    , Current_(&scratch)
    , Parts_(&scratch)
{
    if (policy != IncludeDelimiters::Preserve) {
        const auto slash = inputFilePath.rfind('/');

        if (slash != std::string_view::npos) {
            AppendIncludePathComponents(Current_, inputFilePath.substr(0, slash + 1), InputAbsolute_);
        }
    }
}

bool IncludeDelimiterResolver::Angled(std::string_view path, bool original)
{
    // Bare names carry no evidence about the header's location or search root.
    if (Policy_ == IncludeDelimiters::Preserve || path.find('/') == std::string_view::npos ||
        path.find_first_of("\"<>\\\r\n") != std::string_view::npos)
    {
        return original;
    }

    // Explicit source-relative paths must use the source directory's search root.
    if (path.starts_with("./") || path.starts_with("../")) {
        return false;
    }

    const bool relative = !path.starts_with('/') && Base_ == IncludePathBase::SourceDirectory;
    const bool absolute = relative ? InputAbsolute_ : path.starts_with('/');
    Parts_.clear();

    if (relative) {
        Parts_.assign(Current_.begin(), Current_.end());
    }

    AppendIncludePathComponents(Parts_, path.substr(0, path.rfind('/') + 1), absolute);
    return absolute != InputAbsolute_ || Parts_ != Current_;
}

PreparedIncludeGroups::PreparedIncludeGroups(std::span<const IncludeGroupRule> groups)
    : StandardGroup_(groups.size())
{
    ValidateIncludeGroups(groups);

    if (groups.empty()) {
        return;
    }

    std::size_t count = 0;

    for (const auto& group : groups) {
        count += group.Prefixes.size();
    }

    Groups_.reserve(groups.size());
    Prefixes_.reserve(count);

    for (std::size_t group = 0; group < groups.size(); ++group) {
        if (groups[group].StandardHeaders) {
            StandardGroup_ = group;
        }

        Groups_.push_back({
            groups[group].StandardHeaders ? count : Prefixes_.size(),
            groups[group].StandardHeaders ? std::int8_t{0} : groups[group].Depth,
            groups[group].Priority,
        });

        for (const auto& prefix : groups[group].Prefixes) {
            const auto slash = prefix.rfind('/');
            Prefixes_.push_back({
                prefix,
                slash == std::string::npos ? 0 : slash + 1,
                static_cast<std::size_t>(
                    std::count(prefix.begin(), prefix.end(), '/')),
                group,
            });
        }
    }
    std::vector<std::size_t> order(Prefixes_.size());
    std::iota(order.begin(), order.end(), 0);
    std::sort(
        order.begin(),
        order.end(),
        [&] (auto left, auto right) {
            return Prefixes_[left].Text < Prefixes_[right].Text;
        });
    std::size_t bytes = 0;
    std::size_t longest = 0;
    std::string_view previous;
    for (const auto prefixIndex : order) {
        const std::string_view prefix = Prefixes_[prefixIndex].Text;
        std::size_t common = 0;

        while (common < std::min(previous.size(), prefix.size()) && previous[common] == prefix[common]) {
            ++common;
        }
        if (prefix.size() - common >= std::numeric_limits<std::size_t>::max() - bytes) {
            throw std::length_error("IncludeGroups index is too large");
        }
        bytes += prefix.size() - common;
        longest = std::max(longest, prefix.size());
        previous = prefix;
    }
    Nodes_.reserve(bytes + 1);
    Nodes_.push_back({0, 0, Prefixes_.size()});
    struct PendingEdge
    {
        std::size_t Parent;
        Edge Value;
    };

    std::vector<PendingEdge> pending;
    pending.reserve(bytes);
    std::vector<std::size_t> ancestors;
    ancestors.reserve(longest + 1);
    ancestors.push_back(0);
    previous = {};

    // Sorted prefixes share trie storage without per-node heap allocations.
    for (const auto prefixIndex : order) {
        const std::string_view prefix = Prefixes_[prefixIndex].Text;
        std::size_t common = 0;

        while (common < std::min(previous.size(), prefix.size()) && previous[common] == prefix[common]) {
            ++common;
        }

        ancestors.resize(common + 1);

        for (std::size_t index = common; index < prefix.size(); ++index) {
            const auto parent = ancestors.back();
            const auto child = Nodes_.size();
            Nodes_.push_back({0, 0, Prefixes_.size()});
            pending.push_back({parent, {static_cast<unsigned char>(prefix[index]), child}});
            ++Nodes_[parent].EdgeCount;
            ancestors.push_back(child);
        }

        Nodes_[ancestors.back()].Prefix = prefixIndex;
        previous = prefix;
    }

    std::size_t first = 0;

    for (auto& node : Nodes_) {
        node.FirstEdge = first;
        first += node.EdgeCount;
    }

    Edges_.resize(pending.size());
    std::vector<std::size_t> inserted(Nodes_.size(), 0);
    for (const auto& edge : pending) {
        Edges_[Nodes_[edge.Parent].FirstEdge + inserted[edge.Parent]++] = edge.Value;
    }
}

PreparedIncludeGroups::Match PreparedIncludeGroups::Classify(std::string_view path, bool angled) const noexcept
{
    const auto standard = StandardGroup_ < GroupCount() && IsStandardInclude(path, angled)
        ? StandardGroup_
        : GroupCount();
    auto result = [&] (std::size_t prefix) -> Match {
        const auto group = prefix < Prefixes_.size() ? Prefixes_[prefix].Group : GroupCount();
        const auto selected = std::min(standard, group);
        return {
            selected,
            standard < group ? Prefixes_.size() : prefix,
            selected < GroupCount() && selected == StandardGroup_,
        };
    };

    if (Nodes_.empty()) {
        return result(Prefixes_.size());
    }

    auto match = Nodes_.front().Prefix; // An explicit empty-prefix fallback.

    if (match == 0 && !Prefixes_.empty()) {
        // A leading prefix fallback needs neither validation nor trie traversal.
        return result(0);
    }

    if (NormalizedRelativePath(path)) {
        std::size_t node = 0;

        for (const unsigned char byte : path) {
            const auto& current = Nodes_[node];
            const auto begin = Edges_.begin() + current.FirstEdge;
            const auto end = begin + current.EdgeCount;
            const auto edge = std::lower_bound(
                begin,
                end,
                byte,
                [] (const Edge& value, auto key) {
                    return value.Byte < key;
                });

            if (edge == end || edge->Byte != byte) {
                break;
            }

            node = edge->Child;
            const auto candidate = Nodes_[node].Prefix;

            if (candidate < Prefixes_.size() &&
                (Prefixes_[candidate].Text.ends_with('/') || Prefixes_[candidate].Text.size() == path.size()))
            {
                match = std::min(match, candidate);
            }
        }
    }

    return result(match);
}

std::int8_t PreparedIncludeGroups::Depth(std::size_t group) const noexcept
{
    return group < GroupCount() ? Groups_[group].Depth : IncludeGroupRule::MaxDepth;
}

std::int8_t PreparedIncludeGroups::Priority(std::size_t group) const noexcept
{
    return group < Groups_.size() ? Groups_[group].Priority : 0;
}

std::size_t PreparedIncludeGroups::PrefixDepth(std::size_t prefix) const noexcept
{
    return prefix < Prefixes_.size() ? Prefixes_[prefix].Depth : 0;
}

std::string_view PreparedIncludeGroups::Prefix(std::size_t prefix) const noexcept
{
    return prefix < Prefixes_.size() ? std::string_view(Prefixes_[prefix].Text) : std::string_view{};
}

std::string_view PreparedIncludeGroups::CanonicalPrefix(std::size_t group) const noexcept
{
    const auto prefix = group < GroupCount() ? Groups_[group].FirstPrefix : Prefixes_.size();
    return prefix < Prefixes_.size()
        ? std::string_view(Prefixes_[prefix].Text).substr(0, Prefixes_[prefix].DirectoryLength)
        : std::string_view{};
}

} // namespace formatter
