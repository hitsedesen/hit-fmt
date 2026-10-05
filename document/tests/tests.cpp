#include <document/document.hpp>
#include <tests/golden.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <memory_resource>
#include <string>
#include <vector>

#ifndef DOCUMENT_TESTDATA_DIR
#define DOCUMENT_TESTDATA_DIR "document/tests/testdata/group-layout"
#endif

namespace {

using namespace document;

void ExpectText(
    const Document& document,
    std::uint32_t width,
    std::string_view expected)
{
    auto output = document.Render({.Width = width});
    ASSERT_TRUE(output.State);
    EXPECT_EQ(output.Text, expected);
    auto repeated = document.Render({.Width = width});
    EXPECT_EQ(repeated.Text, output.Text);
    EXPECT_EQ(repeated.Broken, output.Broken);
}

void AppendRender(
    std::string& actual,
    const Document& document,
    std::uint32_t width,
    std::string_view label)
{
    const auto output = document.Render({.Width = width});
    ASSERT_TRUE(output.State);
    const auto repeated = document.Render({.Width = width});
    EXPECT_EQ(repeated.Text, output.Text);
    EXPECT_EQ(repeated.Broken, output.Broken);
    actual += std::string(label) + " width=" + std::to_string(width) + ":\n";
    // Keep CR visible, as in PhysicalLineBasesGolden.
    for (const char c : output.Text) {
        if (c == '\r') {
            actual += "\\r";
        } else if (c == '\\') {
            actual += "\\\\";
        } else {
            actual += c;
        }
    }
    // Mark the end so trailing indentation is visible in the fixture.
    actual += "$\n";
}

TEST(GroupLayout, ContinuationStarterLineGolden)
{
    std::string actual;
    for (const bool newLine : {false, true}) {
        Document document("");
        document.Text("value = ");
        const auto outer = document.BeginGroup({
            .Mode = GroupMode::Fill,
            .Indent = 4,
            .ExpressionContinuation = true,
            .Continuation = true,
        });
        document.Text("first ||");
        document.Gap({newLine ? GapKind::Hard : GapKind::Flat, 1});
        const auto anchor =
            document.MarkAnchor({.Offset = 4, .Continuation = true});
        const auto inner = document.BeginGroup({
            .Mode = GroupMode::Fill,
            .Indent = 4,
            .ExpressionContinuation = true,
            .Continuation = true,
        });
        document.Text("lhs !=");
        document.Gap({GapKind::Hard, 0}, anchor);
        document.Text("rhs;");
        document.EndGroup(inner);
        document.EndGroup(outer);
        ASSERT_TRUE(document.Seal());
        const auto output = document.Render();
        ASSERT_TRUE(output.State);
        actual += newLine ? "new line\n" : "same line\n";
        actual.append(output.Text);
        actual += '\n';
    }
    {
        Document document("");
        const auto block = document.BeginGroup({GroupMode::Fill, 4});
        document.Gap({GapKind::Hard, 0});
        document.Text("value =");
        const auto assignment = document.BeginGroup({
            .Mode = GroupMode::Fill,
            .Indent = 4,
            .ExpressionContinuation = true,
            .Continuation = true,
        });
        const auto anchor =
            document.MarkAnchor({.Offset = 8, .Continuation = true});
        const auto binary = document.BeginGroup({
            .Mode = GroupMode::Fill,
            .Indent = 8,
            .ExpressionContinuation = true,
            .Continuation = true,
        });
        document.Gap({GapKind::Hard, 0});
        document.Text("first ||");
        document.Gap({GapKind::Hard, 0}, anchor);
        document.Text("second {");
        const auto body =
            document.BeginGroup({.Indent = 4, .StructuralBase = true});
        document.Gap({GapKind::Hard, 0});
        document.Text("nested =");
        const auto expression = document.BeginGroup({
            .Indent = 4,
            .ExpressionContinuation = true,
            .Continuation = true,
        });
        document.Gap({GapKind::Hard, 0});
        document.Text("leaf;");
        document.EndGroup(expression);
        document.EndGroup(body);
        document.EndGroup(binary);
        document.EndGroup(assignment);
        document.EndGroup(block);
        ASSERT_TRUE(document.Seal());
        AppendRender(actual, document, 80, "shared continuation with body");
    }
    for (const bool expression : {false, true}) {
        for (const bool realized : {false, true}) {
            Document document("");
            const auto body = document.BeginGroup({.Indent = 4});
            document.Gap({GapKind::Hard, 0});
            document.Text(expression ? "value =" : "if (");
            const auto parentAnchor =
                document.MarkAnchor({.Offset = 4, .Continuation = true});
            const auto parent = document.BeginGroup({
                .Indent = 4,
                .ExpressionContinuation = expression,
                .StructuralBase = !expression,
                .Continuation = true,
            });
            document.Gap({
                realized ? GapKind::Hard : GapKind::Flat,
                std::uint16_t(expression ? 1 : 0),
            });
            document.Text("wrap(");
            const auto callAnchor = document.MarkAnchor({
                .Offset = 4,
                .StructuralBase = true,
                .Continuation = true,
                .SharedWith = parentAnchor,
            });
            const auto call = document.BeginGroup(
                {.Indent = 4, .StructuralBase = true, .Continuation = true});
            document.Gap({GapKind::Hard, 0}, callAnchor);
            document.Text("nested(");
            const auto nestedAnchor = document.MarkAnchor({
                .Offset = 4,
                .StructuralBase = true,
                .Continuation = true,
                .SharedWith = callAnchor,
            });
            const auto nested = document.BeginGroup(
                {.Indent = 4, .StructuralBase = true, .Continuation = true});
            document.Gap({GapKind::Hard, 0}, nestedAnchor);
            document.Text("left ||");
            const auto binaryAnchor = document.MarkAnchor({
                .Offset = 4,
                .Continuation = true,
                .SharedWith = nestedAnchor,
            });
            const auto binary = document.BeginGroup({
                .Indent = 4,
                .ExpressionContinuation = true,
                .Continuation = true,
            });
            document.Gap({GapKind::Hard, 0}, binaryAnchor);
            document.Text("right)))");
            document.EndGroup(binary);
            document.EndGroup(nested);
            document.EndGroup(call);
            document.EndGroup(parent);
            document.EndGroup(body);
            ASSERT_TRUE(document.Seal());
            auto label = std::string(expression ? "expression" : "structural");
            label += realized
                ? " realized shared lists"
                : " prospective shared lists";
            AppendRender(actual, document, 80, label);
        }
    }
    for (const bool inlineLambda : {false, true}) {
        Document document("");
        const auto body = document.BeginGroup({.Indent = 4});
        document.Gap({GapKind::Hard, 0});
        document.Text("wrap(");
        const auto outerAnchor = document.MarkAnchor({
            .Kind = AnchorKind::DelimiterColumn,
            .Fallback = 4,
            .Minimum = 4,
            .StructuralBase = true,
            .Continuation = true,
        });
        const auto outer = document.BeginGroup(
            {.Indent = 4, .StructuralBase = true, .Continuation = true});
        document.Gap({GapKind::Hard, 0}, outerAnchor);
        document.Text("nested(");
        const auto innerAnchor = document.MarkAnchor({
            .Offset = 4,
            .StructuralBase = true,
            .Continuation = true,
            .SharedWith = outerAnchor,
        });
        const auto inner = document.BeginGroup(
            {.Indent = 4, .StructuralBase = true, .Continuation = true});
        if (inlineLambda) {
            document.Text("[&] {");
            const auto closer = document.MarkAnchor({.StructuralBase = true});
            const auto lambda =
                document.BeginGroup({.Indent = 4, .StructuralBase = true});
            document.Gap({GapKind::Hard, 0});
            document.Text("leaf;");
            document.EndGroup(lambda);
            document.Gap({GapKind::Hard, 0}, closer);
            document.Text("}))");
        } else {
            document.Gap({GapKind::Hard, 0}, innerAnchor);
            document.Text("item))");
        }
        document.EndGroup(inner);
        document.EndGroup(outer);
        document.EndGroup(body);
        ASSERT_TRUE(document.Seal());
        for (const auto width : {80u, 9u}) {
            AppendRender(
                actual,
                document,
                width,
                inlineLambda
                    ? "realized delimiter with lambda"
                    : "realized delimiter with item");
        }
    }
    for (const bool rhsLine : {false, true}) {
        for (const bool delimiter : {false, true}) {
            Document document("");
            const auto block = document.BeginGroup({GroupMode::Fill, 4});
            document.Gap({GapKind::Hard, 0});
            document.Text("x =");
            const auto expression =
                document.BeginGroup({GroupMode::Fill, 4, false, true});
            document.Gap({rhsLine ? GapKind::Hard : GapKind::Flat, 1});
            document.Text("f(");
            const auto anchor = document.MarkAnchor({
                delimiter
                    ? AnchorKind::DelimiterColumn
                    : AnchorKind::BlockRelative,
                delimiter ? 0 : 4,
                4,
                4,
                true,
            });
            const auto call =
                document.BeginGroup({GroupMode::Fill, 4, true, false, true});
            document.Gap({GapKind::Soft, 0, 5}, anchor);
            document.Text("item)");
            document.EndGroup(call);
            document.EndGroup(expression);
            document.EndGroup(block);
            ASSERT_TRUE(document.Seal());
            auto label = std::string(rhsLine ? "realized" : "prospective");
            label += delimiter ? " delimiter base" : " block base";
            // At width 13 there is no room for the complete atom: delimiter
            // fallback uses the structural base, not the delimiter's column.
            for (const auto width : {80u, 13u}) {
                AppendRender(actual, document, width, label);
            }
        }
    }
    for (const bool consumer : {false, true}) {
        Document document("");
        auto block = document.BeginGroup({GroupMode::Fill, 4});
        auto outer = document.BeginGroup({GroupMode::Fill, 4, false, true});
        auto child = document.BeginGroup(consumer
            ? GroupSpec{GroupMode::Fill, 4, false, false, true}
            : GroupSpec{GroupMode::Fill, 4, false, true});
        document.Gap({GapKind::Hard, 0});
        document.Text("first");
        document.EndGroup(child);
        auto base =
            document.MarkAnchor({AnchorKind::BlockRelative, 0, 0, 0, true});
        document.Gap({GapKind::Hard, 0}, base);
        document.Text("sibling");
        document.EndGroup(outer);
        document.EndGroup(block);
        ASSERT_TRUE(document.Seal());
        AppendRender(
            actual,
            document,
            80,
            consumer ? "fenced consumer line" : "propagated expression line");
    }
    test::golden::ExpectGolden(
        std::filesystem::path(DOCUMENT_TESTDATA_DIR) /
        "continuation-starter-line.expected",
        actual);
}

TEST(GroupLayout, PhysicalLineBasesGolden)
{
    std::string actual;
    for (const auto prefix :
        {std::string("\n            "), std::string("\r\n\t\t")})
    {
        for (const bool broken : {false, true}) {
            Document document(prefix);
            const auto outer = document.BeginGroup({.Indent = 4});
            document.Text("wrap(");
            document.SourceText(0, prefix.size(), NoIdentity, true);
            const auto header =
                document.BeginGroup({.StructuralBase = true, .LineBase = true});
            document.Text("[");
            const auto capture = document.BeginGroup({
                .Indent = 4,
                .ForceBreak = broken,
                .StructuralBase = true,
                .Continuation = true,
            });
            document.Gap({GapKind::Soft, 0});
            document.Text("first,");
            document.Gap({GapKind::Soft, 1});
            document.Text("second] {");
            document.EndGroup(capture);
            document.EndGroup(header);
            const auto closer =
                document.MarkAnchor({.StructuralBase = true, .LineBase = true});
            const auto body = document.BeginGroup(
                {.Indent = 4, .StructuralBase = true, .LineBase = true});
            document.Gap({GapKind::Hard, 0});
            document.Text("leaf;");
            document.EndGroup(body);
            document.Gap({GapKind::Hard, 0}, closer);
            document.Text("});");
            document.EndGroup(outer);
            ASSERT_TRUE(document.Seal());
            for (const auto indent :
                {IndentationKind::Spaces, IndentationKind::Tabs})
            {
                const Policy policy{
                    .Width = 80,
                    .LineEnding = prefix[0] == '\r'
                        ? Newline::CRLF
                        : Newline::LF,
                    .Indentation = indent,
                };
                const auto output = document.Render(policy);
                ASSERT_TRUE(output.State);
                EXPECT_EQ(document.Render(policy).Text, output.Text);
                actual += broken ? "broken captures\n" : "flat captures\n";
                actual +=
                    indent == IndentationKind::Spaces ? "spaces\n" : "tabs\n";
                for (const char c : output.Text) {
                    if (c == '\r') {
                        actual += "\\r";
                    } else {
                        actual += c;
                    }
                }
                actual += "\n---\n";
            }
        }
    }
    for (const bool broken : {false, true}) {
        const std::string source = "\n        opaque;";
        Document document(source);
        const auto outer = document.BeginGroup({.Indent = 4});
        document.Text("prefix");
        document.Gap({GapKind::Hard, 0});
        const auto base =
            document.MarkAnchor({.StructuralBase = true, .FixedBase = 4});
        const auto header = document.BeginGroup(
            {.Mode = GroupMode::Fill, .StructuralBase = true, .FixedBase = 4});
        document.Text("lambda[");
        const auto capture = document.BeginGroup({
            .Indent = 4,
            .ForceBreak = broken,
            .StructuralBase = true,
            .Continuation = true,
        });
        document.Gap({GapKind::Soft, 0});
        document.Text("state]");
        document.EndGroup(capture);
        document.ConditionalGap(
            header,
            {GapKind::Flat, 1},
            {GapKind::Hard, 0},
            base);
        document.Text("{");
        document.EndGroup(header);
        const auto body = document
            .BeginGroup({.Indent = 4, .StructuralBase = true, .FixedBase = 4});
        document.SourceText(0, source.size(), NoIdentity, true);
        document.Gap({GapKind::Hard, 0});
        document.Text("safe;");
        document.EndGroup(body);
        document.Gap({GapKind::Hard, 0}, base);
        document.Text("}");
        document.EndGroup(outer);
        ASSERT_TRUE(document.Seal());
        AppendRender(
            actual,
            document,
            80,
            broken
                ? "fixed body with broken captures"
                : "fixed body with flat captures");
    }
    test::golden::ExpectGolden(
        std::filesystem::path(DOCUMENT_TESTDATA_DIR) /
        "physical-line-base.expected",
        actual);
}

TEST(GroupLayout, SharedContinuationRejectsAnUnrecoverableBase)
{
    Document document("");
    const auto body =
        document.BeginGroup({.Indent = 4, .StructuralBase = true});
    const auto outer = document.BeginGroup({.Indent = 4, .Continuation = true});
    const auto shifted = document.BeginGroup({.Indent = -8});
    const auto inner = document.BeginGroup({.Indent = 4, .Continuation = true});
    document.EndGroup(inner);
    document.EndGroup(shifted);
    document.EndGroup(outer);
    document.EndGroup(body);
    ASSERT_TRUE(document.Seal());
    const auto result = document.Render({.Width = 80});
    EXPECT_EQ(result.State.Code, Error::IndentationOverflow);
    EXPECT_TRUE(result.Text.empty());
    EXPECT_TRUE(result.Map.empty());
}

TEST(GroupLayout, FixedExpressionGapRealizesWithoutBecomingEditableBreak)
{
    const std::string source = "\n    ";
    for (const bool split : {false, true}) {
        Document document(source);
        auto outer = document.BeginGroup({GroupMode::Fill, 4, true, true});
        if (split) {
            document.SourceLineIndent(0, 1, 1, 5);
        } else {
            document.SourceText(0, 5, NoIdentity, true);
        }
        document.ExpressionLine();
        auto base =
            document.MarkAnchor({AnchorKind::BlockRelative, 0, 0, 0, true});
        auto child =
            document.BeginGroup({GroupMode::Fill, 0, false, false, true});
        document.Text("f(");
        document.Gap({GapKind::Flat, 0});
        document.Text("item)");
        document.EndGroup(child);
        document.Gap({GapKind::Hard, 0}, base);
        document.Text("tail");
        document.EndGroup(outer);
        ASSERT_TRUE(document.Seal());
        ExpectText(document, 80, "\n    f(item)\n    tail");
        auto result = document.Render({.Width = 80});
        // Fixed prefix/marker cannot satisfy ForceBreak at the child's flat gap.
        EXPECT_EQ(result.Broken[child], 0);
        EXPECT_EQ(result.Map.size(), split ? 7 : 6);
    }
}

TEST(GroupLayout, GroupMetadataRejectsInvalidOwnersPrefixesAndOverflow)
{
    for (const std::string source : {"\n// comment", "\n token", "    "}) {
        Document document(source);
        auto group = document.BeginGroup({GroupMode::Fill, 4, false, true});
        document.SourceText(0, source.size(), NoIdentity, true);
        document.ExpressionLine();
        document.EndGroup(group);
        EXPECT_EQ(document.Seal().Code, Error::InvalidLinePrefix);
    }
    Document outside("\n");
    outside.SourceText(0, 1, NoIdentity, true);
    outside.ExpressionLine();
    EXPECT_EQ(outside.Seal().Code, Error::InvalidGroup);
    Document negative("");
    negative.BeginGroup({GroupMode::Fill, -1, false, true});
    EXPECT_EQ(negative.Seal().Code, Error::InvalidPolicy);
    for (const auto penalty : {4u, 255u}) {
        Document invalid("");
        const auto group = invalid
            .BeginGroup({.BreakPenalty = static_cast<std::uint8_t>(penalty)});
        invalid.EndGroup(group);
        EXPECT_EQ(invalid.Seal().Code, Error::InvalidPolicy);
        Document invalidShape("");
        invalidShape.BeginGroup(
            {.FlatInBrokenParentPenalty = static_cast<std::uint8_t>(penalty)});
        EXPECT_EQ(invalidShape.Seal().Code, Error::InvalidPolicy);
    }
    for (const bool structural : {false, true}) {
        Document invalidGroup("");
        invalidGroup.BeginGroup({
            .StructuralBase = structural,
            .Continuation = structural,
            .LineBase = true,
        });
        EXPECT_EQ(invalidGroup.Seal().Code, Error::InvalidPolicy);
        Document invalidAnchor("");
        invalidAnchor.MarkAnchor({
            .StructuralBase = structural,
            .Continuation = structural,
            .LineBase = true,
        });
        EXPECT_EQ(invalidAnchor.Seal().Code, Error::InvalidPolicy);
    }
    Document overflow("");
    auto first = overflow.BeginGroup({GroupMode::Fill, INT32_MAX, false, true});
    auto second =
        overflow.BeginGroup({GroupMode::Fill, INT32_MAX, false, true});
    auto third = overflow.BeginGroup({GroupMode::Fill, 1, false, true});
    overflow.Text("x");
    overflow.EndGroup(third);
    overflow.EndGroup(second);
    overflow.EndGroup(first);
    ASSERT_TRUE(overflow.Seal());
    EXPECT_EQ(overflow.Render().State.Code, Error::IndentationOverflow);
    for (const bool elided : {false, true}) {
        Document validation("");
        const auto a = validation
            .BeginGroup({.Indent = INT32_MAX, .ExpressionContinuation = true});
        const auto b = validation.BeginGroup({.Indent = INT32_MAX});
        const auto c = validation.BeginGroup({.Indent = 4});
        // Saturation invalidates the prospective base. An unused structural
        // marker and its following Begin must reject exactly the same state.
        if (!elided) {
            validation.MarkAnchor({.StructuralBase = true});
        }
        const auto d = validation.BeginGroup({.StructuralBase = true}, elided);
        validation.EndGroup(d);
        validation.EndGroup(c);
        validation.EndGroup(b);
        validation.EndGroup(a);
        ASSERT_TRUE(validation.Seal());
        EXPECT_EQ(validation.Render().State.Code, Error::IndentationOverflow);
    }
    Document logicalOverflow("");
    logicalOverflow.BeginGroup({}, InvalidId);
    EXPECT_EQ(logicalOverflow.Seal().Code, Error::TooLarge);
    // Exercise the checked output-position domain without a multi-GiB buffer.
    constexpr std::size_t maximum = InvalidId - 1;
    EXPECT_TRUE(detail::FitsOutput(0, maximum));
    EXPECT_TRUE(detail::FitsOutput(maximum - 1, 1));
    EXPECT_TRUE(detail::FitsOutput(maximum, 0));
    EXPECT_FALSE(detail::FitsOutput(maximum, 1));
    EXPECT_FALSE(detail::FitsOutput(InvalidId, 0));
    EXPECT_FALSE(detail::FitsOutput(1, std::numeric_limits<std::size_t>::max()));
}

TEST(GroupLayout, FillSegmentDemandKeepsIndependentChildBreaks)
{
    std::string actual;
    {
        Document document("");
        document.Text("call(");
        auto closing = document.MarkAnchor({AnchorKind::BlockRelative, 0, 0});
        auto group = document.BeginGroup({GroupMode::Consistent, 2});
        document.Gap({GapKind::Soft, 0});
        document.Text("alpha,");
        document.Gap();
        document.Text("beta");
        document.ConditionalGap(
            group,
            {GapKind::Flat, 0},
            {GapKind::Hard, 0},
            closing);
        document.Text(")");
        document.EndGroup(group);
        document.Text(";");
        ASSERT_TRUE(document.Seal());
        for (const auto width : {18u, 17u}) {
            AppendRender(
                actual,
                document,
                width,
                "consistent closer and root suffix");
        }
    }
    {
        Document document("");
        document.Text("[");
        auto group = document.BeginGroup({GroupMode::Fill, 1});
        document.Text("aa,");
        document.Gap();
        document.Text("bb,");
        document.Gap();
        document.Text("cc,");
        document.Gap();
        document.Text("dd");
        document.Text("];");
        document.EndGroup(group);
        ASSERT_TRUE(document.Seal());
        for (const auto width : {10u, 20u}) {
            AppendRender(
                actual,
                document,
                width,
                "packed fill segments and closer");
        }
    }
    for (bool segmentFit : {false, true}) {
        Document document("");
        auto header = document.BeginGroup({GroupMode::Fill});
        document.Text("void");
        GapSpec prefix;
        prefix.RequiredWidth = 5; // The indivisible name and opener.
        prefix.FitNextSegment = segmentFit;
        document.Gap(prefix);
        document.Text("Many(");
        auto parameters = document.BeginGroup({GroupMode::Fill, 2});
        document.Gap({GapKind::Soft, 0});
        document.Text("int a,");
        document.Gap();
        document.Text("int b);");
        document.EndGroup(parameters);
        document.EndGroup(header);
        ASSERT_TRUE(document.Seal());
        const auto output = document.Render({.Width = 12});
        ASSERT_TRUE(output.State);
        EXPECT_TRUE(output.Broken[header]);
        EXPECT_TRUE(output.Broken[parameters]);
        actual += segmentFit ? "segment:\n" : "whole child:\n";
        actual.append(output.Text);
        actual += '\n';
    }
    {
        Document document("");
        auto parent = document.BeginGroup();
        document.Gap({GapKind::Hard, 0});
        auto child = document.BeginGroup({GroupMode::Consistent, 2});
        document.Text("aa");
        document.Gap();
        document.Text("bb");
        document.EndGroup(child);
        document.ConditionalText(parent, "", ",");
        document.EndGroup(parent);
        ASSERT_TRUE(document.Seal());
        for (const auto width : {6u, 5u}) {
            AppendRender(
                actual,
                document,
                width,
                "broken owner conditional comma");
        }
    }
    {
        Document document("");
        document.Text("a\t");
        auto anchor = document.MarkAnchor({AnchorKind::DelimiterColumn, 0, 0});
        auto group = document.BeginGroup();
        document.Text("x");
        document.Gap({GapKind::Hard, 0}, anchor);
        document.Text("y");
        document.EndGroup(group);
        ASSERT_TRUE(document.Seal());
        const auto output = document.Render({.Width = 80, .TabWidth = 4});
        ASSERT_TRUE(output.State);
        actual += "actual tab-stop alignment tab=4 width=80:\n";
        actual.append(output.Text);
        actual += "$\n";
    }
    {
        Document document("");
        auto tabs = document.BeginGroup();
        document.Text("a\tb");
        document.Gap();
        document.Text("c");
        document.EndGroup(tabs);
        ASSERT_TRUE(document.Seal());
        // Worst-case tab expansion can break even when actual expansion fits.
        for (const auto width : {8u, 7u}) {
            const auto output =
                document.Render({.Width = width, .TabWidth = 4});
            ASSERT_TRUE(output.State);
            actual += "conservative tab fit tab=4 width=";
            actual += std::to_string(width) + ":\n";
            actual.append(output.Text);
            actual += "$\n";
        }
    }
    test::golden::ExpectGolden(
        std::filesystem::path(DOCUMENT_TESTDATA_DIR) /
        "fill-segment-fit.expected",
        actual);
}

TEST(GroupLayout, BoundedChildFittingGolden)
{
    std::string actual;
    for (const auto width : {80u, 40u, 36u, 25u, 18u}) {
        for (const auto padding : {0u, 300u}) {
            for (const unsigned mode : {0u, 1u, 2u, 3u, 4u}) {
                Document document("");
                const auto expression = document.BeginGroup({
                    .Mode = GroupMode::Fill,
                    .Indent = 4,
                    .ForceBreak = mode == 1,
                    .ExpressionContinuation = true,
                    .Continuation = true,
                });
                document.Text("value =");
                document.Gap();
                for (unsigned i = 0; i < padding; ++i) {
                    document.Text("");
                }
                auto outer = document::InvalidId;
                if (mode >= 3) {
                    document.Text("consume(");
                    outer = document.BeginGroup({
                        .Mode = mode == 4
                            ? GroupMode::Consistent
                            : GroupMode::Fill,
                        .Indent = 4,
                        .StructuralBase = true,
                        .Continuation = true,
                    });
                    document.ConditionalGap(
                        outer,
                        {GapKind::Flat, 0},
                        {GapKind::Hard, 0});
                }
                const auto chain = document.BeginGroup({
                    .Mode = GroupMode::Fill,
                    .Indent = 4,
                    .StructuralBase = true,
                    .Continuation = true,
                });
                document.Text("source");
                document.ConditionalGap(
                    chain,
                    {GapKind::Flat, 0},
                    {GapKind::Soft, 0});
                document.Text(".slice({");
                const auto anchor = mode == 2
                    ? document.MarkAnchor(
                        {
                            .Kind = document::AnchorKind::DelimiterColumn,
                            .Fallback = 4,
                            .Minimum = 4,
                            .StructuralBase = true,
                            .Continuation = true,
                        })
                    : document::InvalidId;
                const auto arguments = document.BeginGroup({
                    .Mode = GroupMode::Fill,
                    .Indent = 4,
                    .StructuralBase = true,
                    .Continuation = true,
                });
                document.ConditionalGap(
                    arguments,
                    {GapKind::Flat, 0},
                    {GapKind::Hard, 0, 15},
                    anchor);
                document.Text("startingOffset,");
                document.ConditionalGap(
                    arguments,
                    {GapKind::Flat, 1},
                    {GapKind::Hard, 1, 16},
                    anchor);
                document.Text("endingOffset");
                document.ConditionalText(arguments, "", ",");
                document.Text("})");
                document.EndGroup(arguments);
                document.EndGroup(chain);
                if (mode >= 3) {
                    document.Text(",");
                    document.ConditionalGap(
                        outer,
                        {GapKind::Flat, 1},
                        {GapKind::Hard, 1});
                    document.Text("tabWidth)");
                    document.EndGroup(outer);
                }
                document.EndGroup(expression);
                document.Text(";");
                ASSERT_TRUE(document.Seal());
                const auto output = document.Render({.Width = width});
                ASSERT_TRUE(output.State);
                const auto repeated = document.Render({.Width = width});
                EXPECT_EQ(output.Text, repeated.Text);
                EXPECT_EQ(output.Broken, repeated.Broken);
                EXPECT_EQ(output.Map.size(), repeated.Map.size());
                // Oversized mixed groups retain the legacy fallback, which
                // may overflow at narrow widths. Complete child-fitting
                // candidates must fit every line, not just their prefix.
                if (width >= 25 && (padding == 0 || mode < 3)) {
                    std::size_t begin = 0;
                    while (begin < output.Text.size()) {
                        auto end = output.Text.find('\n', begin);
                        if (end == std::string_view::npos) {
                            end = output.Text.size();
                        }
                        EXPECT_LE(end - begin, width);
                        begin = end + 1;
                    }
                }
                for (const auto& mapping : output.Map) {
                    EXPECT_LE(mapping.OutputBegin, mapping.OutputEnd);
                    EXPECT_LE(mapping.OutputEnd, output.Text.size());
                }
                actual += "width=" + std::to_string(width) + " padding=" +
                    std::to_string(padding) + " mode=" + std::to_string(mode) +
                    "\n";
                actual.append(output.Text);
                actual += '\n';
            }
        }
    }
    test::golden::ExpectGolden(
        std::filesystem::path(DOCUMENT_TESTDATA_DIR) /
        "bounded-child-fit.expected",
        actual);
}

TEST(GroupLayout, ChildFittingCostGolden)
{
    std::string actual;
    for (const bool longCall : {false, true}) {
        Document document("");
        const auto comparison = document.BeginGroup(
            {.Mode = GroupMode::Fill, .Indent = 4, .Continuation = true});
        document.Text("lhs !=");
        document.Gap();
        const auto call = document.BeginGroup({
            .Mode = GroupMode::Consistent,
            .Indent = 4,
            .StructuralBase = true,
            .Continuation = true,
        });
        document.Text("lookup(");
        document.ConditionalGap(call, {GapKind::Flat, 0}, {GapKind::Hard, 0});
        document.Text(longCall ? "firstLongArg," : "tiny");
        if (longCall) {
            document
                .ConditionalGap(call, {GapKind::Flat, 1}, {GapKind::Hard, 0});
            document.Text("secondLongArg");
        }
        document.Text(")");
        document.EndGroup(call);
        document.EndGroup(comparison);
        document.Text(";");
        ASSERT_TRUE(document.Seal());
        const auto output = document.Render({.Width = longCall ? 24u : 18u});
        ASSERT_TRUE(output.State);
        EXPECT_EQ(output.Broken[call] != 0, longCall);
        actual += longCall ? "long call\n" : "short call\n";
        actual.append(output.Text);
        actual += '\n';
    }
    for (const auto penalty : {0u, 2u}) {
        Document document("");
        const auto comparison = document.BeginGroup(
            {.Mode = GroupMode::Fill, .Indent = 4, .Continuation = true});
        document.Text("lhs !=");
        document.Gap();
        const auto chain = document.BeginGroup({
            .Mode = GroupMode::Fill,
            .Indent = 4,
            .StructuralBase = true,
            .Continuation = true,
            .BreakPenalty = static_cast<std::uint8_t>(penalty),
        });
        document.Text("object");
        document.Gap({GapKind::Soft, 0});
        document.Text(".lookup(");
        const auto arguments = document.BeginGroup({
            .Mode = GroupMode::Consistent,
            .Indent = 4,
            .StructuralBase = true,
            .Continuation = true,
        });
        document
            .ConditionalGap(arguments, {GapKind::Flat, 0}, {GapKind::Hard, 0});
        document.Text("first,");
        document
            .ConditionalGap(arguments, {GapKind::Flat, 1}, {GapKind::Hard, 0});
        document.Text("second)");
        document.EndGroup(arguments);
        document.EndGroup(chain);
        document.EndGroup(comparison);
        document.Text(";");
        ASSERT_TRUE(document.Seal());
        const auto output = document.Render({.Width = 30});
        ASSERT_TRUE(output.State);
        EXPECT_EQ(output.Broken[arguments] != 0, penalty != 0);
        actual += "chain penalty=" + std::to_string(penalty) + "\n";
        actual.append(output.Text);
        actual += '\n';
    }
    for (const auto width : {40u, 80u}) {
        for (const auto penalty : {0u, 1u, 2u}) {
            Document document("");
            document.Text("auto value");
            const auto initializer = document.BeginGroup(
                {.Mode = GroupMode::Fill, .Indent = 4, .Continuation = true});
            document.Text(" =");
            document.Gap();
            const auto ternary = document.BeginGroup({
                .Mode = GroupMode::Consistent,
                .Indent = 4,
                .Continuation = true,
                .FlatInBrokenParentPenalty =
                    static_cast<std::uint8_t>(penalty),
            });
            document.Text("cond");
            document.Gap();
            document.Text("? firstBranch");
            document.Gap();
            document.Text(": secondBranch");
            document.EndGroup(ternary);
            document.EndGroup(initializer);
            document.Text(";");
            ASSERT_TRUE(document.Seal());
            const auto output = document.Render({.Width = width});
            ASSERT_TRUE(output.State);
            EXPECT_EQ(output.Broken[initializer] != 0, width == 40);
            EXPECT_EQ(output.Broken[ternary] != 0, width == 40 && penalty > 1);
            actual += "flat-child penalty=" + std::to_string(penalty) +
                " width=" + std::to_string(width) + "\n";
            actual.append(output.Text);
            actual += '\n';
        }
    }
    test::golden::ExpectGolden(
        std::filesystem::path(DOCUMENT_TESTDATA_DIR) /
        "child-fit-cost.expected",
        actual);
}

TEST(GroupLayout, ChildFittingKeepsConditionalChoicesOwnedShape)
{
    std::string actual;
    for (const bool textChoice : {true, false}) {
        for (const auto width : {80u, 40u, 30u}) {
            Document document("");
            const auto outer = document.BeginGroup(
                {.Mode = GroupMode::Fill, .Indent = 4, .Continuation = true});
            document.Text("wrap(");
            const auto list = document.BeginGroup({
                .Mode = GroupMode::Fill,
                .Indent = 4,
                .StructuralBase = true,
                .Continuation = true,
            });
            document.Text("{first,");
            if (textChoice) {
                document.Gap();
            } else {
                document.ConditionalGap(
                    list,
                    {GapKind::Flat, 1},
                    {GapKind::Soft, 2});
            }
            document.Text("second");
            if (textChoice) {
                document.ConditionalText(list, "", ",");
            }
            document.Text("}");
            document.EndGroup(list);
            document.Text(",");
            document.Gap();
            const auto expression = document.BeginGroup({
                .Mode = GroupMode::Fill,
                .Indent = 4,
                .StructuralBase = true,
                .Continuation = true,
                .ChildBreaks = false,
            });
            document.Text("long_argument +");
            document.Gap();
            document.Text("tail)");
            document.EndGroup(expression);
            document.EndGroup(outer);
            document.Text(";");
            ASSERT_TRUE(document.Seal());
            const auto output = document.Render({.Width = width});
            ASSERT_TRUE(output.State);
            EXPECT_FALSE(output.Broken[list]);
            EXPECT_FALSE(output.Broken[expression]);
            EXPECT_EQ(output.Broken[outer] != 0, width != 80);
            actual += std::string(textChoice ? "text" : "gap") + " width=" +
                std::to_string(width) + "\n";
            actual.append(output.Text);
            actual += '\n';
        }
    }
    test::golden::ExpectGolden(
        std::filesystem::path(DOCUMENT_TESTDATA_DIR) /
        "child-fit-owned-shape.expected",
        actual);
}

TEST(GroupLayout, ForcedMultilineChildPrefixGolden)
{
    std::string actual;
    for (bool spacing : {false, true}) {
        for (bool shrinks : {false, true}) {
            for (bool opaque : {false, true}) {
                for (bool nested : {false, true}) {
                    Document document("b\nc");
                    auto parent = document.BeginGroup({GroupMode::Fill, 0});
                    document.Text("aa");
                    document.Gap();
                    GroupId wrapper = InvalidId;
                    if (nested) {
                        wrapper = document.BeginGroup({GroupMode::Fill, 0});
                    }
                    auto child = document.BeginGroup();
                    if (spacing) {
                        document.ConditionalGap(
                            child,
                            {
                                GapKind::Flat,
                                static_cast<std::uint16_t>(shrinks ? 4 : 0),
                            },
                            {
                                GapKind::Flat,
                                static_cast<std::uint16_t>(shrinks ? 0 : 4),
                            });
                    } else {
                        document.ConditionalText(
                            child,
                            shrinks ? "xxxx" : "",
                            shrinks ? "" : "xxxx");
                    }
                    if (opaque) {
                        document.SourceText(0, 3, NoIdentity, true);
                    } else {
                        document.Text("b");
                        document.Gap({GapKind::Hard, 0});
                        document.Text("c");
                    }
                    document.EndGroup(child);
                    if (nested) {
                        document.EndGroup(wrapper);
                    }
                    document.EndGroup(parent);
                    ASSERT_TRUE(document.Seal());
                    auto output = document.Render({.Width = 5});
                    ASSERT_TRUE(output.State);
                    EXPECT_TRUE(output.Broken[parent]);
                    EXPECT_TRUE(output.Broken[child]);
                    if (nested) {
                        EXPECT_TRUE(output.Broken[wrapper]);
                    }
                    std::string_view remaining = output.Text;
                    while (!remaining.empty()) {
                        const auto end = remaining.find('\n');
                        const auto length = end == std::string_view::npos
                            ? remaining.size()
                            : end;
                        EXPECT_LE(length, 5u);
                        remaining.remove_prefix(
                            length + (end != std::string_view::npos));
                    }
                    EXPECT_EQ(document.Render({.Width = 5}).Text, output.Text);
                    actual += spacing ? "gap " : "text ";
                    actual += shrinks ? "shrink " : "grow ";
                    actual += opaque ? "opaque " : "hard ";
                    actual += nested ? "nested:\n" : "direct:\n";
                    actual.append(output.Text);
                    actual += '\n';
                }
            }
        }
    }
    {
        Document document("");
        auto group = document.BeginGroup({GroupMode::Consistent, 2});
        document.Text("before");
        document.Gap();
        document.Verbatim("raw\r\n\tbytes\n");
        document.ConditionalText(group, "flat", "broken");
        document.EndGroup(group);
        ASSERT_TRUE(document.Seal());
        AppendRender(actual, document, 200, "opaque multiline bytes");
    }
    test::golden::ExpectGolden(
        std::filesystem::path(DOCUMENT_TESTDATA_DIR) /
        "forced-child-prefix.output.txt",
        actual);
}

TEST(GroupLayout, NestedHeaderAndOwnListPunctuationGolden)
{
    Document document("");
    const auto closing = document.MarkAnchor({AnchorKind::BlockRelative, 0, 0});
    document.Text("auto handlers = {");
    auto arguments = document.BeginGroup({GroupMode::Consistent, 2});
    document.Gap({GapKind::Soft, 0});
    document.Text("[&] {");
    auto lambdaBody = document.BeginGroup({GroupMode::Consistent, 2});
    document.Gap({GapKind::Hard, 0});
    auto header = document.BeginGroup();
    document.Text("if (");
    auto condition = document.BeginGroup({GroupMode::Consistent, 2});
    document.Text("first_check");
    document.Gap();
    document.Text("&&");
    document.Gap();
    document.Text("second_check");
    document.EndGroup(condition);
    document.Text(")");
    document.ConditionalGap(header, {GapKind::Flat, 1}, {GapKind::Hard, 0});
    document.Text("{");
    document.EndGroup(header);
    auto body = document.BeginGroup({GroupMode::Consistent, 2});
    document.Gap({GapKind::Hard, 0});
    document.Text("work();");
    document.EndGroup(body);
    document.Gap({GapKind::Hard, 0});
    document.Text("}");
    document.EndGroup(lambdaBody);
    document.Gap({GapKind::Hard, 0});
    document.Text("}");
    document.ConditionalText(arguments, "", ",");
    document.ConditionalGap(
        arguments,
        {GapKind::Flat, 0},
        {GapKind::Hard, 0},
        closing);
    document.EndGroup(arguments);
    document.Text("};");
    ASSERT_TRUE(document.Seal());
    auto output = document.Render({.Width = 24});
    ASSERT_TRUE(output.State);
    test::golden::ExpectGolden(
        std::filesystem::path(DOCUMENT_TESTDATA_DIR) /
        "nested-header.output.txt",
        output.Text);
    EXPECT_EQ(document.Render({.Width = 24}).Text, output.Text);
    EXPECT_TRUE(output.Broken[arguments]);
    EXPECT_TRUE(output.Broken[header]);
    EXPECT_TRUE(output.Broken[condition]);
    const auto wide = document.Render({.Width = 120});
    EXPECT_NE(
        wide.Text.find("if (first_check && second_check) {"),
        std::string_view::npos);
    EXPECT_FALSE(wide.Broken[header]);
    EXPECT_TRUE(wide.Broken[arguments]);
    EXPECT_EQ(document.Render({.Width = 24}).Text, output.Text);
}

TEST(GroupLayout, DelimiterRequirementsAndAnchorReuseGolden)
{
    std::string actual;
    {
        Document document("");
        document.Text("call(");
        auto aligned = document.MarkAnchor({AnchorKind::DelimiterColumn, 0, 2});
        auto group = document.BeginGroup({GroupMode::Consistent, 2});
        document.Text("alpha,");
        document.Gap({}, aligned);
        document.Text("beta)");
        document.EndGroup(group);
        ASSERT_TRUE(document.Seal());
        for (const auto width : {12u, 5u}) {
            AppendRender(actual, document, width, "delimiter alignment");
        }
    }
    {
        Document document("");
        document.Text("call(");
        auto anchor =
            document.MarkAnchor({AnchorKind::DelimiterColumn, 0, 2, 2});
        auto group = document.BeginGroup({GroupMode::Consistent, 2});
        document.Gap({GapKind::Soft, 0, 5}, anchor);
        document.Text("beta");
        document.Text(",");
        document.Gap({GapKind::Hard, 0, 3}, anchor);
        document.Text("xy");
        document.Text(",");
        document.Gap({GapKind::Hard, 0, 4}, anchor);
        document.Text("abc");
        document.Text(",");
        document.ConditionalGap(
            group,
            {GapKind::Flat, 0},
            {GapKind::Hard, 0, 5},
            anchor);
        document.Text("beta");
        document.Text(")");
        document.EndGroup(group);
        ASSERT_TRUE(document.Seal());
        auto output = document.Render({.Width = 8});
        ASSERT_TRUE(output.State);
        AppendRender(actual, document, 8, "anchor reuse requirements");
        // The same anchor independently resolves each gap. Three columns reach
        // Width exactly at column 5; four or five columns need the column-2
        // fallback.
        EXPECT_TRUE(output.Broken[group]);
    }
    for (const bool borrowed : {false, true}) {
        Document document(borrowed ? "\n" : "");
        auto block = document.BeginGroup({GroupMode::Consistent, 3});
        document.Text("outer{");
        document.Gap({GapKind::Soft, 0});
        document.Text("c(");
        // Output column 5, captured block 3; Offset lowers the raw target to 2.
        auto anchor =
            document.MarkAnchor({AnchorKind::DelimiterColumn, -3, 2, 4});
        auto floor = document.MarkAnchor({AnchorKind::DelimiterColumn, -7, 2});
        auto blockAnchor = InvalidId;
        if (borrowed) {
            blockAnchor = document.MarkAnchor(
                {AnchorKind::BlockRelative, 1, INT32_MAX, UINT32_MAX});
        }
        auto continuation = document.BeginGroup({GroupMode::Consistent, 6});
        const auto line = [&] (AnchorId target, std::uint32_t demand) {
            if (borrowed) {
                document.SourceLineIndent(0, 1, 1, 1, target, demand);
            } else {
                document.Gap({GapKind::Hard, 0, demand}, target);
            }
        };
        line(anchor, 2);
        document.Text("x;");
        line(anchor, 4);
        document.Text("abc;");
        line(floor, 1);
        document.Text("z");
        if (borrowed) {
            line(blockAnchor, UINT32_MAX);
            document.Text("q");
        }
        document.EndGroup(continuation);
        document.EndGroup(block);
        ASSERT_TRUE(document.Seal());
        // Minimum gives 3+4=7; fallback gives 3+2=5; default Minimum floors at 3.
        // Width 9 permits Minimum + demand 2 exactly, but 8 needs fallback.
        // None uses the continuation group's indentation (3+6=9) as its block.
        AppendRender(
            actual,
            document,
            9,
            borrowed ? "borrowed captured block" : "generated captured block");
        if (borrowed) {
            AppendRender(actual, document, 8, "borrowed captured block");
        }
    }
    for (bool borrowed : {false, true}) {
        Document document("\n");
        auto line = [&] (AnchorId anchor, std::uint32_t demand) {
            if (borrowed) {
                document.SourceLineIndent(0, 1, 1, 1, anchor, demand);
            } else {
                document.Gap({GapKind::Hard, 0, demand}, anchor);
            }
        };
        document.Text("(");
        auto minimum = document.MarkAnchor(
            {AnchorKind::DelimiterColumn, INT32_MAX, 2, UINT32_MAX});
        auto requirement =
            document.MarkAnchor({AnchorKind::DelimiterColumn, 0, 2});
        auto negative =
            document.MarkAnchor({AnchorKind::DelimiterColumn, INT32_MIN, -1});
        auto group = document.BeginGroup();
        line(minimum, 1);
        document.Text("x");
        line(requirement, UINT32_MAX);
        document.Text("y");
        line(negative, UINT32_MAX);
        document.Text("z");
        document.EndGroup(group);
        ASSERT_TRUE(document.Seal());
        for (const auto width : {8u, UINT32_MAX - 1}) {
            AppendRender(
                actual,
                document,
                width,
                borrowed
                    ? "borrowed saturated demand"
                    : "generated saturated demand");
        }
    }
    for (bool borrowed : {false, true}) {
        Document document("\n");
        document.Text("call(");
        auto anchor =
            document.MarkAnchor({AnchorKind::DelimiterColumn, 0, 2, 2});
        auto group = document.BeginGroup({GroupMode::Consistent, 2});
        if (borrowed) {
            document.SourceLineIndent(0, 1, 1, 1, anchor, 15);
        } else {
            document.Gap({GapKind::Hard, 0, 15}, anchor);
        }
        document.Text("very_long_atom)");
        document.EndGroup(group);
        ASSERT_TRUE(document.Seal());
        for (const auto width : {8u, 20u}) {
            AppendRender(
                actual,
                document,
                width,
                borrowed
                    ? "borrowed unbreakable atom"
                    : "generated unbreakable atom");
        }
    }
    {
        Document document("");
        auto block = document.BeginGroup({GroupMode::Consistent, 3});
        auto anchor = document
            .MarkAnchor({AnchorKind::BlockRelative, 1, INT32_MAX, UINT32_MAX});
        document.Text("a");
        // Block-relative anchors ignore delimiter Minimum/RequiredWidth/fallback.
        document.Gap({GapKind::Hard, 0, UINT32_MAX}, anchor);
        auto nested = document.BeginGroup({GroupMode::Consistent, 2});
        document.Text("bb");
        document.Gap({GapKind::Hard, 0});
        document.Text("cc");
        document.EndGroup(nested);
        document.EndGroup(block);
        ASSERT_TRUE(document.Seal());
        AppendRender(actual, document, 80, "generated block anchor");
    }
    for (bool borrowed : {false, true}) {
        Document document("\n");
        document.Text("call(");
        auto anchor = document.MarkAnchor({AnchorKind::DelimiterColumn, 0, 2});
        auto group = document.BeginGroup();
        if (borrowed) {
            document.SourceLineIndent(0, 1, 1, 1, anchor, 0);
        } else {
            document.Gap({GapKind::Hard, 0, 0}, anchor);
        }
        document.EndGroup(group);
        ASSERT_TRUE(document.Seal());
        // Zero demand does not permit a delimiter target at the column limit.
        // Positive-demand equality below Width is covered by captured blocks.
        for (const auto width : {5u, 6u}) {
            AppendRender(
                actual,
                document,
                width,
                borrowed ? "borrowed zero demand" : "generated zero demand");
        }
    }
    test::golden::ExpectGolden(
        std::filesystem::path(DOCUMENT_TESTDATA_DIR) /
        "alignment-requirements.output.txt",
        actual);
}

TEST(GroupLayout, ProtectedSourceAndEditProvenanceGolden)
{
    const auto directory = std::filesystem::path(DOCUMENT_TESTDATA_DIR);
    const auto source =
        test::golden::ReadFile(directory / "protected.source.txt");
    Document document(source);
    document.SourceText(
        0,
        static_cast<std::uint32_t>(source.size()),
        NoIdentity - 1,
        true);
    const auto end = static_cast<std::uint32_t>(source.size());
    document.Gap({GapKind::Hard, 0}, InvalidId, {end, end, 8});
    document.Text("after;");
    ASSERT_TRUE(document.Seal());
    auto output = document.Render({.Width = 4, .LineEnding = Newline::CRLF});
    ASSERT_TRUE(output.State);
    test::golden::ExpectGolden(directory / "protected.output.txt", output.Text);
    ASSERT_EQ(output.Map.size(), 3u);
    EXPECT_TRUE(output.Map[0].Protected);
    EXPECT_TRUE(output.Map[0].Exact);
    EXPECT_EQ(output.Map[0].Source.Identity, NoIdentity - 1);
    EXPECT_EQ(
        output.Text.substr(
            output.Map[0].OutputBegin,
            output.Map[0].OutputEnd - output.Map[0].OutputBegin),
        std::string_view(source));
    EXPECT_FALSE(output.Map[1].Exact);
    EXPECT_EQ(output.Map[1].Source.Identity, 8u);
    EXPECT_EQ(output.Map[2].Source.Begin, InvalidId);
    const auto tokens = document.Render({
        .Width = 4,
        .LineEnding = Newline::CRLF,
        .SourceMap = false,
        .TokenMap = true,
    });
    ASSERT_TRUE(tokens.State);
    EXPECT_EQ(tokens.Text, output.Text);
    ASSERT_EQ(tokens.Map.size(), 2u);
    EXPECT_EQ(tokens.Map[0].Source.Identity, NoIdentity - 1);
    EXPECT_EQ(tokens.Map[1].Source.Identity, 8u);
    const std::array tracked{Provenance{0, 1}};
    const auto protection = document.Render({
        .Width = 4,
        .LineEnding = Newline::CRLF,
        .SourceMap = false,
        .TrackSource = tracked,
    });
    ASSERT_TRUE(protection.State);
    EXPECT_EQ(protection.Text, output.Text);
    ASSERT_EQ(protection.Map.size(), 1u);
    EXPECT_TRUE(protection.Map[0].Exact);
    EXPECT_TRUE(protection.Map[0].Protected);
    EXPECT_EQ(protection.Map[0].OutputEnd, output.Map[0].OutputEnd);
    const std::array invalidTracked{Provenance{2, 1}};
    EXPECT_EQ(
        document.Render({.SourceMap = false, .TrackSource = invalidTracked})
            .State.Code,
        Error::InvalidPolicy);
}

std::string ExpandTabs(std::string_view text, std::uint8_t tabWidth)
{
    std::string expanded;
    std::size_t column = 0;
    for (char c : text) {
        if (c == '\t') {
            const auto spaces = tabWidth - column % tabWidth;
            expanded.append(spaces, ' ');
            column += spaces;
        } else {
            expanded += c;
            column = c == '\r' || c == '\n' ? 0 : column + 1;
        }
    }
    return expanded;
}

TEST(GroupLayout, GeneratedTabsPreserveVisualLayoutAndOpaqueSource)
{
    const auto source = test::golden::ReadFile(
        std::filesystem::path(DOCUMENT_TESTDATA_DIR) / "protected.source.txt");
    Document document(source);
    document.SourceText(0, static_cast<std::uint32_t>(source.size()), 99, true);
    auto block = document.BeginGroup({GroupMode::Consistent, 6});
    document.Gap({GapKind::Hard, 0});
    document.Text("c(");
    auto anchor = document.MarkAnchor({AnchorKind::DelimiterColumn, 1, 2});
    auto continuation = document.BeginGroup({GroupMode::Consistent, 5});
    document.Gap({GapKind::Hard, 0, 3}, anchor);
    document.Text("xy;");
    document.Gap({GapKind::Hard, 0});
    document.Text("next;");
    document.EndGroup(continuation);
    document.EndGroup(block);
    ASSERT_TRUE(document.Seal());
    Policy policy{.Width = 20, .TabWidth = 4, .LineEnding = Newline::CRLF};
    auto spaces = document.Render(policy);
    policy.Indentation = IndentationKind::Tabs;
    auto tabs = document.Render(policy);
    ASSERT_TRUE(spaces.State);
    ASSERT_TRUE(tabs.State);
    EXPECT_EQ(ExpandTabs(tabs.Text, 4), ExpandTabs(spaces.Text, 4));
    EXPECT_EQ(spaces.Broken, tabs.Broken);
    for (std::uint8_t width : {1, 8, 32}) {
        auto otherSpaces = document.Render(
            {.Width = 20, .TabWidth = width, .LineEnding = Newline::CRLF});
        auto otherTabs = document.Render({
            .Width = 20,
            .TabWidth = width,
            .LineEnding = Newline::CRLF,
            .Indentation = IndentationKind::Tabs,
        });
        ASSERT_TRUE(otherSpaces.State);
        ASSERT_TRUE(otherTabs.State);
        EXPECT_EQ(
            ExpandTabs(otherTabs.Text, width),
            ExpandTabs(otherSpaces.Text, width));
        EXPECT_EQ(otherSpaces.Broken, otherTabs.Broken);
        EXPECT_EQ(
            std::string_view(otherTabs.Text).substr(0, source.size()),
            std::string_view(source));
    }
    EXPECT_EQ(
        std::string_view(spaces.Text).substr(source.size()),
        "\r\n      c(\r\n         xy;\r\n           next;");
    EXPECT_EQ(
        std::string_view(tabs.Text).substr(source.size()),
        "\r\n\t  c(\r\n\t\t xy;\r\n\t\t   next;");
    for (const auto* output : {&spaces, &tabs}) {
        ASSERT_FALSE(output->Map.empty());
        EXPECT_TRUE(output->Map[0].Exact);
        EXPECT_TRUE(output->Map[0].Protected);
        EXPECT_EQ(output->Map[0].Source.Identity, 99u);
        EXPECT_EQ(
            std::string_view(output->Text).substr(0, output->Map[0].OutputEnd),
            std::string_view(source));
    }
    // Generated tab stops represent the policy's maximum supported IndentWidth.
    Document wideIndent("");
    auto wide = wideIndent.BeginGroup({GroupMode::Consistent, 32});
    wideIndent.Gap({GapKind::Hard, 0});
    wideIndent.Text("x");
    wideIndent.EndGroup(wide);
    ASSERT_TRUE(wideIndent.Seal());
    auto wideTabs = wideIndent.Render(
        {.Width = 40, .TabWidth = 32, .Indentation = IndentationKind::Tabs});
    ASSERT_TRUE(wideTabs.State);
    EXPECT_EQ(wideTabs.Text, "\n\tx");
    auto wideSpaces = wideIndent.Render({.Width = 40, .TabWidth = 32});
    EXPECT_EQ(ExpandTabs(wideTabs.Text, 32), std::string_view(wideSpaces.Text));
}

TEST(GroupLayout, SourceCarriageReturnAndLocalGapNewlinesGolden)
{
    constexpr std::string_view source = "// note\r\n";
    constexpr auto tokenEnd = static_cast<std::uint32_t>(source.size() - 1);
    std::string actual;
    for (auto global : {Newline::CRLF, Newline::LF}) {
        Document document(source);
        document.SourceText(0, tokenEnd, 100, true);
        // The source comment owns CR, while the original whitespace owns LF.
        // The generated gap must start with LF even under global CRLF.
        document.Gap(
            {GapKind::Hard, 0, 0, GapNewline::LF},
            InvalidId,
            {tokenEnd, static_cast<std::uint32_t>(source.size()), 101});
        auto call = document.BeginGroup({GroupMode::Consistent, 4});
        document.Text("call(", {.Identity = 200});
        auto anchor = document.MarkAnchor({AnchorKind::DelimiterColumn, 0, 0});
        const auto flatOverride = global == Newline::CRLF
            ? GapNewline::CRLF
            : GapNewline::LF;
        const auto brokenOverride = global == Newline::CRLF
            ? GapNewline::LF
            : GapNewline::CRLF;
        document.ConditionalGap(
            call,
            {GapKind::Flat, 0, 0, flatOverride},
            {GapKind::Soft, 0, 5, brokenOverride},
            anchor,
            {.Identity = 201});
        document.Text("beta", {.Identity = 202});
        document.Text(")", {.Identity = 203});
        document.EndGroup(call);
        document.Gap({GapKind::Hard, 0}, InvalidId, {.Identity = 204});
        document.Text("done;", {.Identity = 205});
        ASSERT_TRUE(document.Seal());
        for (auto width : {20u, 9u}) {
            const bool broken = width == 9;
            auto output = document.Render({
                .Width = width,
                .TabWidth = 4,
                .LineEnding = global,
                .Indentation = IndentationKind::Tabs,
            });
            ASSERT_TRUE(output.State);
            ASSERT_EQ(output.Map.size(), 8u);
            EXPECT_EQ(output.Broken[call] != 0, broken);
            EXPECT_EQ(
                std::string_view(output.Text).substr(0, source.size()),
                source);
            const auto mappedText = [&] (std::size_t index) {
                const auto& mapping = output.Map[index];
                return std::string_view(output.Text).substr(
                    mapping.OutputBegin,
                    mapping.OutputEnd - mapping.OutputBegin);
            };
            EXPECT_EQ(mappedText(0), source.substr(0, tokenEnd));
            EXPECT_EQ(mappedText(0).back(), '\r');
            EXPECT_TRUE(output.Map[0].Exact);
            EXPECT_TRUE(output.Map[0].Protected);
            EXPECT_EQ(output.Map[0].Source.Identity, 100u);
            EXPECT_EQ(mappedText(1), "\n");
            EXPECT_EQ(output.Map[1].Source.Begin, tokenEnd);
            EXPECT_EQ(output.Map[1].Source.End, source.size());
            EXPECT_EQ(output.Map[1].Source.Identity, 101u);
            EXPECT_FALSE(output.Map[1].Exact);
            EXPECT_EQ(
                mappedText(3),
                !broken ? "" : global == Newline::CRLF ? "\n\t" : "\r\n\t");
            EXPECT_EQ(mappedText(6), global == Newline::CRLF ? "\r\n" : "\n");
            const auto callText = std::string_view(output.Text).substr(
                output.Map[2].OutputBegin,
                output.Map[5].OutputEnd - output.Map[2].OutputBegin);
            EXPECT_EQ(
                callText.find_first_of("\r\n") != std::string_view::npos,
                output.Broken[call] != 0);
            std::size_t mappedEnd = 0;
            for (const auto& mapping : output.Map) {
                EXPECT_EQ(mapping.OutputBegin, mappedEnd);
                EXPECT_LE(mapping.OutputBegin, mapping.OutputEnd);
                mappedEnd = mapping.OutputEnd;
            }
            EXPECT_EQ(mappedEnd, output.Text.size());
            EXPECT_EQ(output.Text.find("\r\r\n"), std::string_view::npos);
            EXPECT_EQ(
                document.Render({
                    .Width = width,
                    .TabWidth = 4,
                    .LineEnding = global,
                    .Indentation = IndentationKind::Tabs,
                }).Text,
                output.Text);
            actual += global == Newline::CRLF ? "global CRLF " : "global LF ";
            actual += broken ? "broken:\n" : "flat:\n";
            actual.append(output.Text);
            actual += '\n';
        }
    }
    test::golden::ExpectGolden(
        std::filesystem::path(DOCUMENT_TESTDATA_DIR) /
        "source-cr-gap-newlines.output.txt",
        actual);
}

TEST(GroupLayout, RejectsMalformedGapNewlineOverrides)
{
    for (auto invalid :
        {static_cast<GapNewline>(3), static_cast<GapNewline>(255)})
    {
        for (auto kind : {GapKind::Flat, GapKind::Soft, GapKind::Hard}) {
            Document document("");
            document.Gap({kind, 0, 0, invalid});
            document.Text("later");
            EXPECT_EQ(document.Seal().Code, Error::InvalidPolicy);
            auto output = document.Render();
            EXPECT_EQ(output.State.Code, Error::InvalidPolicy);
            EXPECT_TRUE(output.Text.empty());
        }
        for (bool invalidFlat : {false, true}) {
            Document document("");
            auto group = document.BeginGroup();
            document.ConditionalGap(
                group,
                {
                    GapKind::Flat,
                    0,
                    0,
                    invalidFlat ? invalid : GapNewline::Inherit,
                },
                {GapKind::Soft, 0, 0, invalidFlat ? GapNewline::LF : invalid});
            document.EndGroup(group);
            EXPECT_EQ(document.Seal().Code, Error::InvalidPolicy);
            EXPECT_EQ(document.Render().State.Code, Error::InvalidPolicy);
        }
    }
}

TEST(GroupLayout, VerticalGapCountUsesFirstOverrideAndNoBlankLineIndent)
{
    constexpr std::string_view source = "// note\r\n";
    Document document(source);
    auto group = document.BeginGroup({GroupMode::Consistent, 6});
    document.SourceText(0, 8, 1, true);
    document.Gap(
        {
            .Kind = GapKind::Hard,
            .Spaces = 0,
            .LineEnding = GapNewline::LF,
            .NewlineCount = 3,
        },
        InvalidId,
        {8, 9, 2});
    document.Text("next;");
    document.Gap({.Kind = GapKind::Hard, .Spaces = 0, .NewlineCount = 2});
    document.Text("last;");
    document.EndGroup(group);
    ASSERT_TRUE(document.Seal());
    auto output = document.Render({
        .Width = 80,
        .TabWidth = 4,
        .LineEnding = Newline::CRLF,
        .Indentation = IndentationKind::Tabs,
    });
    ASSERT_TRUE(output.State);
    EXPECT_TRUE(output.Broken[group]);
    ASSERT_EQ(output.Map.size(), 5u);
    EXPECT_EQ(
        std::string_view(output.Text).substr(
            output.Map[1].OutputBegin,
            output.Map[1].OutputEnd - output.Map[1].OutputBegin),
        "\n\r\n\r\n\t  ");
    EXPECT_FALSE(output.Map[1].Exact);
    EXPECT_EQ(output.Map[1].Source.Identity, 2u);
    EXPECT_EQ(output.Text.find("\r\n\t\r\n"), std::string_view::npos);
    std::size_t end = 0;
    for (const auto& mapping : output.Map) {
        EXPECT_EQ(mapping.OutputBegin, end);
        end = mapping.OutputEnd;
    }
    EXPECT_EQ(end, output.Text.size());
    test::golden::ExpectGolden(
        std::filesystem::path(DOCUMENT_TESTDATA_DIR) /
        "vertical-gap.output.txt",
        output.Text);
}

TEST(GroupLayout, SourceRunsMatchIndividualAtoms)
{
    const std::string source = "\ta+b /*x*/\tq\nnext";
    const std::array<Provenance, 2> tracked{{{0, 1}, {5, 12}}};
    for (auto mode : {GroupMode::Consistent, GroupMode::Fill}) {
        // These lengths cross the original 256-logical-node search boundary.
        for (auto repetitions : {0u, 36u, 37u, 38u, 39u, 40u, 41u, 60u}) {
            Document packed(source);
            Document separate(source);
            const auto fill = [&] (Document& document, bool compact) {
                const auto text =
                    [
                        &] (
                        std::uint32_t begin,
                        std::uint32_t end,
                        Identity id,
                        bool verbatim = false) {
                            if (compact) {
                                document
                                    .AppendSourceText(begin, end, id, verbatim);
                            } else {
                                document.SourceText(begin, end, id, verbatim);
                            }
                        };
                const auto begin = [&] (GroupSpec spec) {
                    if (!compact) {
                        document.MarkAnchor({
                            .StructuralBase = spec.StructuralBase,
                            .Continuation = spec.Continuation,
                            .LineBase = spec.LineBase,
                        });
                    }
                    return document.BeginGroup(spec, compact);
                };
                const auto owner = begin({
                    .Mode = mode,
                    .Indent = 2,
                    .ExpressionContinuation = true,
                    .Continuation = true,
                });
                GapSpec prefix{GapKind::Flat, 0};
                prefix.FlatSourceBegin = 0;
                prefix.FlatSourceEnd = 1;
                document.Gap(prefix, InvalidId, {0, 0, 90});
                text(1, 2, 100);
                document.Gap({GapKind::Flat, 0}, InvalidId, {2, 2, 91});
                text(2, 3, 101);
                text(3, 4, 102);
                for (auto i = 0u; i < repetitions; ++i) {
                    for (auto t = 1u; t < 4u; ++t) {
                        document.Gap(
                            {GapKind::Flat, static_cast<std::uint16_t>(t == 1)},
                            InvalidId,
                            {4, 5});
                        text(t, t + 1, 103 + i * 3 + t);
                    }
                }
                document.Gap({GapKind::Soft, 1}, InvalidId, {4, 5});
                document.ConditionalText(owner, "", ",", {5, 5});
                const auto child = begin({
                    .Mode = GroupMode::Fill,
                    .Indent = 4,
                    .StructuralBase = true,
                    .Continuation = true,
                });
                text(5, 10, 200, true);
                text(10, 11, NoIdentity, true);
                text(11, 12, 201);
                document.EndGroup(child);
                document.Gap({GapKind::Soft, 1}, InvalidId, {12, 13});
                text(13, 17, 202);
                document.EndGroup(owner);
                // A multiline source atom must remain a run boundary.
                text(12, 13, NoIdentity, true);
            };
            fill(packed, true);
            fill(separate, false);
            ASSERT_TRUE(packed.Seal());
            ASSERT_TRUE(separate.Seal());
            for (auto tab : {1u, 4u, 8u}) {
                for (auto width : {3u, 10u, 40u, 80u}) {
                    for (auto maps = 0; maps < 4; ++maps) {
                        const Policy policy{.Width = width,
                            .TabWidth = static_cast<std::uint8_t>(tab),
                            .SourceMap = maps == 0, .TokenMap = maps == 2,
                            .TrackSource = maps == 3
                                ? std::span<const Provenance>(tracked)
                                : std::span<const Provenance>()};
                        const auto a = packed.Render(policy);
                        const auto b = separate.Render(policy);
                        ASSERT_TRUE(a.State);
                        ASSERT_TRUE(b.State);
                        EXPECT_EQ(a.Text, b.Text);
                        EXPECT_EQ(a.Broken, b.Broken);
                        ASSERT_EQ(a.Map.size(), b.Map.size());
                        for (std::size_t i = 0; i < a.Map.size(); ++i) {
                            EXPECT_EQ(
                                a.Map[i].OutputBegin,
                                b.Map[i].OutputBegin);
                            EXPECT_EQ(a.Map[i].OutputEnd, b.Map[i].OutputEnd);
                            EXPECT_EQ(
                                a.Map[i].Source.Begin,
                                b.Map[i].Source.Begin);
                            EXPECT_EQ(a.Map[i].Source.End, b.Map[i].Source.End);
                            EXPECT_EQ(
                                a.Map[i].Source.Identity,
                                b.Map[i].Source.Identity);
                            EXPECT_EQ(a.Map[i].Exact, b.Map[i].Exact);
                            EXPECT_EQ(a.Map[i].Protected, b.Map[i].Protected);
                        }
                    }
                }
            }
        }
    }
}

TEST(GroupLayout, BorrowedFlatWhitespaceSelectionMeasurementAndMapping)
{
    auto borrowed = [] (GapKind kind, std::uint32_t begin, std::uint32_t end) {
        GapSpec spec{kind, 999};
        spec.FlatSourceBegin = begin;
        spec.FlatSourceEnd = end;
        return spec;
    };
    // Packing a flat gap with its source token must retain both provenance
    // records, tab geometry and protection independently. An unused anchor
    // prevents packing, providing the equivalent two-node representation.
    for (auto tab : {1u, 4u, 8u}) {
        for (auto width : {3u, 10u, 40u}) {
            const std::string source = "\tword\nnext";
            Document packed(source);
            Document separate(source);
            const auto fill = [&] (Document& document, bool barrier) {
                const auto owner = document.BeginGroup({GroupMode::Fill, 2});
                document
                    .Gap(borrowed(GapKind::Flat, 0, 1), InvalidId, {0, 1, 9});
                if (barrier) {
                    document.MarkAnchor({});
                }
                document.SourceText(1, 5, 7);
                document.Gap({GapKind::Soft, 1}, InvalidId, {5, 6});
                document.SourceText(6, 10, 8, true);
                document.EndGroup(owner);
            };
            fill(packed, false);
            fill(separate, true);
            ASSERT_TRUE(packed.Seal());
            ASSERT_TRUE(separate.Seal());
            const Policy policy{
                .Width = width,
                .TabWidth = static_cast<uint8_t>(tab),
            };
            const auto a = packed.Render(policy);
            const auto b = separate.Render(policy);
            ASSERT_TRUE(a.State);
            ASSERT_TRUE(b.State);
            EXPECT_EQ(a.Text, b.Text);
            EXPECT_EQ(a.Broken, b.Broken);
            ASSERT_EQ(a.Map.size(), b.Map.size());
            for (size_t i = 0; i < a.Map.size(); ++i) {
                EXPECT_EQ(a.Map[i].OutputBegin, b.Map[i].OutputBegin);
                EXPECT_EQ(a.Map[i].OutputEnd, b.Map[i].OutputEnd);
                EXPECT_EQ(a.Map[i].Source.Begin, b.Map[i].Source.Begin);
                EXPECT_EQ(a.Map[i].Source.End, b.Map[i].Source.End);
                EXPECT_EQ(a.Map[i].Source.Identity, b.Map[i].Source.Identity);
                EXPECT_EQ(a.Map[i].Exact, b.Map[i].Exact);
                EXPECT_EQ(a.Map[i].Protected, b.Map[i].Protected);
            }
        }
    }
    Document mixed(" \t ");
    auto group = mixed.BeginGroup();
    mixed.Text("ab");
    mixed.Gap(borrowed(GapKind::Flat, 0, 3), InvalidId, {0, 3, 9});
    mixed.Text("x");
    auto anchor = mixed.MarkAnchor({AnchorKind::DelimiterColumn, 0, 0});
    mixed.Gap({GapKind::Hard, 0, 1}, anchor);
    mixed.Text("y");
    mixed.EndGroup(group);
    ASSERT_TRUE(mixed.Seal());
    auto output = mixed.Render({.Width = 10, .TabWidth = 4});
    ASSERT_TRUE(output.State);
    EXPECT_EQ(output.Text, "ab \t x\n      y");
    EXPECT_TRUE(output.Map[1].Exact);
    EXPECT_EQ(output.Map[1].Source.Begin, 0u);
    EXPECT_EQ(output.Map[1].Source.End, 3u);
    EXPECT_EQ(output.Map[1].Source.Identity, 9u);

    Document choices(" \t ");
    group = choices.BeginGroup({GroupMode::Consistent, 2});
    choices.Text("a");
    choices.ConditionalGap(
        group,
        borrowed(GapKind::Flat, 3, 3),
        borrowed(GapKind::Soft, 0, 3),
        InvalidId,
        {0, 3, 42});
    choices.Text("b");
    choices.EndGroup(group);
    ASSERT_TRUE(choices.Seal());
    auto flat = choices.Render({.Width = 2});
    EXPECT_EQ(flat.Text, "ab");
    EXPECT_FALSE(flat.Broken[group]);
    EXPECT_TRUE(flat.Map[1].Exact);
    EXPECT_EQ(flat.Map[1].Source.Begin, 3u);
    EXPECT_EQ(flat.Map[1].Source.End, 3u);
    EXPECT_EQ(flat.Map[1].OutputBegin, flat.Map[1].OutputEnd);
    auto broken = choices.Render({.Width = 1});
    EXPECT_EQ(broken.Text, "a\n  b");
    EXPECT_TRUE(broken.Broken[group]);
    EXPECT_FALSE(broken.Map[1].Exact);
    EXPECT_EQ(broken.Map[1].Source.Begin, 0u);
    EXPECT_EQ(broken.Map[1].Source.End, 3u);
    EXPECT_EQ(broken.Map[1].Source.Identity, 42u);

    Document selected(" \t ");
    group = selected.BeginGroup();
    selected.Gap({GapKind::Hard, 0});
    selected.ConditionalGap(
        group,
        borrowed(GapKind::Flat, 0, 1),
        borrowed(GapKind::Flat, 1, 2),
        InvalidId,
        {0, 3, 43});
    selected.Text("b");
    selected.EndGroup(group);
    ASSERT_TRUE(selected.Seal());
    output = selected.Render({.TabWidth = 4});
    EXPECT_EQ(output.Text, "\n\tb");
    EXPECT_TRUE(output.Map[1].Exact);
    EXPECT_EQ(output.Map[1].Source.Begin, 1u);
    EXPECT_EQ(output.Map[1].Source.End, 2u);

    Document fit(" \t ");
    group = fit.BeginGroup();
    fit.Text("ab");
    fit.Gap(borrowed(GapKind::Soft, 0, 3));
    fit.Text("x");
    fit.EndGroup(group);
    ASSERT_TRUE(fit.Seal());
    EXPECT_EQ(fit.Render({.Width = 9, .TabWidth = 4}).Text, "ab \t x");
    EXPECT_EQ(fit.Render({.Width = 8, .TabWidth = 4}).Text, "ab\nx");

    Document fill("   ");
    group = fill.BeginGroup({GroupMode::Fill, 0, true});
    fill.Text("aa");
    fill.Gap(borrowed(GapKind::Soft, 0, 3), InvalidId, {0, 3, 45});
    fill.Text("b");
    fill.Gap();
    fill.Text("c");
    fill.EndGroup(group);
    ASSERT_TRUE(fill.Seal());
    output = fill.Render({.Width = 8});
    EXPECT_EQ(output.Text, "aa   b\nc");
    EXPECT_TRUE(output.Map[1].Exact);

    const std::string large(65536, ' ');
    Document longGap(large);
    longGap.Text("a");
    longGap.Gap(borrowed(GapKind::Flat, 0, 65536), InvalidId, {0, 65536, 44});
    longGap.Text("b");
    ASSERT_TRUE(longGap.Seal());
    output = longGap.Render({.Width = 70000});
    EXPECT_EQ(output.Text.size(), 65538u);
    EXPECT_EQ(
        std::string_view(output.Text).substr(1, 65536),
        std::string_view(large));
    EXPECT_TRUE(output.Map[1].Exact);
}

TEST(GroupLayout, RejectsMalformedGapSourcesAndZeroNewlineCounts)
{
    constexpr std::array<Provenance, 5> badRanges{
        {{InvalidId, 0}, {0, InvalidId}, {4, 3}, {0, 8}, {8, 8}},
    };
    for (auto range : badRanges) {
        Document document(" \t ");
        GapSpec spec{GapKind::Flat, 0};
        spec.FlatSourceBegin = range.Begin;
        spec.FlatSourceEnd = range.End;
        EXPECT_NO_THROW(document.Gap(spec));
        EXPECT_EQ(document.Seal().Code, Error::InvalidSourceRange);
    }
    constexpr std::string_view invalidBytes = "a\n\r\v\xff";
    for (std::uint32_t i = 0; i < invalidBytes.size(); ++i) {
        Document document(invalidBytes);
        GapSpec spec;
        spec.FlatSourceBegin = i;
        spec.FlatSourceEnd = i + 1;
        document.Gap(spec);
        EXPECT_EQ(document.Seal().Code, Error::InvalidSourceRange);
    }
    for (bool badFlat : {false, true}) {
        Document document("\n");
        auto group = document.BeginGroup();
        GapSpec flat{GapKind::Flat, 0};
        GapSpec broken{GapKind::Soft, 0};
        auto& invalid = badFlat ? flat : broken;
        invalid.FlatSourceBegin = 0;
        invalid.FlatSourceEnd = 1;
        document.ConditionalGap(group, flat, broken);
        EXPECT_EQ(document.Seal().Code, Error::InvalidSourceRange);
    }
    for (auto kind : {GapKind::Flat, GapKind::Soft, GapKind::Hard}) {
        Document document("");
        document.Gap({.Kind = kind, .Spaces = 0, .NewlineCount = 0});
        EXPECT_EQ(document.Seal().Code, Error::InvalidPolicy);
    }
    for (bool badFlat : {false, true}) {
        Document document("");
        auto group = document.BeginGroup();
        GapSpec flat{GapKind::Flat, 0};
        GapSpec broken{GapKind::Soft, 0};
        (badFlat ? flat : broken).NewlineCount = 0;
        document.ConditionalGap(group, flat, broken);
        EXPECT_EQ(document.Seal().Code, Error::InvalidPolicy);
    }
}

TEST(GroupLayout, BorrowedIndentAfterBlankLinesPreservesBytesAndBlockBase)
{
    constexpr std::string_view source = "// note\r\n\t ";
    Document document(source);
    auto group = document.BeginGroup({GroupMode::Consistent, 2});
    document.SourceText(0, 8, 40, true);
    GapSpec preserved{.Kind = GapKind::Hard, .Spaces = 0,
        .LineEnding = GapNewline::LF, .NewlineCount = 3};
    preserved.IndentSourceBegin = 9;
    preserved.IndentSourceEnd = 11;
    document.Gap(preserved, InvalidId, {8, 11, 42});
    document.Text("x");
    auto delimiter = document.MarkAnchor({AnchorKind::DelimiterColumn, 0, 0});
    document.Gap({GapKind::Hard, 0, 1}, delimiter);
    document.Text("y");
    auto block = document.MarkAnchor({AnchorKind::BlockRelative, 0, 0});
    document.Gap({GapKind::Hard, 0}, block);
    document.Text("z");
    document.EndGroup(group);
    ASSERT_TRUE(document.Seal());
    auto output = document.Render({
        .Width = 20,
        .TabWidth = 4,
        .LineEnding = Newline::CRLF,
        .Indentation = IndentationKind::Spaces,
    });
    ASSERT_TRUE(output.State);
    EXPECT_EQ(output.Text, "// note\r\n\r\n\r\n\t x\r\n      y\r\n  z");
    EXPECT_TRUE(output.Broken[group]);
    ASSERT_EQ(output.Map.size(), 8u);
    EXPECT_FALSE(output.Map[1].Exact);
    EXPECT_EQ(output.Map[1].Source.Identity, 42u);
    EXPECT_TRUE(output.Map[2].Exact);
    EXPECT_EQ(output.Map[2].Source.Begin, 9u);
    EXPECT_EQ(output.Map[2].Source.End, 11u);
    EXPECT_EQ(output.Map[2].Source.Identity, 42u);
    EXPECT_EQ(output.Map[2].OutputEnd - output.Map[2].OutputBegin, 2u);
    std::size_t end = 0;
    for (const auto& mapping : output.Map) {
        EXPECT_EQ(mapping.OutputBegin, end);
        end = mapping.OutputEnd;
    }
    EXPECT_EQ(end, output.Text.size());

    Document choices("\t ");
    group = choices.BeginGroup({GroupMode::Consistent, 4});
    choices.Text("a");
    GapSpec flat{GapKind::Flat, 1};
    flat.IndentSourceBegin = 0;
    flat.IndentSourceEnd = 2;
    GapSpec broken{GapKind::Soft, 0};
    broken.IndentSourceBegin = 2;
    broken.IndentSourceEnd = 2;
    choices.ConditionalGap(group, flat, broken, InvalidId, {0, 2, 43});
    choices.Text("b");
    choices.EndGroup(group);
    ASSERT_TRUE(choices.Seal());
    EXPECT_EQ(choices.Render({.Width = 3}).Text, "a b");
    auto emptyIndent = choices.Render({.Width = 2});
    EXPECT_EQ(emptyIndent.Text, "a\nb");
    ASSERT_EQ(emptyIndent.Map.size(), 4u);
    EXPECT_FALSE(emptyIndent.Map[1].Exact);
    EXPECT_TRUE(emptyIndent.Map[2].Exact);
    EXPECT_EQ(emptyIndent.Map[2].OutputBegin, emptyIndent.Map[2].OutputEnd);
    EXPECT_EQ(emptyIndent.Map[2].Source.Begin, 2u);
}

TEST(GroupLayout, SourceLineIndentPrefixesAndMappingsGolden)
{
    constexpr std::array<std::string_view, 4> prefixes{
        "\n",
        "\r\n",
        " \t\r\n\t\n\n",
        "\n",
    };
    std::string actual;
    for (std::size_t index = 0; index < prefixes.size(); ++index) {
        const bool commentCR = index == prefixes.size() - 1;
        std::string source = commentCR ? "// note\r" : "";
        const auto begin = static_cast<std::uint32_t>(source.size());
        source += prefixes[index];
        const auto end = static_cast<std::uint32_t>(source.size());
        source += " \t ";
        for (auto indent : {0, 6}) {
            Document document(source);
            auto group = document.BeginGroup({GroupMode::Fill, indent});
            if (commentCR) {
                document.SourceText(0, begin, 100, true);
            } else {
                document.Text("head");
            }
            document
                .SourceLineIndent(begin, end, end, end + 3, InvalidId, 0, 101);
            document.Text("x");
            document.EndGroup(group);
            ASSERT_TRUE(document.Seal());
            for (std::uint8_t tabWidth : {1, 4, 32}) {
                for (auto kind :
                    {IndentationKind::Spaces, IndentationKind::Tabs})
                {
                    Policy policy{.Width = 80,
                        .TabWidth = tabWidth,
                        .LineEnding = Newline::CRLF,
                        .Indentation = kind,
                    };
                    auto output = document.Render(policy);
                    ASSERT_TRUE(output.State);
                    EXPECT_TRUE(output.Broken[group]);
                    ASSERT_EQ(output.Map.size(), 4u);
                    const auto mapped = [&] (std::size_t i) {
                        const auto& map = output.Map[i];
                        return std::string_view(output.Text).substr(
                            map.OutputBegin,
                            map.OutputEnd - map.OutputBegin);
                    };
                    EXPECT_EQ(mapped(1), prefixes[index]);
                    EXPECT_TRUE(output.Map[1].Exact);
                    EXPECT_TRUE(output.Map[1].Protected);
                    EXPECT_EQ(output.Map[1].Source.Begin, begin);
                    EXPECT_EQ(output.Map[1].Source.End, end);
                    EXPECT_EQ(output.Map[1].Source.Identity, 101u);
                    EXPECT_EQ(
                        ExpandTabs(mapped(2), tabWidth),
                        std::string(indent, ' '));
                    EXPECT_FALSE(output.Map[2].Exact);
                    EXPECT_FALSE(output.Map[2].Protected);
                    EXPECT_EQ(output.Map[2].Source.Begin, end);
                    EXPECT_EQ(output.Map[2].Source.End, end + 3);
                    EXPECT_EQ(output.Map[2].Source.Identity, NoIdentity);
                    EXPECT_EQ(
                        mapped(2).find_first_of("\r\n"),
                        std::string_view::npos);
                    if (indent == 0) {
                        EXPECT_EQ(
                            output.Map[2].OutputBegin,
                            output.Map[2].OutputEnd);
                    }
                    std::size_t mappedEnd = 0;
                    for (const auto& map : output.Map) {
                        EXPECT_EQ(map.OutputBegin, mappedEnd);
                        EXPECT_LE(map.OutputBegin, map.OutputEnd);
                        mappedEnd = map.OutputEnd;
                    }
                    EXPECT_EQ(mappedEnd, output.Text.size());
                    if (commentCR) {
                        EXPECT_EQ(
                            std::string_view(output.Text).substr(0, end),
                            std::string_view(source).substr(0, end));
                        EXPECT_TRUE(output.Map[0].Exact);
                        EXPECT_EQ(output.Map[0].Source.Identity, 100u);
                        EXPECT_EQ(
                            output.Text.find("\r\r\n"),
                            std::string_view::npos);
                    }
                    auto repeated = document.Render(policy);
                    EXPECT_EQ(repeated.Text, output.Text);
                    EXPECT_EQ(repeated.Broken, output.Broken);
                    policy.SourceMap = false;
                    auto unmapped = document.Render(policy);
                    EXPECT_EQ(unmapped.Text, output.Text);
                    EXPECT_EQ(unmapped.Broken, output.Broken);
                    EXPECT_TRUE(unmapped.Map.empty());
                    actual += "prefix=" + std::to_string(index) + " indent=" +
                        std::to_string(indent) + " tab=" +
                        std::to_string(tabWidth);
                    actual += kind == IndentationKind::Tabs
                        ? " tabs:\n"
                        : " spaces:\n";
                    actual.append(output.Text);
                    actual += '\n';
                }
            }
        }
    }
    test::golden::ExpectGolden(
        std::filesystem::path(DOCUMENT_TESTDATA_DIR) /
        "source-line-indent.output.txt",
        actual);
}

TEST(GroupLayout,
    SourceLineIndentCountsRenderedIndentAndSameLineSuffixInChildFit)
{
    Document document("\n\t ");
    auto parent = document.BeginGroup({GroupMode::Fill, 4});
    document.Text("head");
    document.SourceLineIndent(0, 1, 1, 3);
    auto child = document.BeginGroup({GroupMode::Fill, 0});
    document.Text("aa");
    document.Gap();
    document.Text("bb");
    document.EndGroup(child);
    document.Text(")");
    document.EndGroup(parent);
    document.Text(";");
    ASSERT_TRUE(document.Seal());
    ExpectText(document, 11, "head\n    aa bb);");
    ExpectText(document, 10, "head\n    aa\n    bb);");
    auto equal = document.Render({.Width = 11});
    EXPECT_TRUE(equal.Broken[parent]);
    EXPECT_FALSE(equal.Broken[child]);
    EXPECT_TRUE(document.Render({.Width = 10}).Broken[child]);
    auto tabs = document.Render(
        {.Width = 10, .TabWidth = 4, .Indentation = IndentationKind::Tabs});
    EXPECT_EQ(tabs.Text, "head\n\taa\n\tbb);");
    EXPECT_TRUE(tabs.Broken[child]);
}

TEST(GroupLayout,
    SourceLineIndentFixedChildShapeContributesBrokenPrefixToOwnerFit)
{
    for (bool nested : {false, true}) {
        Document document("\n");
        auto parent = document.BeginGroup({GroupMode::Fill, 0});
        document.Text("aa");
        document.Gap();
        GroupId wrapper = InvalidId;
        if (nested) {
            wrapper = document.BeginGroup({GroupMode::Fill, 0});
        }
        auto child = document.BeginGroup({GroupMode::Fill, 4});
        document.ConditionalText(child, "", "bbbb");
        document.SourceLineIndent(0, 1, 1, 1);
        document.Text("x");
        document.EndGroup(child);
        if (nested) {
            document.EndGroup(wrapper);
        }
        document.EndGroup(parent);
        ASSERT_TRUE(document.Seal());
        ExpectText(document, 7, "aa bbbb\n    x");
        ExpectText(document, 5, "aa\nbbbb\n    x");
        auto output = document.Render({.Width = 5});
        EXPECT_TRUE(output.Broken[parent]);
        EXPECT_TRUE(output.Broken[child]);
        if (nested) {
            EXPECT_TRUE(output.Broken[wrapper]);
        }
    }
}

TEST(GroupLayout, SourceLineIndentForceBreakStillRequiresEditableOwnedBoundary)
{
    Document document("\n");
    auto parent = document.BeginGroup({GroupMode::Fill, 2, true});
    document.SourceLineIndent(0, 1, 1, 1);
    document.Text("aa");
    document.Gap();
    document.Text("bb");
    document.Gap();
    document.Text("cc");
    document.EndGroup(parent);
    ASSERT_TRUE(document.Seal());
    // Borrowing made the group multiline, but cannot satisfy Always. Fill must
    // use the last editable owned gap even though it would otherwise fit.
    ExpectText(document, 80, "\n  aa bb\n  cc");
    ASSERT_EQ(document.Render().Map.size(), 7u);
    EXPECT_FALSE(document.Render().Map[5].Exact);

    Document earlierBoundary("\n");
    parent = earlierBoundary.BeginGroup({GroupMode::Fill, 2, true});
    earlierBoundary.Text("aa");
    earlierBoundary.Gap();
    earlierBoundary.Text("bb");
    earlierBoundary.SourceLineIndent(0, 1, 1, 1);
    earlierBoundary.Text("cc");
    earlierBoundary.EndGroup(parent);
    ASSERT_TRUE(earlierBoundary.Seal());
    // The borrowed node cannot displace an earlier editable OwnedLastBreak.
    ExpectText(earlierBoundary, 80, "aa\n  bb\n  cc");

    Document satisfied("\n");
    parent = satisfied.BeginGroup({GroupMode::Fill, 2, true});
    satisfied.Gap({GapKind::Hard, 0});
    satisfied.Text("head");
    satisfied.SourceLineIndent(0, 1, 1, 1);
    satisfied.Text("aa");
    satisfied.Gap();
    satisfied.Text("bb");
    satisfied.EndGroup(parent);
    ASSERT_TRUE(satisfied.Seal());
    ExpectText(satisfied, 80, "\n  head\n  aa bb");

    Document childOnly("\n");
    parent = childOnly.BeginGroup({GroupMode::Fill, 2, true});
    auto child = childOnly.BeginGroup({GroupMode::Fill, 2});
    childOnly.SourceLineIndent(0, 1, 1, 1);
    childOnly.Text("aa");
    childOnly.Gap();
    childOnly.Text("bb");
    childOnly.EndGroup(child);
    childOnly.Gap();
    childOnly.Text("cc");
    childOnly.EndGroup(parent);
    ASSERT_TRUE(childOnly.Seal());
    ExpectText(childOnly, 80, "\n    aa bb\n  cc");
}

TEST(GroupLayout, SourceLineIndentCannotInventEditableBreakWithoutOwnedSite)
{
    Document document("\n");
    auto parent = document.BeginGroup({GroupMode::Fill, 2, true});
    auto child = document.BeginGroup({GroupMode::Fill, 2});
    document.SourceLineIndent(0, 1, 1, 1);
    document.Text("aa");
    document.Gap();
    document.Text("bb");
    document.EndGroup(child);
    document.ConditionalText(parent, "flat", "broken");
    document.EndGroup(parent);
    ASSERT_TRUE(document.Seal());
    auto output = document.Render();
    EXPECT_EQ(output.Text, "\n    aa bbbroken");
    EXPECT_TRUE(output.Broken[parent]);
    EXPECT_TRUE(output.Broken[child]);

    Document fixedOnly("\n");
    parent = fixedOnly.BeginGroup({GroupMode::Fill, 0, true});
    fixedOnly.SourceLineIndent(0, 1, 1, 1);
    fixedOnly.EndGroup(parent);
    ASSERT_TRUE(fixedOnly.Seal());
    output = fixedOnly.Render({.Width = 1});
    EXPECT_EQ(output.Text, "\n");
    EXPECT_TRUE(output.Broken[parent]);
    ASSERT_EQ(output.Map.size(), 2u);
    EXPECT_TRUE(output.Map[0].Exact);
    EXPECT_FALSE(output.Map[1].Exact);
    EXPECT_FALSE(output.Map[1].Protected);
    EXPECT_EQ(output.Map[1].OutputBegin, output.Map[1].OutputEnd);

    Document empty("");
    parent = empty.BeginGroup({GroupMode::Fill, 0, true});
    empty.EndGroup(parent);
    ASSERT_TRUE(empty.Seal());
    EXPECT_TRUE(empty.Render().Text.empty());
    EXPECT_FALSE(empty.Render().Broken[parent]);
}

TEST(GroupLayout, SourceLineIndentRejectsMalformedRangesPrefixesAndAnchors)
{
    constexpr std::array<Provenance, 6> badRanges{
        {
            {InvalidId, InvalidId},
            {InvalidId, 0},
            {0, InvalidId},
            {2, 1},
            {0, 4},
            {4, 4},
        },
    };
    for (auto range : badRanges) {
        for (bool badPrefix : {false, true}) {
            Document document("\n \t");
            EXPECT_NO_THROW(document.SourceLineIndent(
                badPrefix ? range.Begin : 0,
                badPrefix ? range.End : 1,
                badPrefix ? 1 : range.Begin,
                badPrefix ? 3 : range.End));
            EXPECT_EQ(document.Seal().Code, Error::InvalidSourceRange);
            EXPECT_TRUE(document.Render().Text.empty());
        }
    }
    for (auto prefix : {"", " ", "\n\t", "\r", "\n\r", "x\n", "\v\n"}) {
        Document document(prefix);
        const auto end =
            static_cast<std::uint32_t>(std::string_view(prefix).size());
        document.SourceLineIndent(0, end, end, end);
        EXPECT_EQ(document.Seal().Code, Error::InvalidLinePrefix);
        EXPECT_EQ(document.Render().State.Code, Error::InvalidLinePrefix);
    }
    Document horizontal("\nx");
    horizontal.SourceLineIndent(0, 1, 1, 2);
    EXPECT_EQ(horizontal.Seal().Code, Error::InvalidSourceRange);
    Document newlineIndent("\n");
    newlineIndent.SourceLineIndent(0, 1, 0, 1);
    EXPECT_EQ(newlineIndent.Seal().Code, Error::InvalidSourceRange);
    Document anchor("\n");
    anchor.SourceLineIndent(0, 1, 1, 1, 0);
    anchor.MarkAnchor({});
    EXPECT_EQ(anchor.Seal().Code, Error::InvalidAnchor);
    Document sealed("\n");
    ASSERT_TRUE(sealed.Seal());
    sealed.SourceLineIndent(0, 1, 1, 1);
    EXPECT_EQ(sealed.Render().State.Code, Error::Sealed);

    // A combined node is valid at root or after mid-line text: the fixed prefix
    // itself resets the column. A horizontal-only/out-of-line prefix is not.
    Document root("\n");
    root.Text("head");
    auto block = root.MarkAnchor({AnchorKind::BlockRelative, 2});
    root.SourceLineIndent(0, 1, 1, 1, block);
    root.Text("x");
    ASSERT_TRUE(root.Seal());
    ExpectText(root, 80, "head\n  x");
}

TEST(GroupLayout, RejectsMalformedBorrowedIndentEvenWhenFlat)
{
    constexpr std::array<Provenance, 5> ranges{
        {{InvalidId, 0}, {0, InvalidId}, {2, 1}, {0, 4}, {4, 4}},
    };
    for (auto range : ranges) {
        Document document(" \t ");
        GapSpec spec{GapKind::Flat, 0};
        spec.IndentSourceBegin = range.Begin;
        spec.IndentSourceEnd = range.End;
        EXPECT_NO_THROW(document.Gap(spec));
        EXPECT_EQ(document.Seal().Code, Error::InvalidSourceRange);
    }
    for (bool badFlat : {false, true}) {
        Document document("\n");
        auto group = document.BeginGroup();
        GapSpec flat{GapKind::Flat, 0};
        GapSpec broken{GapKind::Soft, 0};
        auto& bad = badFlat ? flat : broken;
        bad.IndentSourceBegin = 0;
        bad.IndentSourceEnd = 1;
        document.ConditionalGap(group, flat, broken);
        EXPECT_EQ(document.Seal().Code, Error::InvalidSourceRange);
    }
}

TEST(GroupLayout, ForceBreakRequiresOwnedBoundaryDespiteMultilineChild)
{
    Document document("");
    auto parent = document.BeginGroup({GroupMode::Fill, 0, true});
    document.Text("before");
    auto child = document.BeginGroup();
    document.Verbatim("x\ny");
    document.EndGroup(child);
    document.Gap();
    document.Text("after");
    document.EndGroup(parent);
    ASSERT_TRUE(document.Seal());
    ExpectText(document, 80, "beforex\ny\nafter");

    Document satisfied("");
    parent = satisfied.BeginGroup({GroupMode::Fill, 0, true});
    satisfied.Gap({GapKind::Hard, 0});
    child = satisfied.BeginGroup();
    satisfied.Verbatim("x\ny");
    satisfied.EndGroup(child);
    satisfied.Gap();
    satisfied.Text("after");
    satisfied.EndGroup(parent);
    ASSERT_TRUE(satisfied.Seal());
    ExpectText(satisfied, 80, "\nx\ny after");

    Document noOwnedSites("");
    parent = noOwnedSites.BeginGroup({GroupMode::Fill, 0, true});
    child = noOwnedSites.BeginGroup();
    noOwnedSites.Text("a");
    noOwnedSites.Gap();
    noOwnedSites.Text("b");
    noOwnedSites.EndGroup(child);
    noOwnedSites.EndGroup(parent);
    ASSERT_TRUE(noOwnedSites.Seal());
    auto output = noOwnedSites.Render({.Width = 80});
    EXPECT_EQ(output.Text, "a b");
    EXPECT_FALSE(output.Broken[parent]);
    EXPECT_FALSE(output.Broken[child]);
}

TEST(GroupLayout, ConditionalFitFloorReservesWithoutPaddingOrDoubleCount)
{
    Document document("");
    auto group = document.BeginGroup();
    document.Text("a");
    document.Gap();
    document.Text("b");
    document.ConditionalText(group, "", ",", {}, 1);
    document.EndGroup(group);
    document.Text(";");
    ASSERT_TRUE(document.Seal());
    ExpectText(document, 5, "a b;");
    ExpectText(document, 4, "a\nb,;");

    Document nested("");
    auto parent = nested.BeginGroup({GroupMode::Fill, 0});
    nested.Text("xx");
    nested.Gap();
    auto child = nested.BeginGroup();
    nested.Text("b");
    nested.ConditionalText(child, ",", ",", {}, 1);
    nested.EndGroup(child);
    nested.EndGroup(parent);
    ASSERT_TRUE(nested.Seal());
    ExpectText(nested, 5, "xx b,");
    ExpectText(nested, 4, "xx\nb,");
    EXPECT_FALSE(nested.Render({.Width = 4}).Broken[child]);

    Document unbreakable("");
    group = unbreakable.BeginGroup({GroupMode::Fill, 0, true});
    unbreakable
        .ConditionalText(group, "abc", "broken", {.Identity = 9}, UINT32_MAX);
    unbreakable.EndGroup(group);
    ASSERT_TRUE(unbreakable.Seal());
    auto output = unbreakable.Render({.Width = 1});
    EXPECT_EQ(output.Text, "abc");
    EXPECT_FALSE(output.Broken[group]);
    EXPECT_EQ(output.Map[0].OutputEnd, 3u);
}

TEST(GroupLayout, AutomaticSuffixCoversNestedAndTerminalFillBoundaries)
{
    Document nested("");
    nested.Text("(");
    auto parent = nested.BeginGroup();
    auto child = nested.BeginGroup({GroupMode::Fill, 0});
    nested.Text("aa,");
    nested.Gap();
    nested.Text("bb");
    nested.EndGroup(child);
    nested.Text(")");
    nested.EndGroup(parent);
    nested.Text(";");
    ASSERT_TRUE(nested.Seal());
    ExpectText(nested, 9, "(aa, bb);");
    ExpectText(nested, 8, "(aa,\nbb);");
    auto flat = nested.Render({.Width = 9});
    EXPECT_FALSE(flat.Broken[parent]);
    EXPECT_FALSE(flat.Broken[child]);

    Document fill("");
    fill.Text("[");
    auto group = fill.BeginGroup({GroupMode::Fill, 1});
    fill.Text("aa,");
    fill.Gap();
    fill.Text("bb,");
    fill.Gap();
    fill.Text("cc");
    fill.EndGroup(group);
    fill.Text("];");
    ASSERT_TRUE(fill.Seal());
    ExpectText(fill, 13, "[aa, bb, cc];");
    ExpectText(fill, 12, "[aa, bb,\n cc];");
}

TEST(GroupLayout, ForceBreakUsesEditableSitesAndPublishesChildShapeGolden)
{
    std::string actual;
    for (auto mode : {GroupMode::Fill, GroupMode::Consistent}) {
        Document document("");
        auto group = document.BeginGroup({mode, 0, true});
        document.Text("aa");
        document.Gap();
        document.Text("bb");
        document.Gap();
        document.Text("cc");
        document.ConditionalText(group, "", ",");
        document.EndGroup(group);
        ASSERT_TRUE(document.Seal());
        auto output = document.Render({.Width = 80});
        EXPECT_TRUE(output.Broken[group]);
        actual += mode == GroupMode::Fill ? "fill:\n" : "consistent:\n";
        actual.append(output.Text);
        actual += '\n';
    }
    Document nested("");
    auto parent = nested.BeginGroup({GroupMode::Fill, 0});
    nested.Text("xx");
    nested.Gap();
    auto child = nested.BeginGroup({GroupMode::Consistent, 0, true});
    nested.ConditionalText(child, "", "yyyy");
    nested.Text("b");
    nested.Gap();
    nested.Text("c");
    nested.EndGroup(child);
    nested.EndGroup(parent);
    ASSERT_TRUE(nested.Seal());
    auto output = nested.Render({.Width = 6});
    EXPECT_TRUE(output.Broken[parent]);
    EXPECT_TRUE(output.Broken[child]);
    actual += "nested:\n";
    actual.append(output.Text);
    actual += '\n';

    Document header("");
    auto owner = header.BeginGroup({GroupMode::Consistent, 0, true});
    header.Text("if (x)");
    header.ConditionalGap(owner, {GapKind::Flat, 1}, {GapKind::Hard, 0});
    header.Text("{");
    header.EndGroup(owner);
    ASSERT_TRUE(header.Seal());
    output = header.Render({.Width = 80});
    EXPECT_TRUE(output.Broken[owner]);
    actual += "header:\n";
    actual.append(output.Text);
    actual += '\n';

    Document protectedOnly("");
    owner = protectedOnly.BeginGroup({GroupMode::Consistent, 0, true});
    protectedOnly.Verbatim("abc");
    protectedOnly.Gap({GapKind::Flat, 1});
    protectedOnly.Text("d");
    protectedOnly.ConditionalText(owner, "", ",");
    protectedOnly.EndGroup(owner);
    ASSERT_TRUE(protectedOnly.Seal());
    output = protectedOnly.Render({.Width = 1});
    EXPECT_FALSE(output.Broken[owner]);
    EXPECT_EQ(output.Text, "abc d");
    actual += "flat-only:\n";
    actual.append(output.Text);
    actual += '\n';

    Document empty("");
    owner = empty.BeginGroup({GroupMode::Fill, 0, true});
    empty.ConditionalText(owner, "", ",", {}, 1);
    empty.EndGroup(owner);
    ASSERT_TRUE(empty.Seal());
    output = empty.Render({.Width = 1});
    EXPECT_FALSE(output.Broken[owner]);
    EXPECT_TRUE(output.Text.empty());
    actual += "empty:\n";
    test::golden::ExpectGolden(
        std::filesystem::path(DOCUMENT_TESTDATA_DIR) / "force-break.output.txt",
        actual);
}

TEST(GroupLayout, FillExpandedShapeTakesALegalBreakEvenWithConservativeTabs)
{
    Document document("");
    auto group = document.BeginGroup({GroupMode::Fill, 0});
    document.Text("a\tb");
    document.Gap();
    document.Text("c");
    document.ConditionalText(group, "", ",");
    document.EndGroup(group);
    ASSERT_TRUE(document.Seal());
    auto output = document.Render({.Width = 7, .TabWidth = 4});
    EXPECT_EQ(output.Text, "a\tb\nc,");
    EXPECT_TRUE(output.Broken[group]);
    ExpectText(document, 20, "a\tb c");
}

TEST(GroupLayout, ShapeRequirementPropagatesOnlyToAnEditableDescendantGap)
{
    Document document("");
    auto parent = document.BeginGroup({GroupMode::Fill, 0});
    auto child = document.BeginGroup({GroupMode::Fill, 0});
    document.Text("a");
    document.Gap();
    document.Text("b");
    document.EndGroup(child);
    document.ConditionalText(parent, "long_suffix", "");
    document.EndGroup(parent);
    ASSERT_TRUE(document.Seal());
    auto output = document.Render({.Width = 5});
    EXPECT_EQ(output.Text, "a\nb");
    EXPECT_TRUE(output.Broken[parent]);
    EXPECT_TRUE(output.Broken[child]);

    Document unbreakable("");
    parent = unbreakable.BeginGroup({GroupMode::Fill, 0});
    unbreakable.Verbatim("a b");
    unbreakable.ConditionalText(parent, "long_suffix", "");
    unbreakable.EndGroup(parent);
    ASSERT_TRUE(unbreakable.Seal());
    output = unbreakable.Render({.Width = 5});
    EXPECT_EQ(output.Text, "a blong_suffix");
    EXPECT_FALSE(output.Broken[parent]);
}

TEST(GroupLayout, HardFlatGapsAndLongAtoms)
{
    Document document("");
    auto group = document.BeginGroup();
    document.Text("unbreakable");
    auto anchor =
        document.MarkAnchor({AnchorKind::DelimiterColumn, 0, 0, UINT32_MAX});
    // A flat gap ignores alignment demands even when they can never fit.
    document.Gap({GapKind::Flat, 2, UINT32_MAX}, anchor);
    document.Text("x");
    document.EndGroup(group);
    ASSERT_TRUE(document.Seal());
    auto output = document.Render({.Width = 1});
    EXPECT_EQ(output.Text, "unbreakable  x");
    EXPECT_FALSE(output.Broken[group]);
}

TEST(GroupLayout, RejectsMalformedDocumentsAndDependencyCycles)
{
    Document unsealed("");
    EXPECT_EQ(unsealed.Render().State.Code, Error::Unsealed);
    Document nesting("");
    auto parent = nesting.BeginGroup();
    nesting.BeginGroup();
    nesting.EndGroup(parent);
    EXPECT_EQ(nesting.Seal().Code, Error::UnbalancedGroup);
    EXPECT_EQ(nesting.Render().State.Code, Error::UnbalancedGroup);
    Document ancestor("");
    parent = ancestor.BeginGroup();
    ancestor.BeginGroup();
    ancestor.ConditionalText(parent, "x", "y");
    EXPECT_EQ(ancestor.Seal().Code, Error::InvalidConditionalDependency);
    Document external("");
    parent = external.BeginGroup();
    external.EndGroup(parent);
    external.ConditionalText(parent, "x", "y");
    EXPECT_EQ(external.Seal().Code, Error::InvalidConditionalDependency);
    Document invalid("");
    invalid.ConditionalText(42, "x", "y");
    EXPECT_EQ(invalid.Seal().Code, Error::InvalidGroup);
    Document anchor("");
    anchor.Gap({}, 42);
    EXPECT_EQ(anchor.Seal().Code, Error::InvalidAnchor);
    Document sharedAnchor("");
    sharedAnchor.MarkAnchor({.SharedWith = 0});
    EXPECT_EQ(sharedAnchor.Seal().Code, Error::InvalidAnchor);
    Document range("abc");
    range.SourceText(0, 4);
    EXPECT_EQ(range.Seal().Code, Error::InvalidSourceRange);
    Document multiline("");
    multiline.Text("a\nb");
    EXPECT_EQ(multiline.Seal().Code, Error::MultilineText);
    Document incomplete("");
    incomplete.BeginGroup();
    EXPECT_EQ(incomplete.Seal().Code, Error::UnbalancedGroup);
    Document unstable("");
    parent = unstable.BeginGroup();
    unstable.ConditionalGap(parent, {GapKind::Hard, 0}, {GapKind::Flat, 0});
    EXPECT_EQ(unstable.Seal().Code, Error::InvalidConditionalGap);
    Document softFlat("");
    parent = softFlat.BeginGroup();
    softFlat.ConditionalGap(parent, {GapKind::Soft, 0}, {GapKind::Flat, 0});
    EXPECT_EQ(softFlat.Seal().Code, Error::InvalidConditionalGap);
    Document protectedMismatch("source");
    protectedMismatch.Verbatim("changed", {0, 6, 4});
    EXPECT_EQ(protectedMismatch.Seal().Code, Error::InvalidSourceRange);
    Document sealed("");
    ASSERT_TRUE(sealed.Seal());
    sealed.Text("late");
    EXPECT_EQ(sealed.Render().State.Code, Error::Sealed);
}

TEST(GroupLayout, SourceTextRejectsSentinelOffsetsWithoutThrowing)
{
    constexpr std::array<Provenance, 3> ranges{
        {{InvalidId, InvalidId}, {InvalidId, 0}, {0, InvalidId}},
    };
    for (auto source : {std::string_view{}, std::string_view{"abc"}}) {
        for (bool verbatim : {false, true}) {
            for (auto emit :
                {&Document::SourceText, &Document::AppendSourceText})
            {
                for (auto range : ranges) {
                    Document document(source);
                    EXPECT_NO_THROW((document.*emit)(
                        range.Begin, range.End, NoIdentity, verbatim));
                    document.Text("later");
                    EXPECT_EQ(document.Seal().Code, Error::InvalidSourceRange);
                    auto output = document.Render();
                    EXPECT_EQ(output.State.Code, Error::InvalidSourceRange);
                    EXPECT_TRUE(output.Text.empty());
                }
            }
        }
    }
    // Paired sentinels remain valid generated provenance, and an ordinary
    // empty source slice is valid: neither should share SourceText's rejection.
    Document generated("");
    generated.Text("generated", {InvalidId, InvalidId, 7});
    generated.SourceText(0, 0);
    generated.Verbatim("opaque", {InvalidId, InvalidId, 8});
    ASSERT_TRUE(generated.Seal());
    ExpectText(generated, 80, "generatedopaque");
}

TEST(GroupLayout, ValidatesWidthsAndTabPolicy)
{
    Document document("");
    ASSERT_TRUE(document.Seal());
    EXPECT_EQ(document.Render({.Width = 0}).State.Code, Error::InvalidPolicy);
    EXPECT_EQ(
        document.Render({.Width = UINT32_MAX}).State.Code,
        Error::InvalidPolicy);
    EXPECT_EQ(
        document.Render({.TabWidth = 0}).State.Code,
        Error::InvalidPolicy);
    EXPECT_EQ(
        document.Render({.TabWidth = 33}).State.Code,
        Error::InvalidPolicy);
    EXPECT_EQ(
        document.Render({.LineEnding = static_cast<Newline>(2)}).State.Code,
        Error::InvalidPolicy);
    EXPECT_EQ(
        document.Render({.Indentation = static_cast<IndentationKind>(2)}).State
            .Code,
        Error::InvalidPolicy);
    Document invalidMode("");
    invalidMode.BeginGroup({static_cast<GroupMode>(2), 0});
    EXPECT_EQ(invalidMode.Seal().Code, Error::InvalidPolicy);
    EXPECT_TRUE(document.Render({.Width = UINT32_MAX - 1}).State);
}

class CountingResource
    : public std::pmr::memory_resource
{
public:
    std::size_t Allocations = 0;
    std::size_t Bytes = 0;
    std::size_t Peak = 0;

private:
    void* do_allocate(std::size_t bytes, std::size_t alignment) override
    {
        ++Allocations;
        Bytes += bytes;
        Peak = std::max(Peak, Bytes);
        return std::pmr::new_delete_resource()->allocate(bytes, alignment);
    }

    void do_deallocate(
        void* pointer,
        std::size_t bytes,
        std::size_t alignment) override
    {
        Bytes -= bytes;
        std::pmr::new_delete_resource()->deallocate(pointer, bytes, alignment);
    }

    bool do_is_equal(
        const std::pmr::memory_resource& other) const noexcept override
    {
        return this == &other;
    }
};

TEST(GroupLayout, DeepNestingUsesExplicitStacksAndSublinearAllocationCount)
{
    constexpr std::uint32_t depth = 100000;
    CountingResource resource;
    for (unsigned shape : {0u, 1u, 2u}) {
        const auto allocations = resource.Allocations;
        Document document("\n", &resource);
        for (std::uint32_t i = 0; i < depth; ++i) {
            document.BeginGroup(
                {shape == 2 ? GroupMode::Fill : GroupMode::Consistent});
        }
        document.Text("leaf");
        if (shape == 1) {
            document.SourceLineIndent(0, 1, 1, 1);
        } else {
            document.Gap({shape == 2 ? GapKind::Soft : GapKind::Hard, 0});
        }
        document.Text("tail");
        for (std::uint32_t i = depth; i != 0; --i) {
            document.EndGroup(i - 1);
        }
        ASSERT_TRUE(document.Seal());
        auto output =
            document.Render({.Width = 6, .SourceMap = false}, &resource);
        ASSERT_TRUE(output.State);
        EXPECT_EQ(output.Text, "leaf\ntail");
        EXPECT_EQ(output.Broken.size(), depth);
        EXPECT_TRUE(std::all_of(
            output.Broken.begin(),
            output.Broken.end(),
            [] (auto broken) { return broken != 0; }));
        EXPECT_TRUE(output.Map.empty());
        EXPECT_LT(resource.Allocations - allocations, 200u);
        EXPECT_LT(resource.Peak, 128u * 1024u * 1024u);
    }
    EXPECT_EQ(resource.Bytes, 0u);
}

TEST(GroupLayout, DecisionsDescribeActualMultilineAcrossNestedShapes)
{
    std::uint32_t seed = 12345;
    auto next = [&] {
        seed = seed * 1664525u + 1013904223u;
        return seed;
    };
    for (std::uint32_t trial = 0; trial < 200; ++trial) {
        Document document("\n");
        document.Reserve(700, 100);
        std::vector<GroupId> open;
        for (std::uint32_t item = 0; item < 100; ++item) {
            auto group = document.BeginGroup(
                {next() & 1 ? GroupMode::Fill : GroupMode::Consistent, 0});
            open.push_back(group);
            document.Text("", {InvalidId, InvalidId, group * 2u});
            document.Text(next() & 1 ? "a\tb" : "short");
            document.Gap({static_cast<GapKind>(next() % 3), 1});
            document.Text("x");
            document
                .ConditionalText(group, next() & 1 ? "long_suffix" : "", ",");
            if (next() % 7 == 0) {
                if (trial % 2 == 0) {
                    document.Verbatim("opaque\nbytes");
                } else {
                    document.SourceLineIndent(0, 1, 1, 1);
                }
            }
            while (!open.empty() && next() % 3 != 0) {
                group = open.back();
                document.Text("", {InvalidId, InvalidId, group * 2u + 1u});
                document.EndGroup(group);
                open.pop_back();
            }
        }
        while (!open.empty()) {
            const auto group = open.back();
            document.Text("", {InvalidId, InvalidId, group * 2u + 1u});
            document.EndGroup(group);
            open.pop_back();
        }
        ASSERT_TRUE(document.Seal());
        auto output =
            document.Render({.Width = 5 + next() % 30, .TabWidth = 4});
        ASSERT_TRUE(output.State);
        std::vector<std::size_t> boundaries(200);
        std::size_t mappedEnd = 0;
        for (const auto& mapping : output.Map) {
            EXPECT_EQ(mapping.OutputBegin, mappedEnd);
            EXPECT_LE(mapping.OutputBegin, mapping.OutputEnd);
            mappedEnd = mapping.OutputEnd;
            if (mapping.Source.Identity != NoIdentity) {
                boundaries[mapping.Source.Identity] = mapping.OutputBegin;
            }
        }
        EXPECT_EQ(mappedEnd, output.Text.size());
        for (std::size_t group = 0; group < output.Broken.size(); ++group) {
            const auto view = std::string_view(output.Text).substr(
                boundaries[group * 2],
                boundaries[group * 2 + 1] - boundaries[group * 2]);
            EXPECT_EQ(
                output.Broken[group] != 0,
                view.find_first_of("\r\n") != std::string_view::npos)
                << "trial=" << trial << " group=" << group;
        }
    }
}

TEST(GroupLayout, SaturatesWidthsWithoutRescanningExponentialFlatExpansion)
{
    Document document("");
    constexpr std::uint32_t depth = 66000;
    for (std::uint32_t i = 0; i < depth; ++i) {
        document.BeginGroup();
        document.Text("prefix");
        document.Gap({GapKind::Soft, UINT16_MAX});
    }
    document.Text("leaf");
    for (std::uint32_t i = depth; i != 0; --i) {
        document.EndGroup(i - 1);
    }
    ASSERT_TRUE(document.Seal());
    auto output = document.Render({.Width = 20, .SourceMap = false});
    ASSERT_TRUE(output.State);
    EXPECT_EQ(output.Text.size(), depth * 7u + 4u);
    EXPECT_TRUE(output.Text.ends_with("leaf"));
}

TEST(GroupLayout, SourceWhitespaceStreamsAtomsWithoutOwningTopologyOrTokenRights)
{
    const std::string source = " \n\t\r\n\r \n";
    Document document(source);
    const auto group = document.BeginGroup({GroupMode::Fill, 0, true});
    document.SourceWhitespace(0, source.size(), Newline::CRLF);
    document.EndGroup(group);
    ASSERT_TRUE(document.Seal());
    const auto result = document.Render();
    ASSERT_TRUE(result.State);
    EXPECT_EQ(result.Text, " \r\n\t\r\n\r \r\n");
    EXPECT_EQ(result.Broken[group], 1u); // fixed multiline, not an owned break.
    unsigned changed = 0;
    for (const auto& map : result.Map) {
        const auto before = std::string_view(source)
            .substr(map.Source.Begin, map.Source.End - map.Source.Begin);
        const auto after = std::string_view(result.Text)
            .substr(map.OutputBegin, map.OutputEnd - map.OutputBegin);
        if (map.Exact) {
            EXPECT_EQ(before, after);
        } else {
            EXPECT_EQ(before, "\n");
            EXPECT_EQ(after, "\r\n");
            EXPECT_FALSE(map.Protected);
            ++changed;
        }
    }
    EXPECT_EQ(changed, 2u);
    // Exercise output expansion and reservation past uint16 bounds without
    // atom nodes, per-atom strings or indentation/topology rebuilding.
    const std::string many(70000, '\n');
    Document expanded(many);
    expanded.SourceWhitespace(0, many.size(), Newline::CRLF);
    ASSERT_TRUE(expanded.Seal());
    const auto large = expanded.Render();
    ASSERT_TRUE(large.State);
    ASSERT_EQ(large.Text.size(), 140000u);
    ASSERT_EQ(large.Map.size(), 70000u);
    EXPECT_EQ(large.Map.back().OutputEnd, 140000u);
    EXPECT_EQ(large.Map.back().Source.End, 70000u);
    for (const auto range : {std::pair{0u, 1u}, std::pair{1u, 2u}}) {
        Document half("\r\n");
        half.SourceWhitespace(range.first, range.second, Newline::LF);
        EXPECT_EQ(half.Seal().Code, Error::InvalidSourceRange);
    }
    Document token("text\n");
    token.SourceWhitespace(0, 5, Newline::CRLF);
    EXPECT_EQ(token.Seal().Code, Error::InvalidSourceRange);
}

TEST(GroupLayout, SourceWhitespaceRetainsBareCRAtSourceAndEmittedBoundaries)
{
    for (const std::string source : {"\r\r\n", "\r \r\n"}) {
        Document document(source);
        document.SourceText(0, 1, NoIdentity, true);
        // Second case counts N=1 from source but emits M=0 after omitted space.
        document
            .SourceWhitespace(source.size() - 2, source.size(), Newline::LF);
        ASSERT_TRUE(document.Seal());
        for (const bool maps : {false, true}) {
            const auto result = document.Render({.SourceMap = maps});
            ASSERT_TRUE(result.State);
            EXPECT_EQ(result.Text, "\r\r\n");
            if (maps) {
                ASSERT_EQ(result.Map.size(), 2u);
                EXPECT_TRUE(result.Map[1].Exact);
                EXPECT_EQ(result.Map[1].Source.Begin, source.size() - 2);
                EXPECT_EQ(result.Map[1].Source.End, source.size());
                EXPECT_EQ(result.Map[1].OutputEnd, 3u);
            }
        }
    }
    Document separated("\r \r\n");
    separated.SourceText(0, 2, NoIdentity, true);
    separated.SourceWhitespace(2, 4, Newline::LF);
    ASSERT_TRUE(separated.Seal());
    const auto converted = separated.Render();
    ASSERT_TRUE(converted.State);
    EXPECT_EQ(converted.Text, "\r \n");
    ASSERT_EQ(converted.Map.size(), 2u);
    EXPECT_FALSE(converted.Map[1].Exact);
}

TEST(GroupLayout, SourceWhitespaceZeroChangeLFStillRequiresSafeIncomingBoundary)
{
    for (const bool copySeparator : {false, true}) {
        Document document("\r \n");
        document.SourceText(0, copySeparator ? 2 : 1, NoIdentity, true);
        document.SourceWhitespace(2, 3, Newline::LF);
        ASSERT_TRUE(document.Seal());
        const auto result = document.Render();
        if (copySeparator) {
            ASSERT_TRUE(result.State);
            EXPECT_EQ(result.Text, "\r \n");
            ASSERT_EQ(result.Map.size(), 2u);
            EXPECT_TRUE(result.Map[1].Exact);
        } else {
            EXPECT_EQ(result.State.Code, Error::InvalidSourceRange);
        }
    }
}

TEST(GroupLayout, SourceWhitespaceAlternatingRetentionKeepsLargeBoundsAndExactMaps)
{
    std::string source;
    for (unsigned i = 0; i < 35000; ++i)
        source += "\r\r\n \r\n";
    Document document(source);
    document.SourceWhitespace(0, source.size(), Newline::LF);
    ASSERT_TRUE(document.Seal());
    for (const bool maps : {false, true}) {
        const auto result = document.Render({.SourceMap = maps});
        ASSERT_TRUE(result.State);
        EXPECT_EQ(result.Text.size(), 175000u);
        if (maps) {
            ASSERT_EQ(result.Map.size(), 70000u);
            unsigned changed = 0;
            for (const auto& map : result.Map) {
                EXPECT_LE(map.OutputEnd, result.Text.size());
                const auto before = std::string_view(source).substr(
                    map.Source.Begin,
                    map.Source.End - map.Source.Begin);
                const auto after = std::string_view(result.Text)
                    .substr(map.OutputBegin, map.OutputEnd - map.OutputBegin);
                if (map.Exact) {
                    EXPECT_EQ(before, after);
                } else {
                    EXPECT_EQ(before, "\r\n");
                    EXPECT_EQ(after, "\n");
                    ++changed;
                }
            }
            EXPECT_EQ(changed, 35000u);
        }
    }
}

} // namespace
