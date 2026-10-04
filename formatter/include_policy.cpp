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

std::vector<std::size_t> SortedRules(std::span<const std::string> prefixes)
{
    std::vector<std::size_t> order(prefixes.size());
    std::iota(order.begin(), order.end(), 0);
    std::sort(
        order.begin(),
        order.end(),
        [&] (auto left, auto right) {
            return prefixes[left] < prefixes[right];
        });
    return order;
}

std::vector<std::size_t> ValidateAndSort(std::span<const std::string> prefixes)
{
    for (const auto& prefix : prefixes) {
        if (prefix.empty() || prefix.back() != '/' ||
            prefix.find('\\') != std::string_view::npos ||
            !NormalizedRelativePath(prefix))
        {
            throw std::invalid_argument(
                "IncludeGroupPrefixes requires nonempty normalized relative "
                    "directory prefixes ending in / (no empty, . or .. "
                    "components or backslashes)");
        }
    }
    auto order = SortedRules(prefixes);
    for (std::size_t index = 1; index < order.size(); ++index) {
        if (prefixes[order[index - 1]] == prefixes[order[index]]) {
            throw std::invalid_argument(
                "IncludeGroupPrefixes has duplicate prefix");
        }
    }
    return order;
}

} // namespace

void ValidateIncludeGroupPrefixes(std::span<const std::string> prefixes)
{
    (void)ValidateAndSort(prefixes);
}

PreparedIncludePrefixes::PreparedIncludePrefixes(
    std::span<const std::string> prefixes)
{
    const auto order = ValidateAndSort(prefixes);
    Prefixes_.assign(prefixes.begin(), prefixes.end());
    std::size_t bytes = 0;
    std::size_t longest = 0;
    std::string_view previous;
    for (const auto rule : order) {
        const std::string_view prefix = Prefixes_[rule];
        std::size_t common = 0;
        while (common < std::min(previous.size(), prefix.size()) &&
            previous[common] == prefix[common])
        {
            ++common;
        }
        if (prefix.size() - common >=
            std::numeric_limits<std::size_t>::max() - bytes)
        {
            throw std::length_error("IncludeGroupPrefixes index is too large");
        }
        bytes += prefix.size() - common;
        longest = std::max(longest, prefix.size());
        previous = prefix;
    }
    Nodes_.reserve(bytes + 1);
    Nodes_.push_back({0, 0, RuleCount()});
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
    // Sorted input permits prefix sharing without hash tables or per-node heap
    // allocations. Explicit ancestor indices bound traversal memory by length.
    for (const auto rule : order) {
        const std::string_view prefix = Prefixes_[rule];
        std::size_t common = 0;
        while (common < std::min(previous.size(), prefix.size()) &&
            previous[common] == prefix[common])
        {
            ++common;
        }
        ancestors.resize(common + 1);
        for (std::size_t index = common; index < prefix.size(); ++index) {
            const auto parent = ancestors.back();
            const auto child = Nodes_.size();
            Nodes_.push_back({0, 0, RuleCount()});
            pending.push_back(
                {parent, {static_cast<unsigned char>(prefix[index]), child}});
            ++Nodes_[parent].EdgeCount;
            ancestors.push_back(child);
        }
        Nodes_[ancestors.back()].Rule = rule;
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
        Edges_[Nodes_[edge.Parent].FirstEdge + inserted[edge.Parent]++] =
            edge.Value;
    }
}

std::size_t PreparedIncludePrefixes::ClassifyNormalizedRelativePath(
    std::string_view path) const noexcept
{
    auto match = RuleCount();
    if (!NormalizedRelativePath(path) || Nodes_.empty()) {
        return match;
    }
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
        match = std::min(match, Nodes_[node].Rule);
    }
    return match;
}

} // namespace formatter
