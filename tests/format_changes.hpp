#pragma once

#include <util/types.hpp>

#include <formatter/formatter.hpp>

#include <gtest/gtest.h>

namespace test {

inline void ExpectFormatChanges(std::string_view source, const formatter::FormatResult& result)
{
    ASSERT_TRUE(result.Changes);
    ASSERT_TRUE(result.Summary);
    const auto& changes = *result.Changes;
    std::string reconstructed;
    us original = 0, output = 0;
    us includes = 0;
    bool other = false;

    for (const auto& edit : changes.Edits) {
        ASSERT_LE(original, edit.Original.Begin);
        ASSERT_LE(edit.Original.Begin, edit.Original.End);
        ASSERT_LE(edit.Original.End, source.size());
        ASSERT_LE(output, edit.Output.Begin);
        ASSERT_LE(edit.Output.Begin, edit.Output.End);
        ASSERT_LE(edit.Output.End, result.Text.size());

        if (edit.Kind == formatter::FormatChangeKind::IncludeOrder) {
            ASSERT_LT(includes, result.Summary->IncludeOrderChanges.size());
            const auto& block = result.Summary->IncludeOrderChanges[includes++];
            EXPECT_EQ(block.Original.Begin, edit.Original.Begin);
            EXPECT_EQ(block.Original.End, edit.Original.End);
            EXPECT_EQ(block.Output.Begin, edit.Output.Begin);
            EXPECT_EQ(block.Output.End, edit.Output.End);
            EXPECT_EQ(block.Position.Line, changes.OriginalLines.LineAt(edit.Original.Begin));
            EXPECT_EQ(block.Position.Column, 1u);
        } else {
            other = true;
        }

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
    EXPECT_EQ(includes, result.Summary->IncludeOrderChanges.size());
    EXPECT_EQ(other, result.Summary->OtherChanges);

    const auto checkLines = [] (const formatter::SourceLines& lines, std::string_view text) {
        us cursor = 0;

        for (us line = 1; line <= lines.Count(); ++line) {
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

inline void ExpectChangeSummary(const formatter::FormatResult& summary, const formatter::FormatResult& detailed)
{
    ASSERT_TRUE(summary.Summary);
    ASSERT_TRUE(detailed.Summary);
    EXPECT_EQ(summary.Text, detailed.Text);
    EXPECT_EQ(summary.Summary->OtherChanges, detailed.Summary->OtherChanges);
    const auto& blocks = summary.Summary->IncludeOrderChanges;
    const auto& expected = detailed.Summary->IncludeOrderChanges;
    ASSERT_EQ(blocks.size(), expected.size());

    for (us i = 0; i < blocks.size(); ++i) {
        EXPECT_EQ(blocks[i].Original.Begin, expected[i].Original.Begin);
        EXPECT_EQ(blocks[i].Original.End, expected[i].Original.End);
        EXPECT_EQ(blocks[i].Output.Begin, expected[i].Output.Begin);
        EXPECT_EQ(blocks[i].Output.End, expected[i].Output.End);
        EXPECT_EQ(blocks[i].Position.Line, expected[i].Position.Line);
        EXPECT_EQ(blocks[i].Position.Column, expected[i].Position.Column);
    }
}

} // namespace test
