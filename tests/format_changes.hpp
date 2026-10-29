#pragma once

#include <formatter/formatter.hpp>

#include <gtest/gtest.h>

namespace test {

inline void ExpectFormatChanges(std::string_view source, const formatter::FormatResult& result)
{
    ASSERT_TRUE(result.Changes);
    const auto& changes = *result.Changes;
    std::string reconstructed;
    size_t original = 0, output = 0;
    for (const auto& edit : changes.Edits) {
        ASSERT_LE(original, edit.Original.Begin);
        ASSERT_LE(edit.Original.Begin, edit.Original.End);
        ASSERT_LE(edit.Original.End, source.size());
        ASSERT_LE(output, edit.Output.Begin);
        ASSERT_LE(edit.Output.Begin, edit.Output.End);
        ASSERT_LE(edit.Output.End, result.Text.size());
        const auto unchanged = source.substr(original, edit.Original.Begin - original);
        EXPECT_EQ(unchanged, std::string_view(result.Text).substr(output, edit.Output.Begin - output));
        reconstructed += unchanged;
        reconstructed.append(result.Text, edit.Output.Begin, edit.Output.End - edit.Output.Begin);
        original = edit.Original.End;
        output = edit.Output.End;
    }
    EXPECT_EQ(source.substr(original), std::string_view(result.Text).substr(output));
    reconstructed += source.substr(original);
    EXPECT_EQ(reconstructed, result.Text);
    EXPECT_EQ(changes.Edits.empty(), source == result.Text);

    const auto checkLines = [] (const formatter::SourceLines& lines, std::string_view text) {
        size_t cursor = 0;
        for (size_t line = 1; line <= lines.Count(); ++line) {
            const auto range = lines.Range(line);
            ASSERT_EQ(range.Begin, cursor);
            ASSERT_LT(range.Begin, range.End);
            ASSERT_LE(range.End, text.size());
            const auto bytes = text.substr(range.Begin, range.End - range.Begin);
            EXPECT_EQ(bytes.find('\n'), bytes.ends_with('\n') ? bytes.size() - 1 : bytes.npos);
            EXPECT_EQ(lines.LineAt(range.Begin), line);
            EXPECT_EQ(lines.LineAt(range.End - 1), line);
            cursor = range.End;
        }
        EXPECT_EQ(cursor, text.size());
    };
    checkLines(changes.OriginalLines, source);
    checkLines(changes.OutputLines, result.Text);
}

} // namespace test
