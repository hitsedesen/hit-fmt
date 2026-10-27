#include <parser/operator_info.hpp>
#include <parser/parser.hpp>

#include <gtest/gtest.h>

#include <tests/golden.hpp>

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

void AppendTokens(std::string& output, const parser::SyntaxTree& tree, size_t begin, size_t end, size_t depth)
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
        output += item->Kind == parser::TriviaKind::Comment ? "(comment " : "(whitespace ";
        AppendQuoted(output, tree.SourceText(item->Source));
        output += ")\n";
    }
    AppendIndent(output, depth);
    output += ")\n";
}

void AppendSExpression(std::string& output, const parser::SyntaxTree& tree, const Node& node, size_t depth)
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
        AppendTriviaList(output, tree, trivia->Leading, "leading-trivia", depth + 1);
    }
    auto cursor = node.Tokens.Begin;
    for (auto* child = node.FirstChild; child; child = child->NextSibling) {
        auto childBegin = std::clamp(child->Tokens.Begin, node.Tokens.Begin, node.Tokens.End);
        AppendTokens(output, tree, cursor, childBegin, depth + 1);
        AppendSExpression(output, tree, *child, depth + 1);
        cursor = std::max(cursor, std::min(child->Tokens.End, node.Tokens.End));
    }
    AppendTokens(output, tree, cursor, node.Tokens.End, depth + 1);
    if (trivia) {
        AppendTriviaList(output, tree, trivia->Dangling, "dangling-trivia", depth + 1);
        AppendTriviaList(output, tree, trivia->Trailing, "trailing-trivia", depth + 1);
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
        output += std::format("{}: {} {}\n", index, tree.TokenText(index), RoleName(role));
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
        return {.Data = Storage_.data(), .Size = static_cast<uint32_t>(Size_)};
    }

private:
    size_t Size_;
    std::string Storage_;
};

struct GoldenCase
{
    std::string Name;
    bool HasErrors = false;
    bool HasPunctuationRoles = false;
    std::vector<std::string> FunctionBodyMacroNames;
    std::vector<std::string> ClassMacroNames;
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
    auto tree = parser::Parse(
        paddedInput.View(),
        [&] (parser::ParseError error) {
            errors.push_back(std::move(error));
        },
        {.FunctionBodyMacroNames = GetParam().FunctionBodyMacroNames, .ClassMacroNames = GetParam().ClassMacroNames});
    auto actual = Serialize(tree);
    if (GetParam().HasErrors) {
        EXPECT_FALSE(errors.empty());
    } else {
        EXPECT_TRUE(errors.empty());
    }

    fixture.Expect(actual, ".out");
    if (GetParam().HasPunctuationRoles) {
        fixture.Expect(SerializePunctuationRoles(tree), ".roles.out");
    }
}

TEST(Parser, ClassMacroNestingHasABoundedRecoveryDepth)
{
    std::string source;
    for (size_t i = 0; i < 1024; ++i) {
        source += "OPEN_CLASS(Name)\n";
    }
    for (size_t i = 0; i < 1024; ++i) {
        source += "CLOSE_CLASS()\n";
    }
    const std::vector<std::string> pairs{"OPEN_CLASS:CLOSE_CLASS"};
    PaddedInput input(source);
    bool reportedDepth = false;
    const auto tree = parser::Parse(
        input.View(),
        [&] (parser::ParseError error) {
            reportedDepth |= error.Message == "macro class nesting is too deep";
        },
        {.ClassMacroNames = pairs});
    EXPECT_TRUE(reportedDepth);
    EXPECT_EQ(tree.SourceText(tree.Root().Tokens), std::string_view(source).substr(0, source.size() - 1));
}

// Inspect only the real owning subtree; full-file serialization is
// golden-tested.
std::vector<const Node*> LambdaOwnershipNodes(const Node& root, parser::NodeKind kind)
{
    std::vector<const Node*> found;
    std::vector<const Node*> pending{&root};
    while (!pending.empty()) {
        const auto* node = pending.back();
        pending.pop_back();
        if (node->Kind == kind) {
            found.push_back(node);
        }
        for (auto* child = node->FirstChild; child; child = child->NextSibling) {
            pending.push_back(child);
        }
    }
    return found;
}

TEST(Parser, ConditionalFragmentsKeepRolesAndBodies)
{
    const test::golden::Fixture fixture{PARSER_TESTDATA_DIR, "preprocessing_fragments"};
    PaddedInput input(fixture.InputNamed(".in"));
    auto tree = parser::Parse(input.View(), [] (parser::ParseError) {});
    const auto expressions = LambdaOwnershipNodes(tree.Root(), parser::NodeKind::Expression);
    for (const auto* expression : expressions) {
        EXPECT_NE(tree.Kind(expression->Tokens.Begin), tokenizer::TokenKind::PreprocessingDirective);
    }
    const auto constructors = LambdaOwnershipNodes(tree.Root(), parser::NodeKind::FunctionDefinition);
    size_t constructorCount = 0;
    for (const auto* function : constructors) {
        if (function->GetFunctionKind() == parser::FunctionKind::Constructor) {
            ++constructorCount;
            const auto bodies = LambdaOwnershipNodes(*function, parser::NodeKind::CompoundStatement);
            ASSERT_EQ(bodies.size(), 1u);
            EXPECT_EQ(bodies[0]->Tokens.End, function->Tokens.End);
        }
    }
    EXPECT_EQ(constructorCount, 3u);
    for (parser::TokenIndex token = 0; token + 1 < tree.TokenCount(); ++token) {
        if (tree.TokenText(token) == "*" && tree.TokenText(token + 1) == "this") {
            EXPECT_EQ(tree.Role(token), parser::PunctuationRole::UnaryOperator);
        }
    }
}

TEST(Parser, ConditionalSharedScopesKeepEnclosingDelimiters)
{
    const test::golden::Fixture fixture{PARSER_TESTDATA_DIR, "preprocessing"};
    PaddedInput input(fixture.InputNamed(".in"));
    const auto tree = parser::Parse(input.View(), [] (parser::ParseError) {});
    const auto functions = LambdaOwnershipNodes(tree.Root(), parser::NodeKind::FunctionDefinition);
    ASSERT_EQ(functions.size(), 8u);
    size_t destructorCount = 0;
    for (const auto* function : functions) {
        destructorCount += function->GetFunctionKind() == parser::FunctionKind::Destructor;
        for (auto* child = function->FirstChild; child; child = child->NextSibling) {
            if (child->Kind == parser::NodeKind::CompoundStatement) {
                EXPECT_EQ(tree.MatchingDelimiter(child->Tokens.Begin), child->Tokens.End - 1);
                EXPECT_EQ(tree.MatchingDelimiter(child->Tokens.End - 1), child->Tokens.Begin);
            }
        }
    }
    EXPECT_EQ(destructorCount, 2u);
    const auto records = LambdaOwnershipNodes(tree.Root(), parser::NodeKind::RecordDeclaration);
    ASSERT_EQ(records.size(), 2u);
    for (const auto* record : records) {
        const auto close = record->Tokens.End - 2;
        ASSERT_EQ(tree.Kind(close), tokenizer::TokenKind::Operator_CloseBrace);
        const auto open = tree.MatchingDelimiter(close);
        ASSERT_GT(open, record->Tokens.Begin);
        ASSERT_LT(open, close);
        EXPECT_EQ(tree.MatchingDelimiter(open), close);
        EXPECT_EQ(tree.Role(open), parser::PunctuationRole::RecordDelimiter);
        EXPECT_EQ(tree.Role(close), parser::PunctuationRole::RecordDelimiter);
    }
    const auto owners = LambdaOwnershipNodes(tree.Root(), parser::NodeKind::NamespaceDeclaration);
    ASSERT_EQ(owners.size(), 2u);
    for (const auto* owner : owners) {
        const auto close = owner->Tokens.End - 1;
        const auto open = tree.MatchingDelimiter(close);
        ASSERT_LT(open, close);
        EXPECT_EQ(tree.MatchingDelimiter(open), close);
    }
}

TEST(Parser, ConditionalFragmentProofIsBounded)
{
    for (const bool nesting : {false, true}) {
        std::string source = "void f(){consume(\n";
        const size_t levels = nesting ? 40 : 1;
        for (size_t i = 0; i < levels; ++i) {
            source += "#if FLAG\n";
        }
        source += "value";
        if (!nesting) {
            for (size_t i = 0; i < 2200; ++i) {
                source += "+value";
            }
        }
        source += ",\n";
        for (size_t i = 0; i < levels; ++i) {
            source += "#endif\n";
        }
        source += "*this); auto following=left*right;}\n";
        PaddedInput input(source);
        auto tree = parser::Parse(input.View(), [] (parser::ParseError) {});
        EXPECT_FALSE(LambdaOwnershipNodes(tree.Root(), parser::NodeKind::UnparsedSyntax).empty());
        bool following = false;
        for (parser::TokenIndex t = 0; t + 1 < tree.TokenCount(); ++t) {
            if (tree.TokenText(t) == "*" && tree.TokenText(t + 1) == "right") {
                EXPECT_EQ(tree.Role(t), parser::PunctuationRole::BinaryOperator);
                following = true;
            }
        }
        EXPECT_TRUE(following);
    }
}

TEST(Parser, MacroShapedControlHeaderRetainsRecoveryAndIndependentBody)
{
    using enum parser::NodeKind;
    for (const auto& [candidate, kind] :
        {
            std::pair{"if CHECK(n) { use(); }", IfStatement},
            std::pair{"while CHECK(n) { use(); }", WhileStatement},
            std::pair{"for CHECK(n) { use(); }", ForStatement},
            std::pair{"if CHECK(n) use(); else other();", IfStatement},
        })
    {
        SCOPED_TRACE(candidate);
        PaddedInput input("void sample() { " + std::string(candidate) + " }\nint following;");
        auto tree = parser::Parse(input.View(), [] (parser::ParseError) {});
        const auto controls = LambdaOwnershipNodes(tree.Root(), kind);
        ASSERT_EQ(controls.size(), 1u);
        const auto* control = controls[0];
        ASSERT_NE(control->FirstChild, nullptr);
        EXPECT_EQ(control->FirstChild->Kind, UnparsedSyntax);
        EXPECT_EQ(tree.SourceText(control->FirstChild->Tokens), "CHECK(n)");
        EXPECT_TRUE(LambdaOwnershipNodes(*control, Condition).empty());
        EXPECT_TRUE(LambdaOwnershipNodes(*control, ForHeader).empty());
        ASSERT_NE(control->FirstChild->NextSibling, nullptr);
        EXPECT_TRUE(control->FirstChild->NextSibling->Kind == CompoundStatement ||
            control->FirstChild->NextSibling->Kind == ExpressionStatement);
        EXPECT_EQ(tree.SourceText(tree.Root().LastChild->Tokens), "int following;");
    }
    PaddedInput input("void sample() { if (n) { use(); } }\nint following;");
    auto tree = parser::Parse(input.View(), [] (parser::ParseError) {});
    const auto conditions = LambdaOwnershipNodes(tree.Root(), Condition);
    ASSERT_EQ(conditions.size(), 1u);
    EXPECT_EQ(tree.SourceText(conditions[0]->Tokens), "(n)");
    EXPECT_TRUE(LambdaOwnershipNodes(tree.Root(), UnparsedSyntax).empty());
}

TEST(Parser, IfElseLookaheadLeavesFollowingStatementAttributesInSequence)
{
    using enum parser::NodeKind;
    for (const auto candidate :
        {"if (!ready) return; [[fallthrough]];", "if (outer) if (!ready) return; [[fallthrough]];"})
    {
        SCOPED_TRACE(candidate);
        PaddedInput input("void sample() { " + std::string(candidate) + " }");
        auto tree = parser::Parse(input.View(), [] (parser::ParseError) {});
        const auto controls = LambdaOwnershipNodes(tree.Root(), IfStatement);
        EXPECT_EQ(controls.size(), std::string_view(candidate).starts_with("if (outer)") ? 2u : 1u);
        for (const auto* control : controls) {
            EXPECT_TRUE(LambdaOwnershipNodes(*control, AttributeSpecifier).empty());
            EXPECT_EQ(tree.TokenText(control->Tokens.End - 1), ";");
            EXPECT_EQ(LambdaOwnershipNodes(*control, ReturnStatement).size(), 1u);
        }
        ASSERT_NE(tree.Root().FirstChild, nullptr);
        const auto* body = tree.Root().FirstChild->LastChild;
        ASSERT_NE(body, nullptr);
        ASSERT_EQ(body->Kind, CompoundStatement);
        ASSERT_NE(body->LastChild, nullptr);
        EXPECT_EQ(tree.SourceText(body->LastChild->Tokens), "[[fallthrough]];");
        EXPECT_EQ(body->LastChild->Kind, AttributedStatement);
        EXPECT_EQ(LambdaOwnershipNodes(*body->LastChild, AttributeSpecifier).size(), 1u);
        EXPECT_TRUE(LambdaOwnershipNodes(*body->LastChild, UnparsedSyntax).empty());
        EXPECT_TRUE(LambdaOwnershipNodes(*body->LastChild, MissingToken).empty());
    }
    for (const auto candidate :
        {
            "if (ready) return; else use();",
            "if (ready) return; /* boundary */ else use();",
            "if (ready) return; // boundary\nelse use();",
        })
    {
        SCOPED_TRACE(candidate);
        PaddedInput input("void sample() { " + std::string(candidate) + " }");
        auto tree = parser::Parse(input.View(), [] (parser::ParseError) {});
        const auto controls = LambdaOwnershipNodes(tree.Root(), IfStatement);
        ASSERT_EQ(controls.size(), 1u);
        EXPECT_EQ(tree.SourceText(controls[0]->Tokens), candidate);
        EXPECT_EQ(LambdaOwnershipNodes(*controls[0], ReturnStatement).size(), 1u);
        EXPECT_EQ(LambdaOwnershipNodes(*controls[0], ExpressionStatement).size(), 1u);
        EXPECT_TRUE(LambdaOwnershipNodes(*controls[0], UnparsedSyntax).empty());
        EXPECT_TRUE(LambdaOwnershipNodes(*controls[0], MissingToken).empty());
    }
}

TEST(Parser, NewAndDeleteMissingOperandsDoNotInventCompleteExpressions)
{
    using enum parser::NodeKind;
    for (const auto candidate :
        {"new (1)", "new (int)", "new (Unknown)", "new (int)(1)", "new (Unknown){1}", "new (1)(2)", "new {1}", "new"})
    {
        SCOPED_TRACE(candidate);
        PaddedInput input("void use() { " + std::string(candidate) + "; }\nint following;");
        auto tree = parser::Parse(input.View(), [] (parser::ParseError) {});
        const auto expressions = LambdaOwnershipNodes(tree.Root(), NewExpression);
        ASSERT_EQ(expressions.size(), 1u);
        EXPECT_EQ(tree.SourceText(expressions[0]->Tokens), candidate);
        EXPECT_TRUE(LambdaOwnershipNodes(*expressions[0], NewInitializer).empty());
        EXPECT_TRUE(LambdaOwnershipNodes(*expressions[0], NewTypeId).empty());
        EXPECT_TRUE(LambdaOwnershipNodes(tree.Root(), CallExpression).empty());
        EXPECT_EQ(
            LambdaOwnershipNodes(*expressions[0], UnparsedSyntax).size() +
            LambdaOwnershipNodes(*expressions[0], MissingToken).size(),
            1u);
        EXPECT_EQ(tree.SourceText(tree.Root().LastChild->Tokens), "int following;");
    }
    for (const auto candidate : {"delete []", "delete"}) {
        SCOPED_TRACE(candidate);
        PaddedInput input("void use() { " + std::string(candidate) + "; }\nint following;");
        auto tree = parser::Parse(input.View(), [] (parser::ParseError) {});
        const auto expressions = LambdaOwnershipNodes(tree.Root(), DeleteExpression);
        ASSERT_EQ(expressions.size(), 1u);
        EXPECT_EQ(tree.SourceText(expressions[0]->Tokens), candidate);
        const auto missing = LambdaOwnershipNodes(*expressions[0], MissingToken);
        ASSERT_EQ(missing.size(), 1u);
        EXPECT_TRUE(missing[0]->Tokens.Empty());
        EXPECT_EQ(missing[0]->Tokens.Begin, expressions[0]->Tokens.End);
        const auto markers = LambdaOwnershipNodes(*expressions[0], DeleteArrayMarker);
        EXPECT_EQ(markers.size(), std::string_view(candidate) == "delete []" ? 1u : 0u);
        if (!markers.empty()) {
            EXPECT_EQ(tree.SourceText(markers[0]->Tokens), "[]");
        }
        EXPECT_EQ(tree.SourceText(tree.Root().LastChild->Tokens), "int following;");
    }
    PaddedInput extraSuffix("void use() { new (1)(2)(3); }");
    auto extraTree = parser::Parse(extraSuffix.View(), [] (parser::ParseError) {});
    const auto calls = LambdaOwnershipNodes(extraTree.Root(), CallExpression);
    ASSERT_EQ(calls.size(), 1u);
    EXPECT_EQ(extraTree.SourceText(calls[0]->Tokens), "new (1)(2)(3)");
    const auto recoveredNew = LambdaOwnershipNodes(*calls[0], NewExpression);
    ASSERT_EQ(recoveredNew.size(), 1u);
    EXPECT_EQ(extraTree.SourceText(recoveredNew[0]->Tokens), "new (1)(2)");
    EXPECT_EQ(LambdaOwnershipNodes(*calls[0], UnparsedSyntax).size(), 1u);
    EXPECT_TRUE(LambdaOwnershipNodes(*calls[0], NewInitializer).empty());
    PaddedInput input("void use() { new int(1); new (arena) int(1); " "delete [] pointer; delete pointer; }");
    auto tree = parser::Parse(input.View(), [] (parser::ParseError) {});
    EXPECT_EQ(LambdaOwnershipNodes(tree.Root(), NewInitializer).size(), 2u);
    EXPECT_EQ(LambdaOwnershipNodes(tree.Root(), NewTypeId).size(), 2u);
    EXPECT_EQ(LambdaOwnershipNodes(tree.Root(), NewPlacement).size(), 1u);
    EXPECT_EQ(LambdaOwnershipNodes(tree.Root(), DeleteArrayMarker).size(), 1u);
    EXPECT_TRUE(LambdaOwnershipNodes(tree.Root(), MissingToken).empty());
    EXPECT_TRUE(LambdaOwnershipNodes(tree.Root(), UnparsedSyntax).empty());
}

TEST(Parser, SizeofPackOwnsOneNameWithoutFabricatedCallOrList)
{
    using enum parser::NodeKind;
    for (const auto candidate :
        {"sizeof... ()", "sizeof... (Values)", "sizeof... (first, second)", "sizeof... (1)", "sizeof..."})
    {
        SCOPED_TRACE(candidate);
        PaddedInput input("void use() { " + std::string(candidate) + "; }\nint following;");
        auto tree = parser::Parse(input.View(), [] (parser::ParseError) {});
        const auto expressions = LambdaOwnershipNodes(tree.Root(), UnaryExpression);
        ASSERT_EQ(expressions.size(), 1u);
        EXPECT_EQ(tree.SourceText(expressions[0]->Tokens), candidate);
        EXPECT_TRUE(LambdaOwnershipNodes(*expressions[0], CallExpression).empty());
        EXPECT_TRUE(LambdaOwnershipNodes(*expressions[0], ArgumentList).empty());
        const bool valid = std::string_view(candidate) == "sizeof... (Values)";
        EXPECT_EQ(
            LambdaOwnershipNodes(*expressions[0], UnparsedSyntax).size() +
            LambdaOwnershipNodes(*expressions[0], MissingToken).size(),
            valid ? 0u : std::string_view(candidate) == "sizeof..." ? 2u : 1u);
        if (valid) {
            const auto names = LambdaOwnershipNodes(*expressions[0], IdentifierExpression);
            ASSERT_EQ(names.size(), 1u);
            EXPECT_EQ(tree.SourceText(names[0]->Tokens), "Values");
        }
        EXPECT_EQ(tree.SourceText(tree.Root().LastChild->Tokens), "int following;");
    }
}

TEST(Parser, DirectLambdaArgumentsHaveOneExpressionOwner)
{
    using enum parser::NodeKind;
    for (const auto candidate :
        {
            "APPLY([] { return 1; }, 7);",
            "RENAMED(7, [](int v) -> int { return v; }, 8);",
            "APPLY(7, ::ops::combine(::ops::make()), " "[](const auto& v) -> std::string { return v; });",
            "APPLY([seed = 2](int v) mutable noexcept(true) -> int " "{ return v + seed; });",
            "APPLY([] -> int { return 1; });",
            "APPLY([]<class T, class U>(T left, U right) { return left; }, 7);",
            "APPLY(7, []<class T, class U>(T left, U right) " "{ return left; }, 8);",
            "APPLY(7, []<class T, class U>(T left, U right) { return left; });",
            "APPLY(7, ::ops::template make<int>(value), [] { return 2; });",
            "APPLY(7, object.template make<int>(value), [] { return 2; });",
            "APPLY(7, object.template operator()<int>(value), " "[] { return 2; });",
            "APPLY(7, object.template operator+<int>(value), " "[] { return 2; });",
            "APPLY(7, object.template operator[]<int>(value), " "[] { return 2; });",
        })
    {
        SCOPED_TRACE(candidate);
        PaddedInput input(std::string(candidate) + "\nint following;\n");
        size_t errors = 0;
        auto tree = parser::Parse(input.View(), [&] (parser::ParseError) { ++errors; });
        EXPECT_EQ(errors, 0u);
        const auto* owner = tree.Root().FirstChild;
        ASSERT_NE(owner, nullptr);
        EXPECT_EQ(owner->Kind, ExpressionStatement);
        EXPECT_EQ(LambdaOwnershipNodes(*owner, LambdaExpression).size(), 1u);
        EXPECT_EQ(LambdaOwnershipNodes(*owner, LambdaBody).size(), 1u);
        EXPECT_EQ(LambdaOwnershipNodes(*owner, StructuredBinding).size(), 0u);
        EXPECT_EQ(LambdaOwnershipNodes(*owner, CastExpression).size(), 0u);
        const auto lambdas = LambdaOwnershipNodes(*owner, LambdaExpression);
        if (!lambdas.empty()) {
            const auto source = std::string_view(candidate);
            size_t expectedBegin = std::string_view::npos;
            for (const auto spelling :
                {"[seed = 2]", "[]<class T", "[](int v)", "[](const auto& v)", "[] -> int", "[] { return"})
            {
                const auto found = source.find(spelling);
                if (found != std::string_view::npos) {
                    ASSERT_EQ(expectedBegin, std::string_view::npos);
                    EXPECT_EQ(source.find(spelling, found + 1), std::string_view::npos);
                    expectedBegin = found;
                }
            }
            ASSERT_NE(expectedBegin, std::string_view::npos);
            const auto expectedClose = source.rfind('}');
            EXPECT_EQ(
                tree.SourceText(lambdas[0]->Tokens),
                std::string_view(candidate).substr(expectedBegin, expectedClose - expectedBegin + 1));
            EXPECT_EQ(tree.Token(lambdas[0]->Tokens.End - 1).Text.Begin, expectedClose);
            EXPECT_LT(lambdas[0]->Tokens.End, tree.Root().LastChild->Tokens.Begin);
        }
        EXPECT_EQ(tree.SourceText(tree.Root().LastChild->Tokens), "int following;");
        for (const auto* list : LambdaOwnershipNodes(*owner, ArgumentList)) {
            for (auto* child = list->FirstChild; child; child = child->NextSibling) {
                EXPECT_TRUE(child->Kind == Argument || child->Kind == ListSeparator);
            }
            EXPECT_EQ(list->LastChild ? list->LastChild->Tokens.End : list->Tokens.Begin + 1, list->Tokens.End - 1);
        }
    }
    for (const auto operand :
        {
            "flag ? first, second : third",
            "flag ? other ? first, second : third, fourth : fifth",
            "flag ? first : other ? second, third : fourth",
            "(flag ? first, second : third)",
            "flag ? [] { return 1; }, second : third",
        })
    {
        SCOPED_TRACE(operand);
        PaddedInput input("void use() { consume(" + std::string(operand) + ", [] { return 2; }); }\nint following;");
        size_t errors = 0;
        auto tree = parser::Parse(input.View(), [&] (parser::ParseError) { ++errors; });
        EXPECT_EQ(errors, 0u);
        const auto lists = LambdaOwnershipNodes(tree.Root(), ArgumentList);
        ASSERT_EQ(lists.size(), 1u);
        std::vector<const Node*> arguments;
        for (auto* child = lists[0]->FirstChild; child; child = child->NextSibling) {
            if (child->Kind == Argument) {
                arguments.push_back(child);
            }
        }
        EXPECT_EQ(arguments.size(), 2u);
        if (arguments.size() != 2) {
            continue;
        }
        EXPECT_EQ(tree.SourceText(arguments[0]->Tokens), operand);
        EXPECT_EQ(tree.SourceText(arguments[1]->Tokens), "[] { return 2; }");
        EXPECT_TRUE(LambdaOwnershipNodes(*arguments[0], UnparsedSyntax).empty());
        const auto conditionals = LambdaOwnershipNodes(*arguments[0], ConditionalExpression);
        ASSERT_FALSE(conditionals.empty());
        bool commaMiddle = false;
        for (const auto* conditional : conditionals) {
            ASSERT_NE(conditional->FirstChild, nullptr);
            const auto* middle = conditional->FirstChild->NextSibling;
            ASSERT_NE(middle, nullptr);
            if (middle->Kind == BinaryExpression && tree.SourceText(middle->Tokens).find(',') != std::string_view::npos)
            {
                commaMiddle = true;
            }
        }
        EXPECT_TRUE(commaMiddle);
        EXPECT_EQ(LambdaOwnershipNodes(*arguments[1], LambdaBody).size(), 1u);
        EXPECT_EQ(tree.SourceText(tree.Root().LastChild->Tokens), "int following;");
    }
    for (const auto operand : {"flag ? first, second", "flag ? other ? first, second : third"}) {
        SCOPED_TRACE(operand);
        PaddedInput input("void use() { consume(" + std::string(operand) + ", [] { return 2; }); }\nint following;");
        auto tree = parser::Parse(input.View(), [] (parser::ParseError) {});
        EXPECT_FALSE(LambdaOwnershipNodes(tree.Root(), UnparsedSyntax).empty());
        EXPECT_TRUE(LambdaOwnershipNodes(tree.Root(), LambdaBody).empty());
        EXPECT_EQ(tree.SourceText(tree.Root().LastChild->Tokens), "int following;");
    }
}

TEST(Parser, TypedLambdaDirectInitializersRetainNamedDeclarations)
{
    using enum parser::NodeKind;
    for (const auto candidate :
        {
            "const Registry entry(7, [] { return 2; });",
            "Registry entry(7, [seed = 2](int v) noexcept -> int " "{ return v + seed; });",
            "Registry entry(wrap([] { return 2; }));",
        })
    {
        SCOPED_TRACE(candidate);
        PaddedInput input("struct Registry {};\n" + std::string(candidate) + "\nint following;\n");
        auto tree = parser::Parse(input.View(), [] (parser::ParseError) {});
        const auto* owner = tree.Root().FirstChild->NextSibling;
        ASSERT_NE(owner, nullptr);
        EXPECT_EQ(owner->Kind, Declaration);
        ASSERT_NE(owner->FirstChild, nullptr);
        EXPECT_EQ(owner->FirstChild->Kind, DeclarationSpecifiers);
        EXPECT_EQ(
            tree.SourceText(owner->FirstChild->Tokens),
            std::string_view(candidate).starts_with("const") ? "const Registry" : "Registry");
        const auto* outerList = owner->FirstChild->NextSibling;
        ASSERT_NE(outerList, nullptr);
        ASSERT_EQ(outerList->Kind, InitDeclaratorList);
        const auto* init = outerList->FirstChild;
        ASSERT_NE(init, nullptr);
        ASSERT_EQ(init->Kind, InitDeclarator);
        const auto* declarator = init->FirstChild;
        ASSERT_NE(declarator, nullptr);
        ASSERT_EQ(declarator->Kind, Declarator);
        ASSERT_NE(declarator->FirstChild, nullptr);
        EXPECT_EQ(declarator->FirstChild->Kind, DeclaratorName);
        EXPECT_EQ(tree.SourceText(declarator->FirstChild->Tokens), "entry");
        ASSERT_NE(declarator->NextSibling, nullptr);
        EXPECT_EQ(declarator->NextSibling->Kind, Initializer);
        EXPECT_LE(declarator->Tokens.End, declarator->NextSibling->Tokens.Begin);
        const auto lambdas = LambdaOwnershipNodes(*owner, LambdaExpression);
        ASSERT_EQ(lambdas.size(), 1u);
        EXPECT_EQ(
            LambdaOwnershipNodes(*lambdas[0], ParameterDeclaration).size(),
            std::string_view(candidate).find("int v") != std::string_view::npos ? 1u : 0u);
        EXPECT_EQ(LambdaOwnershipNodes(*owner, Initializer).size(), 1u);
        EXPECT_GE(LambdaOwnershipNodes(*owner, ArgumentList).size(), 1u);
        EXPECT_EQ(LambdaOwnershipNodes(*owner, LambdaBody).size(), 1u);
        EXPECT_EQ(LambdaOwnershipNodes(*owner, StructuredBinding).size(), 0u);
        EXPECT_EQ(LambdaOwnershipNodes(*owner, UnparsedSyntax).size(), 0u);
        EXPECT_EQ(tree.SourceText(tree.Root().LastChild->Tokens), "int following;");
    }
}

TEST(Parser, NestedTemplateLambdaCommasDoNotRetireDeclarations)
{
    using enum parser::NodeKind;
    for (const auto candidate :
        {
            "void declared(Box<int, []() -> int { return 1; }, int> value);",
            "void declared(Box<int, int, []() -> int { return 1; }> value);",
            "void declared(int value = make<int, " "[]() -> int { return 1; }, int>());",
            "void declared(int value = make<int, int, " "[]() -> int { return 1; }>());",
            "void declared(int value = make<int, " "[]() -> int { return 1; }, int>(7));",
            "void declared(int value = ::ops::template make<int>(other), " "int second = [] { return 1; }());",
            "void declared(Foreign<int, []() -> int { return 1; }, " "int> value);",
            "void declared(bool value = 1 < 2, " "int other = [] { return 1; }());",
            "void declared(int value = flag ? first, " "[] { return 1; } : third, int other = 0);",
            "void declared(int value = []() -> int { return 1; }());",
            "void declared(int value = []<class T, class U>(T left, U right) "
                "{ return left; }(1, 2), int other = 0);",
            "void declared(int value = wrap([]<class T, class U>"
                "(T left, U right) { return left; }), int other = 0);",
            "void declared(void (*callback)(int));",
            "void declared([[maybe_unused]] int value);",
            "void declared([ /* attribute */ [maybe_unused]] int value);",
        })
    {
        SCOPED_TRACE(candidate);
        PaddedInput input("template<class, auto, class> struct Box {};\n" "template<class, auto, class> int make();\n" +
            std::string(candidate) + "\nint following;\n");
        auto tree = parser::Parse(input.View(), [] (parser::ParseError) {});
        const auto* owner = tree.Root().LastChild;
        ASSERT_NE(owner, nullptr);
        EXPECT_EQ(tree.SourceText(owner->Tokens), "int following;");
        owner = tree.Root().FirstChild->NextSibling->NextSibling;
        ASSERT_NE(owner, nullptr);
        EXPECT_EQ(owner->Kind, FunctionDeclaration);
        EXPECT_GE(LambdaOwnershipNodes(*owner, ParameterList).size(), 1u);
        for (auto* child = owner->FirstChild; child; child = child->NextSibling) {
            EXPECT_NE(child->Kind, LambdaExpression);
        }
    }
}

TEST(Parser, LambdaSuffixOperandsAreNotBodiesAndParametersRestore)
{
    using enum parser::NodeKind;
    PaddedInput input(
        "struct Item {}; using Type = int;\n" "APPLY([](int Type) noexcept(noexcept(Item{})) -> decltype(Item{}) "
        "{ Type * item; return Item{}; });\n" "Type following;\n");
    auto tree = parser::Parse(input.View(), [] (parser::ParseError) {});
    const auto bodies = LambdaOwnershipNodes(tree.Root(), LambdaBody);
    ASSERT_EQ(bodies.size(), 1u);
    EXPECT_EQ(tree.SourceText(bodies[0]->Tokens), "{ Type * item; return Item{}; }");
    ASSERT_NE(bodies[0]->FirstChild, nullptr);
    EXPECT_EQ(bodies[0]->FirstChild->Kind, ExpressionStatement);
    EXPECT_EQ(tree.Root().LastChild->Kind, Declaration);
    EXPECT_EQ(tree.SourceText(tree.Root().LastChild->Tokens), "Type following;");
    for (const auto constraint :
        {"Check<T{}>", "Check<Box<T{}>>", "ops::template Check<T{}>", "Ready", "noexcept(T{})"})
    {
        SCOPED_TRACE(constraint);
        const std::string source = "APPLY([]<class T, class U>(T value) requires " + std::string(constraint) +
            " { return value; });\nint following;";
        PaddedInput constrained(source);
        auto parsed = parser::Parse(constrained.View(), [] (parser::ParseError) {});
        const auto actualBodies = LambdaOwnershipNodes(parsed.Root(), LambdaBody);
        EXPECT_EQ(actualBodies.size(), 1u);
        if (actualBodies.size() != 1) {
            continue;
        }
        EXPECT_EQ(parsed.SourceText(actualBodies[0]->Tokens), "{ return value; }");
        EXPECT_EQ(parsed.Token(actualBodies[0]->Tokens.Begin).Text.Begin, source.find("{ return value; }"));
        EXPECT_EQ(
            parsed.Token(actualBodies[0]->Tokens.End - 1).Text.End,
            source.find("{ return value; }") + std::string_view("{ return value; }").size());
        EXPECT_EQ(parsed.SourceText(parsed.Root().LastChild->Tokens), "int following;");
    }
    for (const auto constraint : {"Check<T{}", "Check<T{} + marker", "ops::template <T{}>"}) {
        SCOPED_TRACE(constraint);
        PaddedInput uncertain("APPLY([]<class T, class U>(T value) requires " + std::string(constraint) +
            " { return value; });\nint following;");
        auto parsed = parser::Parse(uncertain.View(), [] (parser::ParseError) {});
        EXPECT_TRUE(LambdaOwnershipNodes(parsed.Root(), LambdaBody).empty());
        EXPECT_FALSE(LambdaOwnershipNodes(parsed.Root(), UnparsedSyntax).empty());
        EXPECT_EQ(parsed.SourceText(parsed.Root().LastChild->Tokens), "int following;");
    }
}

TEST(Parser, UncertainLambdaSuffixCannotDiscoverDownstreamBody)
{
    using enum parser::NodeKind;
    for (const auto candidate :
        {
            "APPLY([] marker(int hidden) { return hidden; });",
            "APPLY([]);",
            "APPLY([]() -> { return 1; });",
            "APPLY([]() noexcept(1) marker { return 1; }, [] { return 2; });",
            "APPLY([]<class T, class U(int hidden) { return hidden; }, 7);",
        })
    {
        SCOPED_TRACE(candidate);
        PaddedInput input(std::string(candidate) + "\nint following;\n");
        size_t errors = 0;
        auto tree = parser::Parse(input.View(), [&] (parser::ParseError) { ++errors; });
        const auto* owner = tree.Root().FirstChild;
        ASSERT_NE(owner, nullptr);
        EXPECT_EQ(owner->Kind, ExpressionStatement);
        EXPECT_GT(errors, 0u);
        EXPECT_GT(
            LambdaOwnershipNodes(*owner, UnparsedSyntax).size() + LambdaOwnershipNodes(*owner, MissingToken).size(),
            0u);
        const auto bodies = LambdaOwnershipNodes(*owner, LambdaBody);
        EXPECT_EQ(bodies.size(), std::string_view(candidate).find("return 2") != std::string_view::npos ? 1u : 0u);
        EXPECT_EQ(LambdaOwnershipNodes(*owner, StructuredBinding).size(), 0u);
        EXPECT_EQ(tree.SourceText(tree.Root().LastChild->Tokens), "int following;");
    }
}

TEST(Parser, LeadingGlobalArgumentsRetainMalformedLocalTails)
{
    using enum parser::NodeKind;
    for (const auto operand : {"::ops::make()", "::ops::combine(::ops::make())", "::", "::ops::"}) {
        SCOPED_TRACE(operand);
        const std::string source = "APPLY(" + std::string(operand) + ", [] { return 2; });\nint following;";
        PaddedInput input(source);
        auto tree = parser::Parse(input.View(), [] (parser::ParseError) {});
        const auto* owner = tree.Root().FirstChild;
        ASSERT_NE(owner, nullptr);
        EXPECT_EQ(owner->Kind, ExpressionStatement);
        EXPECT_EQ(LambdaOwnershipNodes(*owner, LambdaBody).size(), 1u);
        const auto lists = LambdaOwnershipNodes(*owner, ArgumentList);
        ASSERT_FALSE(lists.empty());
        const auto outer = std::ranges::find_if(lists, [] (const Node* list) { return list->Tokens.Begin == 1; });
        ASSERT_NE(outer, lists.end());
        unsigned directArguments = 0;
        for (auto* child = (*outer)->FirstChild; child; child = child->NextSibling) {
            directArguments += child->Kind == Argument;
        }
        EXPECT_EQ(directArguments, 2u);
        EXPECT_EQ(LambdaOwnershipNodes(*owner, UnparsedSyntax).empty(), std::string_view(operand).ends_with(')'));
        EXPECT_EQ(tree.SourceText(tree.Root().LastChild->Tokens), "int following;");
    }
}

TEST(Parser, TypedInitializerProofExhaustionCannotRetireDefaultParameters)
{
    using enum parser::NodeKind;
    for (const auto count : {4094u, 4096u, 4100u}) {
        std::string source = "void declared(int value = make<";
        for (unsigned i = 0; i < count; ++i) {
            source += "int,";
        }
        source += "[]() -> int { return 1; }, int>());\nint following;";
        PaddedInput input(source);
        auto tree = parser::Parse(input.View(), [] (parser::ParseError) {});
        const auto* owner = tree.Root().FirstChild;
        ASSERT_NE(owner, nullptr);
        EXPECT_EQ(owner->Kind, FunctionDeclaration);
        for (auto* child = owner->FirstChild; child; child = child->NextSibling) {
            EXPECT_NE(child->Kind, LambdaExpression);
        }
        EXPECT_EQ(tree.SourceText(tree.Root().LastChild->Tokens), "int following;");
    }
}

TEST(Parser, ArgumentRecoveryOwnsConsumedSemanticTailsAndKeepsLaterLambda)
{
    using enum parser::NodeKind;
    for (const auto [operand, expectedTail] :
        {std::pair<std::string_view, std::string_view>{"char**", "**"}, {"*", "*"}, {"value + *", "+ *"}})
    {
        SCOPED_TRACE(operand);
        const std::string source = "APPLY(" + std::string(operand) + ", [] { return 2; });\nint following;";
        PaddedInput input(source);
        auto tree = parser::Parse(input.View(), [] (parser::ParseError) {});
        const auto* owner = tree.Root().FirstChild;
        ASSERT_NE(owner, nullptr);
        ASSERT_EQ(owner->Kind, ExpressionStatement);
        const auto lists = LambdaOwnershipNodes(*owner, ArgumentList);
        ASSERT_EQ(lists.size(), 1u);
        const auto* first = lists[0]->FirstChild;
        ASSERT_NE(first, nullptr);
        ASSERT_EQ(first->Kind, Argument);
        ASSERT_NE(first->LastChild, nullptr);
        EXPECT_EQ(first->LastChild->Kind, UnparsedSyntax);
        EXPECT_EQ(tree.SourceText(first->LastChild->Tokens), expectedTail);
        EXPECT_EQ(first->LastChild->Tokens.End, first->Tokens.End);
        auto cursor = first->Tokens.Begin;
        for (auto* child = first->FirstChild; child; child = child->NextSibling) {
            EXPECT_LE(cursor, child->Tokens.Begin);
            EXPECT_LE(child->Tokens.End, first->Tokens.End);
            cursor = child->Tokens.End;
        }
        ASSERT_NE(first->NextSibling, nullptr);
        ASSERT_EQ(first->NextSibling->Kind, ListSeparator);
        ASSERT_NE(first->NextSibling->NextSibling, nullptr);
        const auto* next = first->NextSibling->NextSibling;
        ASSERT_EQ(next->Kind, Argument);
        const auto lambdas = LambdaOwnershipNodes(*next, LambdaExpression);
        ASSERT_EQ(lambdas.size(), 1u);
        EXPECT_EQ(tree.SourceText(lambdas[0]->Tokens), "[] { return 2; }");
        EXPECT_EQ(LambdaOwnershipNodes(*next, LambdaBody).size(), 1u);
        EXPECT_EQ(tree.SourceText(tree.Root().LastChild->Tokens), "int following;");
    }
}

TEST(Parser, CompleteArgumentRangesExcludeOnlyTrailingCommentTrivia)
{
    using enum parser::NodeKind;
    PaddedInput input("APPLY(\"head\" /* join */ \"tail\" /* trailing */, " "[] { return 2; });\nint following;");
    auto tree = parser::Parse(input.View(), [] (parser::ParseError) {});
    const auto lists = LambdaOwnershipNodes(*tree.Root().FirstChild, ArgumentList);
    ASSERT_EQ(lists.size(), 1u);
    const auto* first = lists[0]->FirstChild;
    ASSERT_NE(first, nullptr);
    ASSERT_NE(first->FirstChild, nullptr);
    EXPECT_EQ(first->FirstChild->Kind, LiteralExpression);
    EXPECT_EQ(first->Tokens.End, first->FirstChild->Tokens.End);
    EXPECT_EQ(tree.SourceText(first->Tokens), "\"head\" /* join */ \"tail\"");
    EXPECT_TRUE(LambdaOwnershipNodes(*first, UnparsedSyntax).empty());
    EXPECT_EQ(LambdaOwnershipNodes(*lists[0], LambdaBody).size(), 1u);
    EXPECT_EQ(tree.SourceText(tree.Root().LastChild->Tokens), "int following;");
}

TEST(Parser, LambdaArgumentFactsStayWithinConditionalAndMalformedLimits)
{
    using enum parser::NodeKind;
    PaddedInput branches("#if CHOICE\nAPPLY([](int value) -> int { return value; });\n"
        "#else\nvoid declared(int value = [] { return 1; }());\n" "#endif\nint following;");
    auto branchTree = parser::Parse(branches.View(), [] (parser::ParseError) {});
    EXPECT_EQ(LambdaOwnershipNodes(branchTree.Root(), LambdaBody).size(), 2u);
    EXPECT_EQ(LambdaOwnershipNodes(branchTree.Root(), ExpressionStatement).size(), 1u);
    EXPECT_EQ(LambdaOwnershipNodes(branchTree.Root(), FunctionDeclaration).size(), 1u);
    EXPECT_EQ(branchTree.SourceText(branchTree.Root().LastChild->Tokens), "int following;");
    for (const auto source :
        {
            "APPLY([](int value) -> int { return value; );\nint following;",
            "#if CHOICE\nAPPLY([\n#else\n] { return 1; });\n#endif\n" "int following;",
        })
    {
        SCOPED_TRACE(source);
        PaddedInput input(source);
        size_t errors = 0;
        auto tree = parser::Parse(input.View(), [&] (parser::ParseError) { ++errors; });
        if (!std::string_view(source).starts_with("#if")) {
            EXPECT_GT(errors, 0u);
        }
        // Malformed-delimiter Recovery is lexical, not necessarily an AST
        // marker; the public-pipeline property checks its protected copies.
        EXPECT_TRUE(LambdaOwnershipNodes(tree.Root(), LambdaBody).empty());
        EXPECT_TRUE(LambdaOwnershipNodes(tree.Root(), LambdaExpression).empty());
        for (const auto* branch : LambdaOwnershipNodes(tree.Root(), PreprocessingBranch)) {
            for (auto* child = branch->FirstChild; child; child = child->NextSibling) {
                EXPECT_LE(branch->Tokens.Begin, child->Tokens.Begin);
                EXPECT_LE(child->Tokens.End, branch->Tokens.End);
            }
        }
        EXPECT_LE(tree.Root().Tokens.End, tree.TokenCount());
    }
}

TEST(Parser, RecoveredLambdaParametersNeverBindOutsideTheLambda)
{
    using enum parser::NodeKind;
    PaddedInput input("using Type = int; APPLY([](int Type) marker { Type * hidden; }); " "Type following;");
    size_t errors = 0;
    auto tree = parser::Parse(input.View(), [&] (parser::ParseError) { ++errors; });
    EXPECT_GT(errors, 0u);
    EXPECT_TRUE(LambdaOwnershipNodes(tree.Root(), LambdaBody).empty());
    EXPECT_EQ(LambdaOwnershipNodes(tree.Root(), ParameterDeclaration).size(), 1u);
    EXPECT_GT(LambdaOwnershipNodes(tree.Root(), UnparsedSyntax).size(), 0u);
    EXPECT_EQ(tree.Root().LastChild->Kind, Declaration);
    EXPECT_EQ(tree.SourceText(tree.Root().LastChild->Tokens), "Type following;");
}

TEST(Parser, ExplicitTemplateLambdaDefaultsKeepNonfinalOuterParameters)
{
    using enum parser::NodeKind;
    for (const auto expression :
        {
            "[]<class T, class U>(T left, U right) { return left; }(1, 2)",
            "wrap([]<class T, class U>(T left, U right) { return left; })",
        })
    {
        PaddedInput input("void declared(int value = " + std::string(expression) + ", int other = 0);\nint following;");
        auto tree = parser::Parse(input.View(), [] (parser::ParseError) {});
        const auto* owner = tree.Root().FirstChild;
        ASSERT_NE(owner, nullptr);
        EXPECT_EQ(owner->Kind, FunctionDeclaration);
        const auto lists = LambdaOwnershipNodes(*owner, ParameterList);
        const auto outer = std::ranges::find_if(lists, [] (const Node* list) { return list->Tokens.Begin == 2; });
        ASSERT_NE(outer, lists.end());
        unsigned parameters = 0;
        for (auto* child = (*outer)->FirstChild; child; child = child->NextSibling) {
            parameters += child->Kind == ParameterDeclaration;
        }
        EXPECT_EQ(parameters, 2u);
        EXPECT_EQ(LambdaOwnershipNodes(*owner, LambdaExpression).size(), 1u);
        EXPECT_EQ(LambdaOwnershipNodes(*owner, LambdaBody).size(), 1u);
        EXPECT_EQ(LambdaOwnershipNodes(*owner, TemplateParameterList).size(), 1u);
        for (auto* child = owner->FirstChild; child; child = child->NextSibling) {
            EXPECT_NE(child->Kind, LambdaExpression);
        }
        EXPECT_EQ(tree.SourceText(tree.Root().LastChild->Tokens), "int following;");
    }
}

TEST(Parser, MalformedExplicitTemplatePrefixesStayLocallyUncertain)
{
    using enum parser::NodeKind;
    for (const auto operand :
        {
            "::template make<int>(value)",
            "::ops::template <int>(value)",
            "::ops::template make<int(value)",
            "::ops::template",
            "::ops::template operator",
        })
    {
        SCOPED_TRACE(operand);
        PaddedInput input("void use() { APPLY(" + std::string(operand) + ", [] { return 2; }); }\nint following;");
        auto tree = parser::Parse(input.View(), [] (parser::ParseError) {});
        EXPECT_GT(LambdaOwnershipNodes(tree.Root(), UnparsedSyntax).size(), 0u);
        EXPECT_EQ(tree.SourceText(tree.Root().LastChild->Tokens), "int following;");
    }
}

TEST(Parser, LambdaBodyBindingsUseOnlyDirectParametersNotRequiresLocals)
{
    using enum parser::NodeKind;
    for (const bool actualParameter : {false, true}) {
        const std::string signature = actualParameter ? "(int Type)" : "";
        PaddedInput input("using Type = int; using Other = int; APPLY([]<class T>" + signature +
            " requires requires(int Other) { Other + 1; } " "{ Type * item; Other * local; });\n"
                "Type following; Other followingOther;");
        auto tree = parser::Parse(input.View(), [] (parser::ParseError) {});
        const auto lambdas = LambdaOwnershipNodes(tree.Root(), LambdaExpression);
        ASSERT_EQ(lambdas.size(), 1u);
        const auto bodies = LambdaOwnershipNodes(*lambdas[0], LambdaBody);
        ASSERT_EQ(bodies.size(), 1u);
        const auto* first = bodies[0]->FirstChild;
        ASSERT_NE(first, nullptr);
        EXPECT_EQ(first->Kind, actualParameter ? ExpressionStatement : DeclarationStatement);
        ASSERT_NE(first->NextSibling, nullptr);
        EXPECT_EQ(first->NextSibling->Kind, DeclarationStatement);
        EXPECT_EQ(tree.SourceText(first->NextSibling->Tokens), "Other * local;");
        const auto* outside = tree.Root().LastChild;
        ASSERT_NE(outside, nullptr);
        EXPECT_EQ(outside->Kind, Declaration);
        EXPECT_EQ(tree.SourceText(outside->Tokens), "Other followingOther;");
        const Node* previous = nullptr;
        for (auto* child = tree.Root().FirstChild; child && child != outside; child = child->NextSibling) {
            previous = child;
        }
        ASSERT_NE(previous, nullptr);
        EXPECT_EQ(previous->Kind, Declaration);
        EXPECT_EQ(tree.SourceText(previous->Tokens), "Type following;");
    }
}

TEST(Parser, ExplicitOperatorTemplateNamesPreserveArgumentAndDefaultBoundaries)
{
    using enum parser::NodeKind;
    const auto checkOuterParameters = [] (const parser::SyntaxTree& tree, const std::string& call) {
        const auto functions = LambdaOwnershipNodes(*tree.Root().FirstChild, FunctionDeclarator);
        const Node* outer = nullptr;
        for (const auto* function : functions) {
            if (function->Tokens.Begin == 2) {
                ASSERT_EQ(outer, nullptr);
                outer = function->FirstChild;
            }
        }
        ASSERT_NE(outer, nullptr);
        ASSERT_EQ(outer->Kind, ParameterList);
        std::vector<const Node*> parameters;
        for (auto* child = outer->FirstChild; child; child = child->NextSibling) {
            if (child->Kind == ParameterDeclaration) {
                parameters.push_back(child);
            }
        }
        ASSERT_EQ(parameters.size(), 2u);
        const std::string first = "int value = " + call;
        constexpr std::string_view second = "int other = [] { return 1; }()";
        EXPECT_EQ(tree.SourceText(parameters[0]->Tokens), first);
        EXPECT_EQ(tree.SourceText(parameters[1]->Tokens), second);
        EXPECT_EQ(parameters[0]->Tokens.Begin, outer->Tokens.Begin + 1);
        EXPECT_EQ(tree.Token(parameters[0]->Tokens.Begin).Text.Begin, std::string_view("void declared(").size());
        EXPECT_EQ(
            tree.Token(parameters[0]->Tokens.End - 1).Text.End,
            std::string_view("void declared(").size() + first.size());
        EXPECT_EQ(
            tree.Token(parameters[1]->Tokens.Begin).Text.Begin,
            std::string_view("void declared(").size() + first.size() + 2);
        EXPECT_EQ(parameters[1]->Tokens.End, outer->Tokens.End - 1);
        EXPECT_EQ(
            tree.Token(parameters[1]->Tokens.End - 1).Text.End,
            std::string_view("void declared(").size() + first.size() + 2 + second.size());
    };
    for (const auto operand :
        {
            "flag ? first, second : third",
            "flag ? other ? first, second : third, fourth : fifth",
            "flag ? first : other ? second, third : fourth",
        })
    {
        SCOPED_TRACE(operand);
        PaddedInput input(
            "void declared(int value = " + std::string(operand) + ", int other = [] { return 1; }());\nint following;");
        auto tree = parser::Parse(input.View(), [] (parser::ParseError) {});
        EXPECT_EQ(tree.Root().FirstChild->Kind, FunctionDeclaration);
        checkOuterParameters(tree, operand);
    }
    for (const auto spelling :
        {"()", "[]", "+", "!", ",", "<<", "<", "->", "->*", "co_await", "new", "new[]", "delete", "delete[]"})
    {
        SCOPED_TRACE(spelling);
        const std::string call = "object.template operator " + std::string(spelling) + " <int, short>(value)";
        PaddedInput input("APPLY(7, " + call + ", [] { return 2; });\nint following;");
        auto tree = parser::Parse(input.View(), [] (parser::ParseError) {});
        const auto* owner = tree.Root().FirstChild;
        ASSERT_NE(owner, nullptr);
        ASSERT_EQ(owner->Kind, ExpressionStatement);
        const auto* expression = owner->FirstChild;
        ASSERT_NE(expression, nullptr);
        const Node* list = nullptr;
        for (auto* child = expression->FirstChild; child; child = child->NextSibling) {
            if (child->Kind == ArgumentList) {
                list = child;
            }
        }
        ASSERT_NE(list, nullptr);
        unsigned arguments = 0;
        const Node* qualified = nullptr;
        for (auto* child = list->FirstChild; child; child = child->NextSibling) {
            if (child->Kind == Argument) {
                if (++arguments == 2) {
                    qualified = child;
                }
            }
        }
        EXPECT_EQ(arguments, 3u);
        ASSERT_NE(qualified, nullptr);
        EXPECT_TRUE(LambdaOwnershipNodes(*qualified, UnparsedSyntax).empty());
        ASSERT_NE(qualified->FirstChild, nullptr);
        EXPECT_EQ(qualified->FirstChild->Kind, CallExpression);
        EXPECT_EQ(qualified->FirstChild->Tokens.End, qualified->Tokens.End);
        EXPECT_EQ(LambdaOwnershipNodes(*owner, LambdaBody).size(), 1u);
        EXPECT_TRUE(LambdaOwnershipNodes(*owner, StructuredBinding).empty());
        EXPECT_TRUE(LambdaOwnershipNodes(*owner, CastExpression).empty());
        EXPECT_EQ(tree.SourceText(tree.Root().LastChild->Tokens), "int following;");

        PaddedInput declaration(
            "void declared(int value = " + call + ", int other = [] { return 1; }());\nint following;");
        auto declared = parser::Parse(declaration.View(), [] (parser::ParseError) {});
        EXPECT_EQ(declared.Root().FirstChild->Kind, FunctionDeclaration);
        checkOuterParameters(declared, call);
        EXPECT_EQ(LambdaOwnershipNodes(*declared.Root().FirstChild, LambdaBody).size(), 1u);
        EXPECT_TRUE(LambdaOwnershipNodes(*declared.Root().FirstChild, StructuredBinding).empty());
        EXPECT_EQ(declared.SourceText(declared.Root().LastChild->Tokens), "int following;");
    }
    for (const auto spelling : {",", "<"}) {
        SCOPED_TRACE(spelling);
        const std::string call =
            "make<Box<object.template operator " + std::string(spelling) + " <int>(value), int>, int>()";
        PaddedInput input("APPLY(" + call + ", [] { return 2; });\nint following;");
        auto tree = parser::Parse(input.View(), [] (parser::ParseError) {});
        EXPECT_EQ(tree.Root().FirstChild->Kind, ExpressionStatement);
        EXPECT_EQ(LambdaOwnershipNodes(*tree.Root().FirstChild, LambdaBody).size(), 1u);
        EXPECT_TRUE(LambdaOwnershipNodes(*tree.Root().FirstChild, StructuredBinding).empty());
        EXPECT_EQ(tree.SourceText(tree.Root().LastChild->Tokens), "int following;");
        PaddedInput declaration(
            "void declared(int value = " + call + ", int other = [] { return 1; }());\nint following;");
        auto declared = parser::Parse(declaration.View(), [] (parser::ParseError) {});
        EXPECT_EQ(declared.Root().FirstChild->Kind, FunctionDeclaration);
        checkOuterParameters(declared, call);
        EXPECT_EQ(LambdaOwnershipNodes(*declared.Root().FirstChild, LambdaBody).size(), 1u);
        EXPECT_EQ(declared.SourceText(declared.Root().LastChild->Tokens), "int following;");
    }
    {
        PaddedInput input("void use() { APPLY(make<Box<object.template operator ? "
            "<int>(value), int>, int>(), [] { return 2; }); }\n" "int following;");
        auto tree = parser::Parse(input.View(), [] (parser::ParseError) {});
        EXPECT_FALSE(LambdaOwnershipNodes(tree.Root(), UnparsedSyntax).empty());
        EXPECT_TRUE(LambdaOwnershipNodes(tree.Root(), StructuredBinding).empty());
        EXPECT_LE(LambdaOwnershipNodes(tree.Root(), LambdaBody).size(), 1u);
        EXPECT_EQ(tree.SourceText(tree.Root().LastChild->Tokens), "int following;");
    }
    for (const auto spelling : {"(int)", "[index]", "?", ".", ".*", "sizeof", ""}) {
        SCOPED_TRACE(spelling);
        PaddedInput input("void use() { APPLY(object.template operator " + std::string(spelling) +
            " <int>(value), [] { return 2; }); }\nint following;");
        auto tree = parser::Parse(input.View(), [] (parser::ParseError) {});
        EXPECT_GT(LambdaOwnershipNodes(tree.Root(), UnparsedSyntax).size(), 0u);
        const auto bodies = LambdaOwnershipNodes(tree.Root(), LambdaBody);
        EXPECT_LE(bodies.size(), 1u);
        for (const auto* body : bodies) {
            EXPECT_EQ(tree.SourceText(body->Tokens), "{ return 2; }");
        }
        EXPECT_EQ(tree.SourceText(tree.Root().LastChild->Tokens), "int following;");
    }
}

TEST(Parser, NamelessDirectBodyIsBoundedRecoveryWithoutInventedScope)
{
    for (const auto candidate :
        {
            "HEADER_BLOCK(Thing, test, const Item *, input) { return input; }",
            "OPAQUE_BODY(Label, char*(Box<char*>, Box<bool>)) " "{ int value=1+2; return value; };",
            "HEADER_BLOCK(Type) { Type * item; }",
            "HEADER_BLOCK(Tag) { using Hidden=int; int member; }",
            "HEADER_BLOCK(/* head */ Tag) /* body */ { if(true){return 1;} }",
        })
    {
        const auto source = "struct Type {};\n" + std::string(candidate) +
            "\n// independent\nint following(int value){return value+1;}\n";
        SCOPED_TRACE(source);
        PaddedInput input(source);
        size_t errors = 0;
        const auto tree = parser::Parse(input.View(), [&] (parser::ParseError) { ++errors; });
        EXPECT_EQ(errors, 0u);
        const auto* owner = tree.Root().FirstChild->NextSibling;
        ASSERT_NE(owner, nullptr);
        ASSERT_EQ(owner->Kind, parser::NodeKind::Declaration);
        EXPECT_EQ(tree.SourceText(owner->Tokens), candidate);
        ASSERT_NE(owner->FirstChild, nullptr);
        EXPECT_EQ(owner->FirstChild->Kind, parser::NodeKind::UnparsedSyntax);
        EXPECT_EQ(owner->FirstChild->Tokens.Begin, owner->Tokens.Begin);
        EXPECT_EQ(owner->FirstChild->Tokens.End, owner->Tokens.End);
        EXPECT_EQ(owner->FirstChild->NextSibling, nullptr);
        EXPECT_EQ(owner->FirstChild->FirstChild, nullptr);
        const auto lexical = parser::SyntaxTree::TokenizeOnly(input.View());
        for (auto i = owner->Tokens.Begin; i < owner->Tokens.End; ++i) {
            EXPECT_EQ(tree.Role(i), lexical.Role(i));
        }
        const auto* following = tree.Root().LastChild;
        ASSERT_NE(following, nullptr);
        EXPECT_EQ(following->Kind, parser::NodeKind::FunctionDefinition);
        EXPECT_EQ(tree.SourceText(following->Tokens), "int following(int value){return value+1;}");
        EXPECT_EQ(following->LastChild->Kind, parser::NodeKind::CompoundStatement);
    }

    PaddedInput separated("HEADER_BLOCK(Tag) {}\n// separate\n;\n#define FLAG 1\n" "int following;\n");
    const auto separatedTree = parser::Parse(separated.View(), [] (parser::ParseError) {});
    const auto* owner = separatedTree.Root().FirstChild;
    ASSERT_NE(owner, nullptr);
    EXPECT_EQ(separatedTree.SourceText(owner->Tokens), "HEADER_BLOCK(Tag) {}");
    ASSERT_NE(owner->NextSibling, nullptr);
    EXPECT_EQ(owner->NextSibling->Kind, parser::NodeKind::Comment);
    ASSERT_NE(owner->NextSibling->NextSibling, nullptr);
    EXPECT_EQ(owner->NextSibling->NextSibling->Kind, parser::NodeKind::EmptyDeclaration);
    EXPECT_EQ(separatedTree.SourceText(separatedTree.Root().LastChild->Tokens), "int following;");

    PaddedInput malformed("HEADER_BLOCK(Tag) { int value;\n");
    size_t errors = 0;
    const auto tree = parser::Parse(malformed.View(), [&] (parser::ParseError) { ++errors; });
    EXPECT_GT(errors, 0u);
    ASSERT_NE(tree.Root().FirstChild, nullptr);
    EXPECT_EQ(tree.Root().FirstChild->Kind, parser::NodeKind::Declaration);
    ASSERT_NE(tree.Root().FirstChild->FirstChild, nullptr);
    EXPECT_EQ(tree.Root().FirstChild->FirstChild->Kind, parser::NodeKind::UnparsedSyntax);
}

TEST(Parser, GenuineNamedBodiesRetainDeclaratorsAndScopes)
{
    const std::string source = "struct Type {};\n" "struct Record { Record(){} operator bool() const {return true;} "
        "int method(int value=[]{return 3;}())" "{if(value){return value;}return 0;} };\n" "Record::Record(){}\n"
        "Type* function(Type* value)" "{while(value){return value;}return nullptr;}\n"
        "void defaults(int value=[]{return 1;}()){}\n";
    PaddedInput input(source);
    size_t errors = 0;
    const auto tree = parser::Parse(input.View(), [&] (parser::ParseError) { ++errors; });
    EXPECT_EQ(errors, 0u);
    size_t functions = 0;
    size_t lambdas = 0;
    std::vector<const Node*> pending{&tree.Root()};
    while (!pending.empty()) {
        const auto* node = pending.back();
        pending.pop_back();
        EXPECT_NE(node->Kind, parser::NodeKind::UnparsedSyntax);
        if (node->Kind == parser::NodeKind::FunctionDefinition) {
            ++functions;
            ASSERT_NE(node->LastChild, nullptr);
            EXPECT_EQ(node->LastChild->Kind, parser::NodeKind::CompoundStatement);
        }
        lambdas += node->Kind == parser::NodeKind::LambdaExpression;
        auto end = node->Tokens.Begin;
        for (auto* child = node->FirstChild; child; child = child->NextSibling) {
            EXPECT_GE(child->Tokens.Begin, end);
            EXPECT_LE(child->Tokens.End, node->Tokens.End);
            end = child->Tokens.End;
            pending.push_back(child);
        }
    }
    EXPECT_EQ(functions, 6u);
    EXPECT_EQ(lambdas, 2u);
}

TEST(Parser, UnknownInvocationDoesNotAbsorbIndependentDeclaration)
{
    for (const std::string_view separator : {" ", "\n", " /* join */ ", " /* join\n */ ", " // join\n", "\r\n"}) {
        for (const std::string_view following :
            {
                "const Item *Factory::build(int n) const { return nullptr; }",
                "[[maybe_unused]] static bool check(const Item *p) " "{ return p; }",
            })
        {
            const auto source = "META(Key, const Item *, const Value *)" + std::string(separator) +
                std::string(following) + "\nint tail;\n";
            SCOPED_TRACE(source);
            PaddedInput input(source);
            auto tree = parser::Parse(input.View());
            const auto* unknown = tree.Root().FirstChild;
            ASSERT_NE(unknown, nullptr);
            EXPECT_EQ(unknown->Kind, parser::NodeKind::UnparsedSyntax);
            EXPECT_EQ(tree.SourceText(unknown->Tokens), "META(Key, const Item *, const Value *)");
            auto* function = unknown->NextSibling;
            while (function && function->Kind == parser::NodeKind::Comment) {
                function = function->NextSibling;
            }
            ASSERT_NE(function, nullptr);
            EXPECT_EQ(function->Kind, parser::NodeKind::FunctionDefinition);
            EXPECT_EQ(tree.SourceText(function->Tokens), following);
            EXPECT_EQ(tree.SourceText(tree.Root().LastChild->Tokens), "int tail;");
        }
    }
}

TEST(Parser, RecoveredFunctionTryCandidatesKeepHandlersOpaque)
{
    for (const std::string_view header :
        {
            "Vec NOTE(0) load(int n) noexcept(true)",
            "RAW(int n) noexcept(true)",
            "RAW(int n) & noexcept",
            "Vec NOTE(0) load(int n) const volatile & noexcept",
            "Vec NOTE(0) load(int n) && noexcept(true) [[maybe_unused]]",
            "Vec NOTE(0) load(int n) requires Ready<int> && " "requires(int x) { x; }",
            "Vec NOTE(0) load(int n) requires (Ready<int>)",
            "Vec NOTE(0) load(int n) requires (Ready<int> && requires { 1; })",
        })
    {
        for (const bool functionTry : {false, true}) {
            const auto candidate = std::string(header) + (functionTry ? " try" : "") + " { return n; }" +
                (functionTry
                    ? " /* first */ catch(int Type) { Type * value; }" " /* between */ catch(...) { return 0; }"
                    : "");
            PaddedInput input(candidate + "\nint tail(){return 1;}\n");
            SCOPED_TRACE(candidate);
            auto tree = parser::Parse(input.View());
            const auto* recovery = tree.Root().FirstChild;
            ASSERT_NE(recovery, nullptr);
            EXPECT_EQ(recovery->Kind, parser::NodeKind::UnparsedSyntax);
            EXPECT_EQ(tree.SourceText(recovery->Tokens), candidate);
            std::vector<const Node*> pending{recovery};
            while (!pending.empty()) {
                const auto* node = pending.back();
                pending.pop_back();
                EXPECT_NE(node->Kind, parser::NodeKind::FunctionDefinition);
                EXPECT_NE(node->Kind, parser::NodeKind::CompoundStatement);
                EXPECT_NE(node->Kind, parser::NodeKind::ParameterDeclaration);
                EXPECT_NE(node->Kind, parser::NodeKind::DeclarationSpecifiers);
                for (auto* child = node->FirstChild; child; child = child->NextSibling) {
                    pending.push_back(child);
                }
            }
            ASSERT_NE(recovery->NextSibling, nullptr);
            EXPECT_EQ(tree.SourceText(recovery->NextSibling->Tokens), "int tail(){return 1;}");
        }
    }
}

TEST(Parser, RecoveredConstructorInitializersKeepWholeCandidateOpaque)
{
    for (const std::string_view initializers :
        {"member{n}", "member(n)", "first{n}, second(n), third{}", "Base<int>{n}, member(n)"})
    {
        for (const std::string_view suffix :
            {
                "",
                " noexcept",
                " noexcept(true)",
                " noexcept(noexcept(Value{1}))",
                " throw()",
                " [[maybe_unused]]",
                " __attribute__((unused))",
                " noexcept [[maybe_unused]]",
                " [[tag(Value{1})]] noexcept(false)",
                " requires Ready<int>",
                " requires requires(int x) { x; }",
                " requires Ready<int> && requires(int x) { x; }",
                " requires (Ready<int> && requires { 1; })",
                " requires (Ready<int>) || true",
                " requires (Ready<int>) && (Other<int>)",
                " requires (Ready<int>) || probe<Type>()",
                " requires Ready<int> && requires(int Type) { Type; } || true",
                " requires lhs < rhs",
                " requires lhs < probe(rhs)",
                " requires lhs = rhs",
                " requires (lhs = rhs) || true",
            })
        {
            for (const bool functionTry : {false, true}) {
                const auto candidate =
                    "NOTE(0) C(int n)" + std::string(suffix) + std::string(functionTry ? " try : " : " : ") +
                        std::string(initializers) + " { Type * value; use(n); }" +
                        (functionTry
                            ? " /* first */ catch(int Type) { Type * value; }" " /* second */ catch(...) { recover(); }"
                            : "");
                const auto source =
                    "using Type = int;\nstruct C {\n" + candidate + "\nint member;\n};\nint tail(){return 1;}\n";
                SCOPED_TRACE(source);
                PaddedInput input(source);
                size_t errors = 0;
                auto tree = parser::Parse(input.View(), [&] (auto) { ++errors; });
                EXPECT_EQ(errors, 0u);
                const auto* record = tree.Root().FirstChild->NextSibling;
                ASSERT_NE(record, nullptr);
                ASSERT_EQ(record->Kind, parser::NodeKind::RecordDeclaration);
                const auto* recovery = record->FirstChild;
                ASSERT_NE(recovery, nullptr);
                EXPECT_EQ(recovery->Kind, parser::NodeKind::UnparsedSyntax);
                EXPECT_EQ(tree.SourceText(recovery->Tokens), candidate);
                std::vector<const Node*> pending{recovery};
                while (!pending.empty()) {
                    const auto* node = pending.back();
                    pending.pop_back();
                    EXPECT_NE(node->Kind, parser::NodeKind::FunctionDefinition);
                    EXPECT_NE(node->Kind, parser::NodeKind::CompoundStatement);
                    EXPECT_NE(node->Kind, parser::NodeKind::ParameterDeclaration);
                    EXPECT_NE(node->Kind, parser::NodeKind::DeclarationSpecifiers);
                    for (auto* child = node->FirstChild; child; child = child->NextSibling) {
                        pending.push_back(child);
                    }
                }
                ASSERT_NE(recovery->NextSibling, nullptr);
                EXPECT_EQ(tree.SourceText(recovery->NextSibling->Tokens), "int member;");
                ASSERT_NE(record->NextSibling, nullptr);
                EXPECT_EQ(record->NextSibling->Kind, parser::NodeKind::FunctionDefinition);
                EXPECT_EQ(tree.SourceText(record->NextSibling->Tokens), "int tail(){return 1;}");
            }
        }
    }
}

TEST(Parser, AnnotationRecoveryKeepsTruthfulCatchDiagnostics)
{
    for (const std::string_view source :
        {
            "Vec NOTE(0) load() try {} catch(int n) int tail;",
            "Vec NOTE(0) load() try {} catch(int n { return n; }",
            "NOTE(0) C(int n) : member(n",
            "NOTE(0) C(int n) try : member{n",
            "NOTE(0) C(int n) try : member{n} {} " "catch(int Type { Type * value; }",
            "NOTE(0) C(int n) noexcept try : member{n",
            "NOTE(0) C(int n) requires Ready<int> && " "requires(int value) { value;",
            "NOTE(0) C(int n) [[tag(Value{1})",
        })
    {
        PaddedInput input(source);
        std::vector<parser::ParseError> errors;
        auto tree = parser::Parse(
            input.View(),
            [&] (auto error) {
                errors.push_back(std::move(error));
            });
        EXPECT_FALSE(errors.empty());
        ASSERT_NE(tree.Root().FirstChild, nullptr);
        EXPECT_EQ(tree.Root().FirstChild->Kind, parser::NodeKind::UnparsedSyntax);
        if (source.ends_with("int tail;")) {
            ASSERT_NE(tree.Root().LastChild, tree.Root().FirstChild);
            EXPECT_EQ(tree.SourceText(tree.Root().LastChild->Tokens), "int tail;");
        } else {
            EXPECT_EQ(tree.Root().FirstChild->Tokens.End, tree.TokenCount());
        }
    }
    PaddedInput input(
        "int f(int n) try { return n; } catch(int code) { return code; } " "catch(...) { return 0; }\nint tail;");
    auto tree = parser::Parse(input.View());
    const auto* function = tree.Root().FirstChild;
    ASSERT_EQ(function->Kind, parser::NodeKind::FunctionDefinition);
    size_t catches = 0;
    std::vector<const Node*> pending{function};
    while (!pending.empty()) {
        const auto* node = pending.back();
        pending.pop_back();
        EXPECT_NE(node->Kind, parser::NodeKind::UnparsedSyntax);
        catches += node->Kind == parser::NodeKind::CatchClause;
        for (auto* child = node->FirstChild; child; child = child->NextSibling) {
            pending.push_back(child);
        }
    }
    EXPECT_EQ(catches, 2u);
    EXPECT_EQ(tree.SourceText(function->NextSibling->Tokens), "int tail;");
}

TEST(Parser, BareInvocationSuffixesDoNotEstablishFunctionBindings)
{
    for (const std::string_view suffix :
        {
            " const volatile & noexcept(true)",
            " & noexcept",
            " && noexcept(true)",
            " noexcept(true)",
            " [[maybe_unused]]",
            " __attribute__((unused))",
            " override final",
            " requires Ready<int>",
        })
    {
        const auto candidate = "RAW(int n)" + std::string(suffix) + " { return n; }";
        PaddedInput input(candidate + "\nint tail;\n");
        SCOPED_TRACE(candidate);
        auto tree = parser::Parse(input.View());
        const auto* recovery = tree.Root().FirstChild;
        ASSERT_NE(recovery, nullptr);
        EXPECT_EQ(recovery->Kind, parser::NodeKind::UnparsedSyntax);
        EXPECT_EQ(tree.SourceText(recovery->Tokens), candidate);
        EXPECT_EQ(recovery->FirstChild, nullptr);
        ASSERT_NE(recovery->NextSibling, nullptr);
        EXPECT_EQ(tree.SourceText(recovery->NextSibling->Tokens), "int tail;");
    }
}

TEST(Parser, UnknownInvocationSequenceKeepsFollowingFunctionIndependent)
{
    for (const std::string_view separator : {" ", "\n", " /* between\n */ ", " // between\n", "\r\n"}) {
        const auto prefix = "BIND(Key, const Item *)" + std::string(separator) + "TRACK(Group, const Item *)" +
            std::string(separator) + "MAP(Key, const Item *, const Value *)";
        for (const std::string_view following :
            {
                "const Item *Factory::build(int n) const { return nullptr; }",
                "[[maybe_unused]] static bool check(const Item *p) " "{ return p; }",
            })
        {
            PaddedInput input(prefix + std::string(separator) + std::string(following) + "\nint tail;\n");
            SCOPED_TRACE(following);
            auto tree = parser::Parse(input.View());
            const auto* unknown = tree.Root().FirstChild;
            ASSERT_NE(unknown, nullptr);
            EXPECT_EQ(unknown->Kind, parser::NodeKind::UnparsedSyntax);
            EXPECT_EQ(tree.SourceText(unknown->Tokens), prefix);
            EXPECT_EQ(unknown->FirstChild, nullptr);
            auto* function = unknown->NextSibling;
            while (function && function->Kind == parser::NodeKind::Comment) {
                function = function->NextSibling;
            }
            ASSERT_NE(function, nullptr);
            EXPECT_EQ(function->Kind, parser::NodeKind::FunctionDefinition);
            EXPECT_EQ(tree.SourceText(function->Tokens), following);
            EXPECT_EQ(tree.SourceText(tree.Root().LastChild->Tokens), "int tail;");
        }
    }
    for (const std::string_view candidate :
        {
            "NOTE(0) Vec load(int n) { return n; }",
            "NOTE(0) NOTE2(1) Vec load(int n) { return n; }",
            "NOTE(0) NOTE2(1) RAW(int n) const volatile & noexcept " "{ return n; }",
            "NOTE(0) NOTE2(1) RAW(int n) [[maybe_unused]] noexcept " "{ return n; }",
        })
    {
        PaddedInput input(std::string(candidate) + "\nint tail;\n");
        auto tree = parser::Parse(input.View());
        const auto* unknown = tree.Root().FirstChild;
        EXPECT_EQ(unknown->Kind, parser::NodeKind::UnparsedSyntax);
        EXPECT_EQ(tree.SourceText(unknown->Tokens), candidate);
        EXPECT_EQ(unknown->FirstChild, nullptr);
        EXPECT_EQ(tree.SourceText(tree.Root().LastChild->Tokens), "int tail;");
    }
}

TEST(Parser, UnknownTypedAnnotationRetainsWholeCandidateRecovery)
{
    // A parameter shadowing an outer type must not acquire editable body
    // authority from a header whose bindings were not established.
    const std::string source =
        "using Type = int;\n" "static inline Vec NOTE(\"legacy\") load(int Type) { Type * value; }\n" "int tail;\n";
    PaddedInput input(source);
    auto tree = parser::Parse(input.View());
    auto* candidate = tree.Root().FirstChild->NextSibling;
    ASSERT_NE(candidate, nullptr);
    EXPECT_EQ(candidate->Kind, parser::NodeKind::UnparsedSyntax);
    EXPECT_EQ(
        tree.SourceText(candidate->Tokens),
        "static inline Vec NOTE(\"legacy\") load(int Type) { Type * value; }");
    EXPECT_EQ(candidate->FirstChild, nullptr);
    ASSERT_NE(candidate->NextSibling, nullptr);
    EXPECT_EQ(tree.SourceText(candidate->NextSibling->Tokens), "int tail;");
}

TEST(Parser,
    RecoveredConstraintOperandsCannotSearchThroughFollowingDeclarations)
{
    for (const std::string_view suffix :
        {
            " requires (true) || true",
            " requires lhs < rhs",
            " requires lhs < probe(rhs)",
            " requires Ready<int> || probe<Type>()",
            " requires lhs = rhs",
            " requires (lhs = rhs) || true",
            " = delete",
            " = default",
        })
    {
        const bool assigned = suffix.starts_with(" =");
        const auto candidate =
            "Vec NOTE(0) load(int Type)" + std::string(suffix) + (assigned ? ";" : " { Type * value; }");
        const auto source =
            "using Type = int;\n" + candidate + "\nType * kept;\nint tail(){return 1;}\nbool later = a > b;\n";
        SCOPED_TRACE(source);
        PaddedInput input(source);
        std::vector<parser::ParseError> errors;
        auto tree = parser::Parse(
            input.View(),
            [&] (auto error) {
                errors.push_back(std::move(error));
            });
        EXPECT_TRUE(errors.empty());
        const auto* recovery = tree.Root().FirstChild->NextSibling;
        ASSERT_NE(recovery, nullptr);
        EXPECT_EQ(recovery->Kind, parser::NodeKind::UnparsedSyntax);
        EXPECT_EQ(tree.SourceText(recovery->Tokens), candidate);
        EXPECT_EQ(recovery->FirstChild, nullptr);
        ASSERT_NE(recovery->NextSibling, nullptr);
        EXPECT_EQ(tree.SourceText(recovery->NextSibling->Tokens), "Type * kept;");
        const auto* tail = recovery->NextSibling->NextSibling;
        ASSERT_NE(tail, nullptr);
        EXPECT_EQ(tail->Kind, parser::NodeKind::FunctionDefinition);
        EXPECT_EQ(tree.SourceText(tail->Tokens), "int tail(){return 1;}");
        EXPECT_EQ(tree.SourceText(tree.Root().LastChild->Tokens), "bool later = a > b;");
        for (auto i = recovery->NextSibling->Tokens.Begin; i < recovery->NextSibling->Tokens.End; ++i) {
            if (tree.TokenText(i) == "*") {
                EXPECT_EQ(tree.Role(i), parser::PunctuationRole::PointerDeclarator);
            }
        }
    }
}

TEST(Parser, RecoveredRequiresSkipNeedsValidMapDespitePopulatedOuterPairs)
{
    for (const std::string_view operand : {"requires(int x]) { x; }", "requires(int x) { x]; }"}) {
        const auto source =
            "Vec NOTE(0) run() requires Ready<int> && " + std::string(operand) + " { return 0; }\nint tail;\n";
        SCOPED_TRACE(source);
        PaddedInput input(source);
        std::vector<parser::ParseError> errors;
        auto tree = parser::Parse(
            input.View(),
            [&] (auto error) {
                errors.push_back(std::move(error));
            });
        EXPECT_TRUE(std::any_of(
            errors.begin(),
            errors.end(),
            [] (const auto& e) {
                return e.Message == "mismatched closing delimiter";
            }));
        const auto parameter = source.find("requires(") + 8;
        const auto body = source.find('{');
        for (auto i = parser::TokenIndex{0}; i < tree.TokenCount(); ++i) {
            if (tree.Token(i).Text.Begin != parameter && tree.Token(i).Text.Begin != body) {
                continue;
            }
            const auto mate = tree.MatchingDelimiter(i);
            ASSERT_LT(mate, tree.TokenCount());
            EXPECT_EQ(tree.MatchingDelimiter(mate), i);
            EXPECT_EQ(
                tree.Kind(mate),
                tree.Token(i).Text.Begin == parameter
                ? tokenizer::TokenKind::Operator_CloseParenthesis
                : tokenizer::TokenKind::Operator_CloseBrace);
        }
        ASSERT_NE(tree.Root().FirstChild, nullptr);
        EXPECT_EQ(tree.Root().FirstChild->Kind, parser::NodeKind::UnparsedSyntax);
        EXPECT_LE(tree.Root().FirstChild->Tokens.End, tree.TokenCount());
    }
}

TEST(Parser, RecoveredRequiresPairCannotAdvanceAcrossConditionalParseLimit)
{
    PaddedInput input(
        "#if PICK\nVec NOTE(0) run() requires Ready<int> && " "requires(int x) { x;\n#else\n}\n#endif\nint tail;\n");
    auto tree = parser::Parse(input.View(), [] (auto) {});
    std::vector<const Node*> pending{&tree.Root()};
    while (!pending.empty()) {
        const auto* parent = pending.back();
        pending.pop_back();
        for (auto* child = parent->FirstChild; child; child = child->NextSibling) {
            EXPECT_GE(child->Tokens.Begin, parent->Tokens.Begin);
            EXPECT_LE(child->Tokens.End, parent->Tokens.End);
            pending.push_back(child);
        }
    }
    ASSERT_NE(tree.Root().LastChild, nullptr);
    EXPECT_EQ(tree.SourceText(tree.Root().LastChild->Tokens), "int tail;");
}

TEST(Parser, StringLiteralSequencesHaveCompleteRanges)
{
    const test::golden::Fixture fixture{PARSER_TESTDATA_DIR, "expressions"};
    const auto source = fixture.InputNamed(".in");
    PaddedInput input(source);
    size_t errors = 0;
    auto tree = parser::Parse(
        input.View(),
        [&] (parser::ParseError) {
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
        if (node->Kind == parser::NodeKind::LiteralExpression && tree.Kind(node->Tokens.Begin) == Literal_String) {
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
            foundCommentRange |=
                tree.SourceText(node->Tokens) == "\"first\" /* join */ \"second\" /* again */ \"third\"";
        }
        for (auto* child = node->FirstChild; child; child = child->NextSibling) {
            pending.push_back(child);
        }
    }
    EXPECT_GT(sequences, 0);
    EXPECT_EQ(coveredStrings, stringTokens);
    EXPECT_TRUE(foundCommentRange);
}

TEST(Parser, BracedInitializerRangesStopAtMatchingCloser)
{
    const test::golden::Fixture fixture{PARSER_TESTDATA_DIR, "expressions"};
    PaddedInput input(fixture.InputNamed(".in"));
    auto tree = parser::Parse(input.View(), [] (parser::ParseError) {});
    size_t initializers = 0;
    std::vector<const Node*> pending{&tree.Root()};
    while (!pending.empty()) {
        const auto* node = pending.back();
        pending.pop_back();
        if (node->Kind == parser::NodeKind::BracedInitializer || node->Kind == parser::NodeKind::InitializerList) {
            ++initializers;
            ASSERT_FALSE(node->Tokens.Empty());
            EXPECT_EQ(tree.Kind(node->Tokens.Begin), tokenizer::TokenKind::Operator_OpenBrace);
            EXPECT_EQ(tree.MatchingDelimiter(node->Tokens.Begin), node->Tokens.End - 1);
        }
        for (auto* child = node->FirstChild; child; child = child->NextSibling) {
            pending.push_back(child);
        }
    }
    EXPECT_GT(initializers, 0);
}

TEST(Parser, BinaryOperatorMetadataMatchesGrouping)
{
    const test::golden::Fixture fixture{PARSER_TESTDATA_DIR, "expressions"};
    const auto source = fixture.InputNamed(".in");
    PaddedInput input(source);
    size_t errors = 0;
    auto tree = parser::Parse(
        input.View(),
        [&] (parser::ParseError) {
            ++errors;
        });
    EXPECT_EQ(errors, 0);

    auto isBinary = [] (const Node& node) {
        return node.Kind == parser::NodeKind::BinaryExpression || node.Kind == parser::NodeKind::AssignmentExpression;
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
            EXPECT_EQ(node->Kind == parser::NodeKind::AssignmentExpression, info.Precedence == 2);
            for (const auto* child : {left, right}) {
                if (isBinary(*child)) {
                    ASSERT_NE(child->FirstChild, nullptr);
                    auto childInfo = parser::GetBinaryOperatorInfo(tree.Kind(operatorToken(*child)));
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
            alternativeBinary |= text == "and" || text == "or" || text == "bitand" || text == "bitor" ||
                text == "xor" || text == "and_eq" || text == "or_eq" || text == "xor_eq" || text == "not_eq";
        } else if (node->Kind == parser::NodeKind::IdentifierExpression ||
            node->Kind == parser::NodeKind::LiteralExpression ||
            node->Kind == parser::NodeKind::ParenthesizedExpression)
        {
            auto info = parser::GetBinaryOperatorInfo(tree.Kind(node->Tokens.Begin));
            EXPECT_EQ(info.Precedence, 0);
            EXPECT_FALSE(info.RightAssociative);
            parenthesized |= node->Kind == parser::NodeKind::ParenthesizedExpression;
        } else if (node->Kind == parser::NodeKind::UnaryExpression) {
            auto info = parser::GetBinaryOperatorInfo(tree.Kind(node->Tokens.Begin));
            // A supported token can still have a unary rather than binary role.
            supportedUnary |= info.Precedence != 0;
            unsupportedUnary |= info.Precedence == 0;
        }
        for (auto* child = node->FirstChild; child; child = child->NextSibling) {
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

TEST(Parser, DecltypeOperandsStayInDeclarationSpecifiers)
{
    for (const auto* source :
        {
            "decltype(0) extract(const int& value);",
            "decltype(auto) forward(int&& value);",
            "decltype(*source) dereference(const int* value);",
            "decltype((source)) nested(const int& value);",
            "decltype(combine((source), 0)) expression(const int& value);",
            "decltype(factory<int>(0)) templated(const int& value);",
            "decltype([](int item) { return item; }(0)) " "lambda(const int& value);",
            "decltype /* operand */ (0) commented(const int& value);",
        })
    {
        SCOPED_TRACE(source);
        PaddedInput input(source);
        std::vector<parser::ParseError> errors;
        auto tree = parser::Parse(
            input.View(),
            [&] (parser::ParseError error) {
                errors.push_back(std::move(error));
            });
        ASSERT_TRUE(errors.empty());
        auto* declaration = tree.Root().FirstChild;
        ASSERT_NE(declaration, nullptr);
        EXPECT_EQ(declaration->Kind, parser::NodeKind::FunctionDeclaration);
        EXPECT_EQ(declaration->NextSibling, nullptr);
        auto* specifiers = declaration->FirstChild;
        ASSERT_NE(specifiers, nullptr);
        ASSERT_EQ(specifiers->Kind, parser::NodeKind::DeclarationSpecifiers);
        EXPECT_EQ(specifiers->Tokens.Begin, 0);
        size_t parameterLists = 0;
        std::vector<const Node*> pending{declaration};
        while (!pending.empty()) {
            auto* node = pending.back();
            pending.pop_back();
            EXPECT_NE(node->Kind, parser::NodeKind::UnparsedSyntax);
            if (node->Tokens.Begin >= specifiers->Tokens.End) {
                EXPECT_NE(node->Kind, parser::NodeKind::NestedDeclarator);
            }
            if (node->Kind == parser::NodeKind::ParameterList && node->Tokens.Begin >= specifiers->Tokens.End) {
                ++parameterLists;
                EXPECT_EQ(tree.Role(node->Tokens.Begin), parser::PunctuationRole::DeclarationDelimiter);
            }
            for (auto* child = node->FirstChild; child; child = child->NextSibling) {
                pending.push_back(child);
            }
        }
        EXPECT_EQ(parameterLists, 1);
    }
}

TEST(Parser, UnclosedDecltypeOperandDoesNotCertifyFunction)
{
    const test::golden::Fixture fixture{PARSER_TESTDATA_DIR, "declaration_operands_recovery"};
    PaddedInput input(fixture.InputNamed(".in"));
    std::vector<parser::ParseError> errors;
    auto tree = parser::Parse(
        input.View(),
        [&] (parser::ParseError error) {
            errors.push_back(std::move(error));
        });
    EXPECT_FALSE(errors.empty());
    std::string diagnostics;
    for (const auto& error : errors) {
        diagnostics += std::format("{}:{} ", error.Position.Line, error.Position.Column);
        AppendQuoted(diagnostics, error.Message);
        diagnostics.push_back('\n');
    }
    fixture.Expect(diagnostics, ".diagnostics.out");
    EXPECT_EQ(tree.Root().Tokens.End, tree.TokenCount());
    ASSERT_NE(tree.Root().FirstChild, nullptr);
    EXPECT_NE(tree.Root().FirstChild->Kind, parser::NodeKind::FunctionDeclaration);
    EXPECT_NE(tree.Root().FirstChild->Kind, parser::NodeKind::FunctionDefinition);
}

INSTANTIATE_TEST_SUITE_P(
    Parser,
    FileTest,
    testing::Values(
        GoldenCase{"comments"},
        GoldenCase{"declarations"},
        GoldenCase{"expressions"},
        GoldenCase{"separated_lists"},
        GoldenCase{"template_parameters_recovery", true},
        GoldenCase{"preprocessing"},
        GoldenCase{"preprocessing_fragments"},
        GoldenCase{"modules_and_concepts"},
        GoldenCase{"requires_clause_boundaries"},
        GoldenCase{"requires_template_lambdas", false, true},
        GoldenCase{"requires_forms", false, true},
        GoldenCase{"requires_forms_recovery", true, true},
        GoldenCase{"requires_return_boundaries"},
        GoldenCase{"unhinted_body_recovery"},
        GoldenCase{
            "class_body_macros",
            false,
            true,
            {"DEFINE_METHOD"},
            {"BEGIN_SETTER:END_SETTER", "OPEN_RECORD:CLOSE_RECORD"},
        },
        GoldenCase{
            "class_body_macros_recovery",
            true,
            false,
            {},
            {"BEGIN_SETTER:END_SETTER", "OPEN_RECORD:CLOSE_RECORD"},
        },
        GoldenCase{"function_body_macros", false, true, {"DEFINE_METHOD", "DEFINE_CASE", "DEFINE_CASE"}},
        GoldenCase{"function_body_macros_recovery", true, false, {"DEFINE_CASE"}},
        GoldenCase{"lambda_argument_ownership"},
        GoldenCase{"lambda_argument_tail_recovery"},
        GoldenCase{"template_argument_tail"},
        GoldenCase{"statements"},
        GoldenCase{"macro_generated_cases", false, true},
        GoldenCase{"name_classification"},
        GoldenCase{"ambiguity"},
        GoldenCase{"unknown_record"},
        GoldenCase{"elaborated_initializers", false, true},
        GoldenCase{"ambiguous_call_syntax"},
        GoldenCase{"declarator_syntax"},
        GoldenCase{"constructor_initializer_ranges"},
        GoldenCase{"terminal_constructor_template_ids"},
        GoldenCase{"template_type_aliases"},
        GoldenCase{"template_function_arguments", false, true},
        GoldenCase{"layout_reproductions"},
        GoldenCase{"declaration_operands", false, true},
        GoldenCase{"punctuation_roles", false, true},
        GoldenCase{"declaration_operands_recovery", true},
        GoldenCase{"annotation_boundaries"},
        GoldenCase{"declaration_macros", false, true},
        GoldenCase{"qualified_records"},
        GoldenCase{"opaque_record_owners"},
        GoldenCase{"extensions"},
        GoldenCase{"recovery", true},
        GoldenCase{"terminal_constructor_boundaries"}),
    [] (const auto& info) {
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
    auto tree = parser::Parse(
        paddedSource.View(),
        [&] (parser::ParseError) {
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
    EXPECT_EQ(tree.Kind(outerClosing), tokenizer::TokenKind::Operator_CloseBrace);
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
    auto tree = parser::Parse(
        paddedSource.View(),
        [&] (parser::ParseError) {
            ++errors;
        });

    EXPECT_EQ(errors, 0);

    parser::TokenIndex widestPair = 0;
    for (parser::TokenIndex index = 0; index < tree.TokenCount(); ++index) {
        auto matching = tree.MatchingDelimiter(index);
        if (matching != std::numeric_limits<parser::TokenIndex>::max() && matching > index) {
            widestPair = std::max(widestPair, matching - index);
        }
    }
    EXPECT_GT(widestPair, Depth * 2);
}

} // namespace

TEST(Parser, RecordHeadTemplateArgumentsOwnTheirSeparators)
{
    PaddedInput input("template<class A, class B> struct Pair;\n" "template<class A, class B> struct Pair<A,B> {};\n");
    const auto tree = parser::Parse(input.View(), [] (parser::ParseError) {});
    const auto* declaration = tree.Root().FirstChild->NextSibling->FirstChild;
    ASSERT_EQ(declaration->Kind, parser::NodeKind::TemplateParameterList);
    const auto* record = declaration->NextSibling;
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
    const std::string_view values = "template<auto, auto> struct Pair;\n";
    const std::string_view types = "template<auto, class> struct Wrap;\n" "template<class, auto> struct Pair;\n";
    const BoundaryCase boundaries[] = {
        {values, "(1<2)", "0"},
        {types, "Wrap<(1>0),int>", "2"},
        {types, "Wrap<(8>>1),int>", "2"},
        {types, "Wrap<(1<<2),Wrap<(3>2),int>>", "2"},
        {"constexpr int limits[] = {1,2};\n" "template<auto, auto> struct Pair;\n", "limits[1<2]", "0"},
        {"struct Value { int first, second; };\n" "template<Value, int> struct Pair;\n", "Value{1<2,2}", "0"},
        {
            "struct Value { int first, second; };\n" "template<Value, class> struct Wrap;\n"
                "template<class, int> struct Pair;\n",
            "Wrap<(Value{1>0,2}),int>",
            "0",
        },
        {
            "constexpr int limits[] = {1,2};\n" "template<auto, class> struct Wrap;\n"
                "template<class, auto> struct Pair;\n",
            "Wrap<(limits[1>0]),int>",
            "0",
        },
    };
    const auto checkBoundary = [&] (const BoundaryCase& value) {
        const auto source = std::string(value.Prefix) + "template<> struct Pair<" + std::string(value.First) + "," +
            std::string(value.Second) + "> {};\n";
        SCOPED_TRACE(source);
        PaddedInput padded(source);
        std::vector<parser::ParseError> errors;
        const auto parsed =
            parser::Parse(padded.View(), [&] (parser::ParseError error) { errors.push_back(std::move(error)); });
        EXPECT_TRUE(errors.empty());
        const auto* record = parsed.Root().LastChild->FirstChild->NextSibling;
        ASSERT_EQ(record->Kind, parser::NodeKind::RecordDeclaration);
        const auto* list = record->FirstChild;
        ASSERT_NE(list, nullptr);
        ASSERT_EQ(list->Kind, parser::NodeKind::TemplateArgumentList);
        const auto* first = list->FirstChild;
        ASSERT_NE(first, nullptr);
        ASSERT_EQ(first->Kind, parser::NodeKind::TemplateArgument);
        const auto* separator = first->NextSibling;
        ASSERT_NE(separator, nullptr);
        EXPECT_EQ(separator->Kind, parser::NodeKind::ListSeparator);
        const auto* second = separator->NextSibling;
        ASSERT_NE(second, nullptr);
        EXPECT_EQ(second->Kind, parser::NodeKind::TemplateArgument);
        EXPECT_EQ(second->NextSibling, nullptr);

        const auto firstBegin = source.find(value.First, value.Prefix.size());
        const auto outerComma = firstBegin + value.First.size();
        const auto secondBegin = outerComma + 1;
        const auto tokenAt = [&] (size_t offset) {
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
        EXPECT_EQ(second->Tokens.End, tokenAt(secondBegin + value.Second.size()));
        EXPECT_EQ(parsed.SourceText(first->Tokens), value.First);
        EXPECT_EQ(parsed.SourceText(second->Tokens), value.Second);
        EXPECT_EQ(separator->Tokens.Begin, tokenAt(outerComma));
        EXPECT_EQ(separator->Tokens.End, tokenAt(outerComma) + 1);
        const auto separators = LambdaOwnershipNodes(*list, parser::NodeKind::ListSeparator);
        for (auto i = list->Tokens.Begin; i < list->Tokens.End; ++i) {
            if (parsed.Kind(i) == tokenizer::TokenKind::Operator_Comma) {
                const auto owned =
                    std::ranges::any_of(separators, [i] (const auto* item) { return item->Tokens.Begin == i; });
                EXPECT_EQ(
                    parsed.Role(i),
                    owned ? parser::PunctuationRole::ListSeparator : parser::PunctuationRole::Generic);
            }
        }
    };
    for (const auto& value : boundaries) {
        checkBoundary(value);
    }

    // Head children must compose with injected constructor/self-type scope,
    // without exporting the qualified terminal name over an outer variable.
    const auto source = test::golden::ReadFile(std::filesystem::path(PARSER_TESTDATA_DIR) / "../../.." /
        "formatter/tests/testdata/record-head-commas/" "constructor-composition.input.cpp");
    PaddedInput composedInput(source);
    unsigned errors = 0;
    const auto composed = parser::Parse(composedInput.View(), [&] (parser::ParseError) { ++errors; });
    EXPECT_EQ(errors, 0u);
    const Node* specialization = nullptr;
    const Node* constructor = nullptr;
    size_t outsideExpressions = 0;
    size_t outsideAmbiguities = 0;
    size_t unknownPairNames = 0;
    size_t selfTypes = 0;
    std::vector<const Node *> pending{&composed.Root()};
    while (!pending.empty()) {
        const auto* node = pending.back();
        pending.pop_back();
        const auto text = composed.SourceText(node->Tokens);
        if (node->Kind == parser::NodeKind::RecordDeclaration && text.starts_with("struct Host::Pair<")) {
            specialization = node;
        }
        if (node->Kind == parser::NodeKind::FunctionDefinition && text.starts_with("Pair(")) {
            constructor = node;
        }
        if (node->Kind == parser::NodeKind::DeclarationSpecifiers && text == "Pair") {
            ++selfTypes;
        }
        if (node->Kind == parser::NodeKind::ExpressionStatement && text == "Pair*marker;") {
            ++outsideExpressions;
        }
        if (node->Kind == parser::NodeKind::AmbiguousStatement && text == "Pair*marker;") {
            ++outsideAmbiguities;
        }
        if (node->Kind == parser::NodeKind::UnknownTypeName && text == "Pair") {
            ++unknownPairNames;
        }
        EXPECT_FALSE(node->Kind == parser::NodeKind::DeclarationStatement && text == "Pair*marker;");
        for (auto* child = node->FirstChild; child; child = child->NextSibling) {
            pending.push_back(child);
        }
    }
    ASSERT_NE(specialization, nullptr);
    const auto* head = specialization->FirstChild;
    ASSERT_NE(head, nullptr);
    ASSERT_EQ(head->Kind, parser::NodeKind::TemplateArgumentList);
    const auto* first = head->FirstChild;
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(composed.SourceText(first->Tokens), "Wrap<(1>0),int>");
    const auto* separator = first->NextSibling;
    ASSERT_NE(separator, nullptr);
    EXPECT_EQ(separator->Kind, parser::NodeKind::ListSeparator);
    EXPECT_EQ(separator->Tokens.Begin, first->Tokens.End);
    EXPECT_EQ(composed.Role(separator->Tokens.Begin), parser::PunctuationRole::ListSeparator);
    const auto* second = separator->NextSibling;
    ASSERT_NE(second, nullptr);
    EXPECT_EQ(composed.SourceText(second->Tokens), "2");
    EXPECT_EQ(second->Tokens.Begin, separator->Tokens.End);
    EXPECT_EQ(second->Tokens.End, head->Tokens.End - 1);
    EXPECT_EQ(second->NextSibling, nullptr);
    for (auto i = first->Tokens.Begin; i < first->Tokens.End; ++i) {
        if (composed.Kind(i) == tokenizer::TokenKind::Operator_Comma) {
            EXPECT_EQ(composed.Role(i), parser::PunctuationRole::ListSeparator);
        }
    }
    ASSERT_NE(constructor, nullptr);
    const Node* parameters = nullptr;
    const Node* initializers = nullptr;
    pending = {constructor};
    while (!pending.empty()) {
        const auto* node = pending.back();
        pending.pop_back();
        if (node->Kind == parser::NodeKind::ParameterList) {
            parameters = node;
        }
        if (node->Kind == parser::NodeKind::ConstructorInitializerList) {
            initializers = node;
        }
        for (auto* child = node->FirstChild; child; child = child->NextSibling) {
            pending.push_back(child);
        }
    }
    ASSERT_NE(initializers, nullptr);
    EXPECT_EQ(composed.SourceText(initializers->Tokens), ": stored(first+second)");
    ASSERT_NE(parameters, nullptr);
    const auto* parameter = parameters->FirstChild;
    ASSERT_NE(parameter, nullptr);
    EXPECT_EQ(composed.SourceText(parameter->Tokens), "int first=(1<2)");
    const auto* parameterComma = parameter->NextSibling;
    ASSERT_NE(parameterComma, nullptr);
    EXPECT_EQ(parameterComma->Kind, parser::NodeKind::ListSeparator);
    EXPECT_EQ(composed.Role(parameterComma->Tokens.Begin), parser::PunctuationRole::ListSeparator);
    const auto* other = parameterComma->NextSibling;
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
            for (auto* child = node->FirstChild; child; child = child->NextSibling) {
                pending.push_back(child);
            }
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
    const auto control = parser::Parse(registeredType.View(), [] (parser::ParseError) {});
    pending = {&control.Root()};
    size_t declarations = 0;
    while (!pending.empty()) {
        const auto* node = pending.back();
        pending.pop_back();
        if (node->Kind == parser::NodeKind::DeclarationStatement && control.SourceText(node->Tokens) == "Pair*marker;")
        {
            ++declarations;
        }
        for (auto* child = node->FirstChild; child; child = child->NextSibling) {
            pending.push_back(child);
        }
    }
    EXPECT_EQ(declarations, 1u);

    const std::string_view linkageCases[] = {
        "struct Catalog { using Element = int; };\n" "void inspect() { int consume(Catalog::Element); }\n",
        "struct Catalog { using Element = int; }; struct Catalog;\n"
            "void inspect() { int consume(Catalog::Element); }\n",
        "struct Catalog { using Element = int;\n" "void inspect() { int consume(Catalog::Element); } };\n",
        "struct Host {}; struct Host::Catalog { using Element = int;\n" "Catalog(int) {}\n"
            "void inspect() { int consume(Catalog::Element); } };\n"
            "int Catalog; void outside() { Catalog*marker; }\n",
    };
    for (const auto source : linkageCases) {
        SCOPED_TRACE(source);
        PaddedInput input(source);
        unsigned errors = 0;
        const auto linked = parser::Parse(input.View(), [&] (parser::ParseError) { ++errors; });
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
            if (node->Kind == parser::NodeKind::ParameterDeclaration && text == "Catalog::Element") {
                ++typedParameters;
            }
            if (node->Kind == parser::NodeKind::FunctionDefinition && text == "Catalog(int) {}") {
                ++constructors;
            }
            if (node->Kind == parser::NodeKind::ExpressionStatement && text == "Catalog*marker;") {
                ++outsideExpressions;
            }
            if (node->Kind == parser::NodeKind::AmbiguousStatement && text == "Catalog*marker;") {
                ++outsideAmbiguities;
            }
            EXPECT_FALSE(node->Kind == parser::NodeKind::DeclarationStatement && text == "Catalog*marker;");
            for (auto* child = node->FirstChild; child; child = child->NextSibling) {
                pending.push_back(child);
            }
        }
        EXPECT_EQ(typedParameters, 1u) << Serialize(linked);
        const bool qualified = source.find("Host::Catalog") != std::string_view::npos;
        EXPECT_EQ(constructors, qualified ? 1u : 0u);
        // This global declaration has no initializer: unresolved value names
        // may remain ambiguous, but a leaked type would become a declaration.
        EXPECT_EQ(outsideExpressions + outsideAmbiguities, qualified ? 1u : 0u) << Serialize(linked);
    }
}

TEST(Parser, RecordHeadChildrenAreBoundedWithoutGuessingOperands)
{
    const std::pair<std::string_view, size_t> cases[] =
        {
            {"struct Plain {};", 0},
            {"struct Outer::Inner {};", 0},
            {"template<> struct Outer<int>::Inner<long,char> {};", 2},
            {"template<> struct Pair<> {};", 1},
            {"template<> struct Pair<Wrap<int,long>, char> {};", 2},
            {"template<> struct Pair<(a < b), 2> {};", 1},
            {"template<> struct Pair<Value{1,2}, 3> {};", 1},
            {"template<> struct Pair<decltype(auto), 2> {};", 1},
            {"template<> struct Pair<decltype(call(a,b)), 2> {};", 1},
            {"struct alignas(alignof(int)) Pair<int,long> {};", 1},
            {"struct alignas(1<<4) Pair<int,long> {};", 1},
            {"struct API alignas(1<<4) Pair<int,long> {};", 0},
            {"struct alignas((1<2],0) Pair<int,long> {};", 0},
            {"struct API Pair<int,long> {};", 0},
            {"struct Pair<int, Wrap<long> {};", 0},
            {"struct Pair<(1<2],0> {};", 0},
            {"struct Pair<Value{1>0],0> {};", 0},
        };
    for (const auto& [source, expectedLists] : cases) {
        SCOPED_TRACE(source);
        PaddedInput input(source);
        const auto tree = parser::Parse(input.View(), [] (parser::ParseError) {});
        size_t lists = 0;
        size_t calls = 0;
        std::vector<const Node *> pending{&tree.Root()};
        while (!pending.empty()) {
            const auto* node = pending.back();
            pending.pop_back();
            lists += node->Kind == parser::NodeKind::TemplateArgumentList;
            calls += node->Kind == parser::NodeKind::CallExpression;
            auto end = node->Tokens.Begin;
            for (auto* child = node->FirstChild; child; child = child->NextSibling) {
                EXPECT_GE(child->Tokens.Begin, end);
                EXPECT_LE(child->Tokens.End, node->Tokens.End);
                end = child->Tokens.End;
                pending.push_back(child);
            }
            if (node->Kind == parser::NodeKind::TemplateArgument) {
                // Nested angle lists own their own commas. Only an explicit
                // decltype operand introduces general expression syntax.
                for (auto* child = node->FirstChild; child; child = child->NextSibling) {
                    if (child->Kind != parser::NodeKind::TemplateArgumentList) {
                        EXPECT_EQ(child->Kind, parser::NodeKind::TypeId);
                        EXPECT_EQ(tree.Kind(child->Tokens.Begin), tokenizer::TokenKind::Keyword_decltype);
                    }
                }
            }
        }
        EXPECT_EQ(lists, expectedLists);
        EXPECT_EQ(calls, source.find("decltype(call(") != source.npos ? 1u : 0u);
    }

    // Attaching head syntax must not alter name registration for
    // forward declarations or the declaration following them.
    PaddedInput input(
        "template<class A, class B> struct Pair;\n" "template<> struct Pair<int,long>;\n" "Pair<int,long> value;\n");
    const auto tree = parser::Parse(input.View(), [] (parser::ParseError) {});
    const auto* last = tree.Root().LastChild;
    ASSERT_EQ(last->Kind, parser::NodeKind::Declaration);
    ASSERT_NE(last->FirstChild, nullptr);
    EXPECT_EQ(last->FirstChild->Kind, parser::NodeKind::DeclarationSpecifiers);

    // Ordinary callers retain their independent delimiter/element ownership.
    const std::string ordinary = "void sink(int first=(1<2),int second=(3>2));\n"
        "void use(){ sink((1<2),0); int items[]={(1<2),0}; }\n" "enum Values { first=(1<2), second=0 };\n";
    PaddedInput ordinaryInput(ordinary);
    std::vector<parser::ParseError> errors;
    const auto ordinaryTree =
        parser::Parse(ordinaryInput.View(), [&] (parser::ParseError error) { errors.push_back(std::move(error)); });
    EXPECT_TRUE(errors.empty());
    const std::pair<parser::NodeKind, std::string_view> expected[] = {
        {parser::NodeKind::ParameterList, "int first=(1<2)"},
        {parser::NodeKind::ArgumentList, "(1<2)"},
        {parser::NodeKind::InitializerList, "(1<2)"},
        {parser::NodeKind::EnumeratorList, "first=(1<2)"},
    };
    for (const auto& [kind, firstText] : expected) {
        SCOPED_TRACE(parser::GetNodeKindName(kind));
        size_t found = 0;
        std::vector<const Node *> nodes{&ordinaryTree.Root()};
        while (!nodes.empty()) {
            const auto* node = nodes.back();
            nodes.pop_back();
            for (auto* child = node->FirstChild; child; child = child->NextSibling) {
                nodes.push_back(child);
            }
            if (node->Kind != kind || !node->FirstChild ||
                ordinaryTree.SourceText(node->FirstChild->Tokens) != firstText)
            {
                continue;
            }
            ++found;
            const auto* first = node->FirstChild;
            const auto* comma = first->NextSibling;
            ASSERT_NE(comma, nullptr);
            EXPECT_EQ(comma->Kind, parser::NodeKind::ListSeparator);
            EXPECT_EQ(comma->Tokens.Begin, first->Tokens.End);
            EXPECT_EQ(comma->Tokens.End, first->Tokens.End + 1);
            EXPECT_EQ(ordinaryTree.Role(comma->Tokens.Begin), parser::PunctuationRole::ListSeparator);
            const auto* second = comma->NextSibling;
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
        "requires(T value) { { value.touch() } noexcept; " "typename T::value_type; requires requires "
            "{ typename T::other_type; }; }",
        "requires(T value) /* body */ { value.touch(); } && true",
        "true && requires(T value) { value.touch(); }",
        "(requires(T value) { value.touch(); })",
        "requires(T value) { value.touch(); } || " "requires { typename T::value_type; }",
    };
    for (const auto constraintText : constraints) {
        for (const bool function : {false, true}) {
            const std::string declaration = function
                ? "int inspect(T value) { return 1; }"
                : "struct Container { T member; };";
            const auto source =
                "template<class T> requires " + std::string(constraintText) + "\n" + declaration + "\nint outside;\n";
            SCOPED_TRACE(source);
            PaddedInput input(source);
            std::vector<parser::ParseError> errors;
            const auto tree =
                parser::Parse(input.View(), [&] (parser::ParseError error) { errors.push_back(std::move(error)); });
            EXPECT_TRUE(errors.empty());
            const auto* owner = tree.Root().FirstChild;
            ASSERT_NE(owner, nullptr);
            ASSERT_EQ(owner->Kind, parser::NodeKind::TemplateDeclaration);
            const auto* clause = owner->FirstChild;
            while (clause && clause->Kind != parser::NodeKind::RequiresClause) {
                clause = clause->NextSibling;
            }
            ASSERT_NE(clause, nullptr);
            ASSERT_NE(clause->FirstChild, nullptr);
            EXPECT_EQ(clause->FirstChild->Kind, parser::NodeKind::ConstraintExpression);
            EXPECT_EQ(tree.SourceText(clause->FirstChild->Tokens), constraintText);
            const auto* following = clause->NextSibling;
            ASSERT_NE(following, nullptr);
            EXPECT_EQ(
                following->Kind,
                function ? parser::NodeKind::FunctionDefinition : parser::NodeKind::RecordDeclaration);
            EXPECT_EQ(tree.SourceText(following->Tokens), declaration);
            EXPECT_EQ(following->Tokens.Begin, clause->Tokens.End);
            EXPECT_EQ(following->NextSibling, nullptr);
            size_t bodies = 0;
            size_t parameters = 0;
            std::vector<const Node *> pending{clause};
            while (!pending.empty()) {
                const auto* node = pending.back();
                pending.pop_back();
                EXPECT_NE(node->Kind, parser::NodeKind::UnparsedSyntax);
                EXPECT_GE(node->Tokens.Begin, clause->Tokens.Begin);
                EXPECT_LE(node->Tokens.End, clause->Tokens.End);
                if (node->Kind == parser::NodeKind::RequirementBody) {
                    ++bodies;
                    EXPECT_EQ(tree.Role(node->Tokens.Begin), parser::PunctuationRole::BlockDelimiter);
                    EXPECT_EQ(tree.Role(node->Tokens.End - 1), parser::PunctuationRole::BlockDelimiter);
                    ASSERT_NE(node->FirstChild, nullptr);
                    const auto kind = node->FirstChild->Kind;
                    EXPECT_TRUE(
                        kind == parser::NodeKind::SimpleRequirement || kind == parser::NodeKind::TypeRequirement ||
                        kind == parser::NodeKind::CompoundRequirement || kind == parser::NodeKind::NestedRequirement);
                    EXPECT_EQ(node->LastChild->Kind, parser::NodeKind::ListSeparator);
                    EXPECT_EQ(tree.Role(node->LastChild->Tokens.Begin), parser::PunctuationRole::ListSeparator);
                }
                if (node->Kind == parser::NodeKind::RequiresExpression && node->FirstChild &&
                    node->FirstChild->Kind == parser::NodeKind::ParameterList)
                {
                    ++parameters;
                    EXPECT_EQ(tree.SourceText(node->FirstChild->Tokens), "(T value)");
                    EXPECT_EQ(node->LastChild->Kind, parser::NodeKind::RequirementBody);
                }
                for (const auto* child = node->FirstChild; child; child = child->NextSibling) {
                    pending.push_back(child);
                }
            }
            EXPECT_GE(bodies, 1u);
            EXPECT_EQ(parameters, constraintText.find("(T value)") != std::string_view::npos ? 1u : 0u);
            const auto* outside = owner->NextSibling;
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

TEST(Parser, RequirementsOwnLambdaSyntax)
{
    const test::golden::Fixture fixture{PARSER_TESTDATA_DIR, "requires_template_lambdas"};
    PaddedInput input(fixture.InputNamed(".in"));
    size_t errors = 0;
    const auto tree = parser::Parse(input.View(), [&] (parser::ParseError) { ++errors; });
    EXPECT_EQ(errors, 0u);
    const auto lambdas = LambdaOwnershipNodes(tree.Root(), parser::NodeKind::LambdaExpression);
    ASSERT_EQ(lambdas.size(), 9u);
    size_t templated = 0;
    for (const auto* lambda : lambdas) {
        ASSERT_NE(lambda->FirstChild, nullptr);
        EXPECT_EQ(lambda->FirstChild->Kind, parser::NodeKind::LambdaCaptureList);
        EXPECT_EQ(tree.Role(lambda->FirstChild->Tokens.Begin), parser::PunctuationRole::LambdaDelimiter);
        EXPECT_EQ(tree.Role(lambda->FirstChild->Tokens.End - 1), parser::PunctuationRole::LambdaDelimiter);
        EXPECT_TRUE(LambdaOwnershipNodes(*lambda, parser::NodeKind::UnparsedSyntax).empty());
        const auto parameters = LambdaOwnershipNodes(*lambda, parser::NodeKind::TemplateParameterList);
        templated += !parameters.empty();
        for (const auto* list : parameters) {
            EXPECT_EQ(tree.Role(list->Tokens.Begin), parser::PunctuationRole::TemplateDelimiter);
            EXPECT_EQ(tree.Role(list->Tokens.End - 1), parser::PunctuationRole::TemplateDelimiter);
        }
        ASSERT_NE(lambda->LastChild, nullptr);
        EXPECT_EQ(lambda->LastChild->Kind, parser::NodeKind::LambdaBody);
        EXPECT_EQ(tree.Role(lambda->LastChild->Tokens.Begin), parser::PunctuationRole::LambdaDelimiter);
    }
    EXPECT_EQ(templated, 8u);
}

TEST(Parser, RequirementFormsOwnNamesBracesAndLocalScope)
{
    const test::golden::Fixture fixture{PARSER_TESTDATA_DIR, "requires_forms"};
    PaddedInput input(fixture.InputNamed(".in"));
    size_t errors = 0;
    const auto tree = parser::Parse(input.View(), [&] (parser::ParseError) { ++errors; });
    EXPECT_EQ(errors, 0u);
    EXPECT_TRUE(LambdaOwnershipNodes(tree.Root(), parser::NodeKind::UnparsedSyntax).empty());
    EXPECT_EQ(LambdaOwnershipNodes(tree.Root(), parser::NodeKind::SimpleRequirement).size(), 5u);
    EXPECT_EQ(LambdaOwnershipNodes(tree.Root(), parser::NodeKind::TypeRequirement).size(), 4u);
    EXPECT_EQ(LambdaOwnershipNodes(tree.Root(), parser::NodeKind::NestedRequirement).size(), 2u);
    const auto compounds = LambdaOwnershipNodes(tree.Root(), parser::NodeKind::CompoundRequirement);
    ASSERT_EQ(compounds.size(), 5u);
    for (const auto* compound : compounds) {
        const auto* expression = compound->FirstChild;
        ASSERT_NE(expression, nullptr);
        ASSERT_EQ(expression->Kind, parser::NodeKind::RequirementExpression);
        EXPECT_EQ(tree.Role(expression->Tokens.Begin), parser::PunctuationRole::GroupingDelimiter);
        EXPECT_EQ(tree.Role(expression->Tokens.End - 1), parser::PunctuationRole::GroupingDelimiter);
        EXPECT_TRUE(LambdaOwnershipNodes(*compound, parser::NodeKind::BracedInitializer).empty());
    }
    EXPECT_EQ(LambdaOwnershipNodes(tree.Root(), parser::NodeKind::TypeConstraint).size(), 3u);
    bool hasScopedProduct = false;
    for (const auto* binary : LambdaOwnershipNodes(tree.Root(), parser::NodeKind::BinaryExpression)) {
        if (tree.SourceText(binary->Tokens) == "(Local) * sizeof(T)") {
            ASSERT_NE(binary->FirstChild, nullptr);
            EXPECT_EQ(tree.Role(binary->FirstChild->Tokens.End), parser::PunctuationRole::BinaryOperator);
            hasScopedProduct = true;
        }
    }
    EXPECT_TRUE(hasScopedProduct);
    EXPECT_TRUE(LambdaOwnershipNodes(tree.Root(), parser::NodeKind::CastExpression).empty());
    bool hasOutsideType = false;
    for (const auto* child = tree.Root().FirstChild; child; child = child->NextSibling) {
        if (tree.SourceText(child->Tokens) == "Local outside_type;") {
            ASSERT_NE(child->FirstChild, nullptr);
            EXPECT_EQ(child->FirstChild->Kind, parser::NodeKind::DeclarationSpecifiers);
            hasOutsideType = true;
        }
    }
    EXPECT_TRUE(hasOutsideType);
}

TEST(Parser, RequirementRecoveryDiagnostics)
{
    const test::golden::Fixture fixture{PARSER_TESTDATA_DIR, "requires_forms_recovery"};
    PaddedInput input(fixture.InputNamed(".in"));
    std::vector<parser::ParseError> errors;
    const auto tree =
        parser::Parse(input.View(), [&] (parser::ParseError error) { errors.push_back(std::move(error)); });
    ASSERT_FALSE(errors.empty());
    std::string diagnostics;
    for (const auto& error : errors) {
        diagnostics += std::format("{}:{} ", error.Position.Line, error.Position.Column);
        AppendQuoted(diagnostics, error.Message);
        diagnostics.push_back('\n');
    }
    fixture.Expect(diagnostics, ".diagnostics.out");
    ASSERT_NE(tree.Root().LastChild, nullptr);
    EXPECT_EQ(tree.SourceText(tree.Root().LastChild->Tokens), "int final_outside;");
    EXPECT_FALSE(LambdaOwnershipNodes(tree.Root(), parser::NodeKind::UnparsedSyntax).empty());
    EXPECT_FALSE(LambdaOwnershipNodes(tree.Root(), parser::NodeKind::MissingToken).empty());
}

TEST(Parser, NestedRequirementSyntaxHasBoundedRecovery)
{
    std::string source = "template<class T> concept C = requires { ";
    for (size_t i = 0; i < 128; ++i) {
        source += "requires requires { ";
    }
    source += "true; ";
    for (size_t i = 0; i < 128; ++i) {
        source += "}; ";
    }
    source += "}; int outside;";
    PaddedInput input(source);
    const auto tree = parser::Parse(input.View(), [] (parser::ParseError) { });
    const auto unparsed = LambdaOwnershipNodes(tree.Root(), parser::NodeKind::UnparsedSyntax);
    ASSERT_EQ(unparsed.size(), 1u);
    EXPECT_TRUE(tree
        .SourceText(unparsed.front()->Tokens)
        .starts_with("requires requires {"));
    const auto* outside = tree.Root().LastChild;
    ASSERT_NE(outside, nullptr);
    EXPECT_EQ(tree.SourceText(outside->Tokens), "int outside;");
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
        {"Valid<T>", false},
    };
    for (const auto& declaration : declarations) {
        for (const auto& constraint : constraints) {
            const auto constraintText = constraint.Text;
            const auto declarationText = std::string(declaration.Specifiers) + " " +
                std::string(declaration.Declarator) + " " + std::string(declaration.Body);
            const auto source = "template<class T> requires " + std::string(constraintText) + "\n" + declarationText +
                "\nint outside;\n";
            SCOPED_TRACE(source);
            PaddedInput input(source);
            size_t errors = 0;
            const auto tree = parser::Parse(input.View(), [&] (parser::ParseError) { ++errors; });
            EXPECT_EQ(errors, 0u);
            const auto* owner = tree.Root().FirstChild;
            ASSERT_NE(owner, nullptr);
            EXPECT_EQ(owner->Kind, parser::NodeKind::TemplateDeclaration);
            const auto* clause = owner->FirstChild;
            while (clause && clause->Kind != parser::NodeKind::RequiresClause) {
                clause = clause->NextSibling;
            }
            ASSERT_NE(clause, nullptr);
            ASSERT_NE(clause->FirstChild, nullptr);
            EXPECT_EQ(tree.SourceText(clause->FirstChild->Tokens), constraintText);
            const auto* function = clause->NextSibling;
            ASSERT_NE(function, nullptr);
            EXPECT_EQ(function->Kind, parser::NodeKind::FunctionDefinition);
            EXPECT_EQ(tree.SourceText(function->Tokens), declarationText);
            EXPECT_EQ(function->Tokens.Begin, clause->Tokens.End);
            ASSERT_NE(function->FirstChild, nullptr);
            EXPECT_EQ(function->FirstChild->Kind, parser::NodeKind::DeclarationSpecifiers);
            EXPECT_EQ(tree.SourceText(function->FirstChild->Tokens), declaration.Specifiers);
            const auto* declarator = function->FirstChild->NextSibling;
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
                const auto* node = pending.back();
                pending.pop_back();
                unparsed |= node->Kind == parser::NodeKind::UnparsedSyntax;
                EXPECT_GE(node->Tokens.Begin, clause->Tokens.Begin);
                EXPECT_LE(node->Tokens.End, clause->Tokens.End);
                for (const auto* child = node->FirstChild; child; child = child->NextSibling) {
                    pending.push_back(child);
                }
            }
            EXPECT_EQ(unparsed, constraint.Unparsed);
        }
    }
}

TEST(Parser, RequiresClauseOwnsOrdinaryTemplateAndQualifiedReturnPrefix)
{
    struct DeclarationCase
    {
        std::string_view Specifiers;
        std::string_view Declarator;
    };
    const DeclarationCase declarations[] = {
        {"Envelope<T>", "inspect(T value)"},
        {"library::Envelope<T>", "inspect(T value)"},
        {"T::value_type", "inspect(T value)"},
        {"library::Result", "inspect(T value)"},
        {"Envelope<T>", "Owner::inspect(T value)"},
        {"Envelope<T>", "Owner::operator()(T value)"},
        {"Envelope<T>", "operator+(T value)"},
        {"Envelope<Envelope<T>>", "inspect(T value)"},
    };
    const std::string_view constraints[] = {
        "Ready<T>",
        "library::Ready<T>",
        "requires(T value) { value.touch(); }",
        "(Ready<T> && !Blocked<T>)",
        "requires(T value) { value.touch(); } && (left * invoke(value) > 0)",
        "requires(T value) { value.touch(); } or (left & mask)",
        "Ready<T> and /* operand */ Enabled<T> /* boundary */",
        "(sizeof...(T) > 0)",
    };
    for (const auto& declaration : declarations) {
        const auto declarationText =
            std::string(declaration.Specifiers) + " " + std::string(declaration.Declarator) + " { return {}; }";
        for (const auto constraint : constraints) {
            const auto source =
                "template<class T> requires " + std::string(constraint) + "\n" + declarationText + "\nint outside;\n";
            SCOPED_TRACE(source);
            PaddedInput input(source);
            size_t errors = 0;
            const auto tree = parser::Parse(input.View(), [&] (parser::ParseError) { ++errors; });
            EXPECT_EQ(errors, 0u);
            const auto* owner = tree.Root().FirstChild;
            ASSERT_NE(owner, nullptr);
            EXPECT_EQ(owner->Kind, parser::NodeKind::TemplateDeclaration);
            const auto* clause = owner->FirstChild;
            while (clause && clause->Kind != parser::NodeKind::RequiresClause) {
                clause = clause->NextSibling;
            }
            ASSERT_NE(clause, nullptr);
            ASSERT_NE(clause->FirstChild, nullptr);
            EXPECT_EQ(tree.SourceText(clause->FirstChild->Tokens), constraint);
            const auto* function = clause->NextSibling;
            ASSERT_NE(function, nullptr);
            EXPECT_EQ(function->Kind, parser::NodeKind::FunctionDefinition);
            EXPECT_EQ(tree.SourceText(function->Tokens), declarationText);
            EXPECT_EQ(function->Tokens.Begin, clause->Tokens.End);
            ASSERT_NE(function->FirstChild, nullptr);
            EXPECT_EQ(function->FirstChild->Kind, parser::NodeKind::DeclarationSpecifiers);
            EXPECT_EQ(tree.SourceText(function->FirstChild->Tokens), declaration.Specifiers);
            ASSERT_NE(function->FirstChild->NextSibling, nullptr);
            EXPECT_EQ(tree.SourceText(function->FirstChild->NextSibling->Tokens), declaration.Declarator);
            ASSERT_NE(function->LastChild, nullptr);
            EXPECT_EQ(function->LastChild->Kind, parser::NodeKind::CompoundStatement);
            EXPECT_EQ(tree.SourceText(function->LastChild->Tokens), "{ return {}; }");
            EXPECT_EQ(function->LastChild->Tokens.End, function->Tokens.End);
            ASSERT_NE(owner->NextSibling, nullptr);
            EXPECT_EQ(tree.SourceText(owner->NextSibling->Tokens), "int outside;");
            EXPECT_EQ(owner->NextSibling->Tokens.Begin, owner->Tokens.End);
        }
    }
}

TEST(Parser, RequiresClauseDoesNotSplitQualifiedConstraintOperands)
{
    const std::string_view constraints[] = {
        "library::predicate<T>(value)",
        "Ready<T> && library::predicate<T>(value)",
        "Ready<T> or /* operand */ library::predicate<T>(value)",
        "Ready<T> and (library::value<T> > limit)",
        "Ready<T> && library::value<T> > limit",
        "requires(T value) { value.touch(); } && " "(left * library::invoke(value) > 0)",
    };
    for (const auto constraint : constraints) {
        const auto source =
            "template<class T> requires " + std::string(constraint) + "\nint inspect(T value) { return 1; }\n";
        SCOPED_TRACE(source);
        PaddedInput input(source);
        size_t errors = 0;
        const auto tree = parser::Parse(input.View(), [&] (parser::ParseError) { ++errors; });
        EXPECT_EQ(errors, 0u);
        const auto* clause = tree.Root().FirstChild->FirstChild;
        while (clause && clause->Kind != parser::NodeKind::RequiresClause) {
            clause = clause->NextSibling;
        }
        ASSERT_NE(clause, nullptr);
        ASSERT_NE(clause->FirstChild, nullptr);
        EXPECT_EQ(tree.SourceText(clause->FirstChild->Tokens), constraint);
        ASSERT_NE(clause->NextSibling, nullptr);
        EXPECT_EQ(tree.SourceText(clause->NextSibling->Tokens), "int inspect(T value) { return 1; }");
    }
}

TEST(Parser, RequiresClauseDoesNotCertifyMalformedTemplateReturnPrefix)
{
    for (const auto prefix :
        {"Envelope<T>>", "Envelope<T", "library::Envelope<T>>", "Envelope<T> Owner::<T>", "Envelope<T> Owner<U>>"})
    {
        const auto source =
            "template<class T> requires Ready<T>\n" + std::string(prefix) + " inspect(T value) { return {}; }\n";
        SCOPED_TRACE(source);
        PaddedInput input(source);
        const auto tree = parser::Parse(input.View(), [] (parser::ParseError) {});
        const auto* clause = tree.Root().FirstChild->FirstChild;
        while (clause && clause->Kind != parser::NodeKind::RequiresClause) {
            clause = clause->NextSibling;
        }
        ASSERT_NE(clause, nullptr);
        ASSERT_NE(clause->FirstChild, nullptr);
        EXPECT_TRUE(tree
            .SourceText(clause->FirstChild->Tokens)
            .starts_with("Ready<T>\n" + std::string(prefix)));
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
        {"requires(T v) { v.touch(); } && (unresolved @ tail)", true, true},
    };
    for (const auto& constraint : constraints) {
        const auto source =
            "template<class T> requires " + std::string(constraint.Text) + "\nT* inspect(T* value) { return value; }\n";
        SCOPED_TRACE(source);
        PaddedInput input(source);
        std::vector<parser::ParseError> errors;
        const auto tree =
            parser::Parse(input.View(), [&] (parser::ParseError error) { errors.push_back(std::move(error)); });
        if (constraint.Invalid) {
            ASSERT_EQ(errors.size(), 1u);
            EXPECT_EQ(errors.front().Message, "Unexpected token");
            EXPECT_EQ(errors.front().Position.Line, 1u);
            // The tokenizer reports the position after consuming the
            // invalid character.
            EXPECT_EQ(errors.front().Position.Column, source.find('@') + 2);
        } else {
            EXPECT_TRUE(errors.empty());
        }
        const auto* owner = tree.Root().FirstChild;
        ASSERT_NE(owner, nullptr);
        const auto* clause = owner->FirstChild;
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
            const auto* node = pending.back();
            pending.pop_back();
            unparsed |= node->Kind == parser::NodeKind::UnparsedSyntax;
            EXPECT_GE(node->Tokens.Begin, clause->Tokens.Begin);
            EXPECT_LE(node->Tokens.End, clause->Tokens.End);
            for (const auto* child = node->FirstChild; child; child = child->NextSibling) {
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
    for (const auto& declaration : declarations) {
        for (const auto& constraint : constraints) {
            const auto constraintText = constraint.Text;
            const auto declarationText = std::string(declaration.Specifiers) + " " +
                std::string(declaration.Declarator) + " " + std::string(declaration.Body);
            const auto source = "template<class T> requires " + std::string(constraintText) + "\n" + declarationText +
                "\nint outside;\n";
            SCOPED_TRACE(source);
            PaddedInput input(source);
            std::vector<parser::ParseError> errors;
            const auto tree =
                parser::Parse(input.View(), [&] (parser::ParseError error) { errors.push_back(std::move(error)); });
            const auto invalidOffset = source.find('@');
            if (invalidOffset != std::string::npos) {
                ASSERT_EQ(errors.size(), 1u);
                EXPECT_EQ(errors.front().Message, "Unexpected token");
                EXPECT_EQ(errors.front().Position.Line, 1u);
                EXPECT_EQ(errors.front().Position.Column, invalidOffset + 2);
            } else {
                EXPECT_TRUE(errors.empty());
            }
            const auto* owner = tree.Root().FirstChild;
            ASSERT_NE(owner, nullptr);
            EXPECT_EQ(owner->Kind, parser::NodeKind::TemplateDeclaration);
            const auto* clause = owner->FirstChild;
            while (clause && clause->Kind != parser::NodeKind::RequiresClause) {
                clause = clause->NextSibling;
            }
            ASSERT_NE(clause, nullptr);
            ASSERT_NE(clause->FirstChild, nullptr);
            EXPECT_EQ(tree.SourceText(clause->FirstChild->Tokens), constraintText);
            bool unparsed = false;
            std::vector<const Node *> pending{clause};
            while (!pending.empty()) {
                const auto* node = pending.back();
                pending.pop_back();
                unparsed |= node->Kind == parser::NodeKind::UnparsedSyntax;
                EXPECT_GE(node->Tokens.Begin, clause->Tokens.Begin);
                EXPECT_LE(node->Tokens.End, clause->Tokens.End);
                for (const auto* child = node->FirstChild; child; child = child->NextSibling) {
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
                const auto* tail = clause->FirstChild->LastChild;
                ASSERT_NE(tail, nullptr);
                EXPECT_EQ(tail->Kind, parser::NodeKind::UnparsedSyntax);
                EXPECT_EQ(tree.Kind(tail->Tokens.Begin), tokenizer::TokenKind::Invalid);
                EXPECT_EQ(tree.TokenText(tail->Tokens.Begin), "@");
                EXPECT_EQ(tree.SourceText(tail->Tokens), constraintText.substr(constraintText.find('@')));
                EXPECT_EQ(tail->Tokens.End, clause->Tokens.End);
            }
            const auto* function = clause->NextSibling;
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
            const auto* declarator = function->FirstChild->NextSibling;
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
        const auto source = "template<class T> requires " + constraintText + "\nstruct Container {};\nint outside;\n";
        SCOPED_TRACE(source);
        PaddedInput input(source);
        std::vector<parser::ParseError> errors;
        const auto tree =
            parser::Parse(input.View(), [&] (parser::ParseError error) { errors.push_back(std::move(error)); });
        if (missingBody) {
            ASSERT_FALSE(errors.empty());
            EXPECT_EQ(errors.front().Message, "expected complete requirement body");
        }
        const auto* owner = tree.Root().FirstChild;
        ASSERT_NE(owner, nullptr);
        const auto* clause = owner->FirstChild;
        while (clause && clause->Kind != parser::NodeKind::RequiresClause) {
            clause = clause->NextSibling;
        }
        ASSERT_NE(clause, nullptr);
        ASSERT_NE(clause->FirstChild, nullptr);
        EXPECT_EQ(tree.SourceText(clause->FirstChild->Tokens), constraintText);
        if (!missingBody) {
            const auto* tail = clause->FirstChild->LastChild;
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
    PaddedInput input("template<class T> requires requires(T value) {\n" "#define REQUIREMENT_VALUE 1\n"
        "value.touch();\n}\nstruct Container {};\n");
    size_t errors = 0;
    const auto tree = parser::Parse(input.View(), [&] (parser::ParseError) { ++errors; });
    EXPECT_EQ(errors, 0u);
    const auto* owner = tree.Root().FirstChild;
    ASSERT_NE(owner, nullptr);
    size_t bodies = 0;
    size_t directives = 0;
    std::vector<const Node *> pending{owner};
    while (!pending.empty()) {
        const auto* node = pending.back();
        pending.pop_back();
        if (node->Kind == parser::NodeKind::RequirementBody) {
            ++bodies;
            EXPECT_EQ(tree.Role(node->Tokens.Begin), parser::PunctuationRole::BlockDelimiter);
            for (auto token = node->Tokens.Begin; token < node->Tokens.End; ++token) {
                if (tree.Kind(token) == tokenizer::TokenKind::PreprocessingDirective) {
                    ++directives;
                    EXPECT_TRUE(tree
                        .TokenText(token)
                        .starts_with("#define REQUIREMENT_VALUE 1"));
                }
            }
        }
        for (const auto* child = node->FirstChild; child; child = child->NextSibling) {
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
            "constexpr int values[] = {1,2};\n" "constexpr int select(int left,int right){return left+right;}\n"
                "struct Value { int first, second; };\n" "template<auto, class> struct Wrap {};\n" "void sink(" +
                std::string(firstText) + ",int other=0) {}\n" "Value outside;\n";
        SCOPED_TRACE(firstText);
        PaddedInput input(source);
        std::vector<parser::ParseError> errors;
        const auto tree =
            parser::Parse(input.View(), [&] (parser::ParseError error) { errors.push_back(std::move(error)); });
        EXPECT_TRUE(errors.empty());
        const Node* parameters = nullptr;
        std::vector<const Node *> pending{&tree.Root()};
        while (!pending.empty()) {
            const auto* node = pending.back();
            pending.pop_back();
            if (node->Kind == parser::NodeKind::ParameterList &&
                tree.SourceText(node->Tokens).starts_with("(" + std::string(firstText)))
            {
                parameters = node;
            }
            for (auto* child = node->FirstChild; child; child = child->NextSibling) {
                pending.push_back(child);
            }
        }
        ASSERT_NE(parameters, nullptr);
        const auto* first = parameters->FirstChild;
        ASSERT_NE(first, nullptr);
        EXPECT_EQ(first->Kind, parser::NodeKind::ParameterDeclaration);
        EXPECT_EQ(tree.SourceText(first->Tokens), firstText);
        EXPECT_EQ(first->Tokens.Begin, parameters->Tokens.Begin + 1);
        const auto* comma = first->NextSibling;
        ASSERT_NE(comma, nullptr);
        EXPECT_EQ(comma->Kind, parser::NodeKind::ListSeparator);
        EXPECT_EQ(comma->Tokens.Begin, first->Tokens.End);
        EXPECT_EQ(comma->Tokens.End, first->Tokens.End + 1);
        EXPECT_EQ(tree.Role(comma->Tokens.Begin), parser::PunctuationRole::ListSeparator);
        const auto* second = comma->NextSibling;
        ASSERT_NE(second, nullptr);
        EXPECT_EQ(second->Kind, parser::NodeKind::ParameterDeclaration);
        EXPECT_EQ(tree.SourceText(second->Tokens), "int other=0");
        EXPECT_EQ(second->Tokens.Begin, comma->Tokens.End);
        EXPECT_EQ(second->Tokens.End, parameters->Tokens.End - 1);
        EXPECT_EQ(second->NextSibling, nullptr);
        size_t defaults = 0;
        for (const auto* parameter : {first, second}) {
            pending = {parameter};
            while (!pending.empty()) {
                const auto* node = pending.back();
                pending.pop_back();
                if (node->Kind == parser::NodeKind::Initializer) {
                    ++defaults;
                    EXPECT_EQ(node->Tokens.End, parameter->Tokens.End);
                    EXPECT_NE(node->FirstChild, nullptr);
                }
                for (auto* child = node->FirstChild; child; child = child->NextSibling) {
                    pending.push_back(child);
                }
            }
        }
        EXPECT_EQ(defaults, 2u);
        const auto* outside = tree.Root().LastChild;
        ASSERT_EQ(outside->Kind, parser::NodeKind::Declaration);
        ASSERT_NE(outside->FirstChild, nullptr);
        EXPECT_EQ(outside->FirstChild->Kind, parser::NodeKind::DeclarationSpecifiers);
        EXPECT_EQ(tree.SourceText(outside->FirstChild->Tokens), "Value");
    }
}

// Constructor heads must retain an actual declarator, not become return types.
TEST(Parser, QualifiedConstructorHeadsKeepTerminalDeclaratorNames)
{
    const test::golden::Fixture fixture{PARSER_TESTDATA_DIR, "function-kind"};
    const auto source = fixture.InputNamed(".cpp");
    PaddedInput input(source);
    size_t errors = 0;
    const auto tree = parser::Parse(input.View(), [&] (parser::ParseError) { ++errors; });
    EXPECT_EQ(errors, 0);
    const std::array<std::pair<std::string_view, std::string_view>, 9> cases{
        {
            {"Meter::Meter()", "Meter"},
            {"Meter::Meter(int", "Meter"},
            {"Meter::~Meter()", "~Meter"},
            {"Path::Meter::Meter(", "Meter"},
            {"Vessel<T>::Vessel()", "Vessel"},
            {"Vessel<T>::~Vessel()", "~Vessel"},
            {"Vessel<T>::Item::Item()", "Item"},
            {"Vessel<T>::Item::~Item()", "~Item"},
            {"Vessel<int>::Vessel()", "Vessel"},
        },
    };
    std::array<bool, cases.size()> found{};
    std::vector<const Node*> pending{&tree.Root()};
    while (!pending.empty()) {
        const auto* node = pending.back();
        pending.pop_back();
        const auto text = tree.SourceText(node->Tokens);
        if (node->Kind == parser::NodeKind::FunctionDefinition || node->Kind == parser::NodeKind::FunctionDeclaration) {
            for (size_t i = 0; i < cases.size(); ++i) {
                if (!text.starts_with(cases[i].first)) {
                    continue;
                }
                found[i] = true;
                SCOPED_TRACE(cases[i].first);
                const Node* list = node->FirstChild;
                while (list && list->Kind != parser::NodeKind::InitDeclaratorList) {
                    list = list->NextSibling;
                }
                ASSERT_NE(list, nullptr);
                ASSERT_NE(list->FirstChild, nullptr);
                ASSERT_NE(list->FirstChild->FirstChild, nullptr);
                const Node* name = list->FirstChild->FirstChild->FirstChild;
                while (name && name->Kind == parser::NodeKind::TemplateArgumentList) {
                    name = name->NextSibling;
                }
                ASSERT_NE(name, nullptr);
                EXPECT_EQ(tree.SourceText(name->Tokens), cases[i].second);
                EXPECT_TRUE(
                    name->Kind == parser::NodeKind::DeclaratorName || name->Kind == parser::NodeKind::DestructorName);
            }
        }
        for (auto* child = node->Child(); child; child = child->NextSibling) {
            pending.push_back(child);
        }
    }
    for (size_t i = 0; i < cases.size(); ++i) {
        SCOPED_TRACE(cases[i].first);
        EXPECT_TRUE(found[i]);
    }
}

TEST(Parser, FunctionKindsClassifyOnlyActualFunctionOwners)
{
    const test::golden::Fixture fixture{PARSER_TESTDATA_DIR, "function-kind"};
    const auto source = fixture.InputNamed(".cpp");
    PaddedInput input(source);
    size_t errors = 0;
    auto tree = parser::Parse(input.View(), [&] (parser::ParseError) { ++errors; });
    EXPECT_EQ(errors, 0);
    std::array<size_t, 4> counts{};
    size_t tryBlocks = 0;
    bool factory = false;
    bool nonexport = false;
    std::vector<const Node*> pending{&tree.Root()};
    while (!pending.empty()) {
        const auto* node = pending.back();
        pending.pop_back();
        SCOPED_TRACE(tree.SourceText(node->Tokens));
        auto expected = parser::FunctionKind::Unknown;
        if (node->Kind == parser::NodeKind::FunctionDeclaration || node->Kind == parser::NodeKind::FunctionDefinition) {
            const Node* list = node->FirstChild;
            while (list && list->Kind != parser::NodeKind::InitDeclaratorList) {
                list = list->NextSibling;
            }
            ASSERT_NE(list, nullptr);
            ASSERT_NE(list->FirstChild, nullptr);
            const Node* declarator = list->FirstChild->FirstChild;
            ASSERT_NE(declarator, nullptr);
            const Node* name = declarator->FirstChild;
            if (name && name->Kind == parser::NodeKind::NestedDeclarator) {
                name = name->FirstChild;
            }
            while (name && name->Kind != parser::NodeKind::DeclaratorName &&
                name->Kind != parser::NodeKind::DestructorName && name->Kind != parser::NodeKind::OperatorFunctionName)
            {
                name = name->NextSibling;
            }
            ASSERT_NE(name, nullptr);
            const auto spelling = tree.SourceText(name->Tokens);
            expected = name->Kind == parser::NodeKind::DestructorName
                ? parser::FunctionKind::Destructor
                : spelling == "Meter" || spelling == "Vessel" || spelling == "Item" || spelling == "Local" ||
                    spelling == "Entry"
                    ? parser::FunctionKind::Constructor
                    : parser::FunctionKind::Ordinary;
            factory |= spelling == "factory";
            for (auto* child = node->FirstChild; child; child = child->NextSibling) {
                if (child->Kind == parser::NodeKind::FunctionTryBlock) {
                    ++tryBlocks;
                    EXPECT_EQ(child->GetFunctionKind(), parser::FunctionKind::Unknown);
                    EXPECT_NE(expected, parser::FunctionKind::Unknown);
                }
            }
        }
        if (tree.SourceText(node->Tokens) == "Entry* marker;" && node->Kind == parser::NodeKind::ExpressionStatement) {
            nonexport = true;
        }
        EXPECT_EQ(node->GetFunctionKind(), expected);
        ++counts[static_cast<size_t>(expected)];
        for (auto* child = node->FirstChild; child; child = child->NextSibling) {
            pending.push_back(child);
        }
    }
    EXPECT_GT(counts[static_cast<size_t>(parser::FunctionKind::Constructor)], 10u);
    EXPECT_GT(counts[static_cast<size_t>(parser::FunctionKind::Destructor)], 5u);
    EXPECT_GT(counts[static_cast<size_t>(parser::FunctionKind::Ordinary)], 5u);
    EXPECT_EQ(tryBlocks, 2u);
    EXPECT_TRUE(factory);
    EXPECT_TRUE(nonexport);
}

TEST(Parser, FunctionKindsLeaveUncertainHeadsAndNonfunctionsUnknown)
{
    const std::array<std::string_view, 30> cases{
        {
            "Loose();",
            "Owner::Other();",
            "Owner::~Other();",
            "struct Owner { ~Other(); };",
            "Owner<int>>::Owner();",
            "Owner::Owner(",
            "int bad extra();",
            "int (*pointer)(int);",
            "struct Owner { Owner(int,,int); };",
            "struct Owner { Owner(,int); };",
            "struct Owner { Owner(int,); };",
            "struct Owner { Owner(int,/* empty */,int); };",
            "struct Owner { Owner(int,,int) {} };",
            "struct Owner { Owner(,int) {} };",
            "struct Owner { Owner(int,) {} };",
            "struct Owner { Owner(int,/* empty */,int) {} };",
            "struct Owner { Owner(int,,int) try {} catch (...) {} };",
            "struct Owner { Owner(,int) try {} catch (...) {} };",
            "struct Owner { Owner(int,) try {} catch (...) {} };",
            "struct Owner { Owner(int,/* empty */,int) " "try {} catch (...) {} };",
            "Owner::Owner(int,,int) {}",
            "Owner::Owner(int,,int) try {} catch (...) {}",
            "int f(int,,int) {}",
            "int f() unexpected;",
            "int f(), g();",
            "Owner::Owner() garbage {}",
            "First::Second::First();",
            "int f+(int);",
            "Type<int>> f();",
            "struct { Anonymous(); };",
        },
    };
    for (auto source : cases) {
        SCOPED_TRACE(source);
        PaddedInput input(source);
        auto tree = parser::Parse(input.View(), [] (parser::ParseError) {});
        std::vector<const Node*> pending{&tree.Root()};
        while (!pending.empty()) {
            const auto* node = pending.back();
            pending.pop_back();
            EXPECT_EQ(node->GetFunctionKind(), parser::FunctionKind::Unknown);
            for (auto* child = node->FirstChild; child; child = child->NextSibling) {
                pending.push_back(child);
            }
        }
    }
}

TEST(Parser, FunctionKindsSurviveNodeCopiesAndTreeMoves)
{
    PaddedInput input("#include <z>\n#include <a>\n" "struct Device { Device() { /* note */ if (ready) return; ; } "
        "~Device() {} int Read() { return 1; } };\n");
    auto original = parser::Parse(input.View(), [] (parser::ParseError) {});
    const auto check = [] (const parser::SyntaxTree& tree) {
        std::array<size_t, 4> counts{};
        std::vector<const Node*> pending{&tree.Root()};
        while (!pending.empty()) {
            const auto* node = pending.back();
            pending.pop_back();
            ++counts[static_cast<size_t>(node->GetFunctionKind())];
            if (node->Kind != parser::NodeKind::FunctionDefinition) {
                EXPECT_EQ(node->GetFunctionKind(), parser::FunctionKind::Unknown);
            }
            for (auto* child = node->FirstChild; child; child = child->NextSibling) {
                pending.push_back(child);
            }
        }
        EXPECT_EQ(counts[static_cast<size_t>(parser::FunctionKind::Constructor)], 1u);
        EXPECT_EQ(counts[static_cast<size_t>(parser::FunctionKind::Destructor)], 1u);
        EXPECT_EQ(counts[static_cast<size_t>(parser::FunctionKind::Ordinary)], 1u);
    };
    check(original);
    auto moved = std::move(original);
    check(moved);
    original = std::move(moved);
    check(original);
    const Node* record = original.Root().FirstChild;
    while (record && record->Kind != parser::NodeKind::RecordDeclaration) {
        record = record->NextSibling;
    }
    ASSERT_NE(record, nullptr);
    const Node* constructor = record->FirstChild;
    while (constructor && constructor->Kind != parser::NodeKind::FunctionDefinition) {
        constructor = constructor->NextSibling;
    }
    ASSERT_NE(constructor, nullptr);
    Node copy = *constructor;
    EXPECT_EQ(copy.GetFunctionKind(), parser::FunctionKind::Constructor);
    Node assigned;
    EXPECT_EQ(assigned.GetFunctionKind(), parser::FunctionKind::Unknown);
    assigned = copy;
    EXPECT_EQ(assigned.GetFunctionKind(), parser::FunctionKind::Constructor);
}

TEST(Parser, FunctionKindsRetainBalancedQualifiedAndExplicitReturnEvidence)
{
    struct Case
    {
        std::string_view Source;
        parser::FunctionKind Kind;
        std::string_view Name;
        size_t OuterPointers;
    };
    const std::array<Case, 28> cases{
        {
            {"Owner<Pair<int, long>>::Owner() {}", parser::FunctionKind::Constructor, "Owner", 0},
            {"Owner<Pair<int, long>>::~Owner() {}", parser::FunctionKind::Destructor, "Owner", 0},
            {"Outer<decltype(call(1))>::Part::Part() {}", parser::FunctionKind::Constructor, "Part", 0},
            {"Owner /* gap */ :: /* gap */ Owner() {}", parser::FunctionKind::Constructor, "Owner", 0},
            {"Owner:: ~ /* gap */ Owner() {}", parser::FunctionKind::Destructor, "Owner", 0},
            {"struct Result {}; Result Result();", parser::FunctionKind::Ordinary, "Result", 0},
            {"[[nodiscard]] Type<Pair<int,long>> build();", parser::FunctionKind::Ordinary, "build", 0},
            {
                "template<int, class> struct Owner { Owner(); }; " "template<> Owner<(1 > 2), int>::Owner() {}",
                parser::FunctionKind::Constructor,
                "Owner",
                0,
            },
            {"Owner<(1 > 2), int>::~Owner() {}", parser::FunctionKind::Destructor, "Owner", 0},
            {"Owner<(8 >> 1), int>::Owner() {}", parser::FunctionKind::Constructor, "Owner", 0},
            {"Owner<(8 >> 1), int>::~Owner() {}", parser::FunctionKind::Destructor, "Owner", 0},
            {"Owner<(1 < 2), int>::Owner() {}", parser::FunctionKind::Constructor, "Owner", 0},
            {"Owner<(1 << 2), int>::Owner() {}", parser::FunctionKind::Constructor, "Owner", 0},
            {
                "template<bool> struct Flag { Flag(); }; " "template<> Flag<(true && false)>::Flag() {}",
                parser::FunctionKind::Constructor,
                "Flag",
                0,
            },
            {"Flag<(true & false)>::~Flag() {}", parser::FunctionKind::Destructor, "Flag", 0},
            {"Scale<(2 * 3), int>::Scale() {}", parser::FunctionKind::Constructor, "Scale", 0},
            {"int* Owner<(1 > 2), int>::Read() {}", parser::FunctionKind::Ordinary, "Read", 1},
            {"int& Flag<(true && false)>::Read() {}", parser::FunctionKind::Ordinary, "Read", 1},
            {"Owner::Owner() {}", parser::FunctionKind::Constructor, "Owner", 0},
            {"Owner::Owner(/* empty */) {}", parser::FunctionKind::Constructor, "Owner", 0},
            {"Owner::Owner() try {} catch (...) {}", parser::FunctionKind::Constructor, "Owner", 0},
            {"Owner::Owner(/* empty */) try {} catch (...) {}", parser::FunctionKind::Constructor, "Owner", 0},
            {"Owner::Owner(Pair<int,long>) {}", parser::FunctionKind::Constructor, "Owner", 0},
            {"Owner::Owner(int value = pick(1,2)) {}", parser::FunctionKind::Constructor, "Owner", 0},
            {
                "Owner::Owner(int value = (1 > 2), int other = (8 >> 1)) {}",
                parser::FunctionKind::Constructor,
                "Owner",
                0,
            },
            {
                "Owner::Owner(Type<Pair<int,long>>, " "void (*callback)(int,int)) {}",
                parser::FunctionKind::Constructor,
                "Owner",
                0,
            },
            {
                "Owner::Owner(Pair<int,long> value = {1,2}, int other = 0) {}",
                parser::FunctionKind::Constructor,
                "Owner",
                0,
            },
            {"template<class... Ts> Owner::Owner(Ts... values) {}", parser::FunctionKind::Constructor, "Owner", 0},
        },
    };
    const auto check = [] (const Case& item) {
        SCOPED_TRACE(item.Source);
        PaddedInput input(item.Source);
        auto tree = parser::Parse(input.View(), [] (parser::ParseError) {});
        const auto* function = tree.Root().LastChild;
        ASSERT_NE(function, nullptr);
        while (function->Kind == parser::NodeKind::TemplateDeclaration) {
            function = function->LastChild;
            ASSERT_NE(function, nullptr);
        }
        ASSERT_TRUE(function->Kind == parser::NodeKind::FunctionDeclaration ||
            function->Kind == parser::NodeKind::FunctionDefinition);
        EXPECT_EQ(function->GetFunctionKind(), item.Kind);
        const auto* list = function->FirstChild;
        while (list && list->Kind != parser::NodeKind::InitDeclaratorList) {
            list = list->NextSibling;
        }
        ASSERT_NE(list, nullptr);
        ASSERT_NE(list->FirstChild, nullptr);
        const auto* init = list->FirstChild;
        EXPECT_EQ(init->Kind, parser::NodeKind::InitDeclarator);
        EXPECT_EQ(init->NextSibling, nullptr);
        EXPECT_EQ(init->Tokens.Begin, list->Tokens.Begin);
        EXPECT_EQ(init->Tokens.End, list->Tokens.End);
        const auto* declarator = init->FirstChild;
        ASSERT_NE(declarator, nullptr);
        EXPECT_EQ(declarator->Kind, parser::NodeKind::Declarator);
        size_t pointers = 0;
        size_t names = 0;
        auto parameterBegin = declarator->Tokens.End;
        for (auto* child = declarator->FirstChild; child; child = child->NextSibling) {
            if (child->Kind == parser::NodeKind::PointerOperator) {
                ++pointers;
            } else if (child->Kind == parser::NodeKind::DeclaratorName ||
                child->Kind == parser::NodeKind::DestructorName)
            {
                ++names;
                EXPECT_EQ(tree.TokenText(child->Tokens.End - 1), item.Name);
                EXPECT_EQ(
                    child->Kind,
                    item.Kind == parser::FunctionKind::Destructor
                    ? parser::NodeKind::DestructorName
                    : parser::NodeKind::DeclaratorName);
            } else if (child->Kind != parser::NodeKind::TemplateArgumentList) {
                EXPECT_EQ(child->Kind, parser::NodeKind::FunctionDeclarator);
                parameterBegin = child->Tokens.Begin;
            }
        }
        EXPECT_EQ(names, 1u);
        EXPECT_EQ(pointers, item.OuterPointers);
        ASSERT_LT(parameterBegin, declarator->Tokens.End);
        // Owner template arguments do not own pointer/reference declarators;
        // parameters may contain their own operators.
        int delimiterDepth = 0;
        for (auto i = declarator->Tokens.Begin; i < parameterBegin; ++i) {
            const auto kind = tree.Kind(i);
            if (kind == tokenizer::TokenKind::Operator_OpenParenthesis) {
                ++delimiterDepth;
            } else if (kind == tokenizer::TokenKind::Operator_CloseParenthesis) {
                --delimiterDepth;
            } else if (kind == tokenizer::TokenKind::Operator_Star ||
                kind == tokenizer::TokenKind::Operator_Ampersand || kind == tokenizer::TokenKind::Operator_And)
            {
                const auto role = tree.Role(i);
                if (delimiterDepth != 0) {
                    EXPECT_NE(role, parser::PunctuationRole::PointerDeclarator);
                    EXPECT_NE(role, parser::PunctuationRole::ReferenceDeclarator);
                } else if (item.OuterPointers != 0) {
                    EXPECT_EQ(
                        role,
                        kind == tokenizer::TokenKind::Operator_Star
                        ? parser::PunctuationRole::PointerDeclarator
                        : parser::PunctuationRole::ReferenceDeclarator);
                }
            }
        }
    };
    for (const auto& item : cases) {
        check(item);
    }
}

TEST(Parser, OpaqueRecordHeadsHaveOnlyLocalConstructorOwners)
{
    const std::string_view heads[] = {
        "API Parcel",
        "api Parcel",
        "API EXTRA Parcel",
        "API [[nodiscard]] Parcel",
        "[[nodiscard]] API Parcel",
        "API Parcel [[nodiscard]]",
        "API Owner::Parcel",
        "API Owner<int>::Parcel<long>",
    };
    for (const auto head : heads) {
        for (const auto wrapper : {"", "template<class T> "}) {
            const auto source = std::string(wrapper) + "struct " + std::string(head) +
                " { Parcel(Item *p, Item &r, Item &&v, " "void (*callback)(int)) {} "
                    "void (*member)(int); int (*factory())(double); " "int (ordinary)(double); "
                    "int explicit_return(Item *p) {} " "void local() { Parcel*inside; } }; "
                    "void outside() { Parcel*marker; }";
            SCOPED_TRACE(source);
            PaddedInput input(source);
            const auto tree = parser::Parse(input.View(), [] (parser::ParseError) {});
            size_t headsRecovered = 0;
            size_t constructors = 0;
            size_t names = 0;
            size_t outsideDeclarations = 0;
            std::vector<const Node*> pending{&tree.Root()};
            std::vector<parser::TokenRange> lists;
            std::vector<parser::TokenRange> nested;
            while (!pending.empty()) {
                const auto* node = pending.back();
                pending.pop_back();
                const auto text = tree.SourceText(node->Tokens);
                if (node->Kind == parser::NodeKind::UnparsedSyntax && text.find("API") != std::string_view::npos) {
                    ++headsRecovered;
                }
                if (node->Kind == parser::NodeKind::FunctionDefinition && text.starts_with("Parcel(")) {
                    ++constructors;
                    ASSERT_NE(node->FirstChild, nullptr);
                    EXPECT_NE(node->FirstChild->Kind, parser::NodeKind::DeclarationSpecifiers);
                }
                if (node->Kind == parser::NodeKind::DeclaratorName && text == "Parcel") {
                    ++names;
                }
                if (node->Kind == parser::NodeKind::DeclarationStatement &&
                    (text == "Parcel*marker;" || text == "Parcel*inside;"))
                {
                    ++outsideDeclarations;
                }
                if (node->Kind == parser::NodeKind::ParameterList) {
                    lists.push_back(node->Tokens);
                }
                if (node->Kind == parser::NodeKind::NestedDeclarator) {
                    nested.push_back(node->Tokens);
                }
                for (auto* child = node->FirstChild; child; child = child->NextSibling) {
                    pending.push_back(child);
                }
            }
            EXPECT_EQ(constructors, 1u);
            EXPECT_EQ(names, 1u);
            EXPECT_EQ(outsideDeclarations, 0u);
            if (head.find("API") != std::string_view::npos) {
                EXPECT_EQ(headsRecovered, 1u);
            }
            for (const auto list : lists) {
                for (const auto grouping : nested) {
                    EXPECT_FALSE(list.Begin == grouping.Begin && list.End == grouping.End);
                }
            }
        }
    }

    // Ordinary template records still export their structurally eligible name.
    for (const auto head : {"Parcel", "[[nodiscard]] Parcel"}) {
        PaddedInput input("template<class T> struct " + std::string(head) +
            " { Parcel(Item *p) {} }; void outside() { Parcel<int>*marker; }");
        const auto tree = parser::Parse(input.View(), [] (parser::ParseError) {});
        size_t declarations = 0;
        std::vector<const Node*> pending{&tree.Root()};
        while (!pending.empty()) {
            const auto* node = pending.back();
            pending.pop_back();
            if (node->Kind == parser::NodeKind::DeclarationStatement &&
                tree.SourceText(node->Tokens) == "Parcel<int>*marker;")
            {
                ++declarations;
            }
            for (auto* child = node->FirstChild; child; child = child->NextSibling) {
                pending.push_back(child);
            }
        }
        EXPECT_EQ(declarations, 1u);
    }
}

TEST(Parser, MalformedRecordOwnersRecoverOnlyConstructorInitializerHeaders)
{
    for (const auto head : {"API Parcel + broken", "API Owner::", "API [unresolved] Parcel"}) {
        const auto source = "struct " + std::string(head) +
            " { Parcel(Item &p) : Base(p) { invoke(a,b); } " "void (*callback)(int); int (*factory())(double); };";
        SCOPED_TRACE(source);
        PaddedInput input(source);
        const auto tree = parser::Parse(input.View(), [] (parser::ParseError) {});
        size_t recovered = 0, bodies = 0, fakeLists = 0;
        std::vector<const Node*> pending{&tree.Root()};
        while (!pending.empty()) {
            const auto* node = pending.back();
            pending.pop_back();
            const auto text = tree.SourceText(node->Tokens);
            if (node->Kind == parser::NodeKind::UnparsedSyntax && text == "Parcel(Item &p) : Base(p)") {
                ++recovered;
            }
            if (node->Kind == parser::NodeKind::CompoundStatement && text == "{ invoke(a,b); }") {
                ++bodies;
            }
            if (node->Kind == parser::NodeKind::ParameterList && text == "(Item &p)") {
                ++fakeLists;
            }
            for (auto* child = node->FirstChild; child; child = child->NextSibling) {
                pending.push_back(child);
            }
        }
        EXPECT_EQ(recovered, 1u);
        EXPECT_EQ(bodies, 1u);
        EXPECT_EQ(fakeLists, 0u);
    }
}

TEST(Parser, OpaqueRecordOwnersPreserveGroupedMemberDeclarators)
{
    const std::string_view members = "void (*callback)(int); int (*factory())(double); " "int (ordinary)(double); "
        "int [[nodiscard]] explicit_return(Item *p);";
    std::vector<std::string> expected;
    std::vector<std::vector<parser::PunctuationRole>> expectedRoles;
    for (const auto head : {"Plain", "API Parcel", "API Parcel + broken"}) {
        SCOPED_TRACE(head);
        PaddedInput input("struct " + std::string(head) + " { " + std::string(members) + " };");
        const auto tree = parser::Parse(input.View(), [] (parser::ParseError) {});
        const auto* record = tree.Root().FirstChild;
        ASSERT_NE(record, nullptr);
        ASSERT_EQ(record->Kind, parser::NodeKind::RecordDeclaration);
        std::vector<std::string> actual;
        std::vector<std::vector<parser::PunctuationRole>> roles;
        for (auto* member = record->FirstChild; member; member = member->NextSibling) {
            if (member->Kind == parser::NodeKind::UnparsedSyntax) {
                continue;
            }
            std::string serialized;
            AppendSExpression(serialized, tree, *member, 0);
            actual.push_back(std::move(serialized));
            std::vector<parser::PunctuationRole> memberRoles;
            for (auto i = member->Tokens.Begin; i < member->Tokens.End; ++i) {
                memberRoles.push_back(tree.Role(i));
            }
            roles.push_back(std::move(memberRoles));
        }
        ASSERT_EQ(actual.size(), 4u);
        if (expected.empty()) {
            expected = std::move(actual);
            expectedRoles = std::move(roles);
        } else {
            EXPECT_EQ(actual, expected);
            EXPECT_EQ(roles, expectedRoles);
        }
    }
}

TEST(Parser, ConstructorInitializerCapturesAreNotOuterArrayDeclarators)
{
    const std::string_view sources[] = {
        "struct Task { Task() : callback([this] { stop(); }, " "[this] { start(); }) {} };",
        "struct Task { Task() : first([] { return 1; }), " "second([] { return 2; }) {} };",
        "struct Task { Task() noexcept : callback(" "[this]() noexcept { stop(); }, "
            "[this]() noexcept { start(); }) {} };",
        "struct Task { Task() : first(0), callback(wrap(bind(" "[id = 1](int* value) { consume(value, id); }))) {} };",
        "struct Task { Task() noexcept(noexcept([] { return 1; }())) "
            ": first([] { return 1; }), second([] { return 2; }) {} };",
        "struct Task { Task() try : first([] { return 1; }), " "second([] { return 2; }) {} catch (...) {} };",
    };
    for (const auto source : sources) {
        SCOPED_TRACE(source);
        PaddedInput input(source);
        size_t errors = 0;
        const auto tree = parser::Parse(input.View(), [&] (parser::ParseError) { ++errors; });
        EXPECT_EQ(errors, 0u);
        const Node* function = nullptr;
        const Node* initializers = nullptr;
        std::vector<const Node*> pending{&tree.Root()};
        while (!pending.empty()) {
            const auto* node = pending.back();
            pending.pop_back();
            if (node->Kind == parser::NodeKind::ConstructorInitializerList) {
                initializers = node;
            }
            if (node->Kind == parser::NodeKind::FunctionDeclarator && node->FirstChild &&
                tree.SourceText(node->FirstChild->Tokens) == "()")
            {
                function = node;
            }
            for (auto* child = node->FirstChild; child; child = child->NextSibling) {
                pending.push_back(child);
            }
        }
        ASSERT_NE(function, nullptr);
        ASSERT_NE(initializers, nullptr);
        EXPECT_EQ(function->LastChild, initializers);
        EXPECT_EQ(function->Tokens.End, initializers->Tokens.End);
        EXPECT_EQ(tree.TokenText(initializers->Tokens.Begin), ":");
        EXPECT_EQ(tree.Role(initializers->Tokens.Begin), parser::PunctuationRole::ConstructorInitializerDelimiter);
        const Node* outer = nullptr;
        pending = {&tree.Root()};
        while (!pending.empty()) {
            const auto* node = pending.back();
            pending.pop_back();
            for (auto* child = node->FirstChild; child; child = child->NextSibling) {
                if (child == function) {
                    outer = node;
                }
                pending.push_back(child);
            }
        }
        ASSERT_NE(outer, nullptr);
        EXPECT_EQ(outer->LastChild, function);
        for (auto* child = outer->FirstChild; child; child = child->NextSibling) {
            if (child->Kind == parser::NodeKind::ArrayDeclarator) {
                EXPECT_LE(child->Tokens.End, initializers->Tokens.Begin);
            }
        }
        size_t lambdas = 0, members = 0;
        pending = {initializers};
        while (!pending.empty()) {
            const auto* node = pending.back();
            pending.pop_back();
            EXPECT_GE(node->Tokens.Begin, initializers->Tokens.Begin);
            EXPECT_LE(node->Tokens.End, initializers->Tokens.End);
            if (node->Kind == parser::NodeKind::LambdaExpression) {
                ++lambdas;
                EXPECT_NE(node->FirstChild, nullptr);
                EXPECT_EQ(node->LastChild->Kind, parser::NodeKind::LambdaBody);
            }
            if (node->Kind == parser::NodeKind::LambdaCaptureList || node->Kind == parser::NodeKind::LambdaBody) {
                EXPECT_EQ(tree.Role(node->Tokens.Begin), parser::PunctuationRole::LambdaDelimiter);
                EXPECT_EQ(tree.Role(node->Tokens.End - 1), parser::PunctuationRole::LambdaDelimiter);
            }
            members += node->Kind == parser::NodeKind::ConstructorInitializer;
            EXPECT_NE(node->Kind, parser::NodeKind::ArrayDeclarator);
            for (auto* child = node->FirstChild; child; child = child->NextSibling) {
                pending.push_back(child);
            }
        }
        EXPECT_EQ(
            members,
            source.find("callback") != std::string_view::npos && source.find("first(0)") == std::string_view::npos
            ? 1u
            : 2u);
        EXPECT_EQ(lambdas, source.find("bind") == std::string_view::npos ? 2u : 1u);
    }
}

TEST(Parser, TerminalConstructorTemplateIdsHaveLocalNamesAndSeparateParameters)
{
    struct Case
    {
        std::string_view Source;
        std::string_view Name;
    };
    const Case cases[] = {
        {"template<class T> struct Parcel { " "explicit Parcel<T>(Item<T>* value) {} };", "Parcel"},
        {"template<class T> struct parcel { " "parcel<T>(Item<T>& value) {} };", "parcel"},
        {"template<class T> struct Holder { " "Holder<T>(Item<T>&& value) {} };", "Holder"},
        {
            "template<class T> struct Holder { explicit /* head */ "
                "Holder /* args */ <T> /* params */ (T* value) {} };",
            "Holder",
        },
        {"template<class T> struct Holder { " "Holder<Pair<T*,int>,(2 & 1)>(T* value) {} };", "Holder"},
        {"template<class T> struct Holder { " "Holder<T>(void (*callback)(int), T* value) {} };", "Holder"},
        {"template<class T> struct Holder { " "Holder<T>(/* empty */) {} };", "Holder"},
        {"template<class T> struct Holder { " "Holder<T>(T* value) : member(value) {} };", "Holder"},
        {"template<class T> struct Holder { " "Holder<T>(T* value) try : member(value) {} catch (...) {} };", "Holder"},
    };
    for (const auto& item : cases) {
        SCOPED_TRACE(item.Source);
        PaddedInput input(item.Source);
        size_t errors = 0;
        const auto tree = parser::Parse(input.View(), [&] (parser::ParseError) { ++errors; });
        EXPECT_EQ(errors, 0u);
        size_t constructors = 0;
        std::vector<const Node*> pending{&tree.Root()};
        while (!pending.empty()) {
            const auto* node = pending.back();
            pending.pop_back();
            for (auto* child = node->FirstChild; child; child = child->NextSibling) {
                pending.push_back(child);
            }
            if (node->Kind != parser::NodeKind::FunctionDefinition) {
                EXPECT_EQ(node->GetFunctionKind(), parser::FunctionKind::Unknown);
                continue;
            }
            ++constructors;
            EXPECT_EQ(node->GetFunctionKind(), parser::FunctionKind::Constructor);
            const Node* list = node->FirstChild;
            if (list && list->Kind == parser::NodeKind::DeclarationSpecifiers) {
                EXPECT_EQ(tree.TokenText(list->Tokens.Begin), "explicit");
                list = list->NextSibling;
            }
            ASSERT_NE(list, nullptr);
            ASSERT_EQ(list->Kind, parser::NodeKind::InitDeclaratorList);
            ASSERT_NE(list->FirstChild, nullptr);
            const auto* declarator = list->FirstChild->FirstChild;
            ASSERT_NE(declarator, nullptr);
            ASSERT_EQ(declarator->Kind, parser::NodeKind::Declarator);
            const auto* name = declarator->FirstChild;
            ASSERT_NE(name, nullptr);
            EXPECT_EQ(name->Kind, parser::NodeKind::DeclaratorName);
            EXPECT_EQ(tree.SourceText(name->Tokens), item.Name);
            const auto* arguments = name->NextSibling;
            ASSERT_NE(arguments, nullptr);
            EXPECT_EQ(arguments->Kind, parser::NodeKind::TemplateArgumentList);
            EXPECT_EQ(tree.Role(arguments->Tokens.Begin), parser::PunctuationRole::TemplateDelimiter);
            EXPECT_EQ(tree.Role(arguments->Tokens.End - 1), parser::PunctuationRole::TemplateDelimiter);
            const auto* function = declarator->LastChild;
            ASSERT_NE(function, nullptr);
            ASSERT_EQ(function->Kind, parser::NodeKind::FunctionDeclarator);
            ASSERT_NE(function->FirstChild, nullptr);
            EXPECT_EQ(function->FirstChild->Kind, parser::NodeKind::ParameterList);
            EXPECT_LE(arguments->Tokens.End, function->FirstChild->Tokens.Begin);
            for (auto* child = declarator->FirstChild; child; child = child->NextSibling) {
                EXPECT_NE(child->Kind, parser::NodeKind::NestedDeclarator);
                EXPECT_NE(child->Kind, parser::NodeKind::PointerOperator);
                EXPECT_NE(child->Kind, parser::NodeKind::ArrayDeclarator);
            }
            for (auto i = arguments->Tokens.Begin; i < arguments->Tokens.End; ++i) {
                EXPECT_NE(tree.Role(i), parser::PunctuationRole::PointerDeclarator);
                EXPECT_NE(tree.Role(i), parser::PunctuationRole::ReferenceDeclarator);
            }
        }
        EXPECT_EQ(constructors, 1u);
    }
}

TEST(Parser, TerminalConstructorTemplateIdsDoNotBorrowOuterOrMalformedOwners)
{
    const std::string_view rejected[] = {
        "template<class T> struct Owner { Other<T>(T* value) {} };",
        "template<class T> struct Outer { " "struct Inner { Outer<T>(T* value) {} }; };",
        "Owner<int>(int* value) {}",
        "template<class T> struct Owner { ::Owner<T>(T* value) {} };",
        "template<class T> struct Owner { " "Owner<T>::Owner<T>(T* value) {} };",
        "template<class T> struct Owner { Owner<T>>(T* value) {} };",
        "template<class T> struct Owner { Owner<T(T* value) {} };",
        "template<class T> struct Owner { ~Owner<T>() {} };",
        "template<class T> struct Owner { Owner<T>(,T* value) {} };",
        "template<class T> struct Owner { Owner<T>(T* value,) {} };",
        "template<class T> struct Owner { " "Owner<T>(T* value,/* empty */,T* other) {} };",
    };
    for (auto source : rejected) {
        SCOPED_TRACE(source);
        PaddedInput input(source);
        const auto tree = parser::Parse(input.View(), [] (parser::ParseError) {});
        std::vector<const Node*> pending{&tree.Root()};
        while (!pending.empty()) {
            const auto* node = pending.back();
            pending.pop_back();
            EXPECT_NE(node->GetFunctionKind(), parser::FunctionKind::Constructor);
            for (auto* child = node->FirstChild; child; child = child->NextSibling) {
                pending.push_back(child);
            }
        }
    }
    PaddedInput nested("template<class T> struct Outer { struct Inner { Inner<T>(T* p) {} }; " "Outer<T>(T* p) {} };");
    const auto tree = parser::Parse(nested.View(), [] (parser::ParseError) {});
    size_t constructors = 0;
    std::vector<const Node*> pending{&tree.Root()};
    while (!pending.empty()) {
        const auto* node = pending.back();
        pending.pop_back();
        constructors += node->GetFunctionKind() == parser::FunctionKind::Constructor;
        for (auto* child = node->FirstChild; child; child = child->NextSibling) {
            pending.push_back(child);
        }
    }
    EXPECT_EQ(constructors, 2u);
}

TEST(Parser, TerminalConstructorTemplateIdsSurviveTreeMoves)
{
    PaddedInput input("template<class T> struct Parcel { Parcel<T>(T* p) {} };\n");
    auto tree = parser::Parse(input.View(), [] (parser::ParseError) {});
    const auto check = [] (const parser::SyntaxTree& value) {
        size_t constructors = 0;
        std::vector<const Node*> pending{&value.Root()};
        while (!pending.empty()) {
            const auto* node = pending.back();
            pending.pop_back();
            constructors += node->GetFunctionKind() == parser::FunctionKind::Constructor;
            for (auto* child = node->FirstChild; child; child = child->NextSibling) {
                pending.push_back(child);
            }
        }
        EXPECT_EQ(constructors, 1u);
    };
    check(tree);
    auto moved = std::move(tree);
    check(moved);
    tree = std::move(moved);
    check(tree);
}

TEST(Parser, TerminalConstructorTemplateIdsPreserveSelfTypedStatementBindings)
{
    const std::string_view bodies[] = {
        "Owner<T>(slot); Owner<T>* pointer = &slot;",
        "consume(Owner<T>(seed)); Owner<T>* pointer = nullptr;",
        "Owner<T> copy = Owner<T>(seed); Owner<T>* pointer = &copy;",
        "(void)Owner<T>(seed); Owner<T>* pointer = nullptr;",
        "Owner<T>(seed); Owner<T>* pointer = nullptr;",
    };
    for (auto body : bodies) {
        const auto source = "template<class T> struct Owner { Owner() {} Owner(T input) {} "
            "void consume(Owner<T> input) {} void inspect(T seed) { " + std::string(body) + " } };";
        SCOPED_TRACE(source);
        PaddedInput input(source);
        size_t errors = 0;
        const auto tree = parser::Parse(input.View(), [&] (parser::ParseError) { ++errors; });
        EXPECT_EQ(errors, 0u);
        size_t pointers = 0;
        size_t parenthesizedNames = 0;
        std::vector<const Node*> pending{&tree.Root()};
        while (!pending.empty()) {
            const auto* node = pending.back();
            pending.pop_back();
            for (auto* child = node->FirstChild; child; child = child->NextSibling) {
                pending.push_back(child);
            }
            const auto text = tree.SourceText(node->Tokens);
            if (node->Kind == parser::NodeKind::ExpressionStatement) {
                EXPECT_FALSE(text.starts_with("Owner<T>* pointer"));
            }
            if (node->Kind != parser::NodeKind::DeclarationStatement) {
                continue;
            }
            EXPECT_EQ(node->GetFunctionKind(), parser::FunctionKind::Unknown);
            std::vector<const Node*> declared{node};
            while (!declared.empty()) {
                const auto* part = declared.back();
                declared.pop_back();
                for (auto* child = part->FirstChild; child; child = child->NextSibling) {
                    declared.push_back(child);
                }
                if (part->Kind != parser::NodeKind::DeclaratorName) {
                    continue;
                }
                const auto name = tree.SourceText(part->Tokens);
                EXPECT_NE(name, "Owner");
                pointers += name == "pointer";
                if (text.starts_with("Owner<T>(")) {
                    EXPECT_EQ(name, body.starts_with("Owner<T>(slot)") ? "slot" : "seed");
                    ++parenthesizedNames;
                }
            }
        }
        EXPECT_EQ(pointers, 1u);
        EXPECT_EQ(parenthesizedNames, body.starts_with("Owner<T>(") ? 1u : 0u);
    }
}

TEST(Parser, TerminalConstructorTemplateIdsPreserveGroupedSelfTypeDeclarators)
{
    PaddedInput input("template<class T> struct Owner { " "Owner<T> (*callback)(int); Owner<T> (*factory())(int); "
        "Owner<T>* plain; Owner<T>(T* value) {} };");
    size_t errors = 0;
    const auto tree = parser::Parse(input.View(), [&] (parser::ParseError) { ++errors; });
    EXPECT_EQ(errors, 0u);
    size_t callbacks = 0, factories = 0, pointers = 0, constructors = 0,
        selfTypes = 0;
    std::vector<parser::TokenRange> parameters, groups;
    std::vector<const Node*> pending{&tree.Root()};
    while (!pending.empty()) {
        const auto* node = pending.back();
        pending.pop_back();
        for (auto* child = node->FirstChild; child; child = child->NextSibling) {
            pending.push_back(child);
        }
        constructors += node->GetFunctionKind() == parser::FunctionKind::Constructor;
        if (node->Kind == parser::NodeKind::DeclaratorName) {
            const auto name = tree.SourceText(node->Tokens);
            callbacks += name == "callback";
            factories += name == "factory";
            pointers += name == "plain";
        }
        if (node->Kind == parser::NodeKind::ParameterList) {
            parameters.push_back(node->Tokens);
        }
        if (node->Kind == parser::NodeKind::NestedDeclarator) {
            groups.push_back(node->Tokens);
        }
        if (node->Kind == parser::NodeKind::DeclarationSpecifiers && tree.SourceText(node->Tokens) == "Owner<T>") {
            ++selfTypes;
            ASSERT_NE(node->FirstChild, nullptr);
            EXPECT_EQ(node->FirstChild->Kind, parser::NodeKind::TemplateArgumentList);
        }
    }
    EXPECT_EQ(selfTypes, 3u);
    EXPECT_EQ(callbacks, 1u);
    EXPECT_EQ(factories, 1u);
    EXPECT_EQ(pointers, 1u);
    EXPECT_EQ(constructors, 1u);
    EXPECT_EQ(groups.size(), 2u);
    for (auto group : groups) {
        for (auto parameter : parameters) {
            EXPECT_FALSE(group.Begin == parameter.Begin && group.End == parameter.End);
        }
    }
}

TEST(Parser, TerminalConstructorBoundariesRejectGroupedAndUnownedContinuations)
{
    const std::string_view heads[] = {
        "Owner<T> (make)();",
        "Owner<T> (make)() { return {}; }",
        "Owner<T> ((make))();",
        "Owner<T> (((make)))() { return {}; }",
        "Owner<T> (/* name */ make) /* edge */ (T value);",
        "Owner<T> (make) [[vendor::note]] ();",
        "Owner<T> (make) __attribute__((used)) ();",
        "Owner<T> (make) const ();",
        "Owner<T> (items)[4][2];",
        "Owner<T> (items)[[](){ return 4; }()];",
        "Owner<T>(T* p) noexcept(check(p)) ();",
        "Owner<T>(T* p) noexcept const ();",
        "Owner<T>(T* p) throw(int) ();",
        "Owner<T>(T* p) throw;",
        "Owner<T>(T* p) requires true;",
        "Owner<T>(T* p) requires ready(p) {}",
        "Owner<T>(T* p) requires requires(T v) { v.use(); } {}",
        "Owner<T>(T* p) -> int;",
        "Owner<T>(T* p) -> int {}",
        "Owner<T>(T* p) -> int : saved(p) {}",
        "Owner<T>(T* p) -> int try : saved(p) {} catch (...) {}",
        "Owner<T>(T* p) = opaque();",
        "Owner<T>(T* p) = (default);",
        "Owner<T>(T* p) = default();",
        "Owner<T>(T* p) = delete[];",
        "Owner<T>(T* p) = delete extra;",
        "Owner<T>(T* p) = default [[note]];",
        "Owner<T>(T* p) = delete, other;",
        "Owner<T>(T* p) = default {}",
        "Owner<T>(T* p) = delete try {} catch (...) {}",
        "Owner<T> (make)() = delete;",
        "Owner<T>(T* p) try = default;",
        "Owner<T>(T* p) try = delete;",
        "Owner<T>(T* p) try /* edge */ = /* word */ default /* end */;",
        "Owner<T>(T* p) try /* edge */ = /* word */ delete /* end */;",
    };
    for (auto head : heads) {
        const auto source = "template<class T> struct Owner { " + std::string(head) + " };";
        SCOPED_TRACE(source);
        PaddedInput input(source);
        const auto tree = parser::Parse(input.View(), [] (parser::ParseError) {});
        size_t functionOwners = 0;
        std::vector<const Node*> pending{&tree.Root()};
        while (!pending.empty()) {
            const auto* node = pending.back();
            pending.pop_back();
            for (auto* child = node->FirstChild; child; child = child->NextSibling) {
                pending.push_back(child);
            }
            EXPECT_NE(node->GetFunctionKind(), parser::FunctionKind::Constructor);
            if (node->Kind == parser::NodeKind::FunctionDeclaration ||
                node->Kind == parser::NodeKind::FunctionDefinition)
            {
                ++functionOwners;
                EXPECT_EQ(node->GetFunctionKind(), parser::FunctionKind::Unknown);
            }
            if (node->Kind == parser::NodeKind::DeclaratorName) {
                EXPECT_NE(tree.SourceText(node->Tokens), "Owner");
            }
        }
        if (head.starts_with("Owner<T> (make)()")) {
            EXPECT_GT(functionOwners, 0u);
        }
    }
    // EOF must not complete the default/delete reservation.
    for (auto tail : {"default", "delete"}) {
        PaddedInput input("template<class T> struct Owner { Owner<T>(T* p) = " + std::string(tail));
        const auto tree = parser::Parse(input.View(), [] (parser::ParseError) {});
        std::vector<const Node*> pending{&tree.Root()};
        while (!pending.empty()) {
            const auto* node = pending.back();
            pending.pop_back();
            EXPECT_NE(node->GetFunctionKind(), parser::FunctionKind::Constructor);
            for (auto* child = node->FirstChild; child; child = child->NextSibling) {
                pending.push_back(child);
            }
        }
    }
}

TEST(Parser,
    TerminalConstructorBoundariesKeepImmediateOperandsAndSemicolonEligibility)
{
    const std::string_view heads[] = {
        "chest<T>(T* p);",
        "chest<T>(T* p) noexcept;",
        "chest<T>(T* p) noexcept(/* immediate */ check(p));",
        "chest<T>(T* p) throw(int);",
        "chest<T>(T* p) [[vendor::note(1)]];",
        "chest<T>(T* p) __attribute__ /* immediate */ ((used));",
        "chest<T>(T* p) __declspec(note);",
        "chest<T>(T* p) = default;",
        "chest<T>(T* p) = /* word */ delete /* end */;",
        "chest<T>(T* p) noexcept(check(p)) {}",
        "chest<T>(T* p) : saved(p), callback([](){}) {}",
        "chest<T>(T* p) try : saved(p) {} catch (...) {}",
    };
    for (auto head : heads) {
        const auto source = "template<class T> struct chest { " + std::string(head) + " };";
        SCOPED_TRACE(source);
        PaddedInput input(source);
        const auto tree = parser::Parse(input.View(), [] (parser::ParseError) {});
        size_t constructors = 0;
        std::vector<const Node*> pending{&tree.Root()};
        while (!pending.empty()) {
            const auto* node = pending.back();
            pending.pop_back();
            constructors += node->GetFunctionKind() == parser::FunctionKind::Constructor;
            for (auto* child = node->FirstChild; child; child = child->NextSibling) {
                pending.push_back(child);
            }
        }
        EXPECT_EQ(constructors, 1u);
    }
}

TEST(Parser, TerminalConstructorBoundariesResetAndRequireValidCachedEvidence)
{
    const std::string_view sources[] = {
        "template<class T> struct Owner { Owner<T> (make)(); " "Owner<T>(T* p) {} };",
        "template<class T> struct Owner { Owner<T>(T* p) = opaque(); " "Owner<T>(T* p) {} };",
        "template<class T> struct Owner { Owner<T>(T* p) requires true; " "Owner<T>(T* p) {} };",
        "template<class T> struct Owner { Owner<T>(T* p) {} " "Owner<T> (make)(); };",
        "template<class T> struct Owner { Owner<T>(T* p) try = default; "
            "Owner<T>(T* p) try : saved(p) {} catch (...) {} };",
        "template<class T> struct Owner { " "Owner<T>(T* p) try /* edge */ = /* word */ delete; "
            "Owner<T>(T* p) {} };",
    };
    for (auto source : sources) {
        SCOPED_TRACE(source);
        PaddedInput input(source);
        const auto tree = parser::Parse(input.View(), [] (parser::ParseError) {});
        size_t constructors = 0;
        std::vector<const Node*> pending{&tree.Root()};
        while (!pending.empty()) {
            const auto* node = pending.back();
            pending.pop_back();
            constructors += node->GetFunctionKind() == parser::FunctionKind::Constructor;
            for (auto* child = node->FirstChild; child; child = child->NextSibling) {
                pending.push_back(child);
            }
        }
        EXPECT_EQ(constructors, 1u);
    }
    // Outer cached parentheses can survive malformed interiors. Global map
    // validity, not endpoints alone, must govern terminal-ID promotion.
    for (auto source :
        {
            "template<class T> struct Owner { " "Owner<T>(T* p) noexcept(check([))) {} };",
            "template<class T> struct Owner { " "Owner<T>(T* p) {} int broken[); };",
        })
    {
        SCOPED_TRACE(source);
        PaddedInput input(source);
        size_t errors = 0;
        const auto tree = parser::Parse(input.View(), [&] (parser::ParseError) { ++errors; });
        EXPECT_GT(errors, 0u);
        std::vector<const Node*> pending{&tree.Root()};
        while (!pending.empty()) {
            const auto* node = pending.back();
            pending.pop_back();
            EXPECT_NE(node->GetFunctionKind(), parser::FunctionKind::Constructor);
            for (auto* child = node->FirstChild; child; child = child->NextSibling) {
                pending.push_back(child);
            }
        }
    }
    EXPECT_EQ(sizeof(parser::Node), 40u);
}
