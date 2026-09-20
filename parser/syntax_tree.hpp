#pragma once

#include <tokenizer/token.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <memory_resource>
#include <string_view>
#include <type_traits>

namespace parser {

using TokenIndex = uint32_t;
using SourceOffset = util::SourceOffset;

// The tree is deliberately lossless: a node refers to a half-open range in the
// token pool and every source token belongs to the translation unit.  This is
// useful to format code without having to reconstruct spelling information.
struct TokenRange
{
    TokenIndex Begin = 0;
    TokenIndex End = 0;

    [[nodiscard]] constexpr bool Empty() const
    {
        return Begin == End;
    }

    [[nodiscard]] constexpr TokenIndex Size() const
    {
        return End - Begin;
    }
};

#define SYNTAX_NODE_KINDS                                                      \
    SYNTAX_NODE_KIND(TranslationUnit, "translation-unit")                      \
    SYNTAX_NODE_KIND(PreprocessingConditional, "preprocessing-conditional")    \
    SYNTAX_NODE_KIND(PreprocessingBranch, "preprocessing-branch")              \
    SYNTAX_NODE_KIND(PreprocessingDirective, "preprocessing-directive")        \
    SYNTAX_NODE_KIND(Comment, "comment")                                       \
    SYNTAX_NODE_KIND(EmptyDeclaration, "empty-declaration")                    \
    SYNTAX_NODE_KIND(Declaration, "declaration")                               \
    SYNTAX_NODE_KIND(FunctionDeclaration, "function-declaration")              \
    SYNTAX_NODE_KIND(FunctionDefinition, "function-definition")                \
    SYNTAX_NODE_KIND(FunctionTryBlock, "function-try-block")                   \
    SYNTAX_NODE_KIND(NamespaceDeclaration, "namespace-declaration")            \
    SYNTAX_NODE_KIND(RecordDeclaration, "record-declaration")                  \
    SYNTAX_NODE_KIND(EnumDeclaration, "enum-declaration")                      \
    SYNTAX_NODE_KIND(TemplateDeclaration, "template-declaration")              \
    SYNTAX_NODE_KIND(UsingDeclaration, "using-declaration")                    \
    SYNTAX_NODE_KIND(TypedefDeclaration, "typedef-declaration")                \
    SYNTAX_NODE_KIND(StaticAssertDeclaration, "static-assert-declaration")     \
    SYNTAX_NODE_KIND(ConceptDeclaration, "concept-declaration")                \
    SYNTAX_NODE_KIND(ConceptName, "concept-name")                              \
    SYNTAX_NODE_KIND(ConstraintExpression, "constraint-expression")            \
    SYNTAX_NODE_KIND(ModuleDeclaration, "module-declaration")                  \
    SYNTAX_NODE_KIND(ModuleName, "module-name")                                \
    SYNTAX_NODE_KIND(ModulePartition, "module-partition")                      \
    SYNTAX_NODE_KIND(ImportDeclaration, "import-declaration")                  \
    SYNTAX_NODE_KIND(ImportName, "import-name")                                \
    SYNTAX_NODE_KIND(HeaderName, "header-name")                                \
    SYNTAX_NODE_KIND(ExportDeclaration, "export-declaration")                  \
    SYNTAX_NODE_KIND(AsmDeclaration, "asm-declaration")                        \
    SYNTAX_NODE_KIND(MacroInvocation, "macro-invocation")                      \
    SYNTAX_NODE_KIND(MacroBlock, "macro-block")                                \
    SYNTAX_NODE_KIND(MacroDeclarationBody, "macro-declaration-body")           \
    SYNTAX_NODE_KIND(MacroControlStatement, "macro-control-statement")         \
    SYNTAX_NODE_KIND(MacroSpecifier, "macro-specifier")                        \
    SYNTAX_NODE_KIND(LinkageSpecification, "linkage-specification")            \
    SYNTAX_NODE_KIND(AccessSpecifier, "access-specifier")                      \
    SYNTAX_NODE_KIND(RequiresClause, "requires-clause")                        \
    SYNTAX_NODE_KIND(DeclarationSpecifiers, "declaration-specifiers")          \
    SYNTAX_NODE_KIND(InitDeclaratorList, "init-declarator-list")               \
    SYNTAX_NODE_KIND(InitDeclarator, "init-declarator")                        \
    SYNTAX_NODE_KIND(Declarator, "declarator")                                 \
    SYNTAX_NODE_KIND(AbstractDeclarator, "abstract-declarator")                \
    SYNTAX_NODE_KIND(DeclaratorName, "declarator-name")                        \
    SYNTAX_NODE_KIND(DestructorName, "destructor-name")                        \
    SYNTAX_NODE_KIND(OperatorFunctionName, "operator-function-name")           \
    SYNTAX_NODE_KIND(PointerOperator, "pointer-operator")                      \
    SYNTAX_NODE_KIND(MemberPointerOperator, "member-pointer-operator")         \
    SYNTAX_NODE_KIND(NestedDeclarator, "nested-declarator")                    \
    SYNTAX_NODE_KIND(FunctionDeclarator, "function-declarator")                \
    SYNTAX_NODE_KIND(FunctionQualifier, "function-qualifier")                  \
    SYNTAX_NODE_KIND(ExceptionSpecification, "exception-specification")        \
    SYNTAX_NODE_KIND(TrailingReturnType, "trailing-return-type")               \
    SYNTAX_NODE_KIND(                                                          \
        ConstructorInitializerList, "constructor-initializer-list")            \
    SYNTAX_NODE_KIND(ConstructorInitializer, "constructor-initializer")        \
    SYNTAX_NODE_KIND(AttributeSpecifier, "attribute-specifier")                \
    SYNTAX_NODE_KIND(                                                          \
        ExtensionAttributeSpecifier, "extension-attribute-specifier")          \
    SYNTAX_NODE_KIND(                                                          \
        CallingConventionSpecifier, "calling-convention-specifier")            \
    SYNTAX_NODE_KIND(ArrayDeclarator, "array-declarator")                      \
    SYNTAX_NODE_KIND(StructuredBinding, "structured-binding")                  \
    SYNTAX_NODE_KIND(BitField, "bit-field")                                    \
    SYNTAX_NODE_KIND(Initializer, "initializer")                               \
    SYNTAX_NODE_KIND(CompoundStatement, "compound-statement")                  \
    SYNTAX_NODE_KIND(DeclarationStatement, "declaration-statement")            \
    SYNTAX_NODE_KIND(ExpressionStatement, "expression-statement")              \
    SYNTAX_NODE_KIND(Expression, "expression")                                 \
    SYNTAX_NODE_KIND(IdentifierExpression, "identifier-expression")            \
    SYNTAX_NODE_KIND(LiteralExpression, "literal-expression")                  \
    SYNTAX_NODE_KIND(UnaryExpression, "unary-expression")                      \
    SYNTAX_NODE_KIND(CastExpression, "cast-expression")                        \
    SYNTAX_NODE_KIND(TypeId, "type-id")                                        \
    SYNTAX_NODE_KIND(FoldExpression, "fold-expression")                        \
    SYNTAX_NODE_KIND(FoldOperand, "fold-operand")                              \
    SYNTAX_NODE_KIND(FoldOperator, "fold-operator")                            \
    SYNTAX_NODE_KIND(FoldEllipsis, "fold-ellipsis")                            \
    SYNTAX_NODE_KIND(RequiresExpression, "requires-expression")                \
    SYNTAX_NODE_KIND(RequirementBody, "requirement-body")                      \
    SYNTAX_NODE_KIND(Requirement, "requirement")                               \
    SYNTAX_NODE_KIND(NewExpression, "new-expression")                          \
    SYNTAX_NODE_KIND(NewPlacement, "new-placement")                            \
    SYNTAX_NODE_KIND(NewTypeId, "new-type-id")                                 \
    SYNTAX_NODE_KIND(NewInitializer, "new-initializer")                        \
    SYNTAX_NODE_KIND(DeleteExpression, "delete-expression")                    \
    SYNTAX_NODE_KIND(DeleteArrayMarker, "delete-array-marker")                 \
    SYNTAX_NODE_KIND(BinaryExpression, "binary-expression")                    \
    SYNTAX_NODE_KIND(AssignmentExpression, "assignment-expression")            \
    SYNTAX_NODE_KIND(ConditionalExpression, "conditional-expression")          \
    SYNTAX_NODE_KIND(CallExpression, "call-expression")                        \
    SYNTAX_NODE_KIND(SubscriptExpression, "subscript-expression")              \
    SYNTAX_NODE_KIND(MemberExpression, "member-expression")                    \
    SYNTAX_NODE_KIND(TemplateIdExpression, "template-id-expression")           \
    SYNTAX_NODE_KIND(ParenthesizedExpression, "parenthesized-expression")      \
    SYNTAX_NODE_KIND(StatementExpression, "statement-expression")              \
    SYNTAX_NODE_KIND(BracedInitializer, "braced-initializer")                  \
    SYNTAX_NODE_KIND(LambdaExpression, "lambda-expression")                    \
    SYNTAX_NODE_KIND(LambdaCaptureList, "lambda-capture-list")                 \
    SYNTAX_NODE_KIND(TemplateParameterList, "template-parameter-list")         \
    SYNTAX_NODE_KIND(TemplateParameter, "template-parameter")                  \
    SYNTAX_NODE_KIND(LambdaBody, "lambda-body")                                \
    SYNTAX_NODE_KIND(AmbiguousStatement, "ambiguous-statement")                \
    SYNTAX_NODE_KIND(UnknownTypeName, "unknown-type-name")                     \
    SYNTAX_NODE_KIND(UnparsedSyntax, "unparsed-syntax")                        \
    SYNTAX_NODE_KIND(MissingToken, "missing-token")                            \
    SYNTAX_NODE_KIND(UnexpectedToken, "unexpected-token")                      \
    SYNTAX_NODE_KIND(IfStatement, "if-statement")                              \
    SYNTAX_NODE_KIND(SwitchStatement, "switch-statement")                      \
    SYNTAX_NODE_KIND(WhileStatement, "while-statement")                        \
    SYNTAX_NODE_KIND(DoWhileStatement, "do-while-statement")                   \
    SYNTAX_NODE_KIND(ForStatement, "for-statement")                            \
    SYNTAX_NODE_KIND(TryStatement, "try-statement")                            \
    SYNTAX_NODE_KIND(CatchClause, "catch-clause")                              \
    SYNTAX_NODE_KIND(ReturnStatement, "return-statement")                      \
    SYNTAX_NODE_KIND(BreakStatement, "break-statement")                        \
    SYNTAX_NODE_KIND(ContinueStatement, "continue-statement")                  \
    SYNTAX_NODE_KIND(GotoStatement, "goto-statement")                          \
    SYNTAX_NODE_KIND(ThrowStatement, "throw-statement")                        \
    SYNTAX_NODE_KIND(CoYieldStatement, "co-yield-statement")                   \
    SYNTAX_NODE_KIND(CaseLabel, "case-label")                                  \
    SYNTAX_NODE_KIND(LabelStatement, "label-statement")                        \
    SYNTAX_NODE_KIND(ParameterList, "parameter-list")                          \
    SYNTAX_NODE_KIND(ParameterDeclaration, "parameter-declaration")            \
    SYNTAX_NODE_KIND(ArgumentList, "argument-list")                            \
    SYNTAX_NODE_KIND(Argument, "argument")                                     \
    SYNTAX_NODE_KIND(TemplateArgumentList, "template-argument-list")           \
    SYNTAX_NODE_KIND(TemplateArgument, "template-argument")                    \
    SYNTAX_NODE_KIND(BaseClause, "base-clause")                                \
    SYNTAX_NODE_KIND(BaseSpecifier, "base-specifier")                          \
    SYNTAX_NODE_KIND(BaseType, "base-type")                                    \
    SYNTAX_NODE_KIND(EnumeratorList, "enumerator-list")                        \
    SYNTAX_NODE_KIND(Enumerator, "enumerator")                                 \
    SYNTAX_NODE_KIND(EnumeratorName, "enumerator-name")                        \
    SYNTAX_NODE_KIND(EnumeratorValue, "enumerator-value")                      \
    SYNTAX_NODE_KIND(InitializerList, "initializer-list")                      \
    SYNTAX_NODE_KIND(InitializerElement, "initializer-element")                \
    SYNTAX_NODE_KIND(LambdaCapture, "lambda-capture")                          \
    SYNTAX_NODE_KIND(ListSeparator, "list-separator")                          \
    SYNTAX_NODE_KIND(Condition, "condition")                                   \
    SYNTAX_NODE_KIND(ForHeader, "for-header")                                  \
    SYNTAX_NODE_KIND(ForInitStatement, "for-init-statement")                   \
    SYNTAX_NODE_KIND(ForCondition, "for-condition")                            \
    SYNTAX_NODE_KIND(ForIteration, "for-iteration")                            \
    SYNTAX_NODE_KIND(RangeForDeclaration, "range-for-declaration")             \
    SYNTAX_NODE_KIND(RangeForExpression, "range-for-expression")               \
    SYNTAX_NODE_KIND(RangeForSeparator, "range-for-separator")                 \
    SYNTAX_NODE_KIND(Error, "error")

enum class NodeKind : uint8_t
{
#define SYNTAX_NODE_KIND(value, name) value,
    SYNTAX_NODE_KINDS
#undef SYNTAX_NODE_KIND
};

static_assert(static_cast<size_t>(NodeKind::Error) < 256);

[[nodiscard]] constexpr std::string_view GetNodeKindName(NodeKind kind)
{
    constexpr std::string_view Names[] = {
#define SYNTAX_NODE_KIND(value, name) name,
        SYNTAX_NODE_KINDS
#undef SYNTAX_NODE_KIND
    };
    return Names[static_cast<size_t>(kind)];
}

struct SourceRange
{
    SourceOffset Begin = 0;
    SourceOffset End = 0;

    [[nodiscard]] constexpr bool Empty() const
    {
        return Begin == End;
    }
};

enum class PunctuationRole : uint8_t
{
    None,
    Generic,
    PointerDeclarator,
    ReferenceDeclarator,
    UnaryOperator,
    BinaryOperator,
    AssignmentOperator,
    TemplateDelimiter,
    ComparisonOperator,
    ShiftOperator,
    LabelDelimiter,
    BaseDelimiter,
    BitFieldDelimiter,
    ConditionalDelimiter,
    ConstructorInitializerDelimiter,
    BlockDelimiter,
    InitializerDelimiter,
    LambdaDelimiter,
    RecordDelimiter,
    CallDelimiter,
    DeclarationDelimiter,
    CastDelimiter,
    ConditionDelimiter,
    GroupingDelimiter,
    ListSeparator,
};

enum class TriviaKind
{
    Comment,
    Whitespace,
};

struct Trivia
{
    TriviaKind Kind = TriviaKind::Comment;
    TokenRange Tokens;
    SourceRange Source;
    Trivia* Next = nullptr;
};

struct TriviaList
{
    Trivia* First = nullptr;
};

struct TriviaAttachments
{
    TriviaList Leading;
    TriviaList Trailing;
    TriviaList Dangling;
};

static_assert(sizeof(TriviaAttachments) == 24);

// Nodes and sibling links are allocated in the syntax tree arena and remain
// stable for the lifetime of SyntaxTree. They must not be deleted individually.
struct Node
{
    Node* FirstChild = nullptr;
    Node* LastChild = nullptr;
    Node* NextSibling = nullptr;
    TokenRange Tokens;
    NodeKind Kind = NodeKind::Error;

    [[nodiscard]] const Node* Child() const
    {
        return FirstChild;
    }

private:
    // Sparse trivia metadata is resolved through SyntaxTree so nodes do not
    // each carry another pointer.
    uint32_t TriviaIndex_ = std::numeric_limits<uint32_t>::max();

    friend class SyntaxTree;
    friend class ParseContext;
};

static_assert(sizeof(Node) == 40);
static_assert(std::is_trivially_destructible_v<Node>);

} // namespace parser

#include "syntax_tree-detail.hpp"

namespace parser {

class ParseContext;

// SyntaxTree owns its monotonic memory resource and segmented token pool.
// Token spelling is a view into the input passed to Parse(), so that input must
// outlive this object.
class SyntaxTree
{
public:
    SyntaxTree(SyntaxTree&& other) noexcept;
    SyntaxTree& operator=(SyntaxTree&& other) noexcept;
    ~SyntaxTree();

    SyntaxTree(const SyntaxTree&) = delete;
    SyntaxTree& operator=(const SyntaxTree&) = delete;

    [[nodiscard]] const Node& Root() const;
    [[nodiscard]] std::string_view Source() const;
    [[nodiscard]] TokenIndex TokenCount() const;
    [[nodiscard]] tokenizer::Token Token(TokenIndex index) const;
    [[nodiscard]] tokenizer::TokenKind Kind(TokenIndex index) const;
    [[nodiscard]] util::Position TokenPosition(TokenIndex index) const;
    [[nodiscard]] std::string_view TokenText(TokenIndex index) const;
    [[nodiscard]] PunctuationRole Role(TokenIndex index) const;
    [[nodiscard]] const TriviaAttachments*
    AssociatedTrivia(const Node& node) const;
    [[nodiscard]] std::string_view SourceText(TokenRange range) const;
    [[nodiscard]] std::string_view SourceText(SourceRange range) const;
    [[nodiscard]] std::string_view WhitespaceBefore(TokenIndex token) const;
    [[nodiscard]] std::string_view WhitespaceAfter(TokenIndex token) const;

private:
    explicit SyntaxTree(std::string_view input);

    std::unique_ptr<std::pmr::monotonic_buffer_resource> Memory_;
    std::string_view Input_;
    detail::TokenPool Tokens_;
    detail::TriviaPool Trivia_;
    Node* Root_ = nullptr;

    friend class ParseContext;
};

} // namespace parser
