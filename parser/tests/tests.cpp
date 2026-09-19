#include <parser/parser.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <format>
#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using parser::Node;
using parser::NodeKind;

const Node* FindChild(const Node& parent, NodeKind kind)
{
    for (auto* child = parent.FirstChild; child; child = child->NextSibling) {
        if (child->Kind == kind) {
            return child;
        }
    }
    return nullptr;
}

size_t CountRoles(const parser::Ast& ast, parser::PunctuationRole role)
{
    size_t count = 0;
    for (size_t i = 0; i < ast.TokenCount(); ++i) {
        count += ast.Role(i) == role;
    }
    return count;
}

std::string ReadFile(const std::string& path)
{
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error(std::format("failed to open {}", path));
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

std::optional<std::string> ReadGoldenFile(const std::string& path)
{
    std::ifstream file(path);
    if (!file.is_open()) {
        return std::nullopt;
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

void SaveFile(const std::string& path, const std::string& contents)
{
    std::ofstream file(path);
    ASSERT_TRUE(file.is_open());
    file << contents;
}

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
    const parser::Ast& ast,
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
        AppendQuoted(output, ast.TokenText(index));
    }
    output.push_back('\n');
}

void AppendSExpression(
    std::string& output, const parser::Ast& ast, const Node& node, size_t depth)
{
    AppendIndent(output, depth);
    output.push_back('(');
    output += parser::GetNodeKindName(node.Kind);

    if (!node.FirstChild) {
        for (auto index = node.Tokens.Begin; index < node.Tokens.End; ++index) {
            output.push_back(' ');
            AppendQuoted(output, ast.TokenText(index));
        }
        output += ")\n";
        return;
    }

    output.push_back('\n');
    auto cursor = node.Tokens.Begin;
    for (auto* child = node.FirstChild; child; child = child->NextSibling) {
        auto childBegin =
            std::clamp(child->Tokens.Begin, node.Tokens.Begin, node.Tokens.End);
        AppendTokens(output, ast, cursor, childBegin, depth + 1);
        AppendSExpression(output, ast, *child, depth + 1);
        cursor = std::max(cursor, std::min(child->Tokens.End, node.Tokens.End));
    }
    AppendTokens(output, ast, cursor, node.Tokens.End, depth + 1);
    AppendIndent(output, depth);
    output += ")\n";
}

std::string Serialize(const parser::Ast& ast)
{
    std::string output;
    AppendSExpression(output, ast, ast.Root(), 0);
    return output;
}

struct GoldenCase
{
    std::string Name;
    bool HasErrors = false;
};

std::ostream& operator<<(std::ostream& output, const GoldenCase& testCase)
{
    return output << testCase.Name;
}

class ParserGoldenTest : public testing::TestWithParam<GoldenCase>
{ };

TEST_P(ParserGoldenTest, MatchesSExpression)
{
    const auto input =
        ReadFile(std::format("./testdata/{}.in", GetParam().Name));
    std::vector<parser::ParseError> errors;
    auto ast = parser::Parse(input, [&](parser::ParseError error) {
        errors.push_back(std::move(error));
    });
    auto actual = Serialize(ast);
    auto outputPath = std::format("./testdata/{}.out", GetParam().Name);
    auto expected = ReadGoldenFile(outputPath);

    if (GetParam().HasErrors) {
        EXPECT_FALSE(errors.empty());
    } else {
        EXPECT_TRUE(errors.empty());
    }

    if (std::getenv("UPDATE_GOLDEN")) {
        SaveFile(outputPath, actual);
    } else if (expected) {
        EXPECT_EQ(actual, *expected);
    } else {
        FAIL() << "missing golden file " << outputPath;
    }
}

INSTANTIATE_TEST_SUITE_P(
    Parser,
    ParserGoldenTest,
    testing::Values(
        GoldenCase{"declarations"},
        GoldenCase{"expressions"},
        GoldenCase{"separated_lists"},
        GoldenCase{"preprocessing"},
        GoldenCase{"modules_and_concepts"},
        GoldenCase{"statements"},
        GoldenCase{"name_classification"},
        GoldenCase{"extensions"},
        GoldenCase{"recovery", true}),
    [](const auto& info) {
        return info.param.Name;
    });

TEST(Parser, AssociatesLeadingTrailingAndDanglingComments)
{
    constexpr std::string_view Source = R"cpp(

// leading declaration
int value; // trailing declaration
void empty()
{

    // dangling in empty body
}
)cpp";

    auto ast = parser::Parse(Source);
    const auto* declaration = FindChild(ast.Root(), NodeKind::Declaration);
    ASSERT_NE(declaration, nullptr);
    ASSERT_NE(declaration->AssociatedTrivia, nullptr);
    ASSERT_NE(declaration->AssociatedTrivia->Leading.First, nullptr);
    ASSERT_NE(declaration->AssociatedTrivia->Trailing.First, nullptr);
    auto* leadingWhitespace = declaration->AssociatedTrivia->Leading.First;
    ASSERT_EQ(leadingWhitespace->Kind, parser::TriviaKind::Whitespace);
    EXPECT_EQ(ast.SourceText(leadingWhitespace->Source), "\n\n");
    ASSERT_NE(leadingWhitespace->Next, nullptr);
    EXPECT_EQ(
        ast.TokenText(leadingWhitespace->Next->Tokens.Begin),
        "// leading declaration");
    EXPECT_EQ(
        ast.TokenText(
            declaration->AssociatedTrivia->Trailing.First->Tokens.Begin),
        "// trailing declaration");

    const auto* function = FindChild(ast.Root(), NodeKind::FunctionDefinition);
    ASSERT_NE(function, nullptr);
    const auto* body = FindChild(*function, NodeKind::CompoundStatement);
    ASSERT_NE(body, nullptr);
    ASSERT_NE(body->AssociatedTrivia, nullptr);
    ASSERT_NE(body->AssociatedTrivia->Dangling.First, nullptr);
    auto* danglingWhitespace = body->AssociatedTrivia->Dangling.First;
    ASSERT_EQ(danglingWhitespace->Kind, parser::TriviaKind::Whitespace);
    EXPECT_EQ(ast.SourceText(danglingWhitespace->Source), "\n\n    ");
    ASSERT_NE(danglingWhitespace->Next, nullptr);
    EXPECT_EQ(
        ast.TokenText(danglingWhitespace->Next->Tokens.Begin),
        "// dangling in empty body");

    size_t intToken = 0;
    size_t semicolonToken = 0;
    for (size_t i = 0; i < ast.TokenCount(); ++i) {
        if (ast.TokenText(i) == "int") {
            intToken = i;
        } else if (ast.TokenText(i) == ";") {
            semicolonToken = i;
        }
    }
    EXPECT_EQ(ast.WhitespaceBefore(intToken), "\n");
    EXPECT_EQ(ast.WhitespaceAfter(semicolonToken), " ");
}

TEST(Parser, ClassifiesContextualPunctuation)
{
    constexpr std::string_view Source = R"cpp(template<class T>
struct Derived : Base<T> {
    int* pointer;
    int function(int value) {
        return (value < 2 ? *pointer : value >> 1);
    }
};
)cpp";

    auto ast = parser::Parse(Source);
    for (size_t i = 0; i < ast.TokenCount(); ++i) {
        auto kind = ast.Kind(i);
        if (kind >= tokenizer::TokenKind::Operator_OpenBrace
            && kind <= tokenizer::TokenKind::Operator_Comma)
        {
            EXPECT_NE(ast.Role(i), parser::PunctuationRole::None);
        }
    }
    EXPECT_GE(CountRoles(ast, parser::PunctuationRole::TemplateDelimiter), 2);
    EXPECT_GE(CountRoles(ast, parser::PunctuationRole::BaseDelimiter), 1);
    EXPECT_GE(CountRoles(ast, parser::PunctuationRole::PointerDeclarator), 1);
    EXPECT_GE(CountRoles(ast, parser::PunctuationRole::ComparisonOperator), 1);
    EXPECT_GE(CountRoles(ast, parser::PunctuationRole::UnaryOperator), 1);
    EXPECT_GE(CountRoles(ast, parser::PunctuationRole::ShiftOperator), 1);
    EXPECT_GE(
        CountRoles(ast, parser::PunctuationRole::ConditionalDelimiter), 2);
    EXPECT_GE(CountRoles(ast, parser::PunctuationRole::BlockDelimiter), 2);
}

} // namespace
