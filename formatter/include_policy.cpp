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
    for (const auto& group : groups) {
        if (group.Depth < 0) {
            throw std::invalid_argument("IncludeGroups Depth must be an integer from 0 to 127");
        }
        if (group.Prefixes.empty()) {
            throw std::invalid_argument("IncludeGroups requires a Prefix or nonempty Prefixes array");
        }
        for (const auto& prefix : group.Prefixes) {
            if (prefix.empty()) {
                if (group.Prefixes.size() != 1) {
                    throw std::invalid_argument(
                        "IncludeGroups empty fallback prefix must be " "the group's only prefix");
                }
            } else if (prefix.back() != '/' || prefix.find('\\') != std::string::npos ||
                !NormalizedRelativePath(prefix))
            {
                throw std::invalid_argument("IncludeGroups requires normalized relative directory "
                    "prefixes ending in / (no empty, . or .. components " "or backslashes)");
            }
            prefixes.push_back(prefix);
        }
    }
    std::sort(prefixes.begin(), prefixes.end());
    if (std::adjacent_find(prefixes.begin(), prefixes.end()) != prefixes.end()) {
        throw std::invalid_argument("IncludeGroups has duplicate prefix");
    }
}

PreparedIncludeGroups::PreparedIncludeGroups(std::span<const IncludeGroupRule> groups)
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
        Groups_.push_back({Prefixes_.size(), groups[group].Depth, groups[group].Priority});
        for (const auto& prefix : groups[group].Prefixes) {
            Prefixes_.push_back({
                prefix,
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

PreparedIncludeGroups::Match PreparedIncludeGroups::Classify(std::string_view path) const noexcept
{
    if (Nodes_.empty()) {
        return {GroupCount(), Prefixes_.size()};
    }
    auto match = Nodes_.front().Prefix; // An explicit empty-prefix fallback.
    if (match == 0) {
        // A first-rule fallback needs neither validation nor trie traversal.
        return {Prefixes_.front().Group, 0};
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
            match = std::min(match, Nodes_[node].Prefix);
        }
    }
    return {match < Prefixes_.size() ? Prefixes_[match].Group : GroupCount(), match};
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
    return group < GroupCount() ? Prefix(Groups_[group].FirstPrefix) : std::string_view{};
}

} // namespace formatter
