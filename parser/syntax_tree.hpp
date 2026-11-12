#pragma once

#include <util/memory.hpp>
#include <util/types.hpp>

#include <tokenizer/token.hpp>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <memory_resource>
#include <new>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

namespace parser {

using TokenIndex = u32;

using SourceOffset = util::SourceOffset;

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
    SYNTAX_NODE_KIND(PreprocessingBody, "preprocessing-body")                  \
    SYNTAX_NODE_KIND(PreprocessingBranch, "preprocessing-branch")              \
    SYNTAX_NODE_KIND(PreprocessingDirective, "preprocessing-directive")        \
    SYNTAX_NODE_KIND(Comment, "comment")                                       \
    SYNTAX_NODE_KIND(EmptyDeclaration, "empty-declaration")                    \
    SYNTAX_NODE_KIND(Declaration, "declaration")                               \
    SYNTAX_NODE_KIND(FunctionDeclaration, "function-declaration")              \
    SYNTAX_NODE_KIND(FunctionDefinition, "function-definition")                \
    SYNTAX_NODE_KIND(MacroFunctionDefinition, "macro-function-definition")    \
    SYNTAX_NODE_KIND(MacroFunctionHeader, "macro-function-header")            \
    SYNTAX_NODE_KIND(MacroEnumDeclaration, "macro-enum-declaration")          \
    SYNTAX_NODE_KIND(MacroEnumHeader, "macro-enum-header")                    \
    SYNTAX_NODE_KIND(MacroEnumEntry, "macro-enum-entry")                      \
    SYNTAX_NODE_KIND(MacroRecordDeclaration, "macro-record-declaration")      \
    SYNTAX_NODE_KIND(MacroRecordBegin, "macro-record-begin")                  \
    SYNTAX_NODE_KIND(MacroRecordEnd, "macro-record-end")                      \
    SYNTAX_NODE_KIND(FunctionTryBlock, "function-try-block")                   \
    SYNTAX_NODE_KIND(NamespaceDeclaration, "namespace-declaration")            \
    SYNTAX_NODE_KIND(RecordDeclaration, "record-declaration")                  \
    SYNTAX_NODE_KIND(ClassVirtSpecifier, "class-virt-specifier")               \
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
    /* FunctionDeclarator + ParameterList or Initializer; */                  \
    /* neither speculative tree is attached. */                               \
    SYNTAX_NODE_KIND(                                                          \
        AmbiguousDeclaratorSuffix, "ambiguous-declarator-suffix")              \
    SYNTAX_NODE_KIND(AttributedStatement, "attributed-statement")              \
    SYNTAX_NODE_KIND(CompoundStatement, "compound-statement")                  \
    SYNTAX_NODE_KIND(DeclarationStatement, "declaration-statement")            \
    SYNTAX_NODE_KIND(ExpressionStatement, "expression-statement")              \
    SYNTAX_NODE_KIND(Expression, "expression")                                 \
    SYNTAX_NODE_KIND(IdentifierExpression, "identifier-expression")            \
    SYNTAX_NODE_KIND(LiteralExpression, "literal-expression")                  \
    SYNTAX_NODE_KIND(UnaryExpression, "unary-expression")                      \
    SYNTAX_NODE_KIND(CastExpression, "cast-expression")                        \
    SYNTAX_NODE_KIND(                                                          \
        TypeConstructionExpression, "type-construction-expression")            \
    SYNTAX_NODE_KIND(TypeId, "type-id")                                        \
    SYNTAX_NODE_KIND(FoldExpression, "fold-expression")                        \
    SYNTAX_NODE_KIND(FoldOperand, "fold-operand")                              \
    SYNTAX_NODE_KIND(FoldOperator, "fold-operator")                            \
    SYNTAX_NODE_KIND(FoldEllipsis, "fold-ellipsis")                            \
    SYNTAX_NODE_KIND(RequiresExpression, "requires-expression")                \
    SYNTAX_NODE_KIND(RequirementBody, "requirement-body")                      \
    SYNTAX_NODE_KIND(Requirement, "requirement")                               \
    SYNTAX_NODE_KIND(SimpleRequirement, "simple-requirement")                  \
    SYNTAX_NODE_KIND(TypeRequirement, "type-requirement")                      \
    SYNTAX_NODE_KIND(CompoundRequirement, "compound-requirement")              \
    SYNTAX_NODE_KIND(NestedRequirement, "nested-requirement")                  \
    SYNTAX_NODE_KIND(RequirementExpression, "requirement-expression")          \
    SYNTAX_NODE_KIND(TypeConstraint, "type-constraint")                        \
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

enum class NodeKind : u8
{
#define SYNTAX_NODE_KIND(value, name) value,
    SYNTAX_NODE_KINDS
#undef SYNTAX_NODE_KIND
};

static_assert(static_cast<us>(NodeKind::Error) < 256);

[[nodiscard]] constexpr std::string_view GetNodeKindName(NodeKind kind)
{
    constexpr std::string_view Names[] = {
#define SYNTAX_NODE_KIND(value, name) name,
        SYNTAX_NODE_KINDS
#undef SYNTAX_NODE_KIND
    };
    return Names[static_cast<us>(kind)];
}

enum class FunctionKind : u8
{
    Unknown,
    Ordinary,
    Constructor,
    Destructor,
};

struct SourceRange
{
    SourceOffset Begin = 0;
    SourceOffset End = 0;

    [[nodiscard]] constexpr bool Empty() const
    {
        return Begin == End;
    }
};

enum class PunctuationRole : u8
{
    None,
    Generic,
    PointerDeclarator,
    ReferenceDeclarator,
    UnaryOperator,
    BinaryOperator,
    AmbiguousOperator,
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
    AttributeDelimiter,
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

inline constexpr u32 NoTrivia = std::numeric_limits<u32>::max();

struct Node
{
    Node* FirstChild = nullptr;
    Node* LastChild = nullptr;
    Node* NextSibling = nullptr;
    TokenRange Tokens;
    NodeKind Kind = NodeKind::Error;

    // Set for recognized function declarations and definitions; Unknown otherwise.
    FunctionKind Function = FunctionKind::Unknown;

    // Index into SyntaxTree's comments and whitespace table.
    u32 TriviaIndex = NoTrivia;
};

static_assert(sizeof(Node) == 40);

static_assert(std::is_trivially_destructible_v<Node>);

[[nodiscard]] inline const Node* FirstInitDeclarator(const Node& declaration)
{
    for (auto* child = declaration.FirstChild; child; child = child->NextSibling) {
        if (child->Kind == NodeKind::InitDeclarator) {
            return child;
        }

        if (child->Kind == NodeKind::InitDeclaratorList) {
            for (auto* init = child->FirstChild; init; init = init->NextSibling) {
                if (init->Kind == NodeKind::InitDeclarator) {
                    return init;
                }
            }
        }
    }

    return nullptr;
}

namespace detail {

inline constexpr us InitialMemoryResourceSize = 64 * 1024;

template <class T, class... Args>
[[nodiscard]] T* Make(std::pmr::memory_resource& memory, Args&&... args)
{
    static_assert(std::is_trivially_destructible_v<T>);
    return ::new (memory.allocate(sizeof(T), alignof(T)))
        T(std::forward<Args>(args)...);
}

template <class T>
[[nodiscard]] T* MakeDefaultInitialized(std::pmr::memory_resource& memory)
{
    static_assert(std::is_trivially_destructible_v<T>);
    return ::new (memory.allocate(sizeof(T), alignof(T))) T;
}

// Grow an arena-owned chunk directory during construction so indexed access
// remains constant-time both before and after Seal().
template <class Chunk, u16 Capacity>
class SegmentedStorage
{
public:
    struct Slot
    {
        Chunk* ChunkPtr;
        u16 Offset;
        u32 Index;
    };

    explicit SegmentedStorage(std::pmr::memory_resource& memory)
        : Memory_(&memory)
    { }

    SegmentedStorage(SegmentedStorage&& other) noexcept
    {
        MoveFrom(std::move(other));
    }

    SegmentedStorage(const SegmentedStorage& other) = delete;
    SegmentedStorage& operator=(const SegmentedStorage& other) = delete;

    void MoveFrom(SegmentedStorage&& other) noexcept
    {
        Memory_ = std::exchange(other.Memory_, nullptr);
        First_ = std::exchange(other.First_, nullptr);
        Last_ = std::exchange(other.Last_, nullptr);
        Chunks_ = std::exchange(other.Chunks_, nullptr);
        ChunkCount_ = std::exchange(other.ChunkCount_, 0);
        ChunkCapacity_ = std::exchange(other.ChunkCapacity_, 0);
        Size_ = std::exchange(other.Size_, 0);
        Sealed_ = std::exchange(other.Sealed_, false);
    }

    [[nodiscard]] Slot Append()
    {
        assert(!Sealed_ && Memory_);
        assert(Size_ != std::numeric_limits<u32>::max());

        if (!Last_ || Last_->Size == Capacity) {
            if (ChunkCount_ == ChunkCapacity_) {
                constexpr auto maxChunks = std::numeric_limits<u32>::max() / Capacity +
                    (std::numeric_limits<u32>::max() % Capacity != 0);
                const auto capacity = ChunkCapacity_ == 0
                    ? u32(4)
                    : ChunkCapacity_ > maxChunks / 2 ? maxChunks : ChunkCapacity_ * 2;

                if (capacity > std::numeric_limits<us>::max() / sizeof(Chunk*)) {
                    throw std::bad_array_new_length();
                }

                auto** chunks = static_cast<Chunk**>(Memory_->allocate(sizeof(Chunk*) * capacity, alignof(Chunk*)));

                if (ChunkCount_ != 0) {
                    std::memcpy(chunks, Chunks_, sizeof(Chunk*) * ChunkCount_);
                }

                // Superseded directories share the input file's arena lifetime.
                Chunks_ = chunks;
                ChunkCapacity_ = capacity;
            }

            auto* chunk = MakeDefaultInitialized<Chunk>(*Memory_);

            if (Last_) {
                Last_->Next = chunk;
            } else {
                First_ = chunk;
            }

            Last_ = chunk;
            Chunks_[ChunkCount_++] = chunk;
        }

        auto offset = Last_->Size++;
        auto index = Size_++;
        return {.ChunkPtr = Last_, .Offset = offset, .Index = index};
    }

    void Seal()
    {
        assert(!Sealed_ && Memory_);
        Sealed_ = true;
    }

    // Formatter-facing access requires completed tokenization.
    [[nodiscard]] Chunk* ChunkAt(u32 index) const
    {
        assert(Sealed_ && index < ChunkCount_);
        return Chunks_[index];
    }

    [[nodiscard]] Chunk* ResolveChunk(u32 index) const
    {
        assert(index < ChunkCount_);
        return Chunks_[index];
    }

    [[nodiscard]] u32 Size() const
    {
        return Size_;
    }

private:
    static_assert(std::is_trivially_destructible_v<Chunk>);
    static_assert(Capacity != 0);

    std::pmr::memory_resource* Memory_;
    Chunk* First_ = nullptr;
    Chunk* Last_ = nullptr;
    Chunk** Chunks_ = nullptr;
    u32 ChunkCount_ = 0;
    u32 ChunkCapacity_ = 0;
    u32 Size_ = 0;
    bool Sealed_ = false;
};

struct StoredToken
{
    util::SourceView Text;
    u32 Line;
    u32 Column;
};

static_assert(sizeof(StoredToken) == 16);

class TriviaPool
{
    static constexpr u16 AttachmentsPerChunk = 128;

    struct Chunk
    {
        Chunk* Next = nullptr;
        u16 Size = 0;
        std::array<TriviaAttachments, AttachmentsPerChunk> Items;
        std::array<Node*, AttachmentsPerChunk> Owners;
    };

public:
    explicit TriviaPool(std::pmr::memory_resource& memory)
        : Storage_(memory)
    { }

    TriviaPool(TriviaPool&& other) noexcept = default;

    TriviaPool(const TriviaPool& other) = delete;
    TriviaPool& operator=(const TriviaPool& other) = delete;

    void MoveFrom(TriviaPool&& other) noexcept
    {
        Storage_.MoveFrom(std::move(other.Storage_));
    }

    [[nodiscard]] u32 Add(Node& owner)
    {
        auto slot = Storage_.Append();
        slot.ChunkPtr->Items[slot.Offset] = {};
        slot.ChunkPtr->Owners[slot.Offset] = &owner;
        return slot.Index;
    }

    void Seal()
    {
        Storage_.Seal();
    }

    [[nodiscard]] u32 Size() const
    {
        return Storage_.Size();
    }

    [[nodiscard]] Node& Owner(u32 index) const
    {
        assert(index < Storage_.Size());
        return *Storage_.ResolveChunk(index / AttachmentsPerChunk)->Owners[index % AttachmentsPerChunk];
    }

    [[nodiscard]] TriviaAttachments& operator[](u32 index)
    {
        assert(index < Storage_.Size());
        return Storage_.ResolveChunk(index / AttachmentsPerChunk)->Items[index % AttachmentsPerChunk];
    }

    [[nodiscard]] const TriviaAttachments& operator[](u32 index) const
    {
        assert(index < Storage_.Size());
        return Storage_.ResolveChunk(index / AttachmentsPerChunk)->Items[index % AttachmentsPerChunk];
    }

private:
    SegmentedStorage<Chunk, AttachmentsPerChunk> Storage_;
};

class TokenPool
{
    static constexpr u16 TokensPerChunk = 512;

    struct Chunk
    {
        Chunk* Next = nullptr;
        u16 Size = 0;
        bool HasPreprocessingDirective = false;
        bool HasComment = false;
        // Separate kind and role arrays let syntax scans read only the
        // metadata they use, without spelling and source positions.
        std::array<StoredToken, TokensPerChunk> Tokens;
        std::array<tokenizer::TokenKind, TokensPerChunk> Kinds;
        std::array<PunctuationRole, TokensPerChunk> Roles;
        std::array<TokenIndex, TokensPerChunk> MatchingDelimiters;
    };

public:
    explicit TokenPool(std::pmr::memory_resource& memory)
        : Memory_(&memory)
        , Storage_(memory)
    { }

    TokenPool(TokenPool&& other) noexcept = default;

    TokenPool(const TokenPool& other) = delete;
    TokenPool& operator=(const TokenPool& other) = delete;

    void MoveFrom(TokenPool&& other) noexcept
    {
        Memory_ = std::exchange(other.Memory_, nullptr);
        Kinds_ = std::exchange(other.Kinds_, nullptr);
        Storage_.MoveFrom(std::move(other.Storage_));
    }

    void Add(tokenizer::Token token)
    {
        auto slot = Storage_.Append();
        auto* chunk = slot.ChunkPtr;
        chunk->HasPreprocessingDirective |= token.Kind == tokenizer::TokenKind::PreprocessingDirective;
        chunk->HasComment |= token.Kind == tokenizer::TokenKind::Comment;
        chunk->Tokens[slot.Offset] = {.Text = token.Text, .Line = token.Position.Line, .Column = token.Position.Column};
        chunk->Kinds[slot.Offset] = token.Kind;
        chunk->Roles[slot.Offset] = token.Kind >= tokenizer::TokenKind::Operator_OpenBrace &&
            token.Kind <= tokenizer::TokenKind::Operator_Comma
            ? PunctuationRole::Generic
            : PunctuationRole::None;
        chunk->MatchingDelimiters[slot.Offset] = std::numeric_limits<TokenIndex>::max();
    }

    [[nodiscard]] std::span<const tokenizer::TokenKind> KindBlock(TokenIndex begin) const
    {
        assert(begin < Storage_.Size());
        auto* chunk = Storage_.ResolveChunk(begin / TokensPerChunk);
        const auto offset = begin % TokensPerChunk;
        return {chunk->Kinds.data() + offset, chunk->Size - offset};
    }

    [[nodiscard]] std::span<TokenIndex> MatchingBlock(TokenIndex begin)
    {
        assert(begin < Storage_.Size());
        auto* chunk = Storage_.ResolveChunk(begin / TokensPerChunk);
        const auto offset = begin % TokensPerChunk;
        return {chunk->MatchingDelimiters.data() + offset, chunk->Size - offset};
    }

    void Seal()
    {
        Storage_.Seal();
        const auto count = Storage_.Size();

        if (!count) {
            return;
        }

        if (count <= TokensPerChunk) {
            Kinds_ = Storage_.ChunkAt(0)->Kinds.data();
            return;
        }

        // Grammar scans need kinds much more often than spelling/positions.
        // Keep their immutable bytes directly indexed after tokenization.
        Kinds_ = static_cast<tokenizer::TokenKind*>(Memory_->allocate(
            sizeof(tokenizer::TokenKind) * count,
            alignof(tokenizer::TokenKind)));
        for (u32 begin = 0; begin < count;) {
            const auto size = std::min<u32>(TokensPerChunk, count - begin);
            std::memcpy(
                Kinds_ + begin,
                Storage_.ChunkAt(begin / TokensPerChunk)->Kinds.data(),
                sizeof(tokenizer::TokenKind) * size);
            begin += size;
        }
    }

    [[nodiscard]] const StoredToken& operator[](TokenIndex index) const
    {
        assert(index < Storage_.Size());
        return Storage_.ChunkAt(index / TokensPerChunk)->Tokens[index % TokensPerChunk];
    }

    [[nodiscard]] TokenIndex Size() const
    {
        return Storage_.Size();
    }

    [[nodiscard]] tokenizer::TokenKind Kind(TokenIndex index) const
    {
        assert(index < Storage_.Size());
        assert(Kinds_);
        return Kinds_[index];
    }

    [[nodiscard]] PunctuationRole Role(TokenIndex index) const
    {
        assert(index < Storage_.Size());
        return Storage_.ChunkAt(index / TokensPerChunk)->Roles[index % TokensPerChunk];
    }

    void SetRole(TokenIndex index, PunctuationRole role)
    {
        assert(index < Storage_.Size());
        Storage_.ChunkAt(index / TokensPerChunk)->Roles[index % TokensPerChunk] = role;
    }

    [[nodiscard]] TokenIndex MatchingDelimiter(TokenIndex index) const
    {
        assert(index < Storage_.Size());
        return Storage_.ChunkAt(index / TokensPerChunk)->MatchingDelimiters[index % TokensPerChunk];
    }

    void SetMatchingDelimiter(TokenIndex index, TokenIndex matching)
    {
        assert(index < Storage_.Size());
        Storage_.ResolveChunk(index / TokensPerChunk)->MatchingDelimiters[index % TokensPerChunk] = matching;
    }

    [[nodiscard]] TokenIndex FindPreprocessingDirective(TokenIndex begin, TokenIndex end) const;
    [[nodiscard]] TokenIndex FindComment(TokenIndex begin, TokenIndex end) const;

private:
    std::pmr::memory_resource* Memory_;
    tokenizer::TokenKind* Kinds_ = nullptr;
    SegmentedStorage<Chunk, TokensPerChunk> Storage_;
};

} // namespace detail

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

    SyntaxTree(const SyntaxTree& other) = delete;
    SyntaxTree& operator=(const SyntaxTree& other) = delete;

    [[nodiscard]] const Node& Root() const;

    [[nodiscard]] std::pmr::memory_resource* MemoryResource() const
    {
        return Memory_ ? Memory_->upstream_resource() : std::pmr::get_default_resource();
    }

    [[nodiscard]] std::string_view Source() const
    {
        return Input_;
    }

    [[nodiscard]] TokenIndex TokenCount() const
    {
        return Tokens_.Size();
    }

    [[nodiscard]] tokenizer::Token Token(TokenIndex index) const
    {
        return {.Kind = Kind(index), .Position = TokenPosition(index), .Text = Tokens_[index].Text};
    }

    [[nodiscard]] tokenizer::TokenKind Kind(TokenIndex index) const
    {
        return Tokens_.Kind(index);
    }

    [[nodiscard]] util::Position TokenPosition(TokenIndex index) const
    {
        const auto& token = Tokens_[index];
        return {.Line = token.Line, .Column = token.Column};
    }

    [[nodiscard]] std::string_view TokenText(TokenIndex index) const
    {
        return Tokens_[index].Text.View(Input_);
    }

    [[nodiscard]] PunctuationRole Role(TokenIndex index) const
    {
        return Tokens_.Role(index);
    }

    [[nodiscard]] TokenIndex MatchingDelimiter(TokenIndex index) const
    {
        return Tokens_.MatchingDelimiter(index);
    }

    [[nodiscard]] const TriviaAttachments* AssociatedTrivia(const Node& node) const
    {
        return node.TriviaIndex == NoTrivia ? nullptr : &Trivia_[node.TriviaIndex];
    }

    [[nodiscard]] std::string_view SourceText(TokenRange range) const;
    [[nodiscard]] std::string_view SourceText(SourceRange range) const;

    [[nodiscard]] std::string_view WhitespaceBefore(TokenIndex token) const
    {
        assert(token < TokenCount());
        auto* begin = token == 0 ? Input_.data() : Input_.data() + Tokens_[token - 1].Text.End;
        auto* end = Input_.data() + Tokens_[token].Text.Begin;
        return {begin, static_cast<us>(end - begin)};
    }

    [[nodiscard]] std::string_view WhitespaceAfter(TokenIndex token) const
    {
        assert(token < TokenCount());
        auto* begin = Input_.data() + Tokens_[token].Text.End;
        auto* end = token + 1 == TokenCount()
            ? Input_.data() + Input_.size()
            : Input_.data() + Tokens_[token + 1].Text.Begin;
        return {begin, static_cast<us>(end - begin)};
    }

private:
    explicit SyntaxTree(std::string_view input, std::pmr::memory_resource* memory = nullptr);

    std::unique_ptr<Arena> Memory_;
    std::unique_ptr<Arena> OriginalMemory_;
    std::string_view Input_;
    detail::TokenPool Tokens_;
    detail::TriviaPool Trivia_;
    Node* Root_ = nullptr;

    friend class ParseContext;
};

} // namespace parser
