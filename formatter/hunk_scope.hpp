#pragma once
#include "hunks.hpp"
#include <algorithm>
#include <iterator>
#include <memory_resource>
#include <stdexcept>
namespace formatter::detail {

// One ORIGINAL-input line index. Physical lines contain bytes: empty input has
// none; a final LF adds no phantom line. CR belongs to its original line, never
// an extra boundary. Storage is operation-owned and has no tree/source views.
class OriginalHunkLines
{
public:
    OriginalHunkLines(
        std::string_view source,
        std::pmr::memory_resource& memory)
        : Boundaries_(&memory)
    {
        const auto size = CheckedSourceAnalysisSize(source.size());
        Boundaries_.push_back(0);
        for (parser::SourceOffset i = 0; i < size; ++i) {
            if (source[i] == '\n') {
                Boundaries_.push_back(i + 1);
            }
        }
        if (size && source.back() != '\n') {
            Boundaries_.push_back(size);
        }
    }
    std::pmr::vector<Hunk> Coalesce(
        std::span<const Hunk> hunks,
        std::pmr::memory_resource& memory) const
    {
        for (const auto hunk : hunks) {
            Validate(hunk);
        }
        std::pmr::vector<Hunk> result(hunks.begin(), hunks.end(), &memory);
        std::sort(
            result.begin(),
            result.end(),
            [] (Hunk a, Hunk b) {
                return a.Begin != b.Begin ? a.Begin < b.Begin : a.End < b.End;
            });
        size_t count = 0;
        for (const auto hunk : result) {
            if (count && hunk.Begin <= result[count - 1].End + 1) {
                result[count - 1].End =
                    std::max(result[count - 1].End, hunk.End);
            } else {
                result[count++] = hunk;
            }
        }
        result.resize(count);
        return result;
    }
    parser::SourceRange Bytes(Hunk hunk) const
    {
        Validate(hunk);
        return {Boundaries_[hunk.Begin - 1], Boundaries_[hunk.End]};
    }

    // A nonempty parsed unit's physical line envelope. End is exclusive, so a
    // token/atom ending just after LF still belongs to the preceding line.
    Hunk Cover(parser::SourceRange source) const
    {
        if (source.Begin >= source.End || source.End > Boundaries_.back()) {
            throw std::invalid_argument("invalid original hunk unit range");
        }
        const auto line = [&] (parser::SourceOffset offset) {
            return size_t(
                std::upper_bound(
                    Boundaries_.begin(),
                    Boundaries_.end(),
                    offset) - Boundaries_.begin());
        };
        return {line(source.Begin), line(source.End - 1)};
    }
private:
    void Validate(Hunk hunk) const
    {
        if (!hunk.Begin || hunk.Begin > hunk.End ||
            hunk.End >= Boundaries_.size())
        {
            throw std::invalid_argument("invalid original physical hunk lines");
        }
    }

    std::pmr::vector<parser::SourceOffset> Boundaries_;
};
// Queries require sorted/coalesced nonempty ranges. A zero-width insertion
// owns [Begin,End), never End (including EOF); a map anchor grants no
// authority.
inline bool HunkScopeContains(
    std::span<const parser::SourceRange> scope,
    parser::SourceRange source)
{
    const auto it = std::upper_bound(
        scope.begin(),
        scope.end(),
        source.Begin,
        [] (auto begin, const auto range) { return begin < range.Begin; });
    if (it == scope.begin()) {
        return false;
    }
    const auto range = *std::prev(it);
    return range.Begin <= source.Begin && source.Begin < range.End &&
        source.End <= range.End;
}

inline bool HunkScopeIntersects(
    std::span<const parser::SourceRange> scope,
    parser::SourceRange source)
{
    if (source.Empty()) {
        return HunkScopeContains(scope, source);
    }
    const auto it = std::lower_bound(
        scope.begin(),
        scope.end(),
        source.Begin,
        [] (const auto range, auto begin) { return range.End <= begin; });
    return it != scope.end() && it->Begin < source.End;
}

} // namespace formatter::detail
