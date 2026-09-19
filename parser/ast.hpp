#pragma once

#include <tokenizer/token.hpp>

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <new>
#include <string_view>
#include <utility>

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

#define AST_NODE_KINDS                                                         \
    AST_NODE_KIND(TranslationUnit, "translation-unit")                         \
    AST_NODE_KIND(PreprocessingConditional, "preprocessing-conditional")       \
    AST_NODE_KIND(PreprocessingBranch, "preprocessing-branch")                 \
    AST_NODE_KIND(PreprocessingDirective, "preprocessing-directive")           \
    AST_NODE_KIND(Comment, "comment")                                          \
    AST_NODE_KIND(EmptyDeclaration, "empty-declaration")                       \
    AST_NODE_KIND(Declaration, "declaration")                                  \
    AST_NODE_KIND(FunctionDeclaration, "function-declaration")                 \
    AST_NODE_KIND(FunctionDefinition, "function-definition")                   \
    AST_NODE_KIND(FunctionTryBlock, "function-try-block")                      \
    AST_NODE_KIND(NamespaceDeclaration, "namespace-declaration")               \
    AST_NODE_KIND(RecordDeclaration, "record-declaration")                     \
    AST_NODE_KIND(EnumDeclaration, "enum-declaration")                         \
    AST_NODE_KIND(TemplateDeclaration, "template-declaration")                 \
    AST_NODE_KIND(UsingDeclaration, "using-declaration")                       \
    AST_NODE_KIND(TypedefDeclaration, "typedef-declaration")                   \
    AST_NODE_KIND(StaticAssertDeclaration, "static-assert-declaration")        \
    AST_NODE_KIND(ConceptDeclaration, "concept-declaration")                   \
    AST_NODE_KIND(ConceptName, "concept-name")                                 \
    AST_NODE_KIND(ConstraintExpression, "constraint-expression")               \
    AST_NODE_KIND(ModuleDeclaration, "module-declaration")                     \
    AST_NODE_KIND(ModuleName, "module-name")                                   \
    AST_NODE_KIND(ModulePartition, "module-partition")                         \
    AST_NODE_KIND(ImportDeclaration, "import-declaration")                     \
    AST_NODE_KIND(ImportName, "import-name")                                   \
    AST_NODE_KIND(HeaderName, "header-name")                                   \
    AST_NODE_KIND(ExportDeclaration, "export-declaration")                     \
    AST_NODE_KIND(AsmDeclaration, "asm-declaration")                           \
    AST_NODE_KIND(MacroInvocation, "macro-invocation")                         \
    AST_NODE_KIND(MacroBlock, "macro-block")                                   \
    AST_NODE_KIND(MacroDeclarationBody, "macro-declaration-body")              \
    AST_NODE_KIND(MacroControlStatement, "macro-control-statement")            \
    AST_NODE_KIND(MacroSpecifier, "macro-specifier")                           \
    AST_NODE_KIND(LinkageSpecification, "linkage-specification")               \
    AST_NODE_KIND(AccessSpecifier, "access-specifier")                         \
    AST_NODE_KIND(RequiresClause, "requires-clause")                           \
    AST_NODE_KIND(DeclarationSpecifiers, "declaration-specifiers")             \
    AST_NODE_KIND(InitDeclaratorList, "init-declarator-list")                  \
    AST_NODE_KIND(InitDeclarator, "init-declarator")                           \
    AST_NODE_KIND(Declarator, "declarator")                                    \
    AST_NODE_KIND(AbstractDeclarator, "abstract-declarator")                   \
    AST_NODE_KIND(DeclaratorName, "declarator-name")                           \
    AST_NODE_KIND(DestructorName, "destructor-name")                           \
    AST_NODE_KIND(OperatorFunctionName, "operator-function-name")              \
    AST_NODE_KIND(PointerOperator, "pointer-operator")                         \
    AST_NODE_KIND(MemberPointerOperator, "member-pointer-operator")            \
    AST_NODE_KIND(NestedDeclarator, "nested-declarator")                       \
    AST_NODE_KIND(FunctionDeclarator, "function-declarator")                   \
    AST_NODE_KIND(FunctionQualifier, "function-qualifier")                     \
    AST_NODE_KIND(ExceptionSpecification, "exception-specification")           \
    AST_NODE_KIND(TrailingReturnType, "trailing-return-type")                  \
    AST_NODE_KIND(ConstructorInitializerList, "constructor-initializer-list")  \
    AST_NODE_KIND(ConstructorInitializer, "constructor-initializer")           \
    AST_NODE_KIND(AttributeSpecifier, "attribute-specifier")                   \
    AST_NODE_KIND(                                                             \
        ExtensionAttributeSpecifier, "extension-attribute-specifier")          \
    AST_NODE_KIND(CallingConventionSpecifier, "calling-convention-specifier")  \
    AST_NODE_KIND(ArrayDeclarator, "array-declarator")                         \
    AST_NODE_KIND(StructuredBinding, "structured-binding")                     \
    AST_NODE_KIND(BitField, "bit-field")                                       \
    AST_NODE_KIND(Initializer, "initializer")                                  \
    AST_NODE_KIND(CompoundStatement, "compound-statement")                     \
    AST_NODE_KIND(DeclarationStatement, "declaration-statement")               \
    AST_NODE_KIND(ExpressionStatement, "expression-statement")                 \
    AST_NODE_KIND(Expression, "expression")                                    \
    AST_NODE_KIND(IdentifierExpression, "identifier-expression")               \
    AST_NODE_KIND(LiteralExpression, "literal-expression")                     \
    AST_NODE_KIND(UnaryExpression, "unary-expression")                         \
    AST_NODE_KIND(CastExpression, "cast-expression")                           \
    AST_NODE_KIND(TypeId, "type-id")                                           \
    AST_NODE_KIND(FoldExpression, "fold-expression")                           \
    AST_NODE_KIND(FoldOperand, "fold-operand")                                 \
    AST_NODE_KIND(FoldOperator, "fold-operator")                               \
    AST_NODE_KIND(FoldEllipsis, "fold-ellipsis")                               \
    AST_NODE_KIND(RequiresExpression, "requires-expression")                   \
    AST_NODE_KIND(RequirementBody, "requirement-body")                         \
    AST_NODE_KIND(Requirement, "requirement")                                  \
    AST_NODE_KIND(NewExpression, "new-expression")                             \
    AST_NODE_KIND(NewPlacement, "new-placement")                               \
    AST_NODE_KIND(NewTypeId, "new-type-id")                                    \
    AST_NODE_KIND(NewInitializer, "new-initializer")                           \
    AST_NODE_KIND(DeleteExpression, "delete-expression")                       \
    AST_NODE_KIND(DeleteArrayMarker, "delete-array-marker")                    \
    AST_NODE_KIND(BinaryExpression, "binary-expression")                       \
    AST_NODE_KIND(AssignmentExpression, "assignment-expression")               \
    AST_NODE_KIND(ConditionalExpression, "conditional-expression")             \
    AST_NODE_KIND(CallExpression, "call-expression")                           \
    AST_NODE_KIND(SubscriptExpression, "subscript-expression")                 \
    AST_NODE_KIND(MemberExpression, "member-expression")                       \
    AST_NODE_KIND(TemplateIdExpression, "template-id-expression")              \
    AST_NODE_KIND(ParenthesizedExpression, "parenthesized-expression")         \
    AST_NODE_KIND(StatementExpression, "statement-expression")                 \
    AST_NODE_KIND(BracedInitializer, "braced-initializer")                     \
    AST_NODE_KIND(LambdaExpression, "lambda-expression")                       \
    AST_NODE_KIND(LambdaCaptureList, "lambda-capture-list")                    \
    AST_NODE_KIND(TemplateParameterList, "template-parameter-list")            \
    AST_NODE_KIND(TemplateParameter, "template-parameter")                     \
    AST_NODE_KIND(LambdaBody, "lambda-body")                                   \
    AST_NODE_KIND(AmbiguousStatement, "ambiguous-statement")                   \
    AST_NODE_KIND(UnknownTypeName, "unknown-type-name")                        \
    AST_NODE_KIND(UnparsedSyntax, "unparsed-syntax")                           \
    AST_NODE_KIND(MissingToken, "missing-token")                               \
    AST_NODE_KIND(UnexpectedToken, "unexpected-token")                         \
    AST_NODE_KIND(IfStatement, "if-statement")                                 \
    AST_NODE_KIND(SwitchStatement, "switch-statement")                         \
    AST_NODE_KIND(WhileStatement, "while-statement")                           \
    AST_NODE_KIND(DoWhileStatement, "do-while-statement")                      \
    AST_NODE_KIND(ForStatement, "for-statement")                               \
    AST_NODE_KIND(TryStatement, "try-statement")                               \
    AST_NODE_KIND(CatchClause, "catch-clause")                                 \
    AST_NODE_KIND(ReturnStatement, "return-statement")                         \
    AST_NODE_KIND(BreakStatement, "break-statement")                           \
    AST_NODE_KIND(ContinueStatement, "continue-statement")                     \
    AST_NODE_KIND(GotoStatement, "goto-statement")                             \
    AST_NODE_KIND(ThrowStatement, "throw-statement")                           \
    AST_NODE_KIND(CoYieldStatement, "co-yield-statement")                      \
    AST_NODE_KIND(CaseLabel, "case-label")                                     \
    AST_NODE_KIND(LabelStatement, "label-statement")                           \
    AST_NODE_KIND(ParameterList, "parameter-list")                             \
    AST_NODE_KIND(ParameterDeclaration, "parameter-declaration")               \
    AST_NODE_KIND(ArgumentList, "argument-list")                               \
    AST_NODE_KIND(Argument, "argument")                                        \
    AST_NODE_KIND(TemplateArgumentList, "template-argument-list")              \
    AST_NODE_KIND(TemplateArgument, "template-argument")                       \
    AST_NODE_KIND(BaseClause, "base-clause")                                   \
    AST_NODE_KIND(BaseSpecifier, "base-specifier")                             \
    AST_NODE_KIND(BaseType, "base-type")                                       \
    AST_NODE_KIND(EnumeratorList, "enumerator-list")                           \
    AST_NODE_KIND(Enumerator, "enumerator")                                    \
    AST_NODE_KIND(EnumeratorName, "enumerator-name")                           \
    AST_NODE_KIND(EnumeratorValue, "enumerator-value")                         \
    AST_NODE_KIND(InitializerList, "initializer-list")                         \
    AST_NODE_KIND(InitializerElement, "initializer-element")                   \
    AST_NODE_KIND(LambdaCapture, "lambda-capture")                             \
    AST_NODE_KIND(ListSeparator, "list-separator")                             \
    AST_NODE_KIND(Condition, "condition")                                      \
    AST_NODE_KIND(ForHeader, "for-header")                                     \
    AST_NODE_KIND(ForInitStatement, "for-init-statement")                      \
    AST_NODE_KIND(ForCondition, "for-condition")                               \
    AST_NODE_KIND(ForIteration, "for-iteration")                               \
    AST_NODE_KIND(RangeForDeclaration, "range-for-declaration")                \
    AST_NODE_KIND(RangeForExpression, "range-for-expression")                  \
    AST_NODE_KIND(RangeForSeparator, "range-for-separator")                    \
    AST_NODE_KIND(Error, "error")

enum class NodeKind
{
#define AST_NODE_KIND(value, name) value,
    AST_NODE_KINDS
#undef AST_NODE_KIND
};

[[nodiscard]] constexpr std::string_view GetNodeKindName(NodeKind kind)
{
    constexpr std::string_view Names[] = {
#define AST_NODE_KIND(value, name) name,
        AST_NODE_KINDS
#undef AST_NODE_KIND
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
    Trivia* Last = nullptr;
};

struct TriviaAttachments
{
    TriviaList Leading;
    TriviaList Trailing;
    TriviaList Dangling;
};

// Nodes and sibling links are allocated in the AST arena and remain stable for
// the lifetime of Ast.  They must not be deleted individually.
struct Node
{
    NodeKind Kind = NodeKind::Error;
    TokenRange Tokens;

    Node* Parent = nullptr;
    Node* FirstChild = nullptr;
    Node* LastChild = nullptr;
    Node* NextSibling = nullptr;
    TriviaAttachments* AssociatedTrivia = nullptr;

    [[nodiscard]] const Node* Child() const
    {
        return FirstChild;
    }
};

namespace detail {

class Arena
{
public:
    Arena() = default;
    Arena(Arena&& other) noexcept;
    Arena& operator=(Arena&& other) noexcept;
    ~Arena();

    Arena(const Arena&) = delete;
    Arena& operator=(const Arena&) = delete;

    void* Allocate(size_t size, size_t alignment);

    template<class T, class... Args>
    T* Make(Args&&... args)
    {
        return new (Allocate(sizeof(T), alignof(T)))
            T(std::forward<Args>(args)...);
    }

    template<class T>
    T* MakeDefaultInitialized()
    {
        return ::new (Allocate(sizeof(T), alignof(T))) T;
    }

private:
    struct Block
    {
        Block* Next;
        size_t Used;
        size_t Capacity;

        [[nodiscard]] std::byte* Data();
    };

    [[nodiscard]] static bool
    Fits(const Block& block, size_t size, size_t alignment);

    void AddBlock(size_t minimumCapacity);
    void Reset();

    Block* Head_ = nullptr;
    size_t NextBlockSize_ = 64 * 1024;
};

struct StoredToken
{
    util::SourceView Text;
    uint32_t Line;
};

static_assert(sizeof(StoredToken) == 12);

class TokenPool
{
public:
    explicit TokenPool(Arena& arena);
    TokenPool(TokenPool&& other, Arena& arena) noexcept;

    TokenPool(const TokenPool&) = delete;
    TokenPool& operator=(const TokenPool&) = delete;

    void MoveFrom(TokenPool&& other, Arena& arena) noexcept;

    [[nodiscard]] uint32_t* Add(tokenizer::Token token)
    {
        if (!Last_ || Last_->Size == TokensPerChunk) {
            auto* chunk = Arena_->MakeDefaultInitialized<Chunk>();
            if (Last_) {
                Last_->Next = chunk;
            } else {
                First_ = chunk;
            }
            Last_ = chunk;
            ++ChunkCount_;
        }
        Last_->HasPreprocessingDirective |=
            token.Kind == tokenizer::TokenKind::PreprocessingDirective;
        Last_->HasComment |= token.Kind == tokenizer::TokenKind::Comment;
        auto offset = Last_->Size++;
        Last_->Tokens[offset] = {
            .Text = token.Text,
            .Line = token.Position.Line,
        };
        Last_->Kinds[offset] = token.Kind;
        Last_->Roles[offset] =
            token.Kind >= tokenizer::TokenKind::Operator_OpenBrace
                && token.Kind <= tokenizer::TokenKind::Operator_Comma
            ? PunctuationRole::Generic
            : PunctuationRole::None;
        Last_->DelimiterEnds[offset] = 0;
        ++Size_;
        return &Last_->DelimiterEnds[offset];
    }

    void Seal();

    [[nodiscard]] const StoredToken& operator[](TokenIndex index) const
    {
        assert(index < Size_);
        return Chunks_[index / TokensPerChunk]->Tokens[index % TokensPerChunk];
    }

    [[nodiscard]] TokenIndex Size() const
    {
        return Size_;
    }

    [[nodiscard]] tokenizer::TokenKind Kind(TokenIndex index) const
    {
        assert(index < Size_);
        return Chunks_[index / TokensPerChunk]->Kinds[index % TokensPerChunk];
    }

    [[nodiscard]] PunctuationRole Role(TokenIndex index) const
    {
        assert(index < Size_);
        return Chunks_[index / TokensPerChunk]->Roles[index % TokensPerChunk];
    }

    void SetRole(TokenIndex index, PunctuationRole role)
    {
        assert(index < Size_);
        Chunks_[index / TokensPerChunk]->Roles[index % TokensPerChunk] = role;
    }

    [[nodiscard]] uint32_t DelimiterEnd(TokenIndex index) const
    {
        assert(index < Size_);
        return Chunks_[index / TokensPerChunk]
            ->DelimiterEnds[index % TokensPerChunk];
    }

    [[nodiscard]] TokenIndex
    FindPreprocessingDirective(TokenIndex begin, TokenIndex end) const;
    [[nodiscard]] TokenIndex
    FindComment(TokenIndex begin, TokenIndex end) const;

private:
    static constexpr TokenIndex TokensPerChunk = 512;

    struct Chunk
    {
        Chunk* Next = nullptr;
        uint16_t Size = 0;
        bool HasPreprocessingDirective = false;
        bool HasComment = false;
        std::array<StoredToken, TokensPerChunk> Tokens;
        std::array<tokenizer::TokenKind, TokensPerChunk> Kinds;
        std::array<PunctuationRole, TokensPerChunk> Roles;
        std::array<uint32_t, TokensPerChunk> DelimiterEnds;
    };

    Arena* Arena_;
    Chunk* First_ = nullptr;
    Chunk* Last_ = nullptr;
    Chunk** Chunks_ = nullptr;
    uint32_t ChunkCount_ = 0;
    TokenIndex Size_ = 0;
};

} // namespace detail

class ParseContext;

// Ast directly owns its arena and segmented token pool. Token spelling is a
// view into the input passed to Parse(), so that input must outlive this
// object.
class Ast
{
public:
    Ast(Ast&& other) noexcept;
    Ast& operator=(Ast&& other) noexcept;
    ~Ast();

    Ast(const Ast&) = delete;
    Ast& operator=(const Ast&) = delete;

    [[nodiscard]] const Node& Root() const;
    [[nodiscard]] std::string_view Source() const;
    [[nodiscard]] TokenIndex TokenCount() const;
    [[nodiscard]] tokenizer::Token Token(TokenIndex index) const;
    [[nodiscard]] tokenizer::TokenKind Kind(TokenIndex index) const;
    [[nodiscard]] util::Position TokenPosition(TokenIndex index) const;
    [[nodiscard]] std::string_view TokenText(TokenIndex index) const;
    [[nodiscard]] PunctuationRole Role(TokenIndex index) const;
    [[nodiscard]] std::string_view SourceText(TokenRange range) const;
    [[nodiscard]] std::string_view SourceText(SourceRange range) const;
    [[nodiscard]] std::string_view WhitespaceBefore(TokenIndex token) const;
    [[nodiscard]] std::string_view WhitespaceAfter(TokenIndex token) const;

private:
    explicit Ast(std::string_view input);

    detail::Arena Memory_;
    std::string_view Input_;
    detail::TokenPool Tokens_;
    Node* Root_ = nullptr;

    friend class ParseContext;
};

} // namespace parser
