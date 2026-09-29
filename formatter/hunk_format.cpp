#include "hunks.hpp"

#include <algorithm>
#include <cctype>
#include <limits>
#include <ranges>

namespace formatter {
namespace {
constexpr size_t DiffLookahead = 16;

struct Line
{ std::string_view Text; };

std::vector<Line> SplitLines(std::string_view text)
{
    std::vector<Line> lines;
    for (size_t begin = 0; begin < text.size();) {
        auto end = text.find('\n', begin);
        if (end == std::string_view::npos) {
            end = text.size();
        }
        lines.push_back({text.substr(begin, end - begin)});
        begin = end == text.size() ? end : end + 1;
    }
    return lines;
}

bool IsIncludeLine(const Line& line)
{
    auto begin = line.Text.find_first_not_of(" \t");
    if (begin == std::string_view::npos) {
        return false;
    }
    auto text = line.Text.substr(begin);
    return text.starts_with("#include") && text.size() > 8 &&
        (std::isspace(static_cast<unsigned char>(text[8])) ||
            text[8] == '<' || text[8] == '"');
}

bool Overlaps(size_t begin, size_t end, const Hunk& hunk)
{
    return begin <= hunk.End && hunk.Begin <= end;
}

} // namespace

bool InHunks(size_t begin, size_t end, std::span<const Hunk> hunks)
{
    return std::ranges::any_of(hunks, [&] (const Hunk& hunk) {
        return Overlaps(begin, end, hunk);
    });
}

// An edit to a list can need to reflow its opening or closing line even when
// the changed line is an interior argument. Only promote bounded parsed lists;
// unrelated statements in the same function must stay source-owned.
std::vector<Hunk> HunkWindows(
    const parser::SyntaxTree& tree,
    std::span<const Hunk> hunks,
    bool allowLong)
{
    std::vector<Hunk> windows(hunks.begin(), hunks.end());
    auto lines = SplitLines(tree.Source());
    for (size_t i = 0; i < lines.size();) {
        if (!IsIncludeLine(lines[i])) {
            ++i;
            continue;
        }
        size_t begin = i + 1;
        while (i < lines.size() && IsIncludeLine(lines[i])) {
            ++i;
        }
        if ((allowLong || i + 1 - begin <= 200) && InHunks(begin, i, hunks)) {
            windows.push_back({begin, i});
        }
    }
    std::vector<const parser::Node*> pending{&tree.Root()};
    while (!pending.empty()) {
        auto* node = pending.back();
        pending.pop_back();
        for (auto* child = node->Child(); child; child = child->NextSibling) {
            pending.push_back(child);
        }
        if (node->Tokens.Empty()) {
            continue;
        }
        using NK = parser::NodeKind;
        if (node->Kind != NK::ArgumentList &&
            node->Kind != NK::ParameterList &&
            node->Kind != NK::InitializerList &&
            node->Kind != NK::LambdaCaptureList &&
            node->Kind != NK::ForHeader)
        {
            continue;
        }
        auto first = tree.TokenPosition(node->Tokens.Begin).Line;
        auto last = tree.TokenPosition(node->Tokens.End - 1).Line;
        if (last >= first && (allowLong || last - first < 200) &&
            InHunks(first, last, hunks))
        {
            windows.push_back({first, last});
        }
    }
    return windows;
}

// For a gap retaining its line breaks, each line owns its own whitespace.
// A change to the line breaks themselves requires one window containing the
// whole gap, so it cannot pull an unrelated statement across a boundary.
std::string SelectGap(
    std::string_view original,
    std::string_view formatted,
    size_t firstLine,
    size_t lastLine,
    std::span<const Hunk> windows,
    bool allowLong = false)
{
    auto oldBreaks = std::count(original.begin(), original.end(), '\n');
    auto newBreaks = std::count(formatted.begin(), formatted.end(), '\n');
    if (oldBreaks != newBreaks) {
        if ((!allowLong && lastLine - firstLine > 200) ||
            std::ranges::none_of(windows, [&] (const Hunk& window) {
                return window.Begin <= firstLine && lastLine <= window.End;
            }))
        {
            return std::string(original);
        }
        return std::string(formatted);
    }
    std::string result;
    result.reserve(original.size());
    size_t oldStart = 0;
    size_t newStart = 0;
    for (size_t line = firstLine;; ++line) {
        auto oldEnd = original.find('\n', oldStart);
        auto newEnd = formatted.find('\n', newStart);
        if (oldEnd == std::string_view::npos) {
            oldEnd = original.size();
        }
        if (newEnd == std::string_view::npos) {
            newEnd = formatted.size();
        }
        auto segment = InHunks(line, line, windows)
            ? formatted.substr(newStart, newEnd - newStart)
            : original.substr(oldStart, oldEnd - oldStart);
        result.append(segment);
        if (oldEnd == original.size()) {
            break;
        }
        result.push_back('\n');
        oldStart = oldEnd + 1;
        newStart = newEnd + 1;
    }
    return result;
}

// Gap edits are selected independently. Token changes are selected only if
// their complete aligned run belongs to one window (for example include sort).
std::string FormatHunks(
    const parser::SyntaxTree& tree,
    std::string_view candidate,
    std::span<const Hunk> windows,
    bool* unsupported,
    bool allowLong)
{
    auto source = tree.Source();
    if (candidate == source) {
        return std::string(source);
    }
    // When token order and spelling match, gaps can be selected independently.
    // Also separates adjacent calls whose reflows share a line-diff chunk.
    std::string padded(candidate);
    padded.append(util::SourcePaddingSize, '\0');
    auto formattedTree = parser::SyntaxTree::TokenizeOnly(
        {.Data = padded.data(), .Size = candidate.size()});
    bool sameTokens = tree.TokenCount() == formattedTree.TokenCount();
    for (parser::TokenIndex i = 0; sameTokens && i < tree.TokenCount(); ++i) {
        sameTokens = tree.Kind(i) == formattedTree.Kind(i) &&
            tree.TokenText(i) == formattedTree.TokenText(i);
    }
    if (sameTokens && tree.TokenCount() != 0) {
        std::string result;
        result.reserve(source.size());
        for (parser::TokenIndex i = 0; i < tree.TokenCount(); ++i) {
            auto line = tree.TokenPosition(i).Line;
            auto firstLine = i == 0 ? size_t{1} :
                tree.TokenPosition(i - 1).Line +
                std::count(tree.TokenText(i - 1).begin(),
                    tree.TokenText(i - 1).end(), '\n');
            result += SelectGap(
                tree.WhitespaceBefore(i),
                formattedTree.WhitespaceBefore(i),
                firstLine,
                line,
                windows,
                allowLong);
            result.append(tree.TokenText(i));
        }
        auto last = tree.TokenCount() - 1;
        auto line = tree.TokenPosition(last).Line + std::count(
            tree.TokenText(last).begin(),
            tree.TokenText(last).end(),
            '\n');
        result += SelectGap(
            tree.WhitespaceAfter(last),
            formattedTree.WhitespaceAfter(last),
            line,
            line,
            windows,
            allowLong);
        return result;
    }
    // Align unchanged tokens even if another part of the file inserts,
    // removes, or reorders tokens. This lets a selected whitespace rule work
    // independently of (for example) an unselected include sort.
    auto equal = [&] (parser::TokenIndex a, parser::TokenIndex b) {
        return tree.Kind(a) == formattedTree.Kind(b) &&
            tree.TokenText(a) == formattedTree.TokenText(b);
    };
    auto includesOnly = [&] (parser::TokenIndex firstA,
        parser::TokenIndex lastA, parser::TokenIndex firstB,
        parser::TokenIndex lastB) {
            for (auto i = firstA; i < lastA; ++i) {
                if (!IsIncludeLine({tree.TokenText(i)})) {
                    return false;
                }
            }
            for (auto i = firstB; i < lastB; ++i) {
                if (!IsIncludeLine({formattedTree.TokenText(i)})) {
                    return false;
                }
            }
            return true;
        };
    std::string result;
    result.reserve(source.size());
    size_t sourceCursor = 0;
    size_t formattedCursor = 0;
    parser::TokenIndex a = 0;
    parser::TokenIndex b = 0;
    while (a < tree.TokenCount() && b < formattedTree.TokenCount()) {
        auto nextA = a;
        auto nextB = b;
        if (!equal(a, b)) {
            size_t best = std::numeric_limits<size_t>::max();
            auto endA = std::min<size_t>(tree.TokenCount(), a + DiffLookahead);
            auto endB = std::min<size_t>(
                formattedTree.TokenCount(),
                b + DiffLookahead);
            for (auto x = a; x < endA; ++x) {
                for (auto y = b; y < endB; ++y) {
                    if (equal(x, y) && x - a + y - b < best) {
                        best = x - a + y - b;
                        nextA = x;
                        nextB = y;
                    }
                }
            }
            if (best == std::numeric_limits<size_t>::max()) {
                break;
            }
        }
        bool keptBlock = false;
        if (a != nextA || b != nextB) {
            auto first = a < nextA
                ? tree.TokenPosition(a).Line
                : tree.TokenPosition(nextA).Line;
            auto last = first;
            if (a < nextA) {
                auto tokenText = tree.TokenText(nextA - 1);
                last = tree.TokenPosition(nextA - 1).Line +
                    std::count(tokenText.begin(), tokenText.end(), '\n');
            }
            auto sourceEnd = a < nextA
                ? tree.Token(nextA - 1).Text.End
                : sourceCursor;
            auto formattedEnd = b < nextB
                ? formattedTree.Token(nextB - 1).Text.End
                : formattedCursor;
            bool selected = last >= first &&
                (allowLong || last - first < 200) &&
                std::ranges::any_of(windows, [&] (const Hunk& window) {
                    return window.Begin <= first && last <= window.End;
                });
            if (selected && !includesOnly(a, nextA, b, nextB)) {
                // Inserting braces or other syntax needs an atomic formatter
                // edit, not independently selected token changes.
                if (unsupported) {
                    *unsupported = true;
                }
                return std::string(source);
            }
            result.append(
                selected ? candidate.substr(
                    formattedCursor,
                    formattedEnd - formattedCursor) : source.substr(
                        sourceCursor,
                        sourceEnd - sourceCursor));
            keptBlock = !selected;
            sourceCursor = sourceEnd;
            formattedCursor = formattedEnd;
        }
        auto sourceToken = tree.Token(nextA).Text;
        auto formattedToken = formattedTree.Token(nextB).Text;
        auto previousLine = nextA == 0 ? size_t{1} :
            tree.TokenPosition(nextA - 1).Line +
                std::count(tree.TokenText(nextA - 1).begin(),
                    tree.TokenText(nextA - 1).end(), '\n');
        auto oldGap = source.substr(
            sourceCursor,
            sourceToken.Begin - sourceCursor);
        auto newGap = candidate.substr(
            formattedCursor,
            formattedToken.Begin - formattedCursor);
        result += keptBlock ? std::string(oldGap) : SelectGap(
            oldGap,
            newGap,
            previousLine,
            tree.TokenPosition(nextA).Line,
            windows,
            allowLong);
        result.append(tree.TokenText(nextA));
        sourceCursor = sourceToken.End;
        formattedCursor = formattedToken.End;
        a = nextA + 1;
        b = nextB + 1;
    }
    if (a < tree.TokenCount() || b < formattedTree.TokenCount()) {
        auto first = a < tree.TokenCount()
            ? tree.TokenPosition(a).Line
            : (a ? tree.TokenPosition(a - 1).Line : size_t{1});
        auto last = a < tree.TokenCount()
            ? tree.TokenPosition(tree.TokenCount() - 1).Line
            : first;
        auto sourceEnd = a < tree.TokenCount()
            ? tree.Token(tree.TokenCount() - 1).Text.End
            : sourceCursor;
        auto formattedEnd = b < formattedTree.TokenCount()
            ? formattedTree.Token(formattedTree.TokenCount() - 1).Text.End
            : formattedCursor;
        bool selected = last >= first &&
            (allowLong || last - first < 200) &&
            std::ranges::any_of(windows, [&] (const Hunk& window) {
                return window.Begin <= first && last <= window.End;
            });
        if (selected && !includesOnly(
            a,
            tree.TokenCount(),
            b,
            formattedTree.TokenCount()))
        {
            if (unsupported) {
                *unsupported = true;
            }
            return std::string(source);
        }
        result.append(
            selected ? candidate.substr(
                formattedCursor,
                formattedEnd - formattedCursor) : source.substr(
                    sourceCursor,
                    sourceEnd - sourceCursor));
        sourceCursor = sourceEnd;
        formattedCursor = formattedEnd;
    }
    auto lastLine = tree.TokenCount() == 0 ? size_t{1} :
        tree.TokenPosition(tree.TokenCount() - 1).Line +
        std::count(tree.TokenText(tree.TokenCount() - 1).begin(),
            tree.TokenText(tree.TokenCount() - 1).end(), '\n');
    result += SelectGap(
        source.substr(sourceCursor),
        candidate.substr(formattedCursor),
        lastLine,
        lastLine,
        windows,
        allowLong);
    return result;
}

std::string FormatChanged(
    const parser::SyntaxTree& tree,
    const Style& style,
    std::span<const Hunk> hunks,
    bool spillover)
{
    auto candidate = Format(tree, style);
    auto windows = HunkWindows(tree, hunks);
    auto spill = FormatHunks(tree, candidate, windows);
    auto extended = HunkWindows(tree, hunks, true);
    auto required = windows == extended
        ? spill
        : FormatHunks(tree, candidate, extended, nullptr, true);
    if (spillover && spill == required) {
        return spill;
    }

    auto applied = spillover ? spill : FormatHunks(tree, candidate, hunks);
    if (applied == required) {
        return applied;
    }

    // A fix which needs more than the permitted spill is left untouched;
    // independent hunks in the same file can still be fixed.
    std::vector<Hunk> safe;
    for (const auto& hunk : hunks) {
        std::span<const Hunk> one(&hunk, 1);
        auto expanded = HunkWindows(tree, one, true);
        auto expected = FormatHunks(tree, candidate, expanded, nullptr, true);
        auto permitted = spillover
            ? FormatHunks(tree, candidate, HunkWindows(tree, one))
            : FormatHunks(tree, candidate, one);
        if (permitted == expected) {
            safe.push_back(hunk);
        }
    }
    auto allowed = spillover ? HunkWindows(tree, safe) : safe;
    return FormatHunks(tree, candidate, allowed);
}

} // namespace formatter
