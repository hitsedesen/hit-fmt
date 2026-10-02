#include <parser/operator_info.hpp>
#include <parser/parser.hpp>
#include <tests/golden.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
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
        output += item->Kind == parser::TriviaKind::Comment
            ? "(comment "
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
        case AmbiguousOperator:
            return "ambiguous-operator";
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

class FileTest
    : public testing::TestWithParam<GoldenCase>
{ };

TEST_P(FileTest, Golden)
{
    const test::golden::Fixture fixture{PARSER_TESTDATA_DIR, GetParam().Name};
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

TEST(Parser, StringLiteralSequencesHaveCompleteRanges)
{
    const test::golden::Fixture fixture{PARSER_TESTDATA_DIR, "expressions"};
    const auto source = fixture.InputNamed(".in");
    PaddedInput input(source);
    size_t errors = 0;
    auto tree = parser::Parse(input.View(), [&] (parser::ParseError) {
        ++errors;
    });
    EXPECT_EQ(errors, 0);

    using enum tokenizer::TokenKind;
    size_t stringTokens = 0;
    for (parser::TokenIndex i = 0; i < tree.TokenCount(); ++i) {
        stringTokens += tree.Kind(i) == Literal_String;
    }
    size_t coveredStrings = 0;
    size_t sequences = 0;
    bool foundCommentRange = false;
    std::vector<const Node*> pending{&tree.Root()};
    while (!pending.empty()) {
        const auto* node = pending.back();
        pending.pop_back();
        EXPECT_NE(node->Kind, parser::NodeKind::UnparsedSyntax);
        if (node->Kind == parser::NodeKind::Argument) {
            ASSERT_NE(node->FirstChild, nullptr);
            EXPECT_EQ(node->Tokens.Begin, node->FirstChild->Tokens.Begin);
            EXPECT_EQ(node->Tokens.End, node->FirstChild->Tokens.End);
        }
        if (node->Kind == parser::NodeKind::LiteralExpression &&
            tree.Kind(node->Tokens.Begin) == Literal_String)
        {
            ASSERT_GT(node->Tokens.End, node->Tokens.Begin);
            EXPECT_EQ(tree.Kind(node->Tokens.End - 1), Literal_String);
            size_t strings = 0;
            for (auto i = node->Tokens.Begin; i < node->Tokens.End; ++i) {
                auto kind = tree.Kind(i);
                EXPECT_TRUE(kind == Literal_String || kind == Comment);
                strings += kind == Literal_String;
            }
            coveredStrings += strings;
            sequences += strings > 1;
            auto next = node->Tokens.End;
            while (next < tree.TokenCount() && tree.Kind(next) == Comment) {
                ++next;
            }
            if (next < tree.TokenCount()) {
                EXPECT_NE(tree.Kind(next), Literal_String);
            }
            foundCommentRange |= tree.SourceText(node->Tokens) ==
                "\"first\" /* join */ \"second\" /* again */ \"third\"";
        }
        for (auto* child = node->FirstChild; child; child =
            child->NextSibling) {
            pending.push_back(child);
        }
    }
    EXPECT_GT(sequences, 0);
    EXPECT_EQ(coveredStrings, stringTokens);
    EXPECT_TRUE(foundCommentRange);
}

TEST(Parser, BinaryOperatorMetadataMatchesGrouping)
{
    const test::golden::Fixture fixture{PARSER_TESTDATA_DIR, "expressions"};
    const auto source = fixture.InputNamed(".in");
    PaddedInput input(source);
    size_t errors = 0;
    auto tree = parser::Parse(input.View(), [&] (parser::ParseError) {
        ++errors;
    });
    EXPECT_EQ(errors, 0);

    auto isBinary = [] (const Node& node) {
        return node.Kind == parser::NodeKind::BinaryExpression ||
            node.Kind == parser::NodeKind::AssignmentExpression;
    };
    auto operatorToken = [&] (const Node& node) {
        auto token = node.FirstChild->Tokens.End;
        while (tree.Kind(token) == tokenizer::TokenKind::Comment) {
            ++token;
        }
        return token;
    };
    bool tighterChild = false;
    bool equalLeftChild = false;
    bool equalRightChild = false;
    bool parenthesized = false;
    bool alternativeBinary = false;
    bool unsupportedUnary = false;
    bool supportedUnary = false;
    std::vector<const Node*> pending{&tree.Root()};
    while (!pending.empty()) {
        const auto* node = pending.back();
        pending.pop_back();
        if (isBinary(*node)) {
            auto* left = node->FirstChild;
            ASSERT_NE(left, nullptr);
            auto* right = left->NextSibling;
            ASSERT_NE(right, nullptr);
            ASSERT_EQ(right->NextSibling, nullptr);
            auto token = operatorToken(*node);
            ASSERT_LT(token, right->Tokens.Begin);
            auto info = parser::GetBinaryOperatorInfo(tree.Kind(token));
            EXPECT_GT(info.Precedence, 0);
            EXPECT_EQ(
                node->Kind == parser::NodeKind::AssignmentExpression,
                info.Precedence == 2);
            for (const auto* child : {left, right}) {
                if (isBinary(*child)) {
                    ASSERT_NE(child->FirstChild, nullptr);
                    auto childInfo = parser::GetBinaryOperatorInfo(
                        tree.Kind(operatorToken(*child)));
                    EXPECT_GE(childInfo.Precedence, info.Precedence);
                    tighterChild |= childInfo.Precedence > info.Precedence;
                    if (childInfo.Precedence == info.Precedence) {
                        EXPECT_EQ(info.RightAssociative, child == right);
                        equalLeftChild |= child == left;
                        equalRightChild |= child == right;
                    }
                }
            }
            auto text = tree.TokenText(token);
            alternativeBinary |= text == "and" || text == "or" ||
                text == "bitand" || text == "bitor" || text == "xor" ||
                text == "and_eq" || text == "or_eq" || text == "xor_eq" ||
                text == "not_eq";
        } else if (node->Kind == parser::NodeKind::IdentifierExpression ||
            node->Kind == parser::NodeKind::LiteralExpression ||
            node->Kind == parser::NodeKind::ParenthesizedExpression)
        {
            auto info = parser::GetBinaryOperatorInfo(
                tree.Kind(node->Tokens.Begin));
            EXPECT_EQ(info.Precedence, 0);
            EXPECT_FALSE(info.RightAssociative);
            parenthesized |=
                node->Kind == parser::NodeKind::ParenthesizedExpression;
        } else if (node->Kind == parser::NodeKind::UnaryExpression) {
            auto info = parser::GetBinaryOperatorInfo(
                tree.Kind(node->Tokens.Begin));
            // A supported token can still have a unary rather than binary role.
            supportedUnary |= info.Precedence != 0;
            unsupportedUnary |= info.Precedence == 0;
        }
        for (auto* child = node->FirstChild;
            child;
            child = child->NextSibling)
        {
            pending.push_back(child);
        }
    }
    EXPECT_TRUE(tighterChild);
    EXPECT_TRUE(equalLeftChild);
    EXPECT_TRUE(equalRightChild);
    EXPECT_TRUE(parenthesized);
    EXPECT_TRUE(alternativeBinary);
    EXPECT_TRUE(unsupportedUnary);
    EXPECT_TRUE(supportedUnary);
}

TEST(Parser, MatchesPunctuationRoles)
{
    const test::golden::Fixture fixture{
        PARSER_TESTDATA_DIR, "punctuation_roles"};
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
    FileTest,
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
        GoldenCase{"declarator_syntax"},
        GoldenCase{"qualified_records"},
        GoldenCase{"extensions"},
        GoldenCase{"recovery", true}),
    [] (const auto& info) {
        return info.param.Name;
    });

TEST(Parser, RebindsWhitespaceWithoutReclassifyingTokens)
{
    PaddedInput input("int f() { return value + 1; }\n");
    PaddedInput reformatted("int f()\n{\n  return value + 1;\n}\n");
    auto original = parser::Parse(input.View(), [] (parser::ParseError) {});
    auto rebound = original.RebindWhitespace(reformatted.View());
    ASSERT_TRUE(rebound);
    auto reparsed = parser::Parse(
        reformatted.View(),
        [] (parser::ParseError) {});
    ASSERT_EQ(rebound->TokenCount(), reparsed.TokenCount());
    EXPECT_EQ(&rebound->Root(), &original.Root());
    for (parser::TokenIndex i = 0; i < rebound->TokenCount(); ++i) {
        EXPECT_EQ(rebound->Kind(i), reparsed.Kind(i));
        EXPECT_EQ(rebound->TokenText(i), reparsed.TokenText(i));
        EXPECT_EQ(
            rebound->TokenPosition(i).Line,
            reparsed.TokenPosition(i).Line);
        EXPECT_EQ(
            rebound->TokenPosition(i).Column,
            reparsed.TokenPosition(i).Column);
        EXPECT_EQ(rebound->Role(i), reparsed.Role(i));
        EXPECT_EQ(
            rebound->MatchingDelimiter(i),
            reparsed.MatchingDelimiter(i));
    }

    // Gap edits can update positions without running the lexer again.
    const auto source = input.View().View();
    const auto beforeBrace = source.find(" {");
    const auto beforeReturn = source.find(" return");
    const auto beforeClose = source.find(" }");
    const std::array<parser::WhitespaceEdit, 3> edits{{
        {static_cast<util::SourceOffset>(beforeBrace),
            static_cast<util::SourceOffset>(beforeBrace + 1),
            "\n"},
        {static_cast<util::SourceOffset>(beforeReturn),
            static_cast<util::SourceOffset>(beforeReturn + 1),
            "\n  "},
        {static_cast<util::SourceOffset>(beforeClose),
            static_cast<util::SourceOffset>(beforeClose + 1),
            "\n"},
    }};
    auto fromEdits = original.RebindWhitespaceEdits(reformatted.View(), edits);
    auto fromGaps = original.RebindWhitespaceGaps(reformatted.View());
    ASSERT_TRUE(fromEdits);
    ASSERT_TRUE(fromGaps);
    EXPECT_EQ(&fromGaps->Root(), &original.Root());
    for (parser::TokenIndex i = 0; i < reparsed.TokenCount(); ++i) {
        EXPECT_EQ(fromGaps->Token(i).Text, fromEdits->Token(i).Text);
        EXPECT_EQ(
            fromGaps->TokenPosition(i).Line,
            fromEdits->TokenPosition(i).Line);
        EXPECT_EQ(
            fromGaps->TokenPosition(i).Column,
            fromEdits->TokenPosition(i).Column);
        EXPECT_EQ(fromEdits->TokenText(i), reparsed.TokenText(i));
        EXPECT_EQ(fromEdits->Token(i).Text, reparsed.Token(i).Text);
        EXPECT_EQ(
            fromEdits->TokenPosition(i).Line,
            reparsed.TokenPosition(i).Line);
        EXPECT_EQ(
            fromEdits->TokenPosition(i).Column,
            reparsed.TokenPosition(i).Column);
    }
    const auto expanded = reformatted.View().View();
    const auto beforeOpen = expanded.find(")\n{") + 1;
    const auto beforeBody = expanded.find("{\n  return") + 1;
    const auto beforeEnd = expanded.find(";\n}") + 1;
    const std::array<parser::WhitespaceEdit, 3> mergedEdits{{
        {static_cast<util::SourceOffset>(beforeOpen),
            static_cast<util::SourceOffset>(beforeOpen + 1),
            " "},
        {static_cast<util::SourceOffset>(beforeBody),
            static_cast<util::SourceOffset>(beforeBody + 3),
            " "},
        {static_cast<util::SourceOffset>(beforeEnd),
            static_cast<util::SourceOffset>(beforeEnd + 1),
            " "},
    }};
    auto merged = reparsed.RebindWhitespaceEdits(input.View(), mergedEdits);
    ASSERT_TRUE(merged);
    for (parser::TokenIndex i = 0; i < original.TokenCount(); ++i) {
        EXPECT_EQ(
            merged->TokenPosition(i).Line,
            original.TokenPosition(i).Line);
        EXPECT_EQ(
            merged->TokenPosition(i).Column,
            original.TokenPosition(i).Column);
    }
    PaddedInput joined("int f() { returnvalue + 1; }\n");
    const std::array<parser::WhitespaceEdit, 1> unsafe{{
        {static_cast<util::SourceOffset>(source.find(" value")),
            static_cast<util::SourceOffset>(source.find(" value") + 1),
            ""},
    }};
    EXPECT_FALSE(original.RebindWhitespaceEdits(joined.View(), unsafe));
    EXPECT_FALSE(original.RebindWhitespaceGaps(joined.View()));
    PaddedInput tight("int f() { return value+ 1; }\n");
    const std::array<parser::WhitespaceEdit, 1> safe{{
        {static_cast<util::SourceOffset>(source.find(" + ")),
            static_cast<util::SourceOffset>(source.find(" + ") + 1),
            ""},
    }};
    auto tightRebound = original.RebindWhitespaceEdits(tight.View(), safe);
    ASSERT_TRUE(tightRebound);
    EXPECT_TRUE(original.RebindWhitespaceGaps(tight.View()));
    auto tightParsed = parser::Parse(
        tight.View(),
        [] (parser::ParseError) { });
    for (parser::TokenIndex i = 0; i < tightParsed.TokenCount(); ++i) {
        EXPECT_EQ(
            tightRebound->TokenPosition(i).Column,
            tightParsed.TokenPosition(i).Column);
    }
    PaddedInput changedSpelling("int f()\n{\n  return other + 1;\n}\n");
    EXPECT_FALSE(original.RebindWhitespaceEdits(
        changedSpelling.View(),
        edits));

    PaddedInput unicode("int f() { /* ñ */ return x; }\n");
    PaddedInput unicodeSpaced("int f() { /* ñ */ return    x; }\n");
    auto unicodeTree = parser::Parse(
        unicode.View(),
        [] (parser::ParseError) { });
    auto unicodeSource = unicode.View().View();
    auto gap = unicodeSource.find(" x;");
    const std::array<parser::WhitespaceEdit, 1> unicodeEdit{{
        {static_cast<util::SourceOffset>(gap),
            static_cast<util::SourceOffset>(gap + 1),
            "    "},
    }};
    auto unicodeRebound = unicodeTree.RebindWhitespaceEdits(
        unicodeSpaced.View(),
        unicodeEdit);
    auto unicodeFromGaps = unicodeTree.RebindWhitespaceGaps(
        unicodeSpaced.View());
    ASSERT_TRUE(unicodeRebound);
    ASSERT_TRUE(unicodeFromGaps);
    auto unicodeParsed = parser::Parse(
        unicodeSpaced.View(),
        [] (parser::ParseError) { });
    for (parser::TokenIndex i = 0; i < unicodeParsed.TokenCount(); ++i) {
        EXPECT_EQ(
            unicodeRebound->TokenPosition(i).Line,
            unicodeParsed.TokenPosition(i).Line);
        EXPECT_EQ(
            unicodeRebound->TokenPosition(i).Column,
            unicodeParsed.TokenPosition(i).Column);
        EXPECT_EQ(
            unicodeFromGaps->TokenPosition(i).Column,
            unicodeParsed.TokenPosition(i).Column);
    }

    PaddedInput changedToken("int f() { return another + 1; }\n");
    EXPECT_FALSE(original.RebindWhitespace(changedToken.View()));
    EXPECT_FALSE(original.RebindWhitespaceGaps(changedToken.View()));
    PaddedInput comment("int f() { // hi\n return 1; }\n");
    PaddedInput movedComment("int f() {\n // hi\n return 1; }\n");
    auto commented = parser::Parse(comment.View(), [] (parser::ParseError) {});
    EXPECT_FALSE(commented.RebindWhitespace(movedComment.View()));
    EXPECT_FALSE(commented.RebindWhitespaceGaps(movedComment.View()));
}

TEST(Parser, RebindsMatchingTokensWithChangedComments)
{
    PaddedInput input("void f() { // old  \n    Work();\n}\n");
    PaddedInput changed("void f() { // new\n    Work();\n}\n");
    PaddedInput unsafe("void f() { // new\n    Skip();\n}\n");
    auto original = parser::Parse(input.View(), [] (parser::ParseError) { });
    EXPECT_FALSE(original.RebindWhitespaceGaps(changed.View()));
    auto rebound = original.RebindMatchingTokens(changed.View());
    ASSERT_TRUE(rebound);
    EXPECT_EQ(rebound->TokenCount(), original.TokenCount());
    EXPECT_EQ(rebound->Source(), changed.View().View());
    EXPECT_FALSE(original.RebindMatchingTokens(unsafe.View()));
}

TEST(Parser, ReordersBraceCommentsWithoutReparsingSyntax)
{
    PaddedInput input("void f() { /* note */ Work(); }\n");
    PaddedInput edited("void f() /* note */ { Work(); }\n");
    auto original = parser::Parse(input.View(), [] (parser::ParseError) { });
    auto reparsed = parser::Parse(edited.View(), [] (parser::ParseError) { });
    auto rebound = original.RebindReorderedComments(edited.View());
    ASSERT_TRUE(rebound);
    ASSERT_EQ(rebound->TokenCount(), reparsed.TokenCount());
    for (parser::TokenIndex i = 0; i < rebound->TokenCount(); ++i) {
        EXPECT_EQ(rebound->Kind(i), reparsed.Kind(i));
        EXPECT_EQ(
            rebound->MatchingDelimiter(i),
            reparsed.MatchingDelimiter(i));
    }
    std::vector<std::pair<const parser::Node*, const parser::Node*>>
        nodes{{&rebound->Root(), &reparsed.Root()}};
    while (!nodes.empty()) {
        auto [actual, expected] = nodes.back();
        nodes.pop_back();
        EXPECT_EQ(actual->Kind, expected->Kind);
        EXPECT_EQ(actual->Tokens.Begin, expected->Tokens.Begin);
        EXPECT_EQ(actual->Tokens.End, expected->Tokens.End);
        auto* child = actual->Child();
        auto* other = expected->Child();
        while (child && other) {
            nodes.emplace_back(child, other);
            child = child->NextSibling;
            other = other->NextSibling;
        }
        EXPECT_EQ(child, nullptr);
        EXPECT_EQ(other, nullptr);
    }
    PaddedInput controlInput(
        "void f() { if (x) { /* note */ Work(); } }\n");
    PaddedInput controlEdited(
        "void f() { if (x) /* note */ { Work(); } }\n");
    auto control = parser::Parse(
        controlInput.View(),
        [] (parser::ParseError) { });
    auto controlParsed = parser::Parse(
        controlEdited.View(),
        [] (parser::ParseError) { });
    auto controlRebound = control.RebindReorderedComments(
        controlEdited.View());
    ASSERT_TRUE(controlRebound);
    nodes = {{&controlRebound->Root(), &controlParsed.Root()}};
    while (!nodes.empty()) {
        auto [actual, expected] = nodes.back();
        nodes.pop_back();
        EXPECT_EQ(actual->Kind, expected->Kind);
        EXPECT_EQ(actual->Tokens.Begin, expected->Tokens.Begin);
        EXPECT_EQ(actual->Tokens.End, expected->Tokens.End);
        auto* child = actual->Child();
        auto* other = expected->Child();
        while (child && other) {
            nodes.emplace_back(child, other);
            child = child->NextSibling;
            other = other->NextSibling;
        }
        EXPECT_EQ(child, nullptr);
        EXPECT_EQ(other, nullptr);
    }
    PaddedInput unsafe("void f() /* note */ { Skip(); }\n");
    EXPECT_FALSE(original.RebindReorderedComments(unsafe.View()));
}

TEST(Parser, RebindsOnlyPermutedDirectiveRuns)
{
    PaddedInput input("#include <z>\n#include <a>\nvoid f();\n");
    PaddedInput sorted("#include <a>\n#include <z>\nvoid f();\n");
    PaddedInput changed("#include <a>\n#include <z>\nvoid g();\n");
    auto original = parser::Parse(input.View(), [] (parser::ParseError) { });
    auto rebound = original.RebindSortedIncludes(sorted.View());
    ASSERT_TRUE(rebound);
    ASSERT_EQ(rebound->TokenCount(), original.TokenCount());
    EXPECT_EQ(rebound->TokenText(0), "#include <a>");
    EXPECT_EQ(rebound->TokenText(1), "#include <z>");
    EXPECT_EQ(rebound->Root().Tokens.End, original.Root().Tokens.End);
    EXPECT_FALSE(original.RebindSortedIncludes(changed.View()));
}

TEST(Parser, RemovesNullStatementsWithoutReparsingSyntax)
{
    PaddedInput input("void f() {; int x; }\n");
    PaddedInput edited("void f() { int x; }\n");
    auto original = parser::Parse(input.View(), [] (parser::ParseError) { });
    auto reparsed = parser::Parse(edited.View(), [] (parser::ParseError) { });
    parser::TokenIndex removed = original.TokenCount();
    for (parser::TokenIndex i = 0; i < original.TokenCount(); ++i) {
        if (original.Kind(i) == tokenizer::TokenKind::Operator_Semicolon &&
            original.Token(i).Text.Begin == input.View().View().find("{;") + 1)
        {
            removed = i;
            break;
        }
    }
    ASSERT_LT(removed, original.TokenCount());
    const std::array deleted{removed};
    auto rebound = original.RebindRemovedNullStatements(
        edited.View(),
        deleted);
    ASSERT_TRUE(rebound);
    EXPECT_NE(&rebound->Root(), &original.Root());
    ASSERT_EQ(rebound->TokenCount(), reparsed.TokenCount());
    for (parser::TokenIndex i = 0; i < rebound->TokenCount(); ++i) {
        EXPECT_EQ(rebound->Kind(i), reparsed.Kind(i));
        EXPECT_EQ(rebound->Token(i).Text, reparsed.Token(i).Text);
        EXPECT_EQ(
            rebound->TokenPosition(i).Line,
            reparsed.TokenPosition(i).Line);
        EXPECT_EQ(
            rebound->TokenPosition(i).Column,
            reparsed.TokenPosition(i).Column);
        EXPECT_EQ(
            rebound->MatchingDelimiter(i),
            reparsed.MatchingDelimiter(i));
    }
    EXPECT_EQ(rebound->Root().Tokens.End, rebound->TokenCount());
    std::vector<const parser::Node*> nodes{&rebound->Root()};
    while (!nodes.empty()) {
        const auto* node = nodes.back();
        nodes.pop_back();
        EXPECT_FALSE(node->Kind == parser::NodeKind::ExpressionStatement &&
            node->Tokens.End == node->Tokens.Begin + 1 &&
            rebound->Kind(node->Tokens.Begin) ==
                tokenizer::TokenKind::Operator_Semicolon);
        for (auto* child = node->Child();
            child;
            child = child->NextSibling)
        {
            nodes.push_back(child);
        }
    }
}

TEST(Parser, InsertsControlBracesWithoutReparsingSyntax)
{
    PaddedInput input("void f() { if (x) g(); }\n");
    PaddedInput edited("void f() { if (x) { g(); } }\n");
    auto original = parser::Parse(input.View(), [] (parser::ParseError) { });
    auto reparsed = parser::Parse(edited.View(), [] (parser::ParseError) { });
    const parser::Node* body = nullptr;
    std::vector<const parser::Node*> pending{&original.Root()};
    while (!pending.empty()) {
        auto* node = pending.back();
        pending.pop_back();
        if (node->Kind == parser::NodeKind::IfStatement) {
            for (auto* child = node->Child();
                child;
                child = child->NextSibling)
            {
                if (child->Kind == parser::NodeKind::ExpressionStatement) {
                    body = child;
                }
            }
        }
        for (auto* child = node->Child();
            child;
            child = child->NextSibling)
        {
            pending.push_back(child);
        }
    }
    ASSERT_NE(body, nullptr);
    const std::array blocks{parser::InsertedControlBlock{body, false}};
    auto rebound = original.RebindControlBraces(edited.View(), blocks);
    ASSERT_TRUE(rebound);
    ASSERT_EQ(rebound->TokenCount(), reparsed.TokenCount());
    for (parser::TokenIndex i = 0; i < rebound->TokenCount(); ++i) {
        EXPECT_EQ(rebound->Kind(i), reparsed.Kind(i));
        EXPECT_EQ(rebound->Role(i), reparsed.Role(i));
        EXPECT_EQ(
            rebound->MatchingDelimiter(i),
            reparsed.MatchingDelimiter(i));
    }
    std::vector<std::pair<const parser::Node*, const parser::Node*>>
        nodes{{&rebound->Root(), &reparsed.Root()}};
    while (!nodes.empty()) {
        auto [actual, expected] = nodes.back();
        nodes.pop_back();
        EXPECT_EQ(actual->Kind, expected->Kind);
        EXPECT_EQ(actual->Tokens.Begin, expected->Tokens.Begin);
        EXPECT_EQ(actual->Tokens.End, expected->Tokens.End);
        auto* child = actual->Child();
        auto* other = expected->Child();
        while (child && other) {
            nodes.emplace_back(child, other);
            child = child->NextSibling;
            other = other->NextSibling;
        }
        EXPECT_EQ(child, nullptr);
        EXPECT_EQ(other, nullptr);
    }

    PaddedInput nestedInput(
        "void f(){if(a) if(b) Go(); else Stop();}\n");
    PaddedInput nestedEdited(
        "void f(){if(a) {if(b) {Go();} else "
            "{Stop();}}}\n");
    auto nested = parser::Parse(
        nestedInput.View(),
        [] (parser::ParseError) { });
    auto nestedParsed = parser::Parse(
        nestedEdited.View(),
        [] (parser::ParseError) { });
    std::vector<parser::InsertedControlBlock> nestedBlocks;
    pending = {&nested.Root()};
    while (!pending.empty()) {
        auto* node = pending.back();
        pending.pop_back();
        if (node->Kind == parser::NodeKind::IfStatement) {
            for (auto* child = node->Child();
                child;
                child = child->NextSibling)
            {
                if (child->Kind == parser::NodeKind::IfStatement ||
                    child->Kind == parser::NodeKind::ExpressionStatement)
                {
                    nestedBlocks.push_back({child, false});
                }
            }
        }
        for (auto* child = node->Child();
            child;
            child = child->NextSibling)
        {
            pending.push_back(child);
        }
    }
    ASSERT_EQ(nestedBlocks.size(), 3);
    auto nestedRebound = nested.RebindControlBraces(
        nestedEdited.View(),
        nestedBlocks);
    ASSERT_TRUE(nestedRebound);
    ASSERT_EQ(nestedRebound->TokenCount(), nestedParsed.TokenCount());
    for (parser::TokenIndex i = 0; i < nestedRebound->TokenCount(); ++i) {
        EXPECT_EQ(
            nestedRebound->MatchingDelimiter(i),
            nestedParsed.MatchingDelimiter(i));
    }
}

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
        tree.Kind(outerClosing),
        tokenizer::TokenKind::Operator_CloseBrace);
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
