#include <parser/parser.hpp>
#include <tests/golden.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <format>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using parser::Node;

void AppendIndent(std::string& output, size_t depth)
{
    output.append(depth * 2, ' ');
}

void AppendQuoted(std::string& output, std::string_view text)
{
    output.push_back('"');
    for (unsigned char character : text) {
        switch (character) {
        case '\\':
            output += "\\\\";
            break;
        case '"':
            output += "\\\"";
            break;
        case '\n':
            output += "\\n";
            break;
        case '\r':
            output += "\\r";
            break;
        case '\t':
            output += "\\t";
            break;
        default:
            if (character < 0x20) {
                output += std::format("\\x{:02x}", character);
            } else {
                output.push_back(static_cast<char>(character));
            }
        }
    }
    output.push_back('"');
}

void AppendTokens(
    std::string& output,
    const parser::SyntaxTree& tree,
    size_t begin,
    size_t end,
    size_t depth)
{
    if (begin >= end) {
        return;
    }
    AppendIndent(output, depth);
    for (auto index = begin; index < end; ++index) {
        if (index != begin) {
            output.push_back(' ');
        }
        AppendQuoted(output, tree.TokenText(index));
    }
    output.push_back('\n');
}

void AppendTriviaList(
    std::string& output,
    const parser::SyntaxTree& tree,
    const parser::TriviaList& trivia,
    std::string_view name,
    size_t depth)
{
    if (!trivia.First) {
        return;
    }
    AppendIndent(output, depth);
    output.push_back('(');
    output += name;
    output.push_back('\n');
    for (auto* item = trivia.First; item; item = item->Next) {
        AppendIndent(output, depth + 1);
        output += item->Kind == parser::TriviaKind::Comment ? "(comment "
                                                            : "(whitespace ";
        AppendQuoted(output, tree.SourceText(item->Source));
        output += ")\n";
    }
    AppendIndent(output, depth);
    output += ")\n";
}

void AppendSExpression(
    std::string& output,
    const parser::SyntaxTree& tree,
    const Node& node,
    size_t depth)
{
    AppendIndent(output, depth);
    output.push_back('(');
    output += parser::GetNodeKindName(node.Kind);

    auto* trivia = tree.AssociatedTrivia(node);
    if (!node.FirstChild && !trivia) {
        for (auto index = node.Tokens.Begin; index < node.Tokens.End; ++index) {
            output.push_back(' ');
            AppendQuoted(output, tree.TokenText(index));
        }
        output += ")\n";
        return;
    }

    output.push_back('\n');
    if (trivia) {
        AppendTriviaList(
            output,
            tree,
            trivia->Leading,
            "leading-trivia",
            depth + 1);
    }
    auto cursor = node.Tokens.Begin;
    for (auto* child = node.FirstChild; child; child = child->NextSibling) {
        auto childBegin =
            std::clamp(child->Tokens.Begin, node.Tokens.Begin, node.Tokens.End);
        AppendTokens(output, tree, cursor, childBegin, depth + 1);
        AppendSExpression(output, tree, *child, depth + 1);
        cursor = std::max(cursor, std::min(child->Tokens.End, node.Tokens.End));
    }
    AppendTokens(output, tree, cursor, node.Tokens.End, depth + 1);
    if (trivia) {
        AppendTriviaList(
            output,
            tree,
            trivia->Dangling,
            "dangling-trivia",
            depth + 1);
        AppendTriviaList(
            output,
            tree,
            trivia->Trailing,
            "trailing-trivia",
            depth + 1);
    }
    AppendIndent(output, depth);
    output += ")\n";
}

std::string Serialize(const parser::SyntaxTree& tree)
{
    std::string output;
    AppendSExpression(output, tree, tree.Root(), 0);
    return output;
}

std::string_view RoleName(parser::PunctuationRole role)
{
    using enum parser::PunctuationRole;
    switch (role) {
    case None:
        return "none";
    case Generic:
        return "generic";
    case PointerDeclarator:
        return "pointer-declarator";
    case ReferenceDeclarator:
        return "reference-declarator";
    case UnaryOperator:
        return "unary-operator";
    case BinaryOperator:
        return "binary-operator";
    case AssignmentOperator:
        return "assignment-operator";
    case TemplateDelimiter:
        return "template-delimiter";
    case ComparisonOperator:
        return "comparison-operator";
    case ShiftOperator:
        return "shift-operator";
    case LabelDelimiter:
        return "label-delimiter";
    case BaseDelimiter:
        return "base-delimiter";
    case BitFieldDelimiter:
        return "bit-field-delimiter";
    case ConditionalDelimiter:
        return "conditional-delimiter";
    case ConstructorInitializerDelimiter:
        return "constructor-initializer-delimiter";
    case BlockDelimiter:
        return "block-delimiter";
    case InitializerDelimiter:
        return "initializer-delimiter";
    case LambdaDelimiter:
        return "lambda-delimiter";
    case AttributeDelimiter:
        return "attribute-delimiter";
    case RecordDelimiter:
        return "record-delimiter";
    case CallDelimiter:
        return "call-delimiter";
    case DeclarationDelimiter:
        return "declaration-delimiter";
    case CastDelimiter:
        return "cast-delimiter";
    case ConditionDelimiter:
        return "condition-delimiter";
    case GroupingDelimiter:
        return "grouping-delimiter";
    case ListSeparator:
        return "list-separator";
    }
    return "unknown";
}

std::string SerializePunctuationRoles(const parser::SyntaxTree& tree)
{
    std::string output;
    for (size_t index = 0; index < tree.TokenCount(); ++index) {
        auto role = tree.Role(index);
        if (role == parser::PunctuationRole::None) {
            continue;
        }
        output += std::format(
            "{}: {} {}\n",
            index,
            tree.TokenText(index),
            RoleName(role));
    }
    return output;
}

class PaddedInput
{
public:
    explicit PaddedInput(std::string_view input)
        : Size_(input.size())
        , Storage_(input)
    {
        Storage_.append(util::SourcePaddingSize, '\0');
    }

    [[nodiscard]] util::PaddedSourceView View() const
    {
        return {.Data = Storage_.data(), .Size = Size_};
    }

private:
    size_t Size_;
    std::string Storage_;
};

struct GoldenCase
{
    std::string Name;
    bool HasErrors = false;
};

std::ostream& operator<<(std::ostream& output, const GoldenCase& testCase)
{
    return output << testCase.Name;
}

class ParserGoldenTest
    : public testing::TestWithParam<GoldenCase>
{ };

TEST_P(ParserGoldenTest, MatchesSExpression)
{
    const test::golden::Fixture fixture{"./testdata", GetParam().Name};
    const auto input = fixture.InputNamed(".in");
    std::vector<parser::ParseError> errors;
    PaddedInput paddedInput(input);
    auto tree =
        parser::Parse(paddedInput.View(), [&] (parser::ParseError error) {
            errors.push_back(std::move(error));
        });
    auto actual = Serialize(tree);
    if (GetParam().HasErrors) {
        EXPECT_FALSE(errors.empty());
    } else {
        EXPECT_TRUE(errors.empty());
    }

    fixture.Expect(actual, ".out");
}

TEST(ParserGoldenTest, MatchesPunctuationRoles)
{
    const test::golden::Fixture fixture{"./testdata", "punctuation_roles"};
    const auto input = fixture.InputNamed(".in");
    PaddedInput paddedInput(input);
    std::vector<parser::ParseError> errors;
    auto tree =
        parser::Parse(paddedInput.View(), [&] (parser::ParseError error) {
            errors.push_back(std::move(error));
        });
    EXPECT_TRUE(errors.empty());
    auto actual = SerializePunctuationRoles(tree);
    fixture.Expect(actual, ".out");
}

INSTANTIATE_TEST_SUITE_P(
    Parser,
    ParserGoldenTest,
    testing::Values(
        GoldenCase{"comments"},
        GoldenCase{"declarations"},
        GoldenCase{"expressions"},
        GoldenCase{"separated_lists"},
        GoldenCase{"preprocessing"},
        GoldenCase{"modules_and_concepts"},
        GoldenCase{"statements"},
        GoldenCase{"name_classification"},
        GoldenCase{"ambiguity"},
        GoldenCase{"unknown_record"},
        GoldenCase{"ambiguous_call_syntax"},
        GoldenCase{"google_declarators"},
        GoldenCase{"extensions"},
        GoldenCase{"recovery", true}),
    [](const auto& info) {
        return info.param.Name;
    });

TEST(Parser, HandlesDeepCompoundNesting)
{
    std::string source = "void deeply_nested() {";
    constexpr size_t Depth = 300;
    source.append(Depth, '{');
    source += "int value;";
    source.append(Depth + 1, '}');

    size_t errors = 0;
    PaddedInput paddedSource(source);
    auto tree = parser::Parse(paddedSource.View(), [&] (parser::ParseError) {
        ++errors;
    });

    EXPECT_EQ(errors, 0);
    auto outerOpening = std::numeric_limits<parser::TokenIndex>::max();
    for (parser::TokenIndex index = 0; index < tree.TokenCount(); ++index) {
        if (tree.Kind(index) == tokenizer::TokenKind::Operator_OpenBrace) {
            outerOpening = index;
            break;
        }
    }
    ASSERT_NE(outerOpening, std::numeric_limits<parser::TokenIndex>::max());
    auto outerClosing = tree.MatchingDelimiter(outerOpening);
    ASSERT_NE(outerClosing, std::numeric_limits<parser::TokenIndex>::max());
    EXPECT_EQ(
        tree.Kind(outerClosing), tokenizer::TokenKind::Operator_CloseBrace);
    EXPECT_GT(outerClosing - outerOpening, Depth * 2);
}

TEST(Parser, HandlesDeepExpressionNesting)
{
    std::string source = "void deeply_nested() { auto value = ";
    constexpr size_t Depth = 300;
    for (size_t index = 0; index < Depth; ++index) {
        source += "Call(";
    }
    source += "argument";
    source.append(Depth, ')');
    source += "; }";

    size_t errors = 0;
    PaddedInput paddedSource(source);
    auto tree = parser::Parse(paddedSource.View(), [&] (parser::ParseError) {
        ++errors;
    });

    EXPECT_EQ(errors, 0);

    parser::TokenIndex widestPair = 0;
    for (parser::TokenIndex index = 0; index < tree.TokenCount(); ++index) {
        auto matching = tree.MatchingDelimiter(index);
        if (matching != std::numeric_limits<parser::TokenIndex>::max() &&
            matching > index)
        {
            widestPair = std::max(widestPair, matching - index);
        }
    }
    EXPECT_GT(widestPair, Depth * 2);
}

} // namespace
