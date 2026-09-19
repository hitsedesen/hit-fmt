#include <parser/parser.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <fstream>
#include <memory_resource>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
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

size_t CountRoles(const parser::SyntaxTree& tree, parser::PunctuationRole role)
{
    size_t count = 0;
    for (size_t i = 0; i < tree.TokenCount(); ++i) {
        count += tree.Role(i) == role;
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

void AppendSExpression(
    std::string& output,
    const parser::SyntaxTree& tree,
    const Node& node,
    size_t depth)
{
    AppendIndent(output, depth);
    output.push_back('(');
    output += parser::GetNodeKindName(node.Kind);

    if (!node.FirstChild) {
        for (auto index = node.Tokens.Begin; index < node.Tokens.End; ++index) {
            output.push_back(' ');
            AppendQuoted(output, tree.TokenText(index));
        }
        output += ")\n";
        return;
    }

    output.push_back('\n');
    auto cursor = node.Tokens.Begin;
    for (auto* child = node.FirstChild; child; child = child->NextSibling) {
        auto childBegin =
            std::clamp(child->Tokens.Begin, node.Tokens.Begin, node.Tokens.End);
        AppendTokens(output, tree, cursor, childBegin, depth + 1);
        AppendSExpression(output, tree, *child, depth + 1);
        cursor = std::max(cursor, std::min(child->Tokens.End, node.Tokens.End));
    }
    AppendTokens(output, tree, cursor, node.Tokens.End, depth + 1);
    AppendIndent(output, depth);
    output += ")\n";
}

std::string Serialize(const parser::SyntaxTree& tree)
{
    std::string output;
    AppendSExpression(output, tree, tree.Root(), 0);
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
    auto tree = parser::Parse(input, [&](parser::ParseError error) {
        errors.push_back(std::move(error));
    });
    auto actual = Serialize(tree);
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

    auto tree = parser::Parse(Source);
    const auto* declaration = FindChild(tree.Root(), NodeKind::Declaration);
    ASSERT_NE(declaration, nullptr);
    auto* declarationTrivia = tree.AssociatedTrivia(*declaration);
    ASSERT_NE(declarationTrivia, nullptr);
    ASSERT_NE(declarationTrivia->Leading.First, nullptr);
    ASSERT_NE(declarationTrivia->Trailing.First, nullptr);
    auto* leadingWhitespace = declarationTrivia->Leading.First;
    ASSERT_EQ(leadingWhitespace->Kind, parser::TriviaKind::Whitespace);
    EXPECT_EQ(tree.SourceText(leadingWhitespace->Source), "\n\n");
    ASSERT_NE(leadingWhitespace->Next, nullptr);
    EXPECT_EQ(
        tree.TokenText(leadingWhitespace->Next->Tokens.Begin),
        "// leading declaration");
    EXPECT_EQ(
        tree.TokenText(declarationTrivia->Trailing.First->Tokens.Begin),
        "// trailing declaration");

    const auto* function = FindChild(tree.Root(), NodeKind::FunctionDefinition);
    ASSERT_NE(function, nullptr);
    const auto* body = FindChild(*function, NodeKind::CompoundStatement);
    ASSERT_NE(body, nullptr);
    auto* bodyTrivia = tree.AssociatedTrivia(*body);
    ASSERT_NE(bodyTrivia, nullptr);
    ASSERT_NE(bodyTrivia->Dangling.First, nullptr);
    auto* danglingWhitespace = bodyTrivia->Dangling.First;
    ASSERT_EQ(danglingWhitespace->Kind, parser::TriviaKind::Whitespace);
    EXPECT_EQ(tree.SourceText(danglingWhitespace->Source), "\n\n    ");
    ASSERT_NE(danglingWhitespace->Next, nullptr);
    EXPECT_EQ(
        tree.TokenText(danglingWhitespace->Next->Tokens.Begin),
        "// dangling in empty body");

    size_t intToken = 0;
    size_t semicolonToken = 0;
    for (size_t i = 0; i < tree.TokenCount(); ++i) {
        if (tree.TokenText(i) == "int") {
            intToken = i;
        } else if (tree.TokenText(i) == ";") {
            semicolonToken = i;
        }
    }
    EXPECT_EQ(tree.WhitespaceBefore(intToken), "\n");
    EXPECT_EQ(tree.WhitespaceAfter(semicolonToken), " ");

    auto moved = std::move(tree);
    auto* movedTrivia = moved.AssociatedTrivia(*declaration);
    ASSERT_NE(movedTrivia, nullptr);
    EXPECT_EQ(
        moved.TokenText(movedTrivia->Trailing.First->Tokens.Begin),
        "// trailing declaration");

    auto assigned = parser::Parse("int old;");
    assigned = std::move(moved);
    auto* assignedTrivia = assigned.AssociatedTrivia(*declaration);
    ASSERT_NE(assignedTrivia, nullptr);
    EXPECT_EQ(
        assigned.TokenText(assignedTrivia->Trailing.First->Tokens.Begin),
        "// trailing declaration");
}

TEST(SegmentedStorage, IndexesChunksBeforeAndAfterSealing)
{
    constexpr uint16_t ItemsPerChunk = 3;

    struct Chunk
    {
        Chunk* Next = nullptr;
        uint16_t Size = 0;
        std::array<int, ItemsPerChunk> Items;
    };

    std::pmr::monotonic_buffer_resource memory;
    parser::detail::SegmentedStorage<Chunk, ItemsPerChunk> storage(memory);
    for (uint32_t i = 0; i < 8; ++i) {
        auto slot = storage.Append();
        EXPECT_EQ(slot.Index, i);
        EXPECT_EQ(slot.Offset, i % ItemsPerChunk);
        slot.ChunkPtr->Items[slot.Offset] = static_cast<int>(i);
    }

    for (uint32_t i = 0; i < storage.Size(); ++i) {
        auto* chunk = storage.ChunkAtDuringBuild(i / ItemsPerChunk);
        EXPECT_EQ(chunk->Items[i % ItemsPerChunk], static_cast<int>(i));
    }

    storage.Seal();
    auto moved = std::move(storage);
    for (uint32_t i = 0; i < moved.Size(); ++i) {
        auto* chunk = moved.ChunkAt(i / ItemsPerChunk);
        EXPECT_EQ(chunk->Items[i % ItemsPerChunk], static_cast<int>(i));
    }
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

    auto tree = parser::Parse(Source);
    for (size_t i = 0; i < tree.TokenCount(); ++i) {
        auto kind = tree.Kind(i);
        if (kind >= tokenizer::TokenKind::Operator_OpenBrace
            && kind <= tokenizer::TokenKind::Operator_Comma)
        {
            EXPECT_NE(tree.Role(i), parser::PunctuationRole::None);
        }
    }
    EXPECT_GE(CountRoles(tree, parser::PunctuationRole::TemplateDelimiter), 2);
    EXPECT_GE(CountRoles(tree, parser::PunctuationRole::BaseDelimiter), 1);
    EXPECT_GE(CountRoles(tree, parser::PunctuationRole::PointerDeclarator), 1);
    EXPECT_GE(CountRoles(tree, parser::PunctuationRole::ComparisonOperator), 1);
    EXPECT_GE(CountRoles(tree, parser::PunctuationRole::UnaryOperator), 1);
    EXPECT_GE(CountRoles(tree, parser::PunctuationRole::ShiftOperator), 1);
    EXPECT_GE(
        CountRoles(tree, parser::PunctuationRole::ConditionalDelimiter), 2);
    EXPECT_GE(CountRoles(tree, parser::PunctuationRole::BlockDelimiter), 2);
}

TEST(Parser, HandlesDeepCompoundNesting)
{
    std::string source = "void deeply_nested() {";
    constexpr size_t Depth = 300;
    source.append(Depth, '{');
    source += "int value;";
    source.append(Depth + 1, '}');

    size_t errors = 0;
    auto tree = parser::Parse(source, [&](parser::ParseError) {
        ++errors;
    });

    EXPECT_EQ(errors, 0);
    EXPECT_GT(tree.TokenCount(), Depth * 2);
}

} // namespace
