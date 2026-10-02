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
        GoldenCase{"requires_clause_boundaries"},
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

TEST(Parser, RecordHeadTemplateArgumentsOwnTheirSeparators)
{
    PaddedInput input(
        "template<class A, class B> struct Pair;\n"
        "template<class A, class B> struct Pair<A,B> {};\n");
    const auto tree = parser::Parse(input.View(), [](parser::ParseError) {});
    const auto *declaration = tree.Root().FirstChild->NextSibling->FirstChild;
    ASSERT_EQ(declaration->Kind, parser::NodeKind::TemplateParameterList);
    const auto *record = declaration->NextSibling;
    ASSERT_EQ(record->Kind, parser::NodeKind::RecordDeclaration);
    ASSERT_NE(record->FirstChild, nullptr);
    EXPECT_EQ(record->FirstChild->Kind, parser::NodeKind::TemplateArgumentList);
    for (auto i = record->Tokens.Begin; i < record->Tokens.End; ++i) {
        if (tree.Kind(i) == tokenizer::TokenKind::Operator_Comma) {
            EXPECT_EQ(tree.Role(i), parser::PunctuationRole::ListSeparator);
        }
    }

    struct BoundaryCase
    {
        std::string_view Prefix;
        std::string_view First;
        std::string_view Second;
    };
    const std::string_view values =
        "template<auto, auto> struct Pair;\n";
    const std::string_view types =
        "template<auto, class> struct Wrap;\n"
        "template<class, auto> struct Pair;\n";
    const BoundaryCase boundaries[] = {
        {values, "(1<2)", "0"},
        {types, "Wrap<(1>0),int>", "2"},
        {types, "Wrap<(8>>1),int>", "2"},
        {types, "Wrap<(1<<2),Wrap<(3>2),int>>", "2"},
        {"constexpr int limits[] = {1,2};\n"
         "template<auto, auto> struct Pair;\n", "limits[1<2]", "0"},
        {"struct Value { int first, second; };\n"
         "template<Value, int> struct Pair;\n", "Value{1<2,2}", "0"},
        {"struct Value { int first, second; };\n"
         "template<Value, class> struct Wrap;\n"
         "template<class, int> struct Pair;\n", "Wrap<(Value{1>0,2}),int>", "0"},
        {"constexpr int limits[] = {1,2};\n"
         "template<auto, class> struct Wrap;\n"
         "template<class, auto> struct Pair;\n", "Wrap<(limits[1>0]),int>", "0"},
    };
    const auto checkBoundary = [&](const BoundaryCase &value) {
        const auto source = std::string(value.Prefix) +
            "template<> struct Pair<" + std::string(value.First) + "," +
            std::string(value.Second) + "> {};\n";
        SCOPED_TRACE(source);
        PaddedInput padded(source);
        std::vector<parser::ParseError> errors;
        const auto parsed = parser::Parse(padded.View(),
            [&](parser::ParseError error) { errors.push_back(std::move(error)); });
        EXPECT_TRUE(errors.empty());
        const auto *record = parsed.Root().LastChild->FirstChild->NextSibling;
        ASSERT_EQ(record->Kind, parser::NodeKind::RecordDeclaration);
        const auto *list = record->FirstChild;
        ASSERT_NE(list, nullptr);
        ASSERT_EQ(list->Kind, parser::NodeKind::TemplateArgumentList);
        const auto *first = list->FirstChild;
        ASSERT_NE(first, nullptr);
        ASSERT_EQ(first->Kind, parser::NodeKind::TemplateArgument);
        const auto *separator = first->NextSibling;
        ASSERT_NE(separator, nullptr);
        EXPECT_EQ(separator->Kind, parser::NodeKind::ListSeparator);
        const auto *second = separator->NextSibling;
        ASSERT_NE(second, nullptr);
        EXPECT_EQ(second->Kind, parser::NodeKind::TemplateArgument);
        EXPECT_EQ(second->NextSibling, nullptr);

        const auto firstBegin = source.find(value.First, value.Prefix.size());
        const auto outerComma = firstBegin + value.First.size();
        const auto secondBegin = outerComma + 1;
        const auto tokenAt = [&](size_t offset) {
            for (parser::TokenIndex i = 0; i < parsed.TokenCount(); ++i) {
                if (parsed.Token(i).Text.Begin == offset) {
                    return i;
                }
            }
            return parsed.TokenCount();
        };
        EXPECT_EQ(first->Tokens.Begin, tokenAt(firstBegin));
        EXPECT_EQ(first->Tokens.End, tokenAt(outerComma));
        EXPECT_EQ(second->Tokens.Begin, tokenAt(secondBegin));
        EXPECT_EQ(second->Tokens.End,
            tokenAt(secondBegin + value.Second.size()));
        EXPECT_EQ(parsed.SourceText(first->Tokens), value.First);
        EXPECT_EQ(parsed.SourceText(second->Tokens), value.Second);
        EXPECT_EQ(separator->Tokens.Begin, tokenAt(outerComma));
        EXPECT_EQ(separator->Tokens.End, tokenAt(outerComma) + 1);
        for (auto i = list->Tokens.Begin; i < list->Tokens.End; ++i) {
            if (parsed.Kind(i) == tokenizer::TokenKind::Operator_Comma) {
                EXPECT_EQ(parsed.Role(i), i == tokenAt(outerComma)
                    ? parser::PunctuationRole::ListSeparator
                    : parser::PunctuationRole::Generic);
            }
        }
    };
    for (const auto &value : boundaries) {
        checkBoundary(value);
    }

    // Head children must compose with injected constructor/self-type scope,
    // without exporting the qualified terminal name over an outer variable.
    const auto source = test::golden::ReadFile(
        std::filesystem::path(PARSER_TESTDATA_DIR) / "../../.." /
        "formatter/tests/testdata/record-head-commas/constructor-composition.input.cpp");
    PaddedInput composedInput(source);
    unsigned errors = 0;
    const auto composed = parser::Parse(composedInput.View(),
        [&](parser::ParseError) { ++errors; });
    EXPECT_EQ(errors, 0u);
    const Node *specialization = nullptr;
    const Node *constructor = nullptr;
    size_t outsideExpressions = 0;
    size_t outsideAmbiguities = 0;
    size_t unknownPairNames = 0;
    size_t selfTypes = 0;
    std::vector<const Node *> pending{&composed.Root()};
    while (!pending.empty()) {
        const auto *node = pending.back();
        pending.pop_back();
        const auto text = composed.SourceText(node->Tokens);
        if (node->Kind == parser::NodeKind::RecordDeclaration &&
            text.starts_with("struct Host::Pair<"))
            specialization = node;
        if (node->Kind == parser::NodeKind::FunctionDefinition &&
            text.starts_with("Pair("))
            constructor = node;
        if (node->Kind == parser::NodeKind::DeclarationSpecifiers && text == "Pair")
            ++selfTypes;
        if (node->Kind == parser::NodeKind::ExpressionStatement && text == "Pair*marker;")
            ++outsideExpressions;
        if (node->Kind == parser::NodeKind::AmbiguousStatement && text == "Pair*marker;")
            ++outsideAmbiguities;
        if (node->Kind == parser::NodeKind::UnknownTypeName && text == "Pair")
            ++unknownPairNames;
        EXPECT_FALSE(node->Kind == parser::NodeKind::DeclarationStatement &&
            text == "Pair*marker;");
        for (auto *child = node->FirstChild; child; child = child->NextSibling)
            pending.push_back(child);
    }
    ASSERT_NE(specialization, nullptr);
    const auto *head = specialization->FirstChild;
    ASSERT_NE(head, nullptr);
    ASSERT_EQ(head->Kind, parser::NodeKind::TemplateArgumentList);
    const auto *first = head->FirstChild;
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(composed.SourceText(first->Tokens), "Wrap<(1>0),int>");
    const auto *separator = first->NextSibling;
    ASSERT_NE(separator, nullptr);
    EXPECT_EQ(separator->Kind, parser::NodeKind::ListSeparator);
    EXPECT_EQ(separator->Tokens.Begin, first->Tokens.End);
    EXPECT_EQ(composed.Role(separator->Tokens.Begin), parser::PunctuationRole::ListSeparator);
    const auto *second = separator->NextSibling;
    ASSERT_NE(second, nullptr);
    EXPECT_EQ(composed.SourceText(second->Tokens), "2");
    EXPECT_EQ(second->Tokens.Begin, separator->Tokens.End);
    EXPECT_EQ(second->Tokens.End, head->Tokens.End - 1);
    EXPECT_EQ(second->NextSibling, nullptr);
    for (auto i = first->Tokens.Begin; i < first->Tokens.End; ++i)
        if (composed.Kind(i) == tokenizer::TokenKind::Operator_Comma)
            EXPECT_EQ(composed.Role(i), parser::PunctuationRole::Generic);
    ASSERT_NE(constructor, nullptr);
    const Node *parameters = nullptr;
    const Node *initializers = nullptr;
    pending = {constructor};
    while (!pending.empty()) {
        const auto *node = pending.back();
        pending.pop_back();
        if (node->Kind == parser::NodeKind::ParameterList) parameters = node;
        if (node->Kind == parser::NodeKind::ConstructorInitializerList) initializers = node;
        for (auto *child = node->FirstChild; child; child = child->NextSibling)
            pending.push_back(child);
    }
    ASSERT_NE(initializers, nullptr);
    EXPECT_EQ(composed.SourceText(initializers->Tokens), ": stored(first+second)");
    ASSERT_NE(parameters, nullptr);
    const auto *parameter = parameters->FirstChild;
    ASSERT_NE(parameter, nullptr);
    EXPECT_EQ(composed.SourceText(parameter->Tokens), "int first=(1<2)");
    const auto *parameterComma = parameter->NextSibling;
    ASSERT_NE(parameterComma, nullptr);
    EXPECT_EQ(parameterComma->Kind, parser::NodeKind::ListSeparator);
    EXPECT_EQ(composed.Role(parameterComma->Tokens.Begin), parser::PunctuationRole::ListSeparator);
    const auto *other = parameterComma->NextSibling;
    ASSERT_NE(other, nullptr);
    EXPECT_EQ(composed.SourceText(other->Tokens), "int second=(3>2)");
    EXPECT_EQ(other->Tokens.End, parameters->Tokens.End - 1);
    EXPECT_EQ(other->NextSibling, nullptr);
    size_t defaultTails = 0;
    for (const auto* value : {parameter, other}) {
        pending = {value};
        while (!pending.empty()) {
            const auto* node = pending.back();
            pending.pop_back();
            if (node->Kind == parser::NodeKind::Initializer) {
                ++defaultTails;
                EXPECT_EQ(node->Tokens.End, value->Tokens.End);
                EXPECT_NE(node->FirstChild, nullptr);
            }
            for (auto* child = node->FirstChild; child; child = child->NextSibling)
                pending.push_back(child);
        }
    }
    EXPECT_EQ(defaultTails, 2u);
    EXPECT_EQ(selfTypes, 1u);
    EXPECT_EQ(outsideExpressions, 1u);
    EXPECT_EQ(outsideAmbiguities, 1u);
    EXPECT_EQ(unknownPairNames, 1u);

    // A registered type would trip the no-export assertion above: this is
    // deliberately a declaration, unlike the conservative unknown-global case.
    PaddedInput registeredType("struct Pair {}; void known_type() { Pair*marker; }\n");
    const auto control = parser::Parse(registeredType.View(), [](parser::ParseError) {});
    pending = {&control.Root()};
    size_t declarations = 0;
    while (!pending.empty()) {
        const auto* node = pending.back();
        pending.pop_back();
        if (node->Kind == parser::NodeKind::DeclarationStatement &&
            control.SourceText(node->Tokens) == "Pair*marker;")
            ++declarations;
        for (auto* child = node->FirstChild; child; child = child->NextSibling)
            pending.push_back(child);
    }
    EXPECT_EQ(declarations, 1u);

    // The rebase must retain main's member-scope linkage while suppressing
    // qualified definitions' surrounding-name export. Forward redeclarations
    // and the local injected self-name must both preserve member type lookup.
    const std::string_view linkageCases[] = {
        "struct Catalog { using Element = int; };\n"
        "void inspect() { int consume(Catalog::Element); }\n",
        "struct Catalog { using Element = int; }; struct Catalog;\n"
        "void inspect() { int consume(Catalog::Element); }\n",
        "struct Catalog { using Element = int;\n"
        "void inspect() { int consume(Catalog::Element); } };\n",
        "struct Host {}; struct Host::Catalog { using Element = int;\n"
        "Catalog(int) {}\n"
        "void inspect() { int consume(Catalog::Element); } };\n"
        "int Catalog; void outside() { Catalog*marker; }\n",
    };
    for (const auto source : linkageCases) {
        SCOPED_TRACE(source);
        PaddedInput input(source);
        unsigned errors = 0;
        const auto linked = parser::Parse(input.View(),
            [&](parser::ParseError) { ++errors; });
        EXPECT_EQ(errors, 0u);
        size_t typedParameters = 0;
        size_t outsideExpressions = 0;
        size_t outsideAmbiguities = 0;
        size_t constructors = 0;
        pending = {&linked.Root()};
        while (!pending.empty()) {
            const auto* node = pending.back();
            pending.pop_back();
            const auto text = linked.SourceText(node->Tokens);
            if (node->Kind == parser::NodeKind::ParameterDeclaration &&
                text == "Catalog::Element")
                ++typedParameters;
            if (node->Kind == parser::NodeKind::FunctionDefinition &&
                text == "Catalog(int) {}")
                ++constructors;
            if (node->Kind == parser::NodeKind::ExpressionStatement &&
                text == "Catalog*marker;")
                ++outsideExpressions;
            if (node->Kind == parser::NodeKind::AmbiguousStatement &&
                text == "Catalog*marker;")
                ++outsideAmbiguities;
            EXPECT_FALSE(node->Kind == parser::NodeKind::DeclarationStatement &&
                text == "Catalog*marker;");
            for (auto* child = node->FirstChild; child; child = child->NextSibling)
                pending.push_back(child);
        }
        EXPECT_EQ(typedParameters, 1u) << Serialize(linked);
        const bool qualified = source.find("Host::Catalog") != std::string_view::npos;
        EXPECT_EQ(constructors, qualified ? 1u : 0u);
        // This global declaration has no initializer: unresolved value names
        // may remain ambiguous, but a leaked type would become a declaration.
        EXPECT_EQ(outsideExpressions + outsideAmbiguities,
            qualified ? 1u : 0u) << Serialize(linked);
    }
}

TEST(Parser, RecordHeadChildrenAreBoundedWithoutGuessingOperands)
{
    const std::pair<std::string_view, size_t> cases[] = {
        {"struct Plain {};", 0},
        {"struct Outer::Inner {};", 0},
        {"template<> struct Outer<int>::Inner<long,char> {};", 2},
        {"template<> struct Pair<> {};", 1},
        {"template<> struct Pair<Wrap<int,long>, char> {};", 1},
        {"template<> struct Pair<(a < b), 2> {};", 1},
        {"template<> struct Pair<Value{1,2}, 3> {};", 1},
        {"template<> struct Pair<decltype(auto), 2> {};", 1},
        {"template<> struct Pair<decltype(call(a,b)), 2> {};", 1},
        {"struct alignas(alignof(int)) Pair<int,long> {};", 1},
        {"struct API Pair<int,long> {};", 0},
        {"struct Pair<int, Wrap<long> {};", 0},
        {"struct Pair<(1<2],0> {};", 0},
        {"struct Pair<Value{1>0],0> {};", 0},
    };
    for (const auto &[source, expectedLists] : cases) {
        SCOPED_TRACE(source);
        PaddedInput input(source);
        const auto tree = parser::Parse(input.View(), [](parser::ParseError) {});
        size_t lists = 0;
        size_t calls = 0;
        std::vector<const Node *> pending{&tree.Root()};
        while (!pending.empty()) {
            const auto *node = pending.back();
            pending.pop_back();
            lists += node->Kind == parser::NodeKind::TemplateArgumentList;
            calls += node->Kind == parser::NodeKind::CallExpression;
            auto end = node->Tokens.Begin;
            for (auto *child = node->FirstChild; child;
                child = child->NextSibling)
            {
                EXPECT_GE(child->Tokens.Begin, end);
                EXPECT_LE(child->Tokens.End, node->Tokens.End);
                end = child->Tokens.End;
                pending.push_back(child);
            }
            if (node->Kind == parser::NodeKind::TemplateArgument) {
                // This patch exposes the head list, not the unresolved
                // type/expression operands. In particular, nested commas
                // must not be mistaken for the head's list separators.
                EXPECT_EQ(node->FirstChild, nullptr);
                for (auto i = node->Tokens.Begin; i < node->Tokens.End; ++i) {
                    if (tree.Kind(i) == tokenizer::TokenKind::Operator_Comma) {
                        EXPECT_NE(tree.Role(i),
                            parser::PunctuationRole::ListSeparator);
                    }
                }
            }
        }
        EXPECT_EQ(lists, expectedLists);
        EXPECT_EQ(calls, 0u);
    }

    // Attaching head syntax must not alter the old registration path for
    // forward declarations or the declaration following them.
    PaddedInput input(
        "template<class A, class B> struct Pair;\n"
        "template<> struct Pair<int,long>;\n"
        "Pair<int,long> value;\n");
    const auto tree = parser::Parse(input.View(), [](parser::ParseError) {});
    const auto *last = tree.Root().LastChild;
    ASSERT_EQ(last->Kind, parser::NodeKind::Declaration);
    ASSERT_NE(last->FirstChild, nullptr);
    EXPECT_EQ(last->FirstChild->Kind, parser::NodeKind::DeclarationSpecifiers);

    // Ordinary callers retain their independent delimiter/element ownership.
    const std::string ordinary =
        "void sink(int first=(1<2),int second=(3>2));\n"
        "void use(){ sink((1<2),0); int items[]={(1<2),0}; }\n"
        "enum Values { first=(1<2), second=0 };\n";
    PaddedInput ordinaryInput(ordinary);
    std::vector<parser::ParseError> errors;
    const auto ordinaryTree = parser::Parse(ordinaryInput.View(),
        [&](parser::ParseError error) { errors.push_back(std::move(error)); });
    EXPECT_TRUE(errors.empty());
    const std::pair<parser::NodeKind, std::string_view> expected[] = {
        {parser::NodeKind::ParameterList, "int first=(1<2)"},
        {parser::NodeKind::ArgumentList, "(1<2)"},
        {parser::NodeKind::InitializerList, "(1<2)"},
        {parser::NodeKind::EnumeratorList, "first=(1<2)"},
    };
    for (const auto &[kind, firstText] : expected) {
        SCOPED_TRACE(parser::GetNodeKindName(kind));
        size_t found = 0;
        std::vector<const Node *> nodes{&ordinaryTree.Root()};
        while (!nodes.empty()) {
            const auto *node = nodes.back();
            nodes.pop_back();
            for (auto *child = node->FirstChild; child;
                child = child->NextSibling)
            {
                nodes.push_back(child);
            }
            if (node->Kind != kind || !node->FirstChild ||
                ordinaryTree.SourceText(node->FirstChild->Tokens) != firstText)
            {
                continue;
            }
            ++found;
            const auto *first = node->FirstChild;
            const auto *comma = first->NextSibling;
            ASSERT_NE(comma, nullptr);
            EXPECT_EQ(comma->Kind, parser::NodeKind::ListSeparator);
            EXPECT_EQ(comma->Tokens.Begin, first->Tokens.End);
            EXPECT_EQ(comma->Tokens.End, first->Tokens.End + 1);
            EXPECT_EQ(ordinaryTree.Role(comma->Tokens.Begin),
                parser::PunctuationRole::ListSeparator);
            const auto *second = comma->NextSibling;
            ASSERT_NE(second, nullptr);
            EXPECT_EQ(second->Tokens.Begin, comma->Tokens.End);
            EXPECT_EQ(second->Tokens.End, node->Tokens.End - 1);
            EXPECT_EQ(second->NextSibling, nullptr);
        }
        EXPECT_EQ(found, 1u);
    }
}

TEST(Parser, RequiresClauseOwnsNestedExpressionBeforeDeclaration)
{
    const std::string_view constraints[] = {
        "requires(T value) { value.touch(); }",
        "requires { typename T::value_type; }",
        "requires(T value) { { value.touch() } noexcept; typename T::value_type; requires requires { typename T::other_type; }; }",
        "requires(T value) /* body */ { value.touch(); } && true",
        "true && requires(T value) { value.touch(); }",
        "(requires(T value) { value.touch(); })",
        "requires(T value) { value.touch(); } || requires { typename T::value_type; }",
    };
    for (const auto constraintText : constraints) {
        for (const bool function : {false, true}) {
            const std::string declaration = function
                ? "int inspect(T value) { return 1; }"
                : "struct Container { T member; };";
            const auto source = "template<class T> requires " +
                std::string(constraintText) + "\n" + declaration +
                "\nint outside;\n";
            SCOPED_TRACE(source);
            PaddedInput input(source);
            std::vector<parser::ParseError> errors;
            const auto tree = parser::Parse(input.View(),
                [&](parser::ParseError error) { errors.push_back(std::move(error)); });
            EXPECT_TRUE(errors.empty());
            const auto *owner = tree.Root().FirstChild;
            ASSERT_NE(owner, nullptr);
            ASSERT_EQ(owner->Kind, parser::NodeKind::TemplateDeclaration);
            const auto *clause = owner->FirstChild;
            while (clause && clause->Kind != parser::NodeKind::RequiresClause) {
                clause = clause->NextSibling;
            }
            ASSERT_NE(clause, nullptr);
            ASSERT_NE(clause->FirstChild, nullptr);
            EXPECT_EQ(clause->FirstChild->Kind, parser::NodeKind::ConstraintExpression);
            EXPECT_EQ(tree.SourceText(clause->FirstChild->Tokens), constraintText);
            const auto *following = clause->NextSibling;
            ASSERT_NE(following, nullptr);
            EXPECT_EQ(following->Kind, function
                ? parser::NodeKind::FunctionDefinition
                : parser::NodeKind::RecordDeclaration);
            EXPECT_EQ(tree.SourceText(following->Tokens), declaration);
            EXPECT_EQ(following->Tokens.Begin, clause->Tokens.End);
            EXPECT_EQ(following->NextSibling, nullptr);
            size_t bodies = 0;
            size_t parameters = 0;
            std::vector<const Node *> pending{clause};
            while (!pending.empty()) {
                const auto *node = pending.back();
                pending.pop_back();
                EXPECT_NE(node->Kind, parser::NodeKind::UnparsedSyntax);
                EXPECT_GE(node->Tokens.Begin, clause->Tokens.Begin);
                EXPECT_LE(node->Tokens.End, clause->Tokens.End);
                if (node->Kind == parser::NodeKind::RequirementBody) {
                    ++bodies;
                    EXPECT_EQ(tree.Role(node->Tokens.Begin), parser::PunctuationRole::BlockDelimiter);
                    EXPECT_EQ(tree.Role(node->Tokens.End - 1), parser::PunctuationRole::BlockDelimiter);
                    ASSERT_NE(node->FirstChild, nullptr);
                    EXPECT_EQ(node->FirstChild->Kind, parser::NodeKind::Requirement);
                    EXPECT_EQ(node->LastChild->Kind, parser::NodeKind::ListSeparator);
                    EXPECT_EQ(tree.Role(node->LastChild->Tokens.Begin), parser::PunctuationRole::ListSeparator);
                }
                if (node->Kind == parser::NodeKind::RequiresExpression &&
                    node->FirstChild && node->FirstChild->Kind == parser::NodeKind::ParameterList)
                {
                    ++parameters;
                    EXPECT_EQ(tree.SourceText(node->FirstChild->Tokens), "(T value)");
                    EXPECT_EQ(node->LastChild->Kind, parser::NodeKind::RequirementBody);
                }
                for (const auto *child = node->FirstChild; child; child = child->NextSibling) {
                    pending.push_back(child);
                }
            }
            EXPECT_GE(bodies, 1u);
            EXPECT_EQ(parameters, constraintText.find("(T value)") != std::string_view::npos ? 1u : 0u);
            const auto *outside = owner->NextSibling;
            ASSERT_NE(outside, nullptr);
            EXPECT_EQ(outside->Kind, parser::NodeKind::Declaration);
            EXPECT_EQ(tree.SourceText(outside->Tokens), "int outside;");
            EXPECT_EQ(outside->Tokens.Begin, owner->Tokens.End);
            EXPECT_EQ(outside->NextSibling, nullptr);
            if (function) {
                ASSERT_NE(following->FirstChild, nullptr);
                EXPECT_EQ(following->FirstChild->Kind, parser::NodeKind::DeclarationSpecifiers);
                EXPECT_EQ(tree.SourceText(following->FirstChild->Tokens), "int");
                ASSERT_NE(following->FirstChild->NextSibling, nullptr);
                EXPECT_EQ(following->FirstChild->NextSibling->Kind, parser::NodeKind::InitDeclaratorList);
                EXPECT_EQ(tree.SourceText(following->FirstChild->NextSibling->Tokens), "inspect(T value)");
                EXPECT_EQ(following->LastChild->Kind, parser::NodeKind::CompoundStatement);
            }
        }
    }
}

TEST(Parser, RequiresClauseOwnsDependentFunctionReturnPrefix)
{
    struct DeclarationCase
    {
        std::string_view Specifiers;
        std::string_view Declarator;
        std::string_view Body;
    };
    const DeclarationCase declarations[] = {
        {"T", "* inspect(T* value)", "{ return value; }"},
        {"T", "& access(T& value)", "{ return value; }"},
        {"T", "&& relay(T&& value)", "{ return static_cast<T&&>(value); }"},
        {"T", "* const retain(T* value)", "{ return value; }"},
        {"T::value_type", "** fetch(T::value_type** value)", "{ return value; }"},
        {"const T", "* view(const T* value)", "{ return value; }"},
    };
    struct ConstraintCase
    {
        std::string_view Text;
        bool Unparsed;
    };
    const ConstraintCase constraints[] = {
        {"requires(T v) { v.touch(); }", false},
        {"requires { typename T::value_type; } && enabled", false},
        {"enabled && requires(T v) { v.touch(); }", false},
        {"(requires(T v) { v.touch(); })", false},
        {"requires(T v) { v.touch(); } || (limit * scale > 0)", false},
        {"Valid<T>", true},
    };
    for (const auto &declaration : declarations) {
        for (const auto &constraint : constraints) {
            const auto constraintText = constraint.Text;
            const auto declarationText = std::string(declaration.Specifiers) +
                " " + std::string(declaration.Declarator) + " " +
                std::string(declaration.Body);
            const auto source = "template<class T> requires " +
                std::string(constraintText) + "\n" + declarationText +
                "\nint outside;\n";
            SCOPED_TRACE(source);
            PaddedInput input(source);
            size_t errors = 0;
            const auto tree = parser::Parse(input.View(), [&](parser::ParseError) { ++errors; });
            EXPECT_EQ(errors, 0u);
            const auto *owner = tree.Root().FirstChild;
            ASSERT_NE(owner, nullptr);
            EXPECT_EQ(owner->Kind, parser::NodeKind::TemplateDeclaration);
            const auto *clause = owner->FirstChild;
            while (clause && clause->Kind != parser::NodeKind::RequiresClause) {
                clause = clause->NextSibling;
            }
            ASSERT_NE(clause, nullptr);
            ASSERT_NE(clause->FirstChild, nullptr);
            EXPECT_EQ(tree.SourceText(clause->FirstChild->Tokens), constraintText);
            const auto *function = clause->NextSibling;
            ASSERT_NE(function, nullptr);
            EXPECT_EQ(function->Kind, parser::NodeKind::FunctionDefinition);
            EXPECT_EQ(tree.SourceText(function->Tokens), declarationText);
            EXPECT_EQ(function->Tokens.Begin, clause->Tokens.End);
            ASSERT_NE(function->FirstChild, nullptr);
            EXPECT_EQ(function->FirstChild->Kind, parser::NodeKind::DeclarationSpecifiers);
            EXPECT_EQ(tree.SourceText(function->FirstChild->Tokens), declaration.Specifiers);
            const auto *declarator = function->FirstChild->NextSibling;
            ASSERT_NE(declarator, nullptr);
            EXPECT_EQ(declarator->Kind, parser::NodeKind::InitDeclaratorList);
            EXPECT_EQ(tree.SourceText(declarator->Tokens), declaration.Declarator);
            EXPECT_EQ(declarator->Tokens.Begin, function->FirstChild->Tokens.End);
            ASSERT_NE(function->LastChild, nullptr);
            EXPECT_EQ(function->LastChild->Kind, parser::NodeKind::CompoundStatement);
            EXPECT_EQ(tree.SourceText(function->LastChild->Tokens), declaration.Body);
            EXPECT_EQ(function->LastChild->Tokens.End, function->Tokens.End);
            ASSERT_NE(owner->NextSibling, nullptr);
            EXPECT_EQ(tree.SourceText(owner->NextSibling->Tokens), "int outside;");
            EXPECT_EQ(owner->NextSibling->Tokens.Begin, owner->Tokens.End);
            bool unparsed = false;
            std::vector<const Node *> pending{clause};
            while (!pending.empty()) {
                const auto *node = pending.back();
                pending.pop_back();
                unparsed |= node->Kind == parser::NodeKind::UnparsedSyntax;
                EXPECT_GE(node->Tokens.Begin, clause->Tokens.Begin);
                EXPECT_LE(node->Tokens.End, clause->Tokens.End);
                for (const auto *child = node->FirstChild; child; child = child->NextSibling) {
                    pending.push_back(child);
                }
            }
            EXPECT_EQ(unparsed, constraint.Unparsed);
        }
    }
}

TEST(Parser, RequiresClausePreservesOperatorAndUnknownConstraints)
{
    struct ConstraintCase
    {
        std::string_view Text;
        bool Unparsed;
        bool Invalid;
    };
    const ConstraintCase constraints[] = {
        {"requires(T v) { v.touch(); } && (left * invoke(value) > 0)", false, false},
        {"requires(T v) { v.touch(); } || (left & mask)", false, false},
        {"requires(T v) { v.touch(); } && (left && invoke(value))", false, false},
        {"requires(T v) { v.touch(); } && !enabled", false, false},
        {"requires(T v) { v.touch(); } && unresolved", false, false},
        {"requires(T v) { v.touch(); } @ discarded", true, true},
        {"requires(T v) { v.touch(); } && (unresolved @ tail)", false, true},
    };
    for (const auto &constraint : constraints) {
        const auto source = "template<class T> requires " +
            std::string(constraint.Text) + "\nT* inspect(T* value) { return value; }\n";
        SCOPED_TRACE(source);
        PaddedInput input(source);
        std::vector<parser::ParseError> errors;
        const auto tree = parser::Parse(input.View(),
            [&](parser::ParseError error) { errors.push_back(std::move(error)); });
        if (constraint.Invalid) {
            ASSERT_EQ(errors.size(), 1u);
            EXPECT_EQ(errors.front().Message, "Unexpected token");
            EXPECT_EQ(errors.front().Position.Line, 1u);
            // The tokenizer reports the position after consuming the invalid character.
            EXPECT_EQ(errors.front().Position.Column, source.find('@') + 2);
        } else {
            EXPECT_TRUE(errors.empty());
        }
        const auto *owner = tree.Root().FirstChild;
        ASSERT_NE(owner, nullptr);
        const auto *clause = owner->FirstChild;
        while (clause && clause->Kind != parser::NodeKind::RequiresClause) {
            clause = clause->NextSibling;
        }
        ASSERT_NE(clause, nullptr);
        ASSERT_NE(clause->FirstChild, nullptr);
        EXPECT_EQ(tree.SourceText(clause->FirstChild->Tokens), constraint.Text);
        size_t invalidTokens = 0;
        for (auto token = clause->Tokens.Begin; token < clause->Tokens.End; ++token) {
            if (tree.Kind(token) == tokenizer::TokenKind::Invalid) {
                ++invalidTokens;
                EXPECT_EQ(tree.TokenText(token), "@");
            }
        }
        EXPECT_EQ(invalidTokens, constraint.Invalid ? 1u : 0u);
        bool unparsed = false;
        std::vector<const Node *> pending{clause};
        while (!pending.empty()) {
            const auto *node = pending.back();
            pending.pop_back();
            unparsed |= node->Kind == parser::NodeKind::UnparsedSyntax;
            EXPECT_GE(node->Tokens.Begin, clause->Tokens.Begin);
            EXPECT_LE(node->Tokens.End, clause->Tokens.End);
            for (const auto *child = node->FirstChild; child; child = child->NextSibling) {
                pending.push_back(child);
            }
        }
        EXPECT_EQ(unparsed, constraint.Unparsed);
        ASSERT_NE(clause->NextSibling, nullptr);
        EXPECT_EQ(tree.SourceText(clause->NextSibling->Tokens), "T* inspect(T* value) { return value; }");
        EXPECT_EQ(clause->NextSibling->Tokens.Begin, clause->Tokens.End);
    }
}

TEST(Parser, RequiresClauseKeepsBoundaryCommentsAndTerminalRecovery)
{
    struct DeclarationCase
    {
        std::string_view Specifiers;
        std::string_view Declarator;
        std::string_view Body;
    };
    const DeclarationCase declarations[] = {
        {"int", "inspect(T value)", "{ return 1; }"},
        {"T", "* inspect(T* value)", "{ return value; }"},
        {"T", "& access(T& value)", "{ return value; }"},
    };
    struct ConstraintCase
    {
        std::string_view Text;
        bool Unparsed;
    };
    const ConstraintCase constraints[] = {
        {"requires(T v) { v.touch(); } /* boundary */", false},
        {"requires(T v) { v.touch(); } // boundary", false},
        {"requires(T v) { v.touch(); } @", true},
        {"requires(T v) { v.touch(); } && @", false},
        {"requires(T v) { v.touch(); } @ /* boundary */", true},
        {"requires(T v) { v.touch(); } && /* operand */ enabled", false},
        {"requires(T v) { v.touch(); } || // operand\n enabled", false},
        {"requires(T v) { v.touch(); } && (left * invoke(value) > 0)", false},
        {"requires(T v) { v.touch(); } || (left & mask)", false},
        {"requires(T v) { v.touch(); } && ! /* unary */ enabled", false},
        {"requires(T v) { v.touch(); } @ && /* operand */ enabled", true},
    };
    for (const auto &declaration : declarations) {
        for (const auto &constraint : constraints) {
            const auto constraintText = constraint.Text;
            const auto declarationText = std::string(declaration.Specifiers) +
                " " + std::string(declaration.Declarator) + " " +
                std::string(declaration.Body);
            const auto source = "template<class T> requires " +
                std::string(constraintText) + "\n" + declarationText +
                "\nint outside;\n";
            SCOPED_TRACE(source);
            PaddedInput input(source);
            std::vector<parser::ParseError> errors;
            const auto tree = parser::Parse(input.View(),
                [&](parser::ParseError error) { errors.push_back(std::move(error)); });
            const auto invalidOffset = source.find('@');
            if (invalidOffset != std::string::npos) {
                ASSERT_EQ(errors.size(), 1u);
                EXPECT_EQ(errors.front().Message, "Unexpected token");
                EXPECT_EQ(errors.front().Position.Line, 1u);
                EXPECT_EQ(errors.front().Position.Column, invalidOffset + 2);
            } else {
                EXPECT_TRUE(errors.empty());
            }
            const auto *owner = tree.Root().FirstChild;
            ASSERT_NE(owner, nullptr);
            EXPECT_EQ(owner->Kind, parser::NodeKind::TemplateDeclaration);
            const auto *clause = owner->FirstChild;
            while (clause && clause->Kind != parser::NodeKind::RequiresClause) {
                clause = clause->NextSibling;
            }
            ASSERT_NE(clause, nullptr);
            ASSERT_NE(clause->FirstChild, nullptr);
            EXPECT_EQ(tree.SourceText(clause->FirstChild->Tokens), constraintText);
            bool unparsed = false;
            std::vector<const Node *> pending{clause};
            while (!pending.empty()) {
                const auto *node = pending.back();
                pending.pop_back();
                unparsed |= node->Kind == parser::NodeKind::UnparsedSyntax;
                EXPECT_GE(node->Tokens.Begin, clause->Tokens.Begin);
                EXPECT_LE(node->Tokens.End, clause->Tokens.End);
                for (const auto *child = node->FirstChild; child; child = child->NextSibling) {
                    pending.push_back(child);
                }
            }
            EXPECT_EQ(unparsed, constraint.Unparsed);
            size_t invalidTokens = 0;
            for (auto token = clause->Tokens.Begin; token < clause->Tokens.End; ++token) {
                if (tree.Kind(token) == tokenizer::TokenKind::Invalid) {
                    ++invalidTokens;
                    EXPECT_EQ(tree.TokenText(token), "@");
                    EXPECT_EQ(tree.Token(token).Text.Begin, invalidOffset);
                    EXPECT_EQ(tree.Token(token).Text.End, invalidOffset + 1);
                }
            }
            EXPECT_EQ(invalidTokens, invalidOffset != std::string::npos ? 1u : 0u);
            if (constraint.Unparsed) {
                const auto *tail = clause->FirstChild->LastChild;
                ASSERT_NE(tail, nullptr);
                EXPECT_EQ(tail->Kind, parser::NodeKind::UnparsedSyntax);
                EXPECT_EQ(tree.Kind(tail->Tokens.Begin), tokenizer::TokenKind::Invalid);
                EXPECT_EQ(tree.TokenText(tail->Tokens.Begin), "@");
                EXPECT_EQ(tree.SourceText(tail->Tokens), constraintText.substr(constraintText.find('@')));
                EXPECT_EQ(tail->Tokens.End, clause->Tokens.End);
            }
            const auto *function = clause->NextSibling;
            ASSERT_NE(function, nullptr);
            EXPECT_EQ(tree.SourceText(function->Tokens), declarationText);
            EXPECT_EQ(function->Tokens.Begin, clause->Tokens.End);
            EXPECT_EQ(function->Kind, parser::NodeKind::FunctionDefinition);
            if (function->Kind != parser::NodeKind::FunctionDefinition) {
                continue;
            }
            ASSERT_NE(function->FirstChild, nullptr);
            EXPECT_EQ(function->FirstChild->Kind, parser::NodeKind::DeclarationSpecifiers);
            EXPECT_EQ(tree.SourceText(function->FirstChild->Tokens), declaration.Specifiers);
            const auto *declarator = function->FirstChild->NextSibling;
            ASSERT_NE(declarator, nullptr);
            EXPECT_EQ(declarator->Kind, parser::NodeKind::InitDeclaratorList);
            EXPECT_EQ(tree.SourceText(declarator->Tokens), declaration.Declarator);
            EXPECT_EQ(function->LastChild->Kind, parser::NodeKind::CompoundStatement);
            EXPECT_EQ(tree.SourceText(function->LastChild->Tokens), declaration.Body);
            ASSERT_NE(owner->NextSibling, nullptr);
            EXPECT_EQ(tree.SourceText(owner->NextSibling->Tokens), "int outside;");
            EXPECT_EQ(owner->NextSibling->Tokens.Begin, owner->Tokens.End);
        }
    }
}

TEST(Parser, RequiresClauseKeepsUnparsedSuffixAndDiagnosesMissingBody)
{
    for (const bool missingBody : {false, true}) {
        const std::string constraintText = missingBody
            ? "requires(T value)"
            : "requires(T value) { value.touch(); } @ discarded";
        const auto source = "template<class T> requires " + constraintText +
            "\nstruct Container {};\nint outside;\n";
        SCOPED_TRACE(source);
        PaddedInput input(source);
        std::vector<parser::ParseError> errors;
        const auto tree = parser::Parse(input.View(),
            [&](parser::ParseError error) { errors.push_back(std::move(error)); });
        if (missingBody) {
            ASSERT_FALSE(errors.empty());
            EXPECT_EQ(errors.front().Message, "expected complete requirement body");
        }
        const auto *owner = tree.Root().FirstChild;
        ASSERT_NE(owner, nullptr);
        const auto *clause = owner->FirstChild;
        while (clause && clause->Kind != parser::NodeKind::RequiresClause) {
            clause = clause->NextSibling;
        }
        ASSERT_NE(clause, nullptr);
        ASSERT_NE(clause->FirstChild, nullptr);
        EXPECT_EQ(tree.SourceText(clause->FirstChild->Tokens), constraintText);
        if (!missingBody) {
            const auto *tail = clause->FirstChild->LastChild;
            ASSERT_NE(tail, nullptr);
            EXPECT_EQ(tail->Kind, parser::NodeKind::UnparsedSyntax);
            EXPECT_EQ(tree.SourceText(tail->Tokens), "@ discarded");
            EXPECT_EQ(tail->Tokens.End, clause->Tokens.End);
        }
        ASSERT_NE(clause->NextSibling, nullptr);
        EXPECT_EQ(clause->NextSibling->Kind, parser::NodeKind::RecordDeclaration);
        EXPECT_EQ(tree.SourceText(clause->NextSibling->Tokens), "struct Container {};");
        ASSERT_NE(owner->NextSibling, nullptr);
        EXPECT_EQ(tree.SourceText(owner->NextSibling->Tokens), "int outside;");
    }
}

TEST(Parser, RequiresClauseRetainsDirectiveInsideRequirementBody)
{
    PaddedInput input("template<class T> requires requires(T value) {\n"
        "#define REQUIREMENT_VALUE 1\n"
        "value.touch();\n}\nstruct Container {};\n");
    size_t errors = 0;
    const auto tree = parser::Parse(input.View(), [&](parser::ParseError) { ++errors; });
    EXPECT_EQ(errors, 0u);
    const auto *owner = tree.Root().FirstChild;
    ASSERT_NE(owner, nullptr);
    size_t bodies = 0;
    size_t directives = 0;
    std::vector<const Node *> pending{owner};
    while (!pending.empty()) {
        const auto *node = pending.back();
        pending.pop_back();
        if (node->Kind == parser::NodeKind::RequirementBody) {
            ++bodies;
            EXPECT_EQ(tree.Role(node->Tokens.Begin), parser::PunctuationRole::BlockDelimiter);
            for (auto token = node->Tokens.Begin; token < node->Tokens.End; ++token) {
                if (tree.Kind(token) == tokenizer::TokenKind::PreprocessingDirective) {
                    ++directives;
                    EXPECT_TRUE(tree.TokenText(token).starts_with("#define REQUIREMENT_VALUE 1"));
                }
            }
        }
        for (const auto *child = node->FirstChild; child; child = child->NextSibling) {
            pending.push_back(child);
        }
    }
    EXPECT_EQ(bodies, 1u);
    EXPECT_EQ(directives, 1u);
    ASSERT_NE(owner->LastChild, nullptr);
    EXPECT_EQ(owner->LastChild->Kind, parser::NodeKind::RecordDeclaration);
    EXPECT_EQ(tree.SourceText(owner->LastChild->Tokens), "struct Container {};");
}

TEST(Parser, ParameterDefaultsKeepDelimitedComparisonBoundaries)
{
    const std::string_view firstParameters[] = {
        "int value=(1<2)",
        "int value=(8>>1)",
        "int value=select((1<2),0)",
        "int value=values[1<2]",
        "Value value=Value{1<2,2}",
        "Wrap<(1>0),int> value={}",
        "Wrap<(1<<2),Wrap<(3>2),int>> value={}",
    };
    for (const auto firstText : firstParameters) {
        const auto source =
            "constexpr int values[] = {1,2};\n"
            "constexpr int select(int left,int right){return left+right;}\n"
            "struct Value { int first, second; };\n"
            "template<auto, class> struct Wrap {};\n"
            "void sink(" + std::string(firstText) + ",int other=0) {}\n"
            "Value outside;\n";
        SCOPED_TRACE(firstText);
        PaddedInput input(source);
        std::vector<parser::ParseError> errors;
        const auto tree = parser::Parse(input.View(),
            [&](parser::ParseError error) { errors.push_back(std::move(error)); });
        EXPECT_TRUE(errors.empty());
        const Node *parameters = nullptr;
        std::vector<const Node *> pending{&tree.Root()};
        while (!pending.empty()) {
            const auto *node = pending.back();
            pending.pop_back();
            if (node->Kind == parser::NodeKind::ParameterList &&
                tree.SourceText(node->Tokens).starts_with("(" + std::string(firstText)))
            {
                parameters = node;
            }
            for (auto *child = node->FirstChild; child;
                child = child->NextSibling)
            {
                pending.push_back(child);
            }
        }
        ASSERT_NE(parameters, nullptr);
        const auto *first = parameters->FirstChild;
        ASSERT_NE(first, nullptr);
        EXPECT_EQ(first->Kind, parser::NodeKind::ParameterDeclaration);
        EXPECT_EQ(tree.SourceText(first->Tokens), firstText);
        EXPECT_EQ(first->Tokens.Begin, parameters->Tokens.Begin + 1);
        const auto *comma = first->NextSibling;
        ASSERT_NE(comma, nullptr);
        EXPECT_EQ(comma->Kind, parser::NodeKind::ListSeparator);
        EXPECT_EQ(comma->Tokens.Begin, first->Tokens.End);
        EXPECT_EQ(comma->Tokens.End, first->Tokens.End + 1);
        EXPECT_EQ(tree.Role(comma->Tokens.Begin), parser::PunctuationRole::ListSeparator);
        const auto *second = comma->NextSibling;
        ASSERT_NE(second, nullptr);
        EXPECT_EQ(second->Kind, parser::NodeKind::ParameterDeclaration);
        EXPECT_EQ(tree.SourceText(second->Tokens), "int other=0");
        EXPECT_EQ(second->Tokens.Begin, comma->Tokens.End);
        EXPECT_EQ(second->Tokens.End, parameters->Tokens.End - 1);
        EXPECT_EQ(second->NextSibling, nullptr);
        size_t defaults = 0;
        for (const auto *parameter : {first, second}) {
            pending = {parameter};
            while (!pending.empty()) {
                const auto *node = pending.back();
                pending.pop_back();
                if (node->Kind == parser::NodeKind::Initializer) {
                    ++defaults;
                    EXPECT_EQ(node->Tokens.End, parameter->Tokens.End);
                    EXPECT_NE(node->FirstChild, nullptr);
                }
                for (auto *child = node->FirstChild; child;
                    child = child->NextSibling)
                {
                    pending.push_back(child);
                }
            }
        }
        EXPECT_EQ(defaults, 2u);
        const auto *outside = tree.Root().LastChild;
        ASSERT_EQ(outside->Kind, parser::NodeKind::Declaration);
        ASSERT_NE(outside->FirstChild, nullptr);
        EXPECT_EQ(outside->FirstChild->Kind, parser::NodeKind::DeclarationSpecifiers);
        EXPECT_EQ(tree.SourceText(outside->FirstChild->Tokens), "Value");
    }
}
