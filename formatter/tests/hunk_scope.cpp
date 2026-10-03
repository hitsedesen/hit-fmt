#include <formatter/hunk_scope.hpp>

#include <gtest/gtest.h>

#include <array>
#include <limits>
#include <memory_resource>
#include <string>

namespace {

using formatter::Hunk;

using formatter::detail::OriginalHunkLines;

using Range = parser::SourceRange;

TEST(HunkScope, OriginalPhysicalLinesRejectInvalidAndHaveNoPhantomEOFLine)
{
    std::pmr::monotonic_buffer_resource memory;
    for (const std::string source : {"", "x", "x\n", "x\r\n", "\n"}) {
        const OriginalHunkLines lines(source, memory);
        EXPECT_TRUE(lines.Coalesce({}, memory).empty());
        for (const Hunk bad : {Hunk{0, 1}, {2, 1}, {2, 2},
            {1, std::numeric_limits<size_t>::max()}})
        {
            EXPECT_THROW((void)lines.Bytes(bad), std::invalid_argument);
            EXPECT_THROW(
                (void)lines.Coalesce(std::span(&bad, 1), memory),
                std::invalid_argument);
        }
        if (source.empty()) {
            EXPECT_THROW((void)lines.Bytes({1, 1}), std::invalid_argument);
        } else {
            const auto bytes = lines.Bytes({1, 1});
            EXPECT_EQ(bytes.Begin, 0);
            EXPECT_EQ(bytes.End, source.size());
            const auto covered = lines.Cover(bytes);
            EXPECT_EQ(covered, (Hunk{1, 1}));
        }
    }
}

TEST(HunkScope, CoalescesUnsortedOverlapAndAdjacencyButPreservesDisjointLines)
{
    const std::string source = "one\ntwo\nthree\nfour\nfive\nsix";
    std::pmr::monotonic_buffer_resource memory;
    const OriginalHunkLines lines(source, memory);
    const Hunk input[] = {{6, 6}, {3, 3}, {1, 2}, {2, 3}, {1, 1}};
    const auto result = lines.Coalesce(input, memory);
    ASSERT_EQ(result.size(), 2);
    EXPECT_EQ(result[0], (Hunk{1, 3}));
    EXPECT_EQ(result[1], (Hunk{6, 6}));
    const auto first = lines.Bytes(result[0]);
    const auto last = lines.Bytes(result[1]);
    EXPECT_EQ(std::string_view(source).substr(
        first.Begin,
        first.End - first.Begin),
        "one\ntwo\nthree\n");
    EXPECT_EQ(
        std::string_view(source).substr(last.Begin, last.End - last.Begin),
        "six");
    const Range scope[] = {first, last};
    EXPECT_FALSE(formatter::detail::HunkScopeIntersects(scope, lines.Bytes({
        4,
        5,
    })));
    EXPECT_TRUE(
        formatter::detail::HunkScopeContains(scope, lines.Bytes({2, 3})));
}

TEST(HunkScope, SelectionOwnsCRLFAndCommentCRButExcludesNextIndentPointAndEOF)
{
    const std::string source = "// note\r\n\twork(); \r\nnext();";
    std::pmr::monotonic_buffer_resource memory;
    const OriginalHunkLines lines(source, memory);
    const Range scope[] = {lines.Bytes({2, 2})};
    EXPECT_EQ(std::string_view(source).substr(
        scope[0].Begin,
        scope[0].End - scope[0].Begin), "\twork(); \r\n");
    EXPECT_FALSE(formatter::detail::HunkScopeContains(
        scope,
        {scope[0].Begin - 1, scope[0].Begin})); // previous LF
    EXPECT_TRUE(formatter::detail::HunkScopeContains(
        scope,
        {scope[0].Begin, scope[0].Begin})); // selected zero-width indent point
    EXPECT_FALSE(formatter::detail::HunkScopeContains(
        scope,
        {scope[0].End, scope[0].End})); // following line's indentation point
    EXPECT_FALSE(formatter::detail::HunkScopeIntersects(
        scope,
        {scope[0].End, scope[0].End}));
    const Range all[] = {lines.Bytes({1, 3})};
    const auto eof = parser::SourceOffset(source.size());
    EXPECT_FALSE(formatter::detail::HunkScopeContains(all, {eof, eof}));
    EXPECT_TRUE(formatter::detail::HunkScopeContains(all, {eof - 1, eof}));
    EXPECT_EQ(lines.Cover(lines.Bytes({1, 1})), (Hunk{1, 1}));
    EXPECT_THROW((void)lines.Cover({eof, eof}), std::invalid_argument);
    EXPECT_THROW((void)lines.Cover({0, eof + 1}), std::invalid_argument);
}

TEST(HunkScope, RangeAndPointQueriesDoNotBridgeSeparatedAuthority)
{
    const Range scope[] = {{0, 2}, {5, 7}};
    EXPECT_TRUE(formatter::detail::HunkScopeContains(scope, {0, 2}));
    EXPECT_FALSE(formatter::detail::HunkScopeContains(scope, {1, 6}));
    EXPECT_TRUE(formatter::detail::HunkScopeIntersects(scope, {1, 6}));
    EXPECT_FALSE(formatter::detail::HunkScopeIntersects(scope, {2, 5}));
    EXPECT_TRUE(formatter::detail::HunkScopeContains(scope, {5, 5}));
    EXPECT_FALSE(formatter::detail::HunkScopeContains(scope, {7, 7}));
    EXPECT_FALSE(formatter::detail::HunkScopeIntersects({}, {0, 1}));
    EXPECT_FALSE(formatter::detail::HunkScopeContains({}, {0, 0}));
}

class CountingResource
    : public std::pmr::memory_resource
{
public:
    size_t Bytes = 0;
    size_t Calls = 0;
private:
    void* do_allocate(size_t bytes, size_t alignment) override
    {
        Bytes += bytes;
        ++Calls;
        return std::pmr::new_delete_resource()->allocate(bytes, alignment);
    }

    void do_deallocate(void* pointer, size_t bytes, size_t alignment) override
    {
        std::pmr::new_delete_resource()->deallocate(pointer, bytes, alignment);
    }

    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept
    override
    {
        return this == &other;
    }
};

TEST(HunkScope, IndexOutlivesSourceAndScaleUsesContiguousOperationMemory)
{
    CountingResource memory;
    constexpr size_t count = 4096;
    const auto lines = [&] {
        std::string source;
        source.reserve(count * 2);
        for (size_t i = 0; i < count; ++i) {
            source += "x\n";
        }
        return OriginalHunkLines(source, memory);
    }();
    EXPECT_LE(memory.Bytes, 4 * (count + 1) * sizeof(parser::SourceOffset));
    EXPECT_LE(memory.Calls, 16);
    const auto calls = memory.Calls;
    for (size_t i = 1; i <= count; ++i) {
        const auto source = lines.Bytes({i, i});
        EXPECT_EQ(source.Begin, 2 * (i - 1));
        EXPECT_EQ(source.End, 2 * i);
        EXPECT_EQ(lines.Cover(source), (Hunk{i, i}));
    }
    EXPECT_EQ(memory.Calls, calls);
    std::pmr::vector<Hunk> reverse(&memory);
    reverse.reserve(count);
    for (size_t i = count; i; --i) {
        reverse.push_back({i, i});
    }
    const auto merged = lines.Coalesce(reverse, memory);
    ASSERT_EQ(merged.size(), 1);
    EXPECT_EQ(merged[0], (Hunk{1, count}));
}
} // namespace
