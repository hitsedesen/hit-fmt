#include "parser.hpp"

#include <tokenizer/tokenizer.hpp>
#include <util/assert.hpp>
#include <util/phase_profiler.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory_resource>
#include <new>
#include <utility>
#include <vector>

namespace parser {

TokenIndex detail::TokenPool::FindPreprocessingDirective(
    TokenIndex begin,
    TokenIndex end) const
{
    ASSERT(begin <= end && end <= Storage_.Size())
    while (begin < end) {
        auto chunkIndex = begin / TokensPerChunk;
        auto offset = begin % TokensPerChunk;
        auto count = std::min(end - begin, TokensPerChunk - offset);
        auto* chunk = Storage_.ChunkAt(chunkIndex);
        if (chunk->HasPreprocessingDirective) {
            for (TokenIndex i = offset; i < offset + count; ++i) {
                if (chunk->Kinds[i]
                    == tokenizer::TokenKind::PreprocessingDirective)
                {
                    return chunkIndex * TokensPerChunk + i;
                }
            }
        }
        begin += count;
    }
    return end;
}

TokenIndex
detail::TokenPool::FindComment(TokenIndex begin, TokenIndex end) const
{
    ASSERT(begin <= end && end <= Storage_.Size())
    while (begin < end) {
        auto chunkIndex = begin / TokensPerChunk;
        auto offset = begin % TokensPerChunk;
        auto count = std::min(end - begin, TokensPerChunk - offset);
        auto* chunk = Storage_.ChunkAt(chunkIndex);
        if (chunk->HasComment) {
            for (TokenIndex i = offset; i < offset + count; ++i) {
                if (chunk->Kinds[i] == tokenizer::TokenKind::Comment) {
                    return chunkIndex * TokensPerChunk + i;
                }
            }
        }
        begin += count;
    }
    return end;
}

namespace {

using TK = tokenizer::TokenKind;

bool IsOpenDelimiter(TK kind)
{
    return kind == TK::Operator_OpenParenthesis ||
        kind == TK::Operator_OpenBracket || kind == TK::Operator_OpenBrace;
}

TK ClosingDelimiter(TK kind)
{
    switch (kind) {
        case TK::Operator_OpenParenthesis:
            return TK::Operator_CloseParenthesis;
        case TK::Operator_OpenBracket:
            return TK::Operator_CloseBracket;
        case TK::Operator_OpenBrace:
            return TK::Operator_CloseBrace;
        default:
            return TK::Invalid;
    }
}

bool IsWordToken(TK kind)
{
    return kind == TK::Identifier || (kind >= TK::Literal_UserDefinedInteger &&
        kind <= TK::Literal_String) || (kind >= TK::Keyword_alignas &&
            kind <= TK::Keyword_while);
}

bool IsFundamentalTypeKeyword(TK kind)
{
    switch (kind) {
        case TK::Keyword_auto:
        case TK::Keyword_bool:
        case TK::Keyword_char:
        case TK::Keyword_char8_t:
        case TK::Keyword_char16_t:
        case TK::Keyword_char32_t:
        case TK::Keyword_decltype:
        case TK::Keyword_double:
        case TK::Keyword_float:
        case TK::Keyword_int:
        case TK::Keyword_long:
        case TK::Keyword_short:
        case TK::Keyword_signed:
        case TK::Keyword_unsigned:
        case TK::Keyword_void:
        case TK::Keyword_wchar_t:
            return true;
        default:
            return false;
    }
}

bool IsTypeKeyword(TK kind)
{
    switch (kind) {
        case TK::Keyword_auto:
        case TK::Keyword_bool:
        case TK::Keyword_char:
        case TK::Keyword_char8_t:
        case TK::Keyword_char16_t:
        case TK::Keyword_char32_t:
        case TK::Keyword_const:
        case TK::Keyword_decltype:
        case TK::Keyword_double:
        case TK::Keyword_float:
        case TK::Keyword_int:
        case TK::Keyword_long:
        case TK::Keyword_short:
        case TK::Keyword_signed:
        case TK::Keyword_unsigned:
        case TK::Keyword_void:
        case TK::Keyword_volatile:
        case TK::Keyword_wchar_t:
        case TK::Keyword_typename:
        case TK::Keyword_struct:
        case TK::Keyword_class:
        case TK::Keyword_union:
        case TK::Keyword_enum:
        case TK::Keyword_static:
        case TK::Keyword_extern:
        case TK::Keyword_thread_local:
        case TK::Keyword_constexpr:
        case TK::Keyword_consteval:
        case TK::Keyword_constinit:
        case TK::Keyword_inline:
        case TK::Keyword_mutable:
        case TK::Keyword_register:
            return true;
        default:
            return false;
    }
}

} // namespace

SyntaxTree::SyntaxTree(std::string_view input)
    : Memory_(
        std::make_unique<std::pmr::monotonic_buffer_resource>(
            detail::InitialMemoryResourceSize))
    , Input_(input)
    , Tokens_(*Memory_)
    , Trivia_(*Memory_)
{ }

SyntaxTree::SyntaxTree(SyntaxTree&& other) noexcept
    : Memory_(std::move(other.Memory_))
    , Input_(other.Input_)
    , Tokens_(std::move(other.Tokens_))
    , Trivia_(std::move(other.Trivia_))
    , Root_(std::exchange(other.Root_, nullptr))
    , Original_(other.Original_)
{ }

SyntaxTree& SyntaxTree::operator=(SyntaxTree&& other) noexcept
{
    if (this != &other) {
        Memory_ = std::move(other.Memory_);
        Input_ = other.Input_;
        Tokens_.MoveFrom(std::move(other.Tokens_));
        Trivia_.MoveFrom(std::move(other.Trivia_));
        Root_ = std::exchange(other.Root_, nullptr);
        Original_ = other.Original_;
    }
    return *this;
}

SyntaxTree::~SyntaxTree() = default;

const Node& SyntaxTree::Root() const
{
    ASSERT(Root_)
    return *Root_;
}

std::optional<SyntaxTree> SyntaxTree::RebindWhitespace(
    util::PaddedSourceView source) const
{
    HIT_FMT_PROFILE_PHASE("parser.rebind");
    bool error = false;
    tokenizer::Tokenizer tokenizer(source,
        [&](tokenizer::TokenizationError) { error = true; });
    SyntaxTree rebound(source.View());
    TokenIndex index = 0;
    size_t previousEnd = 0;
    bool previousCommentOrDirective = false;
    {
        HIT_FMT_PROFILE_PHASE("parser.rebind.scan_verify");
        for (auto token : tokenizer) {
            if (index >= TokenCount() || token.Kind != Kind(index) ||
                source.View().substr(token.Text.Begin,
                    token.Text.End - token.Text.Begin) != TokenText(index))
            {
                return std::nullopt;
            }
            const auto gap = source.View().substr(previousEnd,
                token.Text.Begin - previousEnd);
            const auto oldGap = WhitespaceBefore(index);
            // Escaped newlines between tokens must stay byte-for-byte intact;
            // those inside a token are checked by comparing token spellings.
            if ((gap.find("\\\n") != std::string_view::npos ||
                    oldGap.find("\\\n") != std::string_view::npos) &&
                gap != oldGap)
            {
                return std::nullopt;
            }
            const bool commentOrDirective =
                token.Kind == tokenizer::TokenKind::Comment ||
                token.Kind == tokenizer::TokenKind::PreprocessingDirective;
            // Do not reuse comment/directive ownership if a line boundary has
            // moved across either token. The syntax tree keeps their old nodes.
            if ((commentOrDirective || previousCommentOrDirective) &&
                (gap.find('\n') != std::string_view::npos) !=
                    (oldGap.find('\n') != std::string_view::npos))
            {
                return std::nullopt;
            }
            rebound.Tokens_.Add(token);
            previousEnd = token.Text.End;
            previousCommentOrDirective = commentOrDirective;
            ++index;
        }
    }
    const auto tail = source.View().substr(previousEnd);
    const auto oldTail = index ? WhitespaceAfter(index - 1) : Input_;
    if (((tail.find("\\\n") != std::string_view::npos ||
            oldTail.find("\\\n") != std::string_view::npos) &&
            tail != oldTail) ||
        (previousCommentOrDirective &&
            (tail.find('\n') != std::string_view::npos) !=
                (oldTail.find('\n') != std::string_view::npos)))
    {
        return std::nullopt;
    }
    if (error || index != TokenCount()) return std::nullopt;
    {
        HIT_FMT_PROFILE_PHASE("parser.rebind.copy_roles");
        rebound.Tokens_.Seal();
        for (TokenIndex i = 0; i < index; ++i) {
            rebound.Tokens_.SetRole(i, Role(i));
            rebound.Tokens_.SetMatchingDelimiter(i, MatchingDelimiter(i));
        }
    }
    rebound.Root_ = Root_;
    rebound.Original_ = Original_ ? Original_ : this;
    return rebound;
}

std::optional<SyntaxTree> SyntaxTree::RebindWhitespaceEdits(
    util::PaddedSourceView source, std::span<const WhitespaceEdit> edits) const
{
    HIT_FMT_PROFILE_PHASE("parser.rebind_edits");
    if (source.Size > util::MaxSourceSize) return std::nullopt;
    SyntaxTree rebound(source.View());
    int64_t lineDelta = 0;
    int64_t columnDelta = 0;
    uint32_t previousOldLine = 0;
    util::Position previousNewPosition{.Line = 1, .Column = 1};
    auto asciiWhitespace = [] (std::string_view gap) {
        return gap.find_first_not_of(" \t\r\n\f\v") ==
            std::string_view::npos;
    };
    auto safeJoin = [&] (TokenIndex left, TokenIndex right) {
        auto first = TokenText(left);
        auto second = TokenText(right);
        std::string joined;
        joined.reserve(first.size() + second.size() + util::SourcePaddingSize);
        joined.append(first);
        joined.append(second);
        auto length = joined.size();
        joined.append(util::SourcePaddingSize, '\0');
        bool error = false;
        tokenizer::Tokenizer lexer({.Data = joined.data(), .Size = length},
            [&] (tokenizer::TokenizationError) { error = true; });
        const auto a = lexer.Get();
        const auto b = lexer.Get();
        const auto end = lexer.Get();
        return !error && a.Kind == Kind(left) && b.Kind == Kind(right) &&
            a.Text.Size() == first.size() && b.Text.Size() == second.size() &&
            end.Kind == tokenizer::TokenKind::EndOfFile;
    };
    // Verify the edited output in contiguous spans, rather than comparing
    // millions of individual token spellings. Gap ownership is checked below.
    size_t oldSpan = 0;
    size_t newSpan = 0;
    for (const auto& edit : edits) {
        if (edit.Begin < oldSpan || edit.End < edit.Begin ||
            edit.End > Input_.size() || newSpan > source.Size)
        {
            return std::nullopt;
        }
        auto unchanged = Input_.substr(oldSpan, edit.Begin - oldSpan);
        if (newSpan > source.Size || unchanged.size() > source.Size - newSpan ||
            source.View().substr(newSpan, unchanged.size()) != unchanged)
        {
            return std::nullopt;
        }
        newSpan += unchanged.size();
        if (edit.Text.size() > source.Size - newSpan ||
            source.View().substr(newSpan, edit.Text.size()) != edit.Text)
        {
            return std::nullopt;
        }
        newSpan += edit.Text.size();
        oldSpan = edit.End;
    }
    if (newSpan > source.Size ||
        source.View().substr(newSpan) != Input_.substr(oldSpan))
    {
        return std::nullopt;
    }
    size_t oldCursor = 0;
    size_t newCursor = 0;
    size_t editIndex = 0;
    int64_t delta = 0;
    bool previousCommentOrDirective = false;
    for (TokenIndex index = 0; index < TokenCount(); ++index) {
        const auto token = Token(index);
        const auto oldBegin = token.Text.Begin;
        const auto oldEnd = token.Text.End;
        size_t lastEditEnd = oldCursor;
        while (editIndex < edits.size() &&
            edits[editIndex].Begin <= oldBegin)
        {
            const auto& edit = edits[editIndex++];
            if (edit.Begin < lastEditEnd || edit.End < edit.Begin ||
                edit.End > oldBegin || edit.Text.size() > util::MaxSourceSize)
            {
                HIT_FMT_PROFILE_EVENT("edits.invalid_gap_range");
                return std::nullopt;
            }
            delta += static_cast<int64_t>(edit.Text.size()) -
                (edit.End - edit.Begin);
            lastEditEnd = edit.End;
        }
        const auto shifted = static_cast<int64_t>(oldBegin) + delta;
        if (shifted < 0 || static_cast<size_t>(shifted) > source.Size) {
            HIT_FMT_PROFILE_EVENT("edits.invalid_offset");
            return std::nullopt;
        }
        const auto newBegin = static_cast<size_t>(shifted);
        const auto size = oldEnd - oldBegin;
        if (newBegin < newCursor || size > source.Size - newBegin) {
            HIT_FMT_PROFILE_EVENT("edits.token_mismatch");
            return std::nullopt;
        }
        const auto oldGap = Input_.substr(oldCursor, oldBegin - oldCursor);
        const auto newGap = source.View().substr(newCursor,
            newBegin - newCursor);
        if (oldGap != newGap &&
            (!asciiWhitespace(oldGap) || !asciiWhitespace(newGap) ||
                oldGap.find("\\\n") != std::string_view::npos ||
                newGap.find("\\\n") != std::string_view::npos ||
                (newGap.empty() && !oldGap.empty() && index > 0 &&
                    !safeJoin(index - 1, index))))
        {
            HIT_FMT_PROFILE_EVENT("edits.unsafe_gap");
            return std::nullopt;
        }
        const auto commentOrDirective =
            token.Kind == tokenizer::TokenKind::Comment ||
            token.Kind == tokenizer::TokenKind::PreprocessingDirective;
        if (oldGap != newGap &&
            (commentOrDirective || previousCommentOrDirective) &&
            (oldGap.find('\n') != std::string_view::npos) !=
                (newGap.find('\n') != std::string_view::npos))
        {
            HIT_FMT_PROFILE_EVENT("edits.comment_boundary");
            return std::nullopt;
        }
        const auto oldPosition = token.Position;
        if (oldPosition.Line != previousOldLine) columnDelta = 0;
        if (oldGap != newGap) {
            const auto oldLines = std::ranges::count(oldGap, '\n');
            const auto newLines = std::ranges::count(newGap, '\n');
            lineDelta += newLines - oldLines;
            if (newLines) {
                columnDelta = static_cast<int64_t>(newGap.size() -
                    newGap.rfind('\n')) - oldPosition.Column;
            } else if (oldLines) {
                int64_t previousEndColumn = 1;
                if (index) {
                    auto previous = TokenText(index - 1);
                    if (previous.find('\n') != std::string_view::npos ||
                        std::ranges::any_of(previous, [] (unsigned char c) {
                            return c >= 0x80;
                        }))
                    {
                        return std::nullopt;
                    }
                    previousEndColumn = previousNewPosition.Column +
                        previous.size();
                }
                columnDelta = previousEndColumn +
                    static_cast<int64_t>(newGap.size()) - oldPosition.Column;
            } else {
                columnDelta += static_cast<int64_t>(newGap.size()) -
                    static_cast<int64_t>(oldGap.size());
            }
        }
        const auto line = static_cast<int64_t>(oldPosition.Line) + lineDelta;
        const auto column = static_cast<int64_t>(oldPosition.Column) +
            columnDelta;
        if (line < 1 || column < 1 ||
            line > std::numeric_limits<uint32_t>::max() ||
            column > std::numeric_limits<uint32_t>::max())
        {
            return std::nullopt;
        }
        rebound.Tokens_.Add({.Kind = token.Kind,
            .Position = {.Line = static_cast<uint32_t>(line),
                .Column = static_cast<uint32_t>(column)},
            .Text = {.Begin = static_cast<util::SourceOffset>(newBegin),
                .End = static_cast<util::SourceOffset>(newBegin + size)}});
        previousNewPosition = {.Line = static_cast<uint32_t>(line),
            .Column = static_cast<uint32_t>(column)};
        previousOldLine = oldPosition.Line;
        oldCursor = oldEnd;
        newCursor = newBegin + size;
        previousCommentOrDirective = commentOrDirective;
    }
    size_t lastEditEnd = oldCursor;
    while (editIndex < edits.size()) {
        const auto& edit = edits[editIndex++];
        if (edit.Begin < lastEditEnd || edit.End < edit.Begin ||
            edit.End > Input_.size() ||
            edit.Text.size() > util::MaxSourceSize)
        {
            HIT_FMT_PROFILE_EVENT("edits.invalid_tail_edit");
            return std::nullopt;
        }
        delta += static_cast<int64_t>(edit.Text.size()) -
            (edit.End - edit.Begin);
        lastEditEnd = edit.End;
    }
    if (static_cast<int64_t>(Input_.size()) + delta !=
        static_cast<int64_t>(source.Size))
    {
        HIT_FMT_PROFILE_EVENT("edits.length_mismatch");
        return std::nullopt;
    }
    const auto oldTail = Input_.substr(oldCursor);
    const auto newTail = source.View().substr(newCursor);
    if (oldTail != newTail &&
        (!asciiWhitespace(oldTail) || !asciiWhitespace(newTail) ||
            oldTail.find("\\\n") != std::string_view::npos ||
            newTail.find("\\\n") != std::string_view::npos ||
            (previousCommentOrDirective &&
                (oldTail.find('\n') != std::string_view::npos) !=
                    (newTail.find('\n') != std::string_view::npos))))
    {
        HIT_FMT_PROFILE_EVENT("edits.unsafe_tail");
        return std::nullopt;
    }
    HIT_FMT_PROFILE_EVENT("edits.success");
    rebound.Tokens_.Seal();
    for (TokenIndex i = 0; i < TokenCount(); ++i) {
        rebound.Tokens_.SetRole(i, Role(i));
        rebound.Tokens_.SetMatchingDelimiter(i, MatchingDelimiter(i));
    }
    rebound.Root_ = Root_;
    rebound.Original_ = Original_ ? Original_ : this;
    return rebound;
}

tokenizer::Token SyntaxTree::Token(TokenIndex index) const
{
    return {
        .Kind = Kind(index),
        .Position = TokenPosition(index),
        .Text = Tokens_[index].Text,
    };
}

const TriviaAttachments* SyntaxTree::AssociatedTrivia(const Node& node) const
{
    // Borrowed syntax retains original trivia offsets; do not expose them
    // against the rebound source.
    if (Original_) return nullptr;
    if (node.TriviaIndex_ == std::numeric_limits<uint32_t>::max()) {
        return nullptr;
    }
    return &Trivia_[node.TriviaIndex_];
}

std::string_view SyntaxTree::SourceText(TokenRange range) const
{
    ASSERT(range.Begin <= range.End && range.End <= TokenCount())
    if (range.Empty()) {
        return {};
    }
    const auto& first = Tokens_[range.Begin].Text;
    const auto& last = Tokens_[range.End - 1].Text;
    return Input_.substr(first.Begin, last.End - first.Begin);
}

std::string_view SyntaxTree::SourceText(SourceRange range) const
{
    ASSERT(range.Begin <= range.End && range.End <= Input_.size())
    return Input_.substr(range.Begin, range.End - range.Begin);
}

class ParseContext
{
public:
    [[nodiscard]] static SyntaxTree
    Run(util::PaddedSourceView input, ErrorHandler errorHandler);

    ParseContext(SyntaxTree& tree, ErrorHandler& errorHandler)
        : Tree_(tree)
        , ErrorHandler_(errorHandler)
    { }

    void Build()
    {
        {
            HIT_FMT_PROFILE_PHASE("parser.tokenize_and_delimiters");
            Tokenize();
            Tree_.Tokens_.Seal();
        }
        ParseLimit_ = Tree_.Tokens_.Size();
        CurrentScope_ = detail::Make<NameScope>(ScratchMemory_);

        auto* root = MakeNode(NodeKind::TranslationUnit, 0);
        Tree_.Root_ = root;
        {
            HIT_FMT_PROFILE_PHASE("parser.syntax");
            ParseDeclarationSequence(*root, TK::EndOfFile);
            root->Tokens.End = Cursor_;
        }
        {
            HIT_FMT_PROFILE_PHASE("parser.comments");
            AssociateComments(*root);
        }
        {
            HIT_FMT_PROFILE_PHASE("parser.blank_lines");
            AssociateBlankLines(*root);
            Tree_.Trivia_.Seal();
        }
    }

private:
    enum class NameKind
    {
        Type,
        Value,
    };

    struct NameBinding
    {
        std::string_view Name;
        NameKind Kind;
        NameBinding* Next = nullptr;
    };

    struct NameScope
    {
        NameScope* Parent = nullptr;
        NameBinding* Bindings = nullptr;
    };

    void PushScope()
    {
        CurrentScope_ = detail::Make<NameScope>(
            ScratchMemory_,
            NameScope{
                .Parent = CurrentScope_,
            });
    }

    void PopScope()
    {
        ASSERT(CurrentScope_)
        CurrentScope_ = CurrentScope_->Parent;
    }

    void AddName(std::string_view name, NameKind kind)
    {
        ASSERT(CurrentScope_)
        auto* binding = detail::Make<NameBinding>(
            ScratchMemory_,
            NameBinding{
                .Name = name,
                .Kind = kind,
                .Next = CurrentScope_->Bindings,
            });
        CurrentScope_->Bindings = binding;
    }

    [[nodiscard]] std::optional<NameKind>
    LookupName(std::string_view name) const
    {
        for (auto* scope = CurrentScope_; scope; scope = scope->Parent) {
            for (auto* binding = scope->Bindings;
                binding;
                binding = binding->Next)
            {
                if (binding->Name == name) {
                    return binding->Kind;
                }
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] bool IsKnownType(TokenIndex index) const
    {
        if (Kind(index) != TK::Identifier) {
            return false;
        }
        auto kind = LookupName(Text(index));
        return kind && *kind == NameKind::Type;
    }

    void Tokenize()
    {
        tokenizer::Tokenizer tokenizer(
            {.Data = Tree_.Input_.data(), .Size = Tree_.Input_.size()},
            [this](tokenizer::TokenizationError error) {
                OnError(
                    ParseError{
                        .Message = std::string(
                            tokenizer::GetTokenizationErrorDescription(
                                error.Kind)),
                        .Position = error.ErrorPosition,
                    });
            });

        struct Delimiter
        {
            TokenIndex Index;
            TK Closing;
        };

        std::pmr::vector<Delimiter> openings(&ScratchMemory_);
        openings.reserve(256);
        for (const auto& token : tokenizer) {
            Tree_.Tokens_.Add(token);
            auto index = Tree_.Tokens_.Size() - 1;
            if (index >= std::numeric_limits<uint32_t>::max()) {
                HasDelimiterMap_ = false;
            } else if (IsOpenDelimiter(token.Kind)) {
                openings.push_back({
                    .Index = index,
                    .Closing = ClosingDelimiter(token.Kind),
                });
            } else if (token.Kind == TK::Operator_CloseParenthesis ||
                token.Kind == TK::Operator_CloseBracket ||
                token.Kind == TK::Operator_CloseBrace)
            {
                if (openings.empty() || openings.back().Closing != token.Kind) {
                    HasDelimiterMap_ = false;
                } else {
                    auto opening = openings.back();
                    openings.pop_back();
                    Tree_.Tokens_.SetMatchingDelimiter(opening.Index, index);
                    Tree_.Tokens_.SetMatchingDelimiter(index, opening.Index);
                }
            }
        }
        HasDelimiterMap_ &= openings.empty();
    }

    [[nodiscard]] Node* MakeNode(NodeKind kind, TokenIndex begin)
    {
        constexpr size_t NodesPerBlock = 256;
        if (RemainingNodes_ == 0) {
            NextNode_ = static_cast<Node*>(Tree_.Memory_->allocate(
                sizeof(Node) * NodesPerBlock,
                alignof(Node)));
            RemainingNodes_ = NodesPerBlock;
        }
        auto* node = ::new (NextNode_++) Node;
        node->Kind = kind;
        node->Tokens = {.Begin = begin, .End = begin};
        --RemainingNodes_;
        return node;
    }

    [[nodiscard]] static bool NeedsPunctuationRole(NodeKind kind)
    {
        static constexpr auto Kinds = [] {
            std::array<bool, static_cast<size_t>(NodeKind::Error) + 1> result{};
            for (auto roleKind : {
                NodeKind::PointerOperator,
                NodeKind::MemberPointerOperator,
                NodeKind::UnaryExpression,
                NodeKind::BinaryExpression,
                NodeKind::AssignmentExpression,
                NodeKind::ConditionalExpression,
                NodeKind::TemplateArgumentList,
                NodeKind::TemplateParameterList,
                NodeKind::ListSeparator,
                NodeKind::BaseClause,
                NodeKind::BitField,
                NodeKind::ConstructorInitializerList,
                NodeKind::RangeForSeparator,
                NodeKind::CaseLabel,
                NodeKind::LabelStatement,
                NodeKind::AccessSpecifier,
                NodeKind::CompoundStatement,
                NodeKind::RequirementBody,
                NodeKind::RecordDeclaration,
                NodeKind::BracedInitializer,
                NodeKind::InitializerList,
                NodeKind::NewInitializer,
                NodeKind::LambdaCaptureList,
                NodeKind::LambdaBody,
                NodeKind::ArgumentList,
                NodeKind::NewPlacement,
                NodeKind::ParameterList,
                NodeKind::NestedDeclarator,
                NodeKind::ArrayDeclarator,
                NodeKind::StructuredBinding,
                NodeKind::AttributeSpecifier,
                NodeKind::Condition,
                NodeKind::ForHeader,
                NodeKind::ParenthesizedExpression,
                NodeKind::StatementExpression,
                NodeKind::FoldExpression,
                NodeKind::CastExpression,
                NodeKind::SubscriptExpression,
                NodeKind::FoldOperator,
                NodeKind::FoldEllipsis,
            })
            {
                result[static_cast<size_t>(roleKind)] = true;
            }
            return result;
        }();
        return Kinds[static_cast<size_t>(kind)];
    }

    void Append(Node& parent, Node& child)
    {
        if (NeedsPunctuationRole(child.Kind)) {
            MarkPunctuationRoles(child);
        }
        if (parent.LastChild) {
            parent.LastChild->NextSibling = &child;
        } else {
            parent.FirstChild = &child;
        }
        parent.LastChild = &child;
    }

    [[nodiscard]] bool AtEnd() const
    {
        return Cursor_ >= ParseLimit_;
    }

    [[nodiscard]] TK Kind(TokenIndex index) const
    {
        if (index >= ParseLimit_ || index >= Tree_.Tokens_.Size()) {
            return TK::EndOfFile;
        }
        return Tree_.Tokens_.Kind(index);
    }

    [[nodiscard]] std::string_view Text(TokenIndex index) const
    {
        if (index >= Tree_.Tokens_.Size()) {
            return {};
        }
        return Tree_.Tokens_[index].Text.View(Tree_.Input_);
    }

    [[nodiscard]] TK CurrentKind() const
    {
        return Kind(Cursor_);
    }

    [[nodiscard]] TokenIndex NextSignificant(TokenIndex index) const
    {
        while (Kind(index) == TK::Comment) {
            ++index;
        }
        return index;
    }

    [[nodiscard]] TK SignificantKind(TokenIndex index) const
    {
        return Kind(NextSignificant(index));
    }

    void OnError(ParseError error)
    {
        if (ErrorHandler_) {
            ErrorHandler_(std::move(error));
        } else {
            std::cerr << "Parse error at " << error.Position.Line << ':'
                      << error.Position.Column << ": " << error.Message << '\n';
        }
    }

    void ErrorAt(TokenIndex index, std::string message)
    {
        if (BranchParseDepth_ != 0) {
            return;
        }
        util::Position position{.Line = 1, .Column = 1};
        if (Tree_.Tokens_.Size() != 0) {
            position =
                Tree_.TokenPosition(std::min(index, Tree_.Tokens_.Size() - 1));
        }
        OnError({.Message = std::move(message), .Position = position});
    }

    void AddMissingToken(Node& parent, TokenIndex index)
    {
        auto* missing = MakeNode(NodeKind::MissingToken, index);
        missing->Tokens.End = index;
        Append(parent, *missing);
    }

    void AddUnparsedSyntax(Node& parent, TokenIndex begin, TokenIndex end)
    {
        begin = NextSignificant(begin);
        if (begin >= end) {
            return;
        }
        auto* unparsed = MakeNode(NodeKind::UnparsedSyntax, begin);
        unparsed->Tokens.End = end;
        Append(parent, *unparsed);
    }

    void AddDirectiveChildren(Node& parent, TokenIndex begin, TokenIndex end)
    {
        for (auto i = Tree_.Tokens_.FindPreprocessingDirective(begin, end);
            i < end;
            i = Tree_.Tokens_.FindPreprocessingDirective(i + 1, end))
        {
            auto* directive = MakeNode(NodeKind::PreprocessingDirective, i);
            directive->Tokens.End = i + 1;
            Append(parent, *directive);
        }
    }

    TokenIndex ConsumeBalanced()
    {
        ASSERT(IsOpenDelimiter(CurrentKind()))
        if (HasDelimiterMap_) {
            auto matching = Tree_.Tokens_.MatchingDelimiter(Cursor_);
            if (matching != std::numeric_limits<TokenIndex>::max()
                && matching < ParseLimit_)
            {
                Cursor_ = matching + 1;
                return Cursor_;
            }
        }

        constexpr size_t MaximumDepth = 256;
        std::array<TK, MaximumDepth> closings;
        size_t depth = 0;
        auto start = Cursor_;

        while (!AtEnd()) {
            auto kind = CurrentKind();
            if (IsOpenDelimiter(kind)) {
                if (depth == MaximumDepth) {
                    ErrorAt(Cursor_, "delimiter nesting is too deep");
                    ++Cursor_;
                    continue;
                }
                closings[depth++] = ClosingDelimiter(kind);
                ++Cursor_;
                continue;
            }
            if (depth != 0 && kind == closings[depth - 1]) {
                ++Cursor_;
                if (--depth == 0) {
                    return Cursor_;
                }
                continue;
            }
            if (kind == TK::Operator_CloseParenthesis ||
                kind == TK::Operator_CloseBracket ||
                kind == TK::Operator_CloseBrace)
            {
                ErrorAt(Cursor_, "mismatched closing delimiter");
            }
            ++Cursor_;
        }
        ErrorAt(start, "unterminated delimiter");
        return Cursor_;
    }

    void ParseDeclarationSequence(Node& parent, TK closing)
    {
        PushConditionalBraceScope();
        while (!AtEnd()) {
            if (CurrentKind() == closing) {
                if (closing == TK::Operator_CloseBrace &&
                    ConsumeConditionalClosingBrace(parent))
                {
                    continue;
                }
                break;
            }
            if (ParseTrivia(parent, false)) {
                continue;
            }
            auto before = Cursor_;
            Append(parent, *ParseDeclaration());
            if (Cursor_ == before) {
                ErrorAt(Cursor_, "parser made no progress");
                ++Cursor_;
            }
        }
        PopConditionalBraceScope();
        if (closing != TK::EndOfFile && AtEnd()) {
            ErrorAt(Cursor_, "expected closing brace");
        }
    }

    enum class DirectiveKind
    {
        Other,
        If,
        ElseIf,
        Else,
        EndIf,
    };

    [[nodiscard]] DirectiveKind GetDirectiveKind(TokenIndex index) const
    {
        if (Kind(index) != TK::PreprocessingDirective) {
            return DirectiveKind::Other;
        }
        auto text = Text(index);
        if (text.starts_with("%:")) {
            text.remove_prefix(2);
        } else if (text.starts_with('#')) {
            text.remove_prefix(1);
        }
        while (!text.empty() && (text.front() == ' ' || text.front() == '\t' ||
                text.front() == '\v' || text.front() == '\f'))
        {
            text.remove_prefix(1);
        }
        auto wordEnd = text.find_first_not_of(
            "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_");
        auto word = text.substr(0, wordEnd);
        if (word == "if" || word == "ifdef" || word == "ifndef") {
            return DirectiveKind::If;
        }
        if (word == "elif" || word == "elifdef" || word == "elifndef") {
            return DirectiveKind::ElseIf;
        }
        if (word == "else") {
            return DirectiveKind::Else;
        }
        if (word == "endif") {
            return DirectiveKind::EndIf;
        }
        return DirectiveKind::Other;
    }

    Node* ParsePreprocessingConditional(bool statementContext)
    {
        auto start = Cursor_;
        auto* conditional = MakeNode(NodeKind::PreprocessingConditional, start);

        while (!AtEnd()) {
            auto directiveKind = GetDirectiveKind(Cursor_);
            if (directiveKind != DirectiveKind::If &&
                directiveKind != DirectiveKind::ElseIf &&
                directiveKind != DirectiveKind::Else)
            {
                break;
            }

            auto* branch = MakeNode(NodeKind::PreprocessingBranch, Cursor_);
            auto* directive =
                MakeNode(NodeKind::PreprocessingDirective, Cursor_);
            directive->Tokens.End = ++Cursor_;
            Append(*branch, *directive);

            while (!AtEnd()) {
                directiveKind = GetDirectiveKind(Cursor_);
                if (directiveKind == DirectiveKind::ElseIf ||
                    directiveKind == DirectiveKind::Else ||
                    directiveKind == DirectiveKind::EndIf)
                {
                    break;
                }
                if (directiveKind == DirectiveKind::If) {
                    Append(
                        *branch,
                        *ParsePreprocessingConditional(statementContext));
                } else if (CurrentKind() == TK::PreprocessingDirective) {
                    auto* nestedDirective =
                        MakeNode(NodeKind::PreprocessingDirective, Cursor_);
                    nestedDirective->Tokens.End = ++Cursor_;
                    Append(*branch, *nestedDirective);
                } else {
                    auto branchEnd = Cursor_;
                    int nestedDepth = 0;
                    while (branchEnd < ParseLimit_) {
                        auto nestedKind = GetDirectiveKind(branchEnd);
                        if (nestedKind == DirectiveKind::If) {
                            ++nestedDepth;
                        } else if (nestedKind == DirectiveKind::EndIf) {
                            if (nestedDepth == 0) {
                                break;
                            }
                            --nestedDepth;
                        } else if (nestedDepth == 0 && (nestedKind ==
                            DirectiveKind::ElseIf ||
                                nestedKind == DirectiveKind::Else))
                        {
                            break;
                        }
                        ++branchEnd;
                    }

                    auto outerLimit = ParseLimit_;
                    ParseLimit_ = branchEnd;
                    ++BranchParseDepth_;
                    PushScope();
                    while (!AtEnd()) {
                        if (ParseTrivia(*branch, statementContext)) {
                            continue;
                        }
                        auto before = Cursor_;
                        Append(
                            *branch,
                            statementContext
                                ? *ParseStatement()
                                : *ParseDeclaration());
                        if (Cursor_ == before) {
                            ++Cursor_;
                        }
                    }
                    PopScope();
                    --BranchParseDepth_;
                    ParseLimit_ = outerLimit;
                    Cursor_ = branchEnd;
                }
            }
            branch->Tokens.End = Cursor_;
            Append(*conditional, *branch);

            if (GetDirectiveKind(Cursor_) == DirectiveKind::EndIf) {
                auto* endDirective =
                    MakeNode(NodeKind::PreprocessingDirective, Cursor_);
                endDirective->Tokens.End = ++Cursor_;
                Append(*conditional, *endDirective);
                conditional->Tokens.End = Cursor_;
                return conditional;
            }
        }

        ErrorAt(start, "unterminated preprocessing conditional");
        conditional->Tokens.End = Cursor_;
        return conditional;
    }

    [[nodiscard]] int CommonConditionalBraceDelta(const Node& conditional) const
    {
        std::optional<int> commonDelta;
        bool hasElseBranch = false;
        for (auto* branch = conditional.FirstChild;
            branch;
            branch = branch->NextSibling)
        {
            if (branch->Kind != NodeKind::PreprocessingBranch) {
                continue;
            }
            hasElseBranch |=
                GetDirectiveKind(branch->Tokens.Begin) == DirectiveKind::Else;
            int delta = 0;
            for (auto i = branch->Tokens.Begin; i < branch->Tokens.End; ++i) {
                if (Kind(i) == TK::Operator_OpenBrace) {
                    ++delta;
                } else if (Kind(i) == TK::Operator_CloseBrace) {
                    --delta;
                }
            }
            if (commonDelta && *commonDelta != delta) {
                return 0;
            }
            commonDelta = delta;
        }
        return hasElseBranch ? commonDelta.value_or(0) : 0;
    }

    void PushConditionalBraceScope()
    {
        if (ConditionalBraceScopeOverflow_ != 0 ||
            ConditionalBraceScopeDepth_ == MaximumParseDepth)
        {
            ++ConditionalBraceScopeOverflow_;
            return;
        }
        ConditionalOpenBraces_[ConditionalBraceScopeDepth_++] = 0;
    }

    void PopConditionalBraceScope()
    {
        if (ConditionalBraceScopeOverflow_ != 0) {
            --ConditionalBraceScopeOverflow_;
            return;
        }
        ASSERT(ConditionalBraceScopeDepth_ != 0)
        --ConditionalBraceScopeDepth_;
    }

    bool ConsumeConditionalClosingBrace(Node& parent)
    {
        if (CurrentKind() != TK::Operator_CloseBrace ||
            ConditionalBraceScopeOverflow_ != 0 ||
            ConditionalBraceScopeDepth_ == 0 ||
            ConditionalOpenBraces_[ConditionalBraceScopeDepth_ - 1] == 0)
        {
            return false;
        }
        auto* delimiter = MakeNode(NodeKind::UnparsedSyntax, Cursor_);
        delimiter->Tokens.End = ++Cursor_;
        Append(parent, *delimiter);
        --ConditionalOpenBraces_[ConditionalBraceScopeDepth_ - 1];
        return true;
    }

    bool ParseTrivia(Node& parent, bool statementContext)
    {
        NodeKind nodeKind;
        if (CurrentKind() == TK::Comment) {
            nodeKind = NodeKind::Comment;
        } else if (CurrentKind() == TK::PreprocessingDirective) {
            if (GetDirectiveKind(Cursor_) == DirectiveKind::If) {
                auto* conditional =
                    ParsePreprocessingConditional(statementContext);
                if (BranchParseDepth_ == 0 && !HasDelimiterMap_) {
                    auto braceDelta = CommonConditionalBraceDelta(*conditional);
                    if (braceDelta > 0 && ConditionalBraceScopeOverflow_ == 0 &&
                        ConditionalBraceScopeDepth_ != 0)
                    {
                        ConditionalOpenBraces_
                            [ConditionalBraceScopeDepth_ - 1] +=
                            static_cast<size_t>(braceDelta);
                    }
                }
                Append(parent, *conditional);
                return true;
            }
            nodeKind = NodeKind::PreprocessingDirective;
        } else {
            return false;
        }
        auto* node = MakeNode(nodeKind, Cursor_++);
        node->Tokens.End = Cursor_;
        Append(parent, *node);
        return true;
    }

    Node* ParseDeclaration()
    {
        auto first = NextSignificant(Cursor_);
        switch (Kind(first)) {
            case TK::Operator_Semicolon:
                return ParseToSemicolon(NodeKind::EmptyDeclaration);
            case TK::Keyword_template:
                return ParseTemplateDeclaration();
            case TK::Keyword_namespace:
                return ParseNamespaceDeclaration();
            case TK::Keyword_class:
            case TK::Keyword_struct:
            case TK::Keyword_union:
                return ParseRecordDeclaration();
            case TK::Keyword_enum:
                return ParseEnumDeclaration();
            case TK::Keyword_using:
                return ParseUsingDeclaration();
            case TK::Keyword_typedef:
                return ParseTypedefDeclaration();
            case TK::Keyword_static_assert:
                return ParseToSemicolon(NodeKind::StaticAssertDeclaration);
            case TK::Keyword_concept:
                return ParseToSemicolon(NodeKind::ConceptDeclaration);
            case TK::Keyword_asm:
                return ParseToSemicolon(NodeKind::AsmDeclaration);
            case TK::Keyword_export:
                return ParseExportDeclaration();
            case TK::Identifier:
                if (Text(first) == "module") {
                    return ParseToSemicolon(NodeKind::ModuleDeclaration);
                }
                if (Text(first) == "import") {
                    return ParseToSemicolon(NodeKind::ImportDeclaration);
                }
                break;
            case TK::Keyword_extern:
                if (LooksLikeLinkageSpecification(first)) {
                    return ParseLinkageSpecification();
                }
                break;
            case TK::Keyword_public:
            case TK::Keyword_protected:
            case TK::Keyword_private:
                if (SignificantKind(first + 1) == TK::Operator_Colon) {
                    return ParseAccessSpecifier();
                }
                break;
            case TK::Operator_ClassMemberAccess:
            case TK::Operator_IndirectClassMemberAccess:
                return ParseToSemicolon(NodeKind::UnparsedSyntax);
            case TK::Operator_CloseBrace:
            case TK::Operator_CloseParenthesis:
            case TK::Operator_CloseBracket:
            {
                auto* node = MakeNode(NodeKind::UnexpectedToken, Cursor_);
                ErrorAt(Cursor_, "unexpected closing delimiter");
                node->Tokens.End = ++Cursor_;
                return node;
            }
            default:
                break;
        }
        return ParseGeneralDeclaration();
    }

    [[nodiscard]] TokenIndex FindBalancedEnd(TokenIndex opening) const
    {
        if (HasDelimiterMap_ && opening < Tree_.Tokens_.Size()) {
            auto matching = Tree_.Tokens_.MatchingDelimiter(opening);
            if (matching != std::numeric_limits<TokenIndex>::max()) {
                return matching + 1;
            }
        }

        constexpr size_t MaximumDepth = 256;
        std::array<TK, MaximumDepth> closings;
        size_t depth = 0;
        for (auto i = opening; i < Tree_.Tokens_.Size(); ++i) {
            auto kind = Kind(i);
            if (IsOpenDelimiter(kind)) {
                if (depth == MaximumDepth) {
                    return std::numeric_limits<TokenIndex>::max();
                }
                closings[depth++] = ClosingDelimiter(kind);
            } else if (depth != 0 && kind == closings[depth - 1]) {
                if (--depth == 0) {
                    return i + 1;
                }
            } else if (kind == TK::Operator_CloseParenthesis ||
                kind == TK::Operator_CloseBracket ||
                kind == TK::Operator_CloseBrace)
            {
                return std::numeric_limits<TokenIndex>::max();
            }
        }
        return std::numeric_limits<TokenIndex>::max();
    }

    [[nodiscard]] TokenIndex SkipAttributeSpecifiers(TokenIndex index) const
    {
        index = NextSignificant(index);
        while (Kind(index) == TK::Operator_OpenBracket &&
            Kind(index + 1) == TK::Operator_OpenBracket)
        {
            auto end = FindBalancedEnd(index);
            if (end == std::numeric_limits<TokenIndex>::max()) {
                break;
            }
            index = NextSignificant(end);
        }
        return index;
    }

    [[nodiscard]] bool
    LooksLikeLambdaArgument(TokenIndex index, TokenIndex closingEnd) const
    {
        if (Kind(index) != TK::Operator_OpenBracket) {
            return false;
        }
        auto previous = PreviousSignificant(index);
        if (Kind(previous) != TK::Operator_OpenParenthesis &&
            Kind(previous) != TK::Operator_Comma)
        {
            return false;
        }
        auto captureEnd = FindBalancedEnd(index);
        if (captureEnd == std::numeric_limits<TokenIndex>::max() ||
            captureEnd >= closingEnd)
        {
            return false;
        }
        for (auto suffix = NextSignificant(captureEnd);
            suffix < closingEnd - 1;
            ++suffix)
        {
            auto kind = Kind(suffix);
            if (kind == TK::Operator_OpenBrace) {
                return true;
            }
            if (kind == TK::Operator_Comma || kind == TK::Operator_Semicolon) {
                return false;
            }
            if (IsOpenDelimiter(kind)) {
                auto balancedEnd = FindBalancedEnd(suffix);
                if (balancedEnd == std::numeric_limits<TokenIndex>::max() ||
                    balancedEnd >= closingEnd)
                {
                    return false;
                }
                suffix = balancedEnd - 1;
            }
        }
        return false;
    }

    void AddLambdaArguments(
        Node& invocation,
        TokenIndex opening,
        TokenIndex closingEnd)
    {
        for (auto index = opening + 1; index + 1 < closingEnd; ++index) {
            if (!LooksLikeLambdaArgument(index, closingEnd)) {
                continue;
            }
            auto cursor = index;
            auto* lambda = ParseLambdaExpression(cursor, closingEnd - 1);
            if (lambda && lambda->Tokens.End > index &&
                Kind(lambda->Tokens.End - 1) == TK::Operator_CloseBrace)
            {
                Append(invocation, *lambda);
                index = lambda->Tokens.End - 1;
            }
        }
    }

    void AddModuleDeclarationSyntax(
        Node& declaration,
        size_t begin,
        size_t end,
        bool import)
    {
        auto nameBegin = NextSignificant(begin + 1);
        auto nameEnd = end;
        if (nameEnd > nameBegin && Kind(nameEnd - 1) ==
            TK::Operator_Semicolon)
        {
            --nameEnd;
        }
        if (nameBegin >= nameEnd) {
            return;
        }
        if (Kind(nameBegin) == TK::Literal_String) {
            auto* header = MakeNode(NodeKind::HeaderName, nameBegin);
            header->Tokens.End = nameBegin + 1;
            Append(declaration, *header);
            return;
        }
        if (Kind(nameBegin) == TK::Operator_Less) {
            auto close = nameBegin + 1;
            while (close < nameEnd && Kind(close) != TK::Operator_Greater) {
                ++close;
            }
            if (close < nameEnd) {
                ++close;
            }
            auto* header = MakeNode(NodeKind::HeaderName, nameBegin);
            header->Tokens.End = close;
            Append(declaration, *header);
            return;
        }

        size_t colon = nameEnd;
        for (auto i = nameBegin; i < nameEnd; ++i) {
            if (Kind(i) == TK::Operator_Colon) {
                colon = i;
                break;
            }
        }
        if (nameBegin < colon) {
            auto* name = MakeNode(
                import ? NodeKind::ImportName : NodeKind::ModuleName,
                nameBegin);
            name->Tokens.End = colon;
            Append(declaration, *name);
        }
        if (colon < nameEnd) {
            auto* partition = MakeNode(NodeKind::ModulePartition, colon);
            partition->Tokens.End = nameEnd;
            Append(declaration, *partition);
        }
    }

    void
    AddConceptDeclarationSyntax(Node& declaration, size_t begin, size_t end)
    {
        auto name = NextSignificant(begin + 1);
        if (Kind(name) == TK::Identifier) {
            auto* conceptName = MakeNode(NodeKind::ConceptName, name);
            conceptName->Tokens.End = name + 1;
            Append(declaration, *conceptName);
        }
        auto equals = NextSignificant(name + 1);
        if (Kind(equals) != TK::Operator_Assign) {
            return;
        }
        auto expressionEnd = end;
        if (expressionEnd > equals &&
            Kind(expressionEnd - 1) == TK::Operator_Semicolon)
        {
            --expressionEnd;
        }
        auto expressionBegin = NextSignificant(equals + 1);
        if (expressionBegin >= expressionEnd) {
            return;
        }
        auto* constraint =
            MakeNode(NodeKind::ConstraintExpression, expressionBegin);
        constraint->Tokens.End = expressionEnd;
        AddExpressionRange(*constraint, expressionBegin, expressionEnd);
        Append(declaration, *constraint);
    }

    Node* ParseToSemicolon(NodeKind kind)
    {
        auto start = Cursor_;
        auto* node = MakeNode(kind, start);
        bool terminated = false;
        while (!AtEnd()) {
            if (IsOpenDelimiter(CurrentKind())) {
                ConsumeBalanced();
            } else if (CurrentKind() == TK::Operator_Semicolon) {
                ++Cursor_;
                terminated = true;
                break;
            } else if (CurrentKind() == TK::Operator_CloseBrace) {
                ErrorAt(start, "expected semicolon");
                break;
            } else {
                ++Cursor_;
            }
        }
        node->Tokens.End = Cursor_;
        if (!terminated) {
            AddMissingToken(*node, Cursor_);
        }
        if (kind == NodeKind::ModuleDeclaration) {
            AddModuleDeclarationSyntax(*node, start, Cursor_, false);
        } else if (kind == NodeKind::ImportDeclaration) {
            AddModuleDeclarationSyntax(*node, start, Cursor_, true);
        } else if (kind == NodeKind::ConceptDeclaration) {
            AddConceptDeclarationSyntax(*node, start, Cursor_);
        } else if (kind == NodeKind::StaticAssertDeclaration) {
            auto expressionBegin = NextSignificant(start + 1);
            auto expressionEnd = Cursor_;
            if (expressionEnd != 0 &&
                Kind(expressionEnd - 1) == TK::Operator_Semicolon)
            {
                --expressionEnd;
            }
            if (expressionBegin < expressionEnd) {
                auto expressionCursor = expressionBegin;
                if (auto* expression = ParseExpressionSyntax(
                    expressionCursor, expressionEnd))
                {
                    Append(*node, *expression);
                }
            }
        }
        AddDirectiveChildrenInGap(*node, start, Cursor_);
        return node;
    }

    void AddSeparatedElementSyntax(
        Node& element,
        NodeKind kind,
        size_t begin,
        size_t end)
    {
        if (kind == NodeKind::BaseSpecifier) {
            auto typeBegin = begin;
            while (typeBegin < end && (Kind(typeBegin) == TK::Keyword_public ||
                    Kind(typeBegin) == TK::Keyword_protected ||
                    Kind(typeBegin) == TK::Keyword_private ||
                    Kind(typeBegin) == TK::Keyword_virtual))
            {
                typeBegin = NextSignificant(typeBegin + 1);
            }
            if (typeBegin < end) {
                auto* type = MakeNode(NodeKind::BaseType, typeBegin);
                type->Tokens.End = end;
                AddTypeSyntaxChildren(*type, typeBegin, end);
                Append(element, *type);
            }
            return;
        }
        if (kind != NodeKind::Enumerator) {
            return;
        }
        auto name = NextSignificant(begin);
        if (Kind(name) == TK::Identifier) {
            auto* nameNode = MakeNode(NodeKind::EnumeratorName, name);
            nameNode->Tokens.End = name + 1;
            Append(element, *nameNode);
        }
        auto equals = NextSignificant(name + 1);
        if (Kind(equals) == TK::Operator_Assign) {
            auto valueBegin = NextSignificant(equals + 1);
            auto* value = MakeNode(NodeKind::EnumeratorValue, valueBegin);
            value->Tokens.End = end;
            AddExpressionRange(*value, valueBegin, end);
            Append(element, *value);
        }
    }

    void AddSeparatedElements(
        Node& list,
        size_t begin,
        size_t end,
        NodeKind elementKind,
        bool trackAngles = true)
    {
        size_t elementBegin = begin;
        int delimiterDepth = 0;
        int angleDepth = 0;
        for (auto i = begin; i <= end; ++i) {
            auto kind = Kind(i);
            if (i < end) {
                if (IsOpenDelimiter(kind)) {
                    ++delimiterDepth;
                } else if (kind == TK::Operator_CloseParenthesis ||
                    kind == TK::Operator_CloseBracket ||
                    kind == TK::Operator_CloseBrace)
                {
                    --delimiterDepth;
                } else if (trackAngles && kind == TK::Operator_Less) {
                    ++angleDepth;
                } else if (trackAngles && kind == TK::Operator_Greater &&
                    angleDepth > 0)
                {
                    --angleDepth;
                } else if (trackAngles && kind == TK::Operator_RightShift &&
                    angleDepth > 0)
                {
                    angleDepth = std::max(0, angleDepth - 2);
                }
            }
            bool separator = i < end && kind == TK::Operator_Comma &&
                delimiterDepth == 0 && angleDepth == 0;
            if (i != end && !separator) {
                continue;
            }
            if (elementBegin < i) {
                auto* element = MakeNode(elementKind, elementBegin);
                element->Tokens.End = i;
                AddSeparatedElementSyntax(
                    *element,
                    elementKind,
                    elementBegin,
                    i);
                Append(list, *element);
            }
            if (separator) {
                auto* comma = MakeNode(NodeKind::ListSeparator, i);
                comma->Tokens.End = i + 1;
                Append(list, *comma);
            }
            elementBegin = i + 1;
        }
    }

    Node* MakeTemplateArgumentList(size_t opening, size_t end)
    {
        auto* arguments = MakeNode(NodeKind::TemplateArgumentList, opening);
        arguments->Tokens.End = end;
        if (opening + 1 < end - 1) {
            AddSeparatedElements(
                *arguments,
                opening + 1,
                end - 1,
                NodeKind::TemplateArgument);
        }
        AddDirectiveChildrenInGap(*arguments, opening, end);
        return arguments;
    }

    Node* ParseExpressionKeywordStatement(NodeKind kind)
    {
        auto start = Cursor_;
        auto* node = MakeNode(kind, start);
        while (!AtEnd()) {
            if (IsOpenDelimiter(CurrentKind())) {
                ConsumeBalanced();
            } else if (CurrentKind() == TK::Operator_Semicolon) {
                ++Cursor_;
                break;
            } else if (CurrentKind() == TK::Operator_CloseBrace) {
                ErrorAt(start, "expected semicolon");
                break;
            } else {
                ++Cursor_;
            }
        }
        node->Tokens.End = Cursor_;
        auto expressionEnd = Cursor_;
        if (expressionEnd > start &&
            Kind(expressionEnd - 1) == TK::Operator_Semicolon)
        {
            --expressionEnd;
        }
        auto expressionCursor = NextSignificant(start + 1);
        if (auto* expression =
                ParseExpressionSyntax(expressionCursor, expressionEnd))
        {
            Append(*node, *expression);
        }
        AddDirectiveChildrenInGap(*node, start, Cursor_);
        return node;
    }

    Node* ParseUsingDeclaration()
    {
        auto start = Cursor_;
        auto* node = ParseToSemicolon(NodeKind::UsingDeclaration);
        auto name = NextSignificant(start) + 1;
        auto assign = NextSignificant(name + 1);
        if (Kind(name) == TK::Identifier && Kind(
            assign) == TK::Operator_Assign)
        {
            AddName(Text(name), NameKind::Type);
        }
        return node;
    }

    Node* ParseTypedefDeclaration()
    {
        auto start = Cursor_;
        auto* node = ParseToSemicolon(NodeKind::TypedefDeclaration);
        auto declarationBegin = NextSignificant(start + 1);
        // A typedef can name a function type: `typedef int Callback(int*);`.
        // Its parentheses are parameters, never a direct initializer.
        std::optional<TokenRange> parameters;
        auto declarator = FindDeclarationSpecifiersEnd(
            declarationBegin,
            node->Tokens.End);
        if (Kind(declarator) == TK::Identifier &&
            Kind(NextSignificant(declarator + 1)) ==
                TK::Operator_OpenParenthesis)
        {
            auto open = NextSignificant(declarator + 1);
            auto close = FindBalancedEnd(open);
            if (close != std::numeric_limits<TokenIndex>::max() &&
                close <= node->Tokens.End)
            {
                parameters = TokenRange{.Begin = open, .End = close};
            }
        }
        AddDeclarationSyntax(
            *node,
            declarationBegin,
            node->Tokens.End,
            parameters);
        RegisterDeclaredNames(*node, NameKind::Type);
        return node;
    }

    void RegisterTemplateTypeParameters(size_t begin, size_t end)
    {
        for (auto i = begin; i < end; ++i) {
            if (Kind(i) != TK::Keyword_typename && Kind(
                i) != TK::Keyword_class)
            {
                continue;
            }
            auto name = NextSignificant(i + 1);
            if (Kind(name) == TK::Identifier) {
                AddName(Text(name), NameKind::Type);
            }
        }
    }

    Node* ParseTemplateDeclaration()
    {
        auto start = Cursor_;
        auto* node = MakeNode(NodeKind::TemplateDeclaration, start);
        Cursor_ = NextSignificant(Cursor_) + 1;
        auto parametersBegin = Cursor_;

        if (CurrentKind() == TK::Operator_Less) {
            int depth = 0;
            do {
                if (IsOpenDelimiter(CurrentKind())) {
                    ConsumeBalanced();
                    continue;
                }
                switch (CurrentKind()) {
                    case TK::Operator_Less:
                        ++depth;
                        break;
                    case TK::Operator_Greater:
                        --depth;
                        break;
                    case TK::Operator_RightShift:
                        depth -= 2;
                        break;
                    default:
                        break;
                }
                ++Cursor_;
            } while (!AtEnd() && depth > 0);
            if (depth > 0) {
                ErrorAt(start, "unterminated template parameter list");
            }
        }

        auto parametersEnd = Cursor_;
        if (parametersBegin < parametersEnd &&
            Kind(parametersBegin) == TK::Operator_Less)
        {
            auto* parameters =
                MakeNode(NodeKind::TemplateParameterList, parametersBegin);
            parameters->Tokens.End = parametersEnd;
            if (parametersBegin + 1 < parametersEnd) {
                AddSeparatedElements(
                    *parameters,
                    parametersBegin + 1,
                    parametersEnd - 1,
                    NodeKind::TemplateParameter);
            }
            Append(*node, *parameters);
        }
        PushScope();
        RegisterTemplateTypeParameters(parametersBegin, parametersEnd);
        ParseTriviaBeforeChild(*node);
        if (CurrentKind() == TK::Keyword_requires) {
            Append(*node, *ParseRequiresClause());
            ParseTriviaBeforeChild(*node);
        }
        Node* declaration = nullptr;
        if (!AtEnd()) {
            declaration = ParseDeclaration();
            Append(*node, *declaration);
        }
        PopScope();
        if (declaration && (declaration->Kind == NodeKind::RecordDeclaration ||
                declaration->Kind == NodeKind::EnumDeclaration))
        {
            auto name = FindDeclaredTypeName(
                declaration->Tokens.Begin,
                declaration->Kind == NodeKind::EnumDeclaration);
            if (name < Tree_.Tokens_.Size()) {
                AddName(Text(name), NameKind::Type);
            }
        } else if (declaration &&
            declaration->Kind == NodeKind::UsingDeclaration)
        {
            auto name = NextSignificant(declaration->Tokens.Begin) + 1;
            if (Kind(name) == TK::Identifier) {
                AddName(Text(name), NameKind::Type);
            }
        }
        node->Tokens.End = Cursor_;
        AddDirectiveChildrenInGap(*node, start, Cursor_);
        return node;
    }

    Node* ParseRequiresClause()
    {
        auto start = Cursor_;
        auto* node = MakeNode(NodeKind::RequiresClause, start);
        ++Cursor_;

        int angleDepth = 0;
        bool consumedConstraint = false;
        while (!AtEnd()) {
            auto kind = CurrentKind();
            auto next = NextSignificant(Cursor_ + 1);
            bool startsDeclaration = angleDepth ==
                0 && consumedConstraint && (IsTypeKeyword(kind) ||
                kind == TK::Keyword_class ||
                kind == TK::Keyword_struct || kind == TK::Keyword_union ||
                kind == TK::Keyword_enum || kind == TK::Keyword_using ||
                kind == TK::Keyword_concept || (kind ==
                    TK::Identifier && (Kind(next) == TK::Identifier ||
                    Kind(next) == TK::Keyword_operator ||
                    (Kind(next) == TK::Operator_OpenParenthesis &&
                    Kind(PreviousSignificant(Cursor_)) !=
                                    TK::Operator_And &&
                    Kind(PreviousSignificant(Cursor_)) !=
                                    TK::Operator_Or))));
            // A dependent return type may be a template-id (optionally
            // followed by pointer/reference qualifiers) and an overload
            // name. Do not consume it as part of the constraint.
            if (!startsDeclaration && angleDepth == 0 && consumedConstraint &&
                kind == TK::Identifier && Kind(next) == TK::Operator_Less)
            {
                auto afterType = NextSignificant(SkipTemplateArguments(
                    next,
                    Tree_.Tokens_.Size()));
                while (Kind(afterType) == TK::Operator_Star ||
                    Kind(afterType) == TK::Operator_Ampersand ||
                    Kind(afterType) == TK::Operator_And)
                {
                    afterType = NextSignificant(afterType + 1);
                }
                startsDeclaration = Kind(afterType) == TK::Keyword_operator;
            }
            if (startsDeclaration || kind == TK::Operator_Semicolon ||
                kind == TK::Operator_OpenBrace)
            {
                break;
            }

            if (IsOpenDelimiter(kind)) {
                ConsumeBalanced();
                consumedConstraint = true;
                continue;
            }
            if (kind == TK::Operator_Less) {
                ++angleDepth;
            } else if (kind == TK::Operator_Greater && angleDepth > 0) {
                --angleDepth;
            } else if (kind == TK::Operator_RightShift && angleDepth > 0) {
                angleDepth = std::max(0, angleDepth - 2);
            }
            consumedConstraint = true;
            ++Cursor_;
        }

        node->Tokens.End = Cursor_;
        auto constraintBegin = NextSignificant(start + 1);
        if (constraintBegin < Cursor_) {
            auto* constraint =
                MakeNode(NodeKind::ConstraintExpression, constraintBegin);
            constraint->Tokens.End = Cursor_;
            AddExpressionRange(*constraint, constraintBegin, Cursor_);
            Append(*node, *constraint);
        }
        AddDirectiveChildrenInGap(*node, start, Cursor_);
        return node;
    }

    [[nodiscard]] size_t
    FindDeclaredTypeName(size_t start, bool isEnum = false) const
    {
        auto index = NextSignificant(start) + 1;
        if (isEnum && (Kind(index) == TK::Keyword_class ||
                Kind(index) == TK::Keyword_struct))
        {
            index = NextSignificant(index + 1);
        }
        while (index < Tree_.Tokens_.Size()) {
            if (Kind(index) == TK::Identifier) {
                if (Text(index) == "__attribute__" ||
                    Text(index) == "__declspec")
                {
                    auto opening = NextSignificant(index + 1);
                    if (Kind(opening) == TK::Operator_OpenParenthesis) {
                        auto end = FindBalancedEnd(opening);
                        if (end != std::numeric_limits<TokenIndex>::max()) {
                            index = NextSignificant(end);
                            continue;
                        }
                    }
                }
                return index;
            }
            if (Kind(index) == TK::Operator_OpenBracket) {
                auto end = FindBalancedEnd(index);
                if (end == std::numeric_limits<TokenIndex>::max()) {
                    return Tree_.Tokens_.Size();
                }
                index = NextSignificant(end);
                continue;
            }
            if (Kind(index) == TK::Operator_OpenBrace ||
                Kind(index) == TK::Operator_Semicolon)
            {
                break;
            }
            index = NextSignificant(index + 1);
        }
        return Tree_.Tokens_.Size();
    }

    Node* ParseNamespaceDeclaration()
    {
        auto start = Cursor_;
        auto* node = MakeNode(NodeKind::NamespaceDeclaration, start);
        while (!AtEnd() && CurrentKind() != TK::Operator_OpenBrace &&
            CurrentKind() != TK::Operator_Semicolon)
        {
            ++Cursor_;
        }
        if (CurrentKind() == TK::Operator_OpenBrace) {
            ++Cursor_;
            PushScope();
            ParseDeclarationSequence(*node, TK::Operator_CloseBrace);
            PopScope();
            if (CurrentKind() == TK::Operator_CloseBrace) {
                ++Cursor_;
            }
        } else if (CurrentKind() == TK::Operator_Semicolon) {
            ++Cursor_;
        } else {
            ErrorAt(start, "unterminated namespace declaration");
            AddMissingToken(*node, Cursor_);
        }
        node->Tokens.End = Cursor_;
        AddDirectiveChildrenInGap(*node, start, Cursor_);
        return node;
    }

    Node* ParseRecordDeclaration()
    {
        auto start = Cursor_;
        auto typeName = FindDeclaredTypeName(start);
        auto* node = MakeNode(NodeKind::RecordDeclaration, start);
        bool sawBaseClause = false;
        bool unknownHeader = false;
        size_t baseClauseBegin = Tree_.Tokens_.Size();
        int angleDepth = 0;
        while (!AtEnd() && CurrentKind() != TK::Operator_Semicolon &&
            CurrentKind() != TK::Operator_CloseBrace)
        {
            if (CurrentKind() == TK::Operator_OpenBrace) {
                if (angleDepth == 0) {
                    break;
                }
                ConsumeBalanced();
            } else if (CurrentKind() == TK::Operator_Less) {
                ++angleDepth;
                ++Cursor_;
            } else if (CurrentKind() == TK::Operator_Greater) {
                angleDepth = std::max(0, angleDepth - 1);
                ++Cursor_;
            } else if (CurrentKind() == TK::Operator_RightShift) {
                angleDepth = std::max(0, angleDepth - 2);
                ++Cursor_;
            } else if (CurrentKind() == TK::Operator_Colon && angleDepth == 0) {
                sawBaseClause = true;
                baseClauseBegin = Cursor_;
                ++Cursor_;
            } else if (CurrentKind() == TK::Identifier &&
                Kind(PreviousSignificant(Cursor_)) == TK::Identifier &&
                Text(Cursor_) != "final" && !IsExtensionAttribute(Cursor_))
            {
                // A macro may appear between `class` and its name. Do not
                // parse the following `{...}` as a variable initializer.
                unknownHeader = true;
                ++Cursor_;
            } else if (CurrentKind() == TK::Operator_OpenParenthesis) {
                auto previous = PreviousSignificant(Cursor_);
                if (angleDepth == 0 && !sawBaseClause &&
                    Kind(previous) != TK::Keyword_alignas &&
                    Kind(previous) != TK::Keyword_decltype &&
                    !IsExtensionAttribute(previous))
                {
                    Cursor_ = start;
                    return ParseGeneralDeclaration();
                }
                ConsumeBalanced();
            } else if (CurrentKind() == TK::Operator_OpenBracket) {
                ConsumeBalanced();
            } else {
                ++Cursor_;
            }
        }
        if (unknownHeader && CurrentKind() != TK::Operator_OpenBrace) {
            Cursor_ = start;
            return ParseGeneralDeclaration();
        }
        if (unknownHeader) {
            AddUnparsedSyntax(*node, start + 1, Cursor_);
        }
        if (!unknownHeader && baseClauseBegin < Tree_.Tokens_.Size()) {
            auto* bases = MakeNode(NodeKind::BaseClause, baseClauseBegin);
            bases->Tokens.End = Cursor_;
            AddSeparatedElements(
                *bases,
                baseClauseBegin + 1,
                Cursor_,
                NodeKind::BaseSpecifier);
            Append(*node, *bases);
        }
        if (CurrentKind() == TK::Operator_OpenBrace) {
            if (!unknownHeader && typeName < Tree_.Tokens_.Size()) {
                AddName(Text(typeName), NameKind::Type);
            }
            ++Cursor_;
            PushScope();
            RecordNames_.push_back(
                !unknownHeader && typeName < Tree_.Tokens_.Size()
                    ? Text(typeName)
                    : std::string_view{});
            if (!unknownHeader && typeName < Tree_.Tokens_.Size()) {
                AddName(Text(typeName), NameKind::Type);
            }
            ParseDeclarationSequence(*node, TK::Operator_CloseBrace);
            RecordNames_.pop_back();
            PopScope();
            if (CurrentKind() == TK::Operator_CloseBrace) {
                ++Cursor_;
            }
            while (!AtEnd() && CurrentKind() != TK::Operator_Semicolon &&
                CurrentKind() != TK::Operator_CloseBrace)
            {
                if (IsOpenDelimiter(CurrentKind())) {
                    ConsumeBalanced();
                } else {
                    ++Cursor_;
                }
            }
        }
        if (CurrentKind() == TK::Operator_Semicolon) {
            ++Cursor_;
            if (!node->FirstChild && typeName < Tree_.Tokens_.Size()) {
                AddName(Text(typeName), NameKind::Type);
            }
        } else {
            ErrorAt(start, "expected semicolon after record declaration");
            AddMissingToken(*node, Cursor_);
        }
        node->Tokens.End = Cursor_;
        AddDirectiveChildrenInGap(*node, start, Cursor_);
        return node;
    }

    Node* ParseEnumDeclaration()
    {
        auto start = Cursor_;
        auto typeName = FindDeclaredTypeName(start, true);
        if (typeName < Tree_.Tokens_.Size()) {
            AddName(Text(typeName), NameKind::Type);
        }
        auto* node = MakeNode(NodeKind::EnumDeclaration, start);
        while (!AtEnd() && CurrentKind() != TK::Operator_OpenBrace &&
            CurrentKind() != TK::Operator_Semicolon &&
            CurrentKind() != TK::Operator_CloseBrace)
        {
            ++Cursor_;
        }
        if (CurrentKind() == TK::Operator_OpenBrace) {
            auto bodyStart = Cursor_;
            auto bodyEnd = FindBalancedEnd(bodyStart);
            if (bodyEnd == std::numeric_limits<TokenIndex>::max()) {
                ConsumeBalanced();
            } else {
                auto* enumerators =
                    MakeNode(NodeKind::EnumeratorList, bodyStart);
                enumerators->Tokens.End = bodyEnd;
                AddSeparatedElements(
                    *enumerators,
                    bodyStart + 1,
                    bodyEnd - 1,
                    NodeKind::Enumerator);
                AddDirectiveChildrenInGap(*enumerators, bodyStart, bodyEnd);
                Append(*node, *enumerators);
                Cursor_ = bodyEnd;
            }
        }
        while (!AtEnd() && CurrentKind() != TK::Operator_Semicolon &&
            CurrentKind() != TK::Operator_CloseBrace)
        {
            if (IsOpenDelimiter(CurrentKind())) {
                ConsumeBalanced();
            } else {
                ++Cursor_;
            }
        }
        if (CurrentKind() == TK::Operator_Semicolon) {
            ++Cursor_;
        }
        node->Tokens.End = Cursor_;
        AddDirectiveChildrenInGap(*node, start, Cursor_);
        return node;
    }

    bool LooksLikeLinkageSpecification(size_t externIndex) const
    {
        auto next = NextSignificant(externIndex + 1);
        if (Kind(next) != TK::Literal_String) {
            return false;
        }
        next = NextSignificant(next + 1);
        return Kind(next) == TK::Operator_OpenBrace;
    }

    Node* ParseLinkageSpecification()
    {
        auto start = Cursor_;
        auto* node = MakeNode(NodeKind::LinkageSpecification, start);
        while (!AtEnd() && CurrentKind() != TK::Operator_OpenBrace) {
            ++Cursor_;
        }
        if (CurrentKind() == TK::Operator_OpenBrace) {
            ++Cursor_;
            ParseDeclarationSequence(*node, TK::Operator_CloseBrace);
            if (CurrentKind() == TK::Operator_CloseBrace) {
                ++Cursor_;
            }
        }
        node->Tokens.End = Cursor_;
        AddDirectiveChildrenInGap(*node, start, Cursor_);
        return node;
    }

    Node* ParseAccessSpecifier()
    {
        auto* node = MakeNode(NodeKind::AccessSpecifier, Cursor_);
        while (!AtEnd()) {
            auto kind = CurrentKind();
            ++Cursor_;
            if (kind == TK::Operator_Colon) {
                break;
            }
        }
        node->Tokens.End = Cursor_;
        return node;
    }

    Node* ParseExportDeclaration()
    {
        auto start = Cursor_;
        auto* node = MakeNode(NodeKind::ExportDeclaration, start);
        Cursor_ = NextSignificant(Cursor_) + 1;
        ParseTriviaBeforeChild(*node);

        if (CurrentKind() == TK::Operator_OpenBrace) {
            ++Cursor_;
            ParseDeclarationSequence(*node, TK::Operator_CloseBrace);
            if (CurrentKind() == TK::Operator_CloseBrace) {
                ++Cursor_;
            }
        } else if (!AtEnd()) {
            Append(*node, *ParseDeclaration());
        }

        node->Tokens.End = Cursor_;
        AddDirectiveChildrenInGap(*node, start, Cursor_);
        return node;
    }

    [[nodiscard]] bool IsExtensionAttribute(size_t index) const
    {
        return Kind(index) == TK::Identifier &&
            (Text(index) == "__attribute__" || Text(index) == "__declspec");
    }

    [[nodiscard]] bool IsCallingConvention(size_t index) const
    {
        if (Kind(index) != TK::Identifier) {
            return false;
        }
        auto text = Text(index);
        return text == "__cdecl" || text ==
            "__stdcall" || text == "__fastcall" ||
            text == "__thiscall" || text == "__vectorcall" ||
            text == "__regcall" || text == "__clrcall";
    }

    [[nodiscard]] size_t
    FindExtensionAttributeEnd(size_t index, size_t end) const
    {
        if (!IsExtensionAttribute(index)) {
            return index;
        }
        auto opening = NextSignificant(index + 1);
        if (Kind(opening) != TK::Operator_OpenParenthesis) {
            return index + 1;
        }
        auto attributeEnd = FindBalancedEnd(opening);
        return attributeEnd == std::numeric_limits<TokenIndex>::max() ||
            attributeEnd > end
            ? end
            : attributeEnd;
    }

    [[nodiscard]] bool IsDeclarationSpecifier(TK kind) const
    {
        return IsTypeKeyword(kind) || kind == TK::Keyword_alignas ||
            kind == TK::Keyword_explicit ||
            kind == TK::Keyword_friend || kind == TK::Keyword_virtual;
    }

    [[nodiscard]] size_t SkipTemplateArguments(size_t index, size_t end) const
    {
        if (Kind(index) != TK::Operator_Less) {
            return index;
        }
        int depth = 0;
        for (; index < end; ++index) {
            if (Kind(index) == TK::Operator_Less) {
                ++depth;
            } else if (Kind(index) == TK::Operator_Greater) {
                if (--depth == 0) {
                    return index + 1;
                }
            } else if (Kind(index) == TK::Operator_RightShift) {
                depth -= 2;
                if (depth <= 0) {
                    return index + 1;
                }
            } else if (IsOpenDelimiter(Kind(index))) {
                auto balancedEnd = FindBalancedEnd(index);
                if (balancedEnd == std::numeric_limits<TokenIndex>::max()) {
                    return end;
                }
                index = balancedEnd - 1;
            }
        }
        return end;
    }

    [[nodiscard]] bool
    LooksLikeConstructorDeclarator(size_t index, size_t end) const
    {
        if (Kind(index) != TK::Identifier) {
            return false;
        }
        auto firstName = Text(index);
        auto next = NextSignificant(index + 1);
        if (Kind(next) == TK::Operator_OpenParenthesis) {
            return !RecordNames_.empty() && Text(index) == RecordNames_.back();
        }
        if (Kind(next) != TK::Operator_ScopeResolution) {
            return false;
        }
        auto name = NextSignificant(next + 1);
        if (Kind(name) == TK::Operator_Complement) {
            name = NextSignificant(name + 1);
        }
        auto opening = NextSignificant(name + 1);
        return name < end && Kind(name) == TK::Identifier &&
            Text(name) == firstName &&
            Kind(opening) == TK::Operator_OpenParenthesis;
    }

    [[nodiscard]] size_t
    FindDeclarationSpecifiersEnd(size_t begin, size_t end) const
    {
        auto index = NextSignificant(begin);
        bool foundType = false;
        while (index < end) {
            auto kind = Kind(index);
            if (IsDeclarationSpecifier(kind)) {
                foundType |= IsFundamentalTypeKeyword(kind);
                index = NextSignificant(index + 1);
                if ((kind == TK::Keyword_decltype ||
                        kind == TK::Keyword_alignas) &&
                    Kind(index) == TK::Operator_OpenParenthesis)
                {
                    auto balancedEnd = FindBalancedEnd(index);
                    if (balancedEnd == std::numeric_limits<TokenIndex>::max()) {
                        return end;
                    }
                    index = NextSignificant(balancedEnd);
                }
                continue;
            }
            if (kind == TK::Operator_OpenBracket &&
                Kind(index + 1) == TK::Operator_OpenBracket)
            {
                auto balancedEnd = FindBalancedEnd(index);
                if (balancedEnd == std::numeric_limits<TokenIndex>::max()) {
                    return end;
                }
                index = NextSignificant(balancedEnd);
                continue;
            }
            if (IsExtensionAttribute(index)) {
                index = NextSignificant(FindExtensionAttributeEnd(index, end));
                continue;
            }
            if (IsCallingConvention(index)) {
                index = NextSignificant(index + 1);
                continue;
            }
            break;
        }

        if (index >= end || foundType ||
            LooksLikeConstructorDeclarator(index, end) ||
            Kind(index) == TK::Keyword_operator ||
            Kind(index) == TK::Operator_Complement)
        {
            return index;
        }
        if (Kind(index) == TK::Operator_ScopeResolution) {
            auto component = NextSignificant(index + 1);
            if (Kind(component) != TK::Identifier) {
                return index;
            }
            index = component;
        }
        if (Kind(index) != TK::Identifier) {
            return index;
        }

        // Consume a user-defined, qualified type name and optional template
        // arguments. The following token begins the declarator.
        index = NextSignificant(index + 1);
        if (Kind(index) == TK::Operator_Less) {
            index = NextSignificant(SkipTemplateArguments(index, end));
        }
        while (Kind(index) == TK::Operator_ScopeResolution) {
            auto component = NextSignificant(index + 1);
            if (Kind(component) != TK::Identifier) {
                break;
            }
            index = NextSignificant(component + 1);
            if (Kind(index) == TK::Operator_Less) {
                index = NextSignificant(SkipTemplateArguments(index, end));
            }
        }
        return index;
    }

    [[nodiscard]] size_t FindMemberPointerEnd(size_t begin, size_t end) const
    {
        auto index = NextSignificant(begin);
        if (Kind(index) != TK::Identifier) {
            return begin;
        }
        index = NextSignificant(index + 1);
        if (Kind(index) == TK::Operator_Less) {
            index = NextSignificant(SkipTemplateArguments(index, end));
        }
        while (index < end && Kind(index) == TK::Operator_ScopeResolution) {
            auto component = NextSignificant(index + 1);
            if (Kind(component) == TK::Operator_Star) {
                return component + 1;
            }
            if (Kind(component) != TK::Identifier) {
                return begin;
            }
            index = NextSignificant(component + 1);
            if (Kind(index) == TK::Operator_Less) {
                index = NextSignificant(SkipTemplateArguments(index, end));
            }
        }
        return begin;
    }

    [[nodiscard]] size_t FindDeclaratorName(
        size_t begin,
        size_t end,
        std::optional<TokenRange> parameters) const
    {
        if (parameters && !parameters->Empty()) {
            auto candidate = parameters->Begin;
            for (auto i = begin; i < parameters->Begin; ++i) {
                if (Kind(i) == TK::Operator_OpenBracket &&
                    Kind(i + 1) == TK::Operator_OpenBracket)
                {
                    auto attributeEnd = FindBalancedEnd(i);
                    if (attributeEnd ==
                        std::numeric_limits<TokenIndex>::max() ||
                        attributeEnd > parameters->Begin)
                    {
                        break;
                    }
                    i = attributeEnd - 1;
                } else if (Kind(i) == TK::Keyword_operator) {
                    return i;
                } else if (Kind(i) == TK::Identifier) {
                    candidate = i;
                }
            }
            if (candidate < parameters->Begin) {
                return candidate;
            }
            return end;
        }
        auto memberPointerEnd = FindMemberPointerEnd(begin, end);
        if (memberPointerEnd == begin &&
            Kind(begin) == TK::Operator_OpenParenthesis)
        {
            memberPointerEnd = FindMemberPointerEnd(begin + 1, end);
        }
        if (memberPointerEnd != begin) {
            begin = memberPointerEnd;
        }
        for (auto i = begin; i < end; ++i) {
            if (Kind(i) == TK::Operator_OpenBracket &&
                Kind(i + 1) == TK::Operator_OpenBracket)
            {
                auto attributeEnd = FindBalancedEnd(i);
                if (attributeEnd == std::numeric_limits<TokenIndex>::max() ||
                    attributeEnd > end)
                {
                    return end;
                }
                i = attributeEnd - 1;
                continue;
            }
            if (Kind(i) == TK::Identifier) {
                return i;
            }
            if (Kind(i) == TK::Keyword_operator) {
                return i;
            }
        }
        return end;
    }

    [[nodiscard]] size_t FindInitializerStart(
        size_t begin,
        size_t end,
        size_t name,
        bool functionDeclarator) const
    {
        int parenthesisDepth = 0;
        int bracketDepth = 0;
        for (auto i = begin; i < end; ++i) {
            auto kind = Kind(i);
            if (kind == TK::Operator_OpenParenthesis) {
                if (!functionDeclarator && i > name && parenthesisDepth == 0 &&
                    bracketDepth == 0)
                {
                    return i;
                }
                ++parenthesisDepth;
            } else if (kind == TK::Operator_CloseParenthesis) {
                --parenthesisDepth;
            } else if (kind == TK::Operator_OpenBracket) {
                ++bracketDepth;
            } else if (kind == TK::Operator_CloseBracket) {
                --bracketDepth;
            } else if (parenthesisDepth == 0 && bracketDepth == 0 && (kind ==
                TK::Operator_Assign || (!functionDeclarator &&
                    kind == TK::Operator_OpenBrace)))
            {
                if (kind == TK::Operator_Assign &&
                    Kind(PreviousSignificant(i)) == TK::Keyword_operator)
                {
                    continue;
                }
                return i;
            }
        }
        return end;
    }

    void AddParameterDeclarations(Node& list, TokenRange parameters)
    {
        size_t segmentBegin = parameters.Begin + 1;
        int delimiterDepth = 0;
        int angleDepth = 0;
        for (auto i = segmentBegin; i + 1 <= parameters.End; ++i) {
            bool atEnd = i + 1 == parameters.End;
            auto kind = Kind(i);
            if (!atEnd) {
                if (IsOpenDelimiter(kind)) {
                    ++delimiterDepth;
                } else if (kind == TK::Operator_CloseParenthesis ||
                    kind == TK::Operator_CloseBracket ||
                    kind == TK::Operator_CloseBrace)
                {
                    --delimiterDepth;
                } else if (kind == TK::Operator_Less) {
                    ++angleDepth;
                } else if (kind == TK::Operator_Greater && angleDepth > 0) {
                    --angleDepth;
                } else if (kind == TK::Operator_RightShift && angleDepth > 0) {
                    angleDepth = std::max(0, angleDepth - 2);
                }
            }
            if (!atEnd && (kind != TK::Operator_Comma || delimiterDepth != 0 ||
                    angleDepth != 0))
            {
                continue;
            }

            if (segmentBegin < i) {
                auto* parameter =
                    MakeNode(NodeKind::ParameterDeclaration, segmentBegin);
                parameter->Tokens.End = i;
                AddDeclarationSyntax(*parameter, segmentBegin, i);
                Append(list, *parameter);
            }
            if (!atEnd) {
                auto* comma = MakeNode(NodeKind::ListSeparator, i);
                comma->Tokens.End = i + 1;
                Append(list, *comma);
            }
            segmentBegin = i + 1;
        }
    }

    [[nodiscard]] bool HasEmptyArgumentSlot(
        TokenIndex opening,
        TokenIndex closingEnd) const
    {
        bool missingArgument = false;
        bool expectingArgument = true;
        bool sawSeparator = false;
        for (auto i = NextSignificant(opening + 1);
            i + 1 < closingEnd;
            i = NextSignificant(i + 1))
        {
            if (Kind(i) == TK::Operator_Comma) {
                missingArgument |= expectingArgument;
                expectingArgument = true;
                sawSeparator = true;
            } else {
                expectingArgument = false;
                if (IsOpenDelimiter(Kind(i))) {
                    auto nested = FindBalancedEnd(i);
                    if (nested != std::numeric_limits<TokenIndex>::max() &&
                        nested < closingEnd)
                    {
                        i = nested - 1;
                    }
                }
            }
        }
        return missingArgument || (sawSeparator && expectingArgument);
    }

    void AddArgumentList(Node& parent, size_t opening, size_t closingEnd)
    {
        auto* arguments = MakeNode(NodeKind::ArgumentList, opening);
        arguments->Tokens.End = closingEnd;
        auto argumentCursor = NextSignificant(opening + 1);
        // Empty arguments are not expressions. A function-like macro can
        // accept them, so keep the entire list as recovered syntax instead
        // of inventing an expression for a bare separator.
        if (HasEmptyArgumentSlot(opening, closingEnd)) {
            AddUnparsedSyntax(*arguments, opening + 1, closingEnd - 1);
            AddDirectiveChildrenInGap(*arguments, opening, closingEnd);
            Append(parent, *arguments);
            return;
        }
        while (argumentCursor + 1 < closingEnd) {
            auto before = argumentCursor;
            Node* expression = nullptr;
            if (Kind(before) == TK::Operator_OpenBrace) {
                auto braceEnd = FindBalancedEnd(before);
                if (braceEnd != std::numeric_limits<TokenIndex>::max() &&
                    braceEnd < closingEnd)
                {
                    auto following = NextSignificant(braceEnd);
                    if (Kind(following) != TK::Operator_Comma &&
                        following != closingEnd - 1)
                    {
                        braceEnd = before;
                    }
                }
                if (braceEnd > before && braceEnd < closingEnd) {
                    // Top-level semicolons cannot occur in a braced
                    // initializer. A macro argument like `{ f(); }` is a
                    // statement block; semicolons inside nested delimiters
                    // (lambdas, calls, aggregates) are not decisive.
                    bool statementBlock = false;
                    for (auto i = before + 1; i < braceEnd - 1; ++i) {
                        if (Kind(i) == TK::Operator_Semicolon) {
                            statementBlock = true;
                            break;
                        }
                        if (IsOpenDelimiter(Kind(i))) {
                            auto nestedEnd = FindBalancedEnd(i);
                            if (nestedEnd <= braceEnd - 1) {
                                i = nestedEnd - 1;
                            }
                        }
                    }
                    if (statementBlock) {
                        auto savedCursor = Cursor_;
                        Cursor_ = before;
                        ++BranchParseDepth_;
                        expression = ParseCompoundStatement();
                        --BranchParseDepth_;
                        Cursor_ = savedCursor;
                        argumentCursor = braceEnd;
                    }
                }
            }
            if (!expression) {
                expression = ParseExpressionSyntax(
                    argumentCursor,
                    closingEnd - 1,
                    2);
            }
            if (expression) {
                auto* argument = MakeNode(NodeKind::Argument, before);
                argument->Tokens.End = expression->Tokens.End;
                Append(*argument, *expression);
                Append(*arguments, *argument);
            }
            argumentCursor = NextSignificant(argumentCursor);
            if (Kind(argumentCursor) == TK::Operator_Comma) {
                auto* comma = MakeNode(NodeKind::ListSeparator, argumentCursor);
                comma->Tokens.End = argumentCursor + 1;
                Append(*arguments, *comma);
                argumentCursor = NextSignificant(argumentCursor + 1);
            } else if (argumentCursor == before) {
                ++argumentCursor;
            } else {
                break;
            }
        }
        AddDirectiveChildrenInGap(*arguments, opening, closingEnd);
        Append(parent, *arguments);
    }

    void AddBracedInitializerElements(
        Node& braced,
        size_t opening,
        size_t closingEnd)
    {
        auto* list = MakeNode(NodeKind::InitializerList, opening);
        list->Tokens.End = closingEnd;
        auto elementCursor = NextSignificant(opening + 1);
        while (elementCursor + 1 < closingEnd) {
            auto before = elementCursor;
            auto* expression =
                ParseExpressionSyntax(elementCursor, closingEnd - 1, 2);
            if (expression) {
                auto* element = MakeNode(NodeKind::InitializerElement, before);
                element->Tokens.End = expression->Tokens.End;
                Append(*element, *expression);
                Append(*list, *element);
            }
            elementCursor = NextSignificant(elementCursor);
            if (Kind(elementCursor) == TK::Operator_Comma) {
                auto* comma = MakeNode(NodeKind::ListSeparator, elementCursor);
                comma->Tokens.End = elementCursor + 1;
                Append(*list, *comma);
                elementCursor = NextSignificant(elementCursor + 1);
            } else if (elementCursor == before) {
                ++elementCursor;
            } else {
                break;
            }
        }
        AddDirectiveChildrenInGap(*list, opening, closingEnd);
        Append(braced, *list);
    }

    void AddInitializerExpression(Node& initializer, size_t begin, size_t end)
    {
        auto expressionBegin = NextSignificant(begin);
        if (Kind(expressionBegin) == TK::Operator_Assign) {
            expressionBegin = NextSignificant(expressionBegin + 1);
        }
        if (expressionBegin >= end) {
            return;
        }
        if (Kind(expressionBegin) == TK::Operator_OpenBrace) {
            auto* braced =
                MakeNode(NodeKind::BracedInitializer, expressionBegin);
            braced->Tokens.End = end;
            AddBracedInitializerElements(*braced, expressionBegin, end);
            Append(initializer, *braced);
            return;
        }
        if (auto* expression = ParseExpressionSyntax(expressionBegin, end)) {
            Append(initializer, *expression);
        }
        AddUnparsedSyntax(initializer, expressionBegin, end);
    }

    [[nodiscard]] bool
    AddStructuredBindingSyntax(Node& initDeclarator, size_t begin, size_t end)
    {
        auto bindingBegin = begin;
        bool hasReference = false;
        if (Kind(bindingBegin) == TK::Operator_Ampersand ||
            Kind(bindingBegin) == TK::Operator_And)
        {
            hasReference = true;
            ++bindingBegin;
        }
        if (Kind(bindingBegin) != TK::Operator_OpenBracket) {
            return false;
        }
        auto bindingEnd = FindBalancedEnd(bindingBegin);
        if (bindingEnd == std::numeric_limits<TokenIndex>::max() ||
            bindingEnd > end)
        {
            return false;
        }

        auto* declarator = MakeNode(NodeKind::Declarator, begin);
        declarator->Tokens.End = bindingEnd;
        Append(initDeclarator, *declarator);
        if (hasReference) {
            auto* reference = MakeNode(NodeKind::PointerOperator, begin);
            reference->Tokens.End = begin + 1;
            Append(*declarator, *reference);
        }
        auto* binding = MakeNode(NodeKind::StructuredBinding, bindingBegin);
        binding->Tokens.End = bindingEnd;
        int nestedDepth = 0;
        for (auto i = bindingBegin + 1; i + 1 < bindingEnd; ++i) {
            auto kind = Kind(i);
            if (IsOpenDelimiter(kind)) {
                ++nestedDepth;
            } else if (kind == TK::Operator_CloseParenthesis ||
                kind == TK::Operator_CloseBracket ||
                kind == TK::Operator_CloseBrace)
            {
                --nestedDepth;
            } else if (nestedDepth == 0 && kind == TK::Operator_Comma) {
                auto* comma = MakeNode(NodeKind::ListSeparator, i);
                comma->Tokens.End = i + 1;
                Append(*binding, *comma);
            } else if (nestedDepth == 0 && kind == TK::Identifier) {
                auto* name = MakeNode(NodeKind::DeclaratorName, i);
                name->Tokens.End = i + 1;
                Append(*binding, *name);
            }
        }
        Append(*declarator, *binding);
        if (bindingEnd < end) {
            auto* initializer = MakeNode(NodeKind::Initializer, bindingEnd);
            initializer->Tokens.End = end;
            AddInitializerExpression(*initializer, bindingEnd, end);
            Append(initDeclarator, *initializer);
        }
        return true;
    }

    void AddConstructorInitializers(Node& function, size_t begin, size_t end)
    {
        auto* list = MakeNode(NodeKind::ConstructorInitializerList, begin);
        list->Tokens.End = end;
        Append(function, *list);

        size_t initializerBegin = begin + 1;
        int delimiterDepth = 0;
        for (auto i = initializerBegin; i <= end; ++i) {
            auto kind = Kind(i);
            if (i < end && delimiterDepth == 0 && kind == TK::Operator_Less) {
                auto templateEnd = SkipTemplateArguments(i, end);
                auto afterTemplate = NextSignificant(templateEnd);
                if (templateEnd > i + 1 && afterTemplate < end && (Kind(
                    afterTemplate) == TK::Operator_OpenParenthesis ||
                        Kind(afterTemplate) == TK::Operator_OpenBrace))
                {
                    i = templateEnd - 1;
                    continue;
                }
            }
            if (i < end) {
                if (IsOpenDelimiter(kind)) {
                    ++delimiterDepth;
                } else if (kind == TK::Operator_CloseParenthesis ||
                    kind == TK::Operator_CloseBracket ||
                    kind == TK::Operator_CloseBrace)
                {
                    --delimiterDepth;
                }
            }
            if (i != end && (kind !=
                TK::Operator_Comma || delimiterDepth != 0))
            {
                continue;
            }
            if (initializerBegin < i) {
                auto* initializer = MakeNode(
                    NodeKind::ConstructorInitializer,
                    initializerBegin);
                initializer->Tokens.End = i;
                bool hasTemplateName = false;
                for (auto cursor = initializerBegin; cursor < i; ++cursor) {
                    if (Kind(cursor) == TK::Operator_Less) {
                        hasTemplateName = true;
                        break;
                    }
                    if (IsOpenDelimiter(Kind(cursor))) {
                        break;
                    }
                }
                auto expressionCursor =
                    static_cast<TokenIndex>(initializerBegin);
                auto* expression = hasTemplateName
                    ? nullptr
                    : ParseExpressionSyntax(
                        expressionCursor,
                        static_cast<TokenIndex>(i));
                if (expression && NextSignificant(expressionCursor) >= i) {
                    Append(*initializer, *expression);
                } else {
                    for (auto cursor = initializerBegin; cursor < i; ++cursor) {
                        if (Kind(cursor) != TK::Operator_Less) {
                            if (IsOpenDelimiter(Kind(cursor))) {
                                break;
                            }
                            continue;
                        }
                        auto templateEnd = SkipTemplateArguments(cursor, i);
                        if (templateEnd > cursor + 1 && templateEnd <= i) {
                            Append(
                                *initializer,
                                *MakeTemplateArgumentList(cursor, templateEnd));
                        }
                        break;
                    }
                }
                Append(*list, *initializer);
            }
            if (i != end) {
                auto* comma = MakeNode(NodeKind::ListSeparator, i);
                comma->Tokens.End = i + 1;
                Append(*list, *comma);
            }
            initializerBegin = i + 1;
        }
    }

    void AddFunctionSuffixSyntax(Node& function, size_t begin, size_t end)
    {
        auto index = NextSignificant(begin);
        while (index < end) {
            auto kind = Kind(index);
            if (IsExtensionAttribute(index)) {
                auto attributeEnd = FindExtensionAttributeEnd(index, end);
                auto* attribute =
                    MakeNode(NodeKind::ExtensionAttributeSpecifier, index);
                attribute->Tokens.End = attributeEnd;
                Append(function, *attribute);
                index = NextSignificant(attributeEnd);
                continue;
            }
            if (IsCallingConvention(index)) {
                auto* convention =
                    MakeNode(NodeKind::CallingConventionSpecifier, index);
                convention->Tokens.End = index + 1;
                Append(function, *convention);
                index = NextSignificant(index + 1);
                continue;
            }
            if (kind == TK::Keyword_const || kind == TK::Keyword_volatile ||
                kind == TK::Operator_Ampersand || kind == TK::Operator_And ||
                    (kind == TK::Identifier && (Text(
                        index) == "final" || Text(index) == "override")))
            {
                auto* qualifier = MakeNode(NodeKind::FunctionQualifier, index);
                qualifier->Tokens.End = index + 1;
                Append(function, *qualifier);
                index = NextSignificant(index + 1);
                continue;
            }
            if (kind == TK::Keyword_noexcept) {
                auto exceptionEnd = index + 1;
                auto opening = NextSignificant(exceptionEnd);
                if (Kind(opening) == TK::Operator_OpenParenthesis) {
                    auto balancedEnd = FindBalancedEnd(opening);
                    if (balancedEnd != std::numeric_limits<TokenIndex>::max() &&
                        balancedEnd <= end)
                    {
                        exceptionEnd = balancedEnd;
                    }
                }
                auto* exception =
                    MakeNode(NodeKind::ExceptionSpecification, index);
                exception->Tokens.End = exceptionEnd;
                Append(function, *exception);
                index = NextSignificant(exceptionEnd);
                continue;
            }
            if (kind == TK::Operator_OpenBracket &&
                Kind(index + 1) == TK::Operator_OpenBracket)
            {
                auto attributeEnd = FindBalancedEnd(index);
                if (attributeEnd == std::numeric_limits<TokenIndex>::max() ||
                    attributeEnd > end)
                {
                    break;
                }
                auto* attribute = MakeNode(NodeKind::AttributeSpecifier, index);
                attribute->Tokens.End = attributeEnd;
                Append(function, *attribute);
                index = NextSignificant(attributeEnd);
                continue;
            }
            if (kind == TK::Operator_IndirectClassMemberAccess) {
                auto trailingEnd = index + 1;
                int delimiterDepth = 0;
                while (trailingEnd < end) {
                    auto trailingKind = Kind(trailingEnd);
                    if (IsOpenDelimiter(trailingKind)) {
                        ++delimiterDepth;
                    } else if (trailingKind == TK::Operator_CloseParenthesis ||
                        trailingKind == TK::Operator_CloseBracket ||
                        trailingKind == TK::Operator_CloseBrace)
                    {
                        --delimiterDepth;
                    } else if (delimiterDepth == 0 &&
                        trailingKind == TK::Keyword_requires)
                    {
                        break;
                    }
                    ++trailingEnd;
                }
                auto* trailing = MakeNode(NodeKind::TrailingReturnType, index);
                trailing->Tokens.End = trailingEnd;
                Append(function, *trailing);
                index = NextSignificant(trailingEnd);
                continue;
            }
            if (kind == TK::Keyword_requires) {
                auto* requiresClause =
                    MakeNode(NodeKind::RequiresClause, index);
                requiresClause->Tokens.End = end;
                auto constraintBegin = NextSignificant(index + 1);
                if (constraintBegin < end) {
                    auto* constraint = MakeNode(
                        NodeKind::ConstraintExpression,
                        constraintBegin);
                    constraint->Tokens.End = end;
                    AddExpressionRange(*constraint, constraintBegin, end);
                    Append(*requiresClause, *constraint);
                }
                Append(function, *requiresClause);
                break;
            }
            if (kind == TK::Operator_Colon) {
                AddConstructorInitializers(function, index, end);
                break;
            }
            index = NextSignificant(index + 1);
        }
    }

    void AddTypeSyntaxChildren(Node& node, size_t begin, size_t end)
    {
        for (auto i = begin; i < end; ++i) {
            if (Kind(i) == TK::Operator_OpenBracket &&
                Kind(i + 1) == TK::Operator_OpenBracket)
            {
                auto attributeEnd = FindBalancedEnd(i);
                if (attributeEnd == std::numeric_limits<TokenIndex>::max() ||
                    attributeEnd > end)
                {
                    break;
                }
                auto* attribute = MakeNode(NodeKind::AttributeSpecifier, i);
                attribute->Tokens.End = attributeEnd;
                Append(node, *attribute);
                i = attributeEnd - 1;
            } else if (IsExtensionAttribute(i)) {
                auto attributeEnd = FindExtensionAttributeEnd(i, end);
                auto* attribute =
                    MakeNode(NodeKind::ExtensionAttributeSpecifier, i);
                attribute->Tokens.End = attributeEnd;
                Append(node, *attribute);
                i = attributeEnd - 1;
            } else if (IsCallingConvention(i)) {
                auto* convention =
                    MakeNode(NodeKind::CallingConventionSpecifier, i);
                convention->Tokens.End = i + 1;
                Append(node, *convention);
            } else if (node.Kind == NodeKind::NewTypeId &&
                Kind(i) == TK::Operator_Star)
            {
                auto afterPointers = NextSignificant(i + 1);
                while (Kind(afterPointers) == TK::Operator_Star) {
                    afterPointers = NextSignificant(afterPointers + 1);
                }
                if (afterPointers < end &&
                    Kind(afterPointers) == TK::Operator_OpenBracket)
                {
                    for (auto pointer = i;
                        pointer < afterPointers;
                        ++pointer)
                    {
                        if (Kind(pointer) != TK::Operator_Star) {
                            continue;
                        }
                        auto* part = MakeNode(
                            NodeKind::PointerOperator,
                            pointer);
                        part->Tokens.End = pointer + 1;
                        Append(node, *part);
                    }
                    i = afterPointers - 1;
                }
            } else if (Kind(i) == TK::Operator_Less) {
                auto templateEnd = SkipTemplateArguments(i, end);
                if (templateEnd <= end && templateEnd > i && (Kind(templateEnd -
                    1) == TK::Operator_Greater ||
                        Kind(templateEnd - 1) == TK::Operator_RightShift))
                {
                    Append(node, *MakeTemplateArgumentList(i, templateEnd));
                    i = templateEnd - 1;
                }
            }
        }
    }

    [[nodiscard]] std::optional<TokenRange>
    InferNestedFunctionParameters(size_t begin, size_t end) const
    {
        if (Kind(begin) != TK::Operator_OpenParenthesis) {
            return std::nullopt;
        }
        auto nestedEnd = FindBalancedEnd(begin);
        if (nestedEnd == std::numeric_limits<TokenIndex>::max() ||
            nestedEnd >= end)
        {
            return std::nullopt;
        }
        auto suffixBegin = NextSignificant(nestedEnd);
        if (Kind(suffixBegin) != TK::Operator_OpenParenthesis) {
            return std::nullopt;
        }
        auto suffixEnd = FindBalancedEnd(suffixBegin);
        if (suffixEnd == std::numeric_limits<TokenIndex>::max() ||
            suffixEnd > end)
        {
            return std::nullopt;
        }
        return TokenRange{
            .Begin = suffixBegin,
            .End = suffixEnd,
        };
    }

    [[nodiscard]] bool ContainsPointerOperator(size_t begin, size_t end) const
    {
        for (auto i = begin; i < end; ++i) {
            auto kind = Kind(i);
            if (kind == TK::Operator_Star || kind == TK::Operator_Ampersand ||
                kind == TK::Operator_And)
            {
                return true;
            }
        }
        return false;
    }

    void AddDeclaratorSyntax(
        Node& initDeclarator,
        size_t begin,
        size_t end,
        std::optional<TokenRange> parameters)
    {
        if (parameters && !(
                begin <= parameters->Begin &&
                parameters->Begin < parameters->End &&
                parameters->End <= end))
        {
            parameters.reset();
        }
        if (!parameters) {
            parameters = InferNestedFunctionParameters(begin, end);
        }
        bool isFunction = parameters && !parameters->Empty();
        if (!isFunction &&
            AddStructuredBindingSyntax(initDeclarator, begin, end))
        {
            return;
        }

        // Most declarators are a name, optionally preceded by one pointer
        // operator and followed by a direct initializer. Build those without
        // repeatedly rescanning the initializer expression.
        if (!isFunction) {
            auto name = begin;
            bool hasPointer = false;
            auto firstKind = Kind(begin);
            if (firstKind == TK::Operator_Star ||
                firstKind == TK::Operator_Ampersand ||
                firstKind == TK::Operator_And)
            {
                hasPointer = true;
                name = begin + 1;
            }
            if (Kind(name) == TK::Identifier) {
                auto afterName = name + 1;
                auto afterNameKind = Kind(afterName);
                if (afterName == end || afterNameKind == TK::Operator_Assign ||
                    afterNameKind == TK::Operator_OpenBrace ||
                    afterNameKind == TK::Operator_OpenParenthesis)
                {
                    auto* declarator = MakeNode(NodeKind::Declarator, begin);
                    declarator->Tokens.End = afterName;
                    Append(initDeclarator, *declarator);
                    if (hasPointer) {
                        auto* pointer =
                            MakeNode(NodeKind::PointerOperator, begin);
                        pointer->Tokens.End = begin + 1;
                        Append(*declarator, *pointer);
                    }
                    auto* nameNode = MakeNode(NodeKind::DeclaratorName, name);
                    nameNode->Tokens.End = name + 1;
                    Append(*declarator, *nameNode);
                    if (afterName < end) {
                        auto* initializer =
                            MakeNode(NodeKind::Initializer, afterName);
                        initializer->Tokens.End = end;
                        AddInitializerExpression(*initializer, afterName, end);
                        Append(initDeclarator, *initializer);
                    }
                    return;
                }
            }
        }

        auto name = FindDeclaratorName(begin, end, parameters);
        auto initializerStart =
            FindInitializerStart(begin, end, name, isFunction);
        auto declaratorEnd = initializerStart;
        if (isFunction && parameters->End > declaratorEnd) {
            parameters.reset();
            isFunction = false;
            name = FindDeclaratorName(begin, end, std::nullopt);
            initializerStart =
                FindInitializerStart(begin, end, name, isFunction);
            declaratorEnd = initializerStart;
        }
        auto declaratorKind = name < declaratorEnd
            ? NodeKind::Declarator
            : NodeKind::AbstractDeclarator;
        auto* declarator = MakeNode(declaratorKind, begin);
        declarator->Tokens.End = declaratorEnd;
        Append(initDeclarator, *declarator);

        Node* core = declarator;
        size_t coreBegin = begin;
        size_t nestedEnd = begin;
        if (Kind(begin) == TK::Operator_OpenParenthesis) {
            auto balancedEnd = FindBalancedEnd(begin);
            if (balancedEnd != std::numeric_limits<TokenIndex>::max() &&
                balancedEnd <= declaratorEnd && ((begin < name &&
                    name < balancedEnd) ||
                    ContainsPointerOperator(begin + 1, balancedEnd - 1)))
            {
                auto* nested = MakeNode(NodeKind::NestedDeclarator, begin);
                nested->Tokens.End = balancedEnd;
                Append(*declarator, *nested);
                core = nested;
                coreBegin = begin + 1;
                nestedEnd = balancedEnd;
            }
        }

        if (name >= declaratorEnd) {
            auto coreEnd = nestedEnd != begin
                ? nestedEnd - 1
                : (parameters ? parameters->Begin : declaratorEnd);
            auto memberPointerEnd = FindMemberPointerEnd(coreBegin, coreEnd);
            if (memberPointerEnd != coreBegin) {
                auto* memberPointer =
                    MakeNode(NodeKind::MemberPointerOperator, coreBegin);
                memberPointer->Tokens.End = memberPointerEnd;
                Append(*core, *memberPointer);
                coreBegin = memberPointerEnd;
            }
            for (auto i = coreBegin; i < coreEnd; ++i) {
                auto kind = Kind(i);
                if (kind == TK::Operator_Star || kind ==
                    TK::Operator_Ampersand ||
                    kind == TK::Operator_And)
                {
                    auto* pointer = MakeNode(NodeKind::PointerOperator, i);
                    pointer->Tokens.End = i + 1;
                    Append(*core, *pointer);
                }
            }
        }

        if (name < declaratorEnd) {
            auto memberPointerEnd =
                FindMemberPointerEnd(coreBegin, declaratorEnd);
            if (memberPointerEnd != coreBegin && memberPointerEnd <= name) {
                auto* memberPointer =
                    MakeNode(NodeKind::MemberPointerOperator, coreBegin);
                memberPointer->Tokens.End = memberPointerEnd;
                Append(*core, *memberPointer);
                coreBegin = memberPointerEnd;
            }
            for (auto i = coreBegin; i < name; ++i) {
                auto kind = Kind(i);
                if (kind == TK::Operator_Star || kind ==
                    TK::Operator_Ampersand ||
                    kind == TK::Operator_And)
                {
                    auto* pointer = MakeNode(NodeKind::PointerOperator, i);
                    pointer->Tokens.End = i + 1;
                    Append(*core, *pointer);
                } else if (IsExtensionAttribute(i)) {
                    auto attributeEnd = FindExtensionAttributeEnd(i, name);
                    auto* attribute =
                        MakeNode(NodeKind::ExtensionAttributeSpecifier, i);
                    attribute->Tokens.End = attributeEnd;
                    Append(*core, *attribute);
                    i = attributeEnd - 1;
                } else if (IsCallingConvention(i)) {
                    auto* convention =
                        MakeNode(NodeKind::CallingConventionSpecifier, i);
                    convention->Tokens.End = i + 1;
                    Append(*core, *convention);
                } else if (kind == TK::Operator_OpenBracket &&
                    Kind(i + 1) == TK::Operator_OpenBracket)
                {
                    auto attributeEnd = FindBalancedEnd(i);
                    if (attributeEnd ==
                        std::numeric_limits<TokenIndex>::max() ||
                        attributeEnd > name)
                    {
                        break;
                    }
                    auto* attribute = MakeNode(NodeKind::AttributeSpecifier, i);
                    attribute->Tokens.End = attributeEnd;
                    Append(*core, *attribute);
                    i = attributeEnd - 1;
                }
            }
            auto nameBegin = name;
            auto nameEnd = name + 1;
            auto nameKind = NodeKind::DeclaratorName;
            if (Kind(name) == TK::Keyword_operator) {
                nameKind = NodeKind::OperatorFunctionName;
                if (parameters && name < parameters->Begin) {
                    nameEnd = parameters->Begin;
                }
            } else if (name > coreBegin &&
                Kind(PreviousSignificant(name)) == TK::Operator_Complement)
            {
                nameKind = NodeKind::DestructorName;
                nameBegin = PreviousSignificant(name);
            }
            auto* nameNode = MakeNode(nameKind, nameBegin);
            nameNode->Tokens.End = nameEnd;
            Append(*core, *nameNode);
        }

        {
            int delimiterDepth = 0;
            auto suffixBegin = name < declaratorEnd
                ? name + 1
                : (nestedEnd != begin ? nestedEnd : begin);
            for (auto i = suffixBegin; i < declaratorEnd; ++i) {
                if (isFunction && i == parameters->Begin) {
                    i = parameters->End - 1;
                    continue;
                }
                auto kind = Kind(i);
                if (IsExtensionAttribute(i)) {
                    auto suffixEnd =
                        FindExtensionAttributeEnd(i, declaratorEnd);
                    auto* suffix =
                        MakeNode(NodeKind::ExtensionAttributeSpecifier, i);
                    suffix->Tokens.End = suffixEnd;
                    Append(*declarator, *suffix);
                    i = suffixEnd - 1;
                    continue;
                }
                if (IsCallingConvention(i)) {
                    auto* convention =
                        MakeNode(NodeKind::CallingConventionSpecifier, i);
                    convention->Tokens.End = i + 1;
                    Append(*declarator, *convention);
                    continue;
                }
                if (kind == TK::Operator_OpenBracket) {
                    auto suffixEnd = FindBalancedEnd(i);
                    if (suffixEnd == std::numeric_limits<TokenIndex>::max() ||
                        suffixEnd > declaratorEnd)
                    {
                        break;
                    }
                    auto suffixKind = Kind(i + 1) == TK::Operator_OpenBracket
                        ? NodeKind::AttributeSpecifier
                        : NodeKind::ArrayDeclarator;
                    auto* suffix = MakeNode(suffixKind, i);
                    suffix->Tokens.End = suffixEnd;
                    Append(
                        nestedEnd != begin && suffixEnd <= nestedEnd
                            ? *core
                            : *declarator,
                        *suffix);
                    i = suffixEnd - 1;
                    continue;
                }
                if (IsOpenDelimiter(kind)) {
                    ++delimiterDepth;
                } else if (kind == TK::Operator_CloseParenthesis ||
                    kind == TK::Operator_CloseBrace)
                {
                    --delimiterDepth;
                } else if (!isFunction && delimiterDepth == 0 &&
                    kind == TK::Operator_Colon)
                {
                    auto* bitField = MakeNode(NodeKind::BitField, i);
                    bitField->Tokens.End = declaratorEnd;
                    Append(*declarator, *bitField);
                    break;
                }
            }
        }

        if (isFunction) {
            auto* function =
                MakeNode(NodeKind::FunctionDeclarator, parameters->Begin);
            function->Tokens.End = declaratorEnd;
            auto* parameterList =
                MakeNode(NodeKind::ParameterList, parameters->Begin);
            parameterList->Tokens.End = parameters->End;
            AddParameterDeclarations(*parameterList, *parameters);
            AddDirectiveChildrenInGap(
                *parameterList,
                parameters->Begin,
                parameters->End);
            Append(*function, *parameterList);
            AddFunctionSuffixSyntax(*function, parameters->End, declaratorEnd);
            Append(*declarator, *function);
        }

        if (initializerStart < end) {
            auto* initializer =
                MakeNode(NodeKind::Initializer, initializerStart);
            initializer->Tokens.End = end;
            AddInitializerExpression(*initializer, initializerStart, end);
            Append(initDeclarator, *initializer);
        }
    }

    void AddDeclarationSyntax(
        Node& declaration,
        size_t begin,
        size_t end,
        std::optional<TokenRange> parameters = std::nullopt)
    {
        while (end > begin && (Kind(end - 1) == TK::Operator_Semicolon ||
                Kind(end - 1) == TK::Operator_OpenBrace))
        {
            --end;
        }
        auto specifiersEnd = FindDeclarationSpecifiersEnd(begin, end);
        if (specifiersEnd > begin) {
            auto* specifiers = MakeNode(NodeKind::DeclarationSpecifiers, begin);
            specifiers->Tokens.End = specifiersEnd;
            AddTypeSyntaxChildren(*specifiers, begin, specifiersEnd);
            Append(declaration, *specifiers);
        }
        if (specifiersEnd >= end) {
            return;
        }

        auto* list = MakeNode(NodeKind::InitDeclaratorList, specifiersEnd);
        list->Tokens.End = end;
        Append(declaration, *list);

        size_t segmentBegin = specifiersEnd;
        int delimiterDepth = 0;
        int angleDepth = 0;
        for (auto i = specifiersEnd; i <= end; ++i) {
            auto kind = Kind(i);
            if (i < end) {
                if (IsOpenDelimiter(kind)) {
                    ++delimiterDepth;
                } else if (kind == TK::Operator_CloseParenthesis ||
                    kind == TK::Operator_CloseBracket ||
                    kind == TK::Operator_CloseBrace)
                {
                    --delimiterDepth;
                } else if (kind == TK::Operator_Less) {
                    ++angleDepth;
                } else if (kind == TK::Operator_Greater && angleDepth > 0) {
                    --angleDepth;
                } else if (kind == TK::Operator_RightShift && angleDepth > 0) {
                    angleDepth = std::max(0, angleDepth - 2);
                }
            }
            if (i != end && (kind !=
                TK::Operator_Comma || delimiterDepth != 0 ||
                    angleDepth != 0))
            {
                continue;
            }

            auto segmentEnd = i;
            if (segmentBegin < segmentEnd) {
                auto* init = MakeNode(NodeKind::InitDeclarator, segmentBegin);
                init->Tokens.End = segmentEnd;
                Append(*list, *init);
                std::optional<TokenRange> segmentParameters;
                if (parameters && segmentBegin <= parameters->Begin &&
                    parameters->End <= segmentEnd)
                {
                    segmentParameters = parameters;
                }
                AddDeclaratorSyntax(
                    *init,
                    segmentBegin,
                    segmentEnd,
                    segmentParameters);
            }
            if (i != end) {
                auto* comma = MakeNode(NodeKind::ListSeparator, i);
                comma->Tokens.End = i + 1;
                Append(*list, *comma);
            }
            segmentBegin = i + 1;
        }
    }

    void AddSingleDeclaratorDeclarationSyntax(
        Node& declaration,
        size_t begin,
        size_t end,
        TokenRange parameters)
    {
        auto specifiersEnd = FindDeclarationSpecifiersEnd(begin, end);
        if (specifiersEnd > begin) {
            auto* specifiers = MakeNode(NodeKind::DeclarationSpecifiers, begin);
            specifiers->Tokens.End = specifiersEnd;
            AddTypeSyntaxChildren(*specifiers, begin, specifiersEnd);
            Append(declaration, *specifiers);
        }
        if (specifiersEnd >= end) {
            return;
        }
        auto* list = MakeNode(NodeKind::InitDeclaratorList, specifiersEnd);
        list->Tokens.End = end;
        Append(declaration, *list);
        auto* init = MakeNode(NodeKind::InitDeclarator, specifiersEnd);
        init->Tokens.End = end;
        Append(*list, *init);
        AddDeclaratorSyntax(*init, specifiersEnd, end, parameters);
    }

    [[nodiscard]] bool IsParenthesizedDeclarator(size_t begin, size_t end) const
    {
        auto first = NextSignificant(begin + 1);
        if (Kind(first) != TK::Operator_Star &&
            Kind(first) != TK::Operator_Ampersand &&
            Kind(first) != TK::Operator_And)
        {
            return false;
        }
        for (auto i = first + 1; i + 1 < end; ++i) {
            if (Kind(i) == TK::Identifier) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] TokenIndex SkipRequiresExpression(TokenIndex start) const
    {
        auto cursor = NextSignificant(start + 1);
        if (Kind(cursor) == TK::Operator_OpenParenthesis) {
            cursor = FindBalancedEnd(cursor);
            if (cursor == std::numeric_limits<TokenIndex>::max()) {
                return start + 1;
            }
            cursor = NextSignificant(cursor);
        }
        if (Kind(cursor) != TK::Operator_OpenBrace) {
            return start + 1;
        }
        auto bodyEnd = FindBalancedEnd(cursor);
        return bodyEnd == std::numeric_limits<TokenIndex>::max()
            ? start + 1
            : bodyEnd;
    }

    [[nodiscard]] bool IsFunctionSuffixStart(TokenIndex index) const
    {
        auto kind = Kind(index);
        if (kind == TK::Operator_Semicolon || kind == TK::Operator_OpenBrace ||
            kind == TK::Operator_Colon || kind == TK::Operator_Assign ||
            kind == TK::Operator_IndirectClassMemberAccess ||
            kind == TK::Operator_Ampersand || kind == TK::Operator_And ||
            kind == TK::Operator_OpenParenthesis ||
            kind == TK::Operator_OpenBracket || kind == TK::Keyword_const ||
            kind == TK::Keyword_volatile || kind == TK::Keyword_noexcept ||
            kind == TK::Keyword_requires || kind == TK::Keyword_throw ||
            kind == TK::Keyword_try)
        {
            return true;
        }
        return kind == TK::Identifier && (Text(index) == "final" ||
            Text(index) == "override" ||
            IsExtensionAttribute(index) || IsCallingConvention(index));
    }

    [[nodiscard]] bool HasMacroLikeLambdaArgument(
        TokenIndex opening,
        TokenIndex closingEnd) const
    {
        if (opening >= closingEnd ||
            Kind(opening) != TK::Operator_OpenParenthesis)
        {
            return false;
        }
        bool hasDefault = false;
        for (auto i = opening + 1; i + 1 < closingEnd; ++i) {
            auto kind = Kind(i);
            if (kind == TK::Operator_Comma) {
                hasDefault = false;
                continue;
            }
            if (kind == TK::Operator_Assign) {
                hasDefault = true;
                continue;
            }
            if (kind == TK::Operator_OpenBracket && !hasDefault) {
                auto captureEnd = FindBalancedEnd(i);
                if (captureEnd != std::numeric_limits<TokenIndex>::max() &&
                    captureEnd < closingEnd &&
                    Kind(NextSignificant(captureEnd)) ==
                        TK::Operator_OpenParenthesis)
                {
                    auto parameterEnd = FindBalancedEnd(
                        NextSignificant(captureEnd));
                    if (parameterEnd !=
                        std::numeric_limits<TokenIndex>::max() &&
                        parameterEnd < closingEnd)
                    {
                        auto body = NextSignificant(parameterEnd);
                        if (Kind(body) == TK::Operator_OpenBrace) {
                            auto bodyEnd = FindBalancedEnd(body);
                            if (bodyEnd < closingEnd &&
                                (Kind(bodyEnd) == TK::Operator_Comma ||
                                    Kind(bodyEnd) ==
                                        TK::Operator_CloseParenthesis))
                            {
                                return true;
                            }
                        }
                    }
                }
            }
            if (IsOpenDelimiter(kind)) {
                auto nested = FindBalancedEnd(i);
                if (nested != std::numeric_limits<TokenIndex>::max() &&
                    nested <= closingEnd)
                {
                    i = nested - 1;
                }
            }
        }
        return false;
    }

    Node* ParseGeneralDeclaration()
    {
        auto start = Cursor_;
        TokenIndex parameterBegin = std::numeric_limits<TokenIndex>::max();
        TokenIndex parameterEnd = parameterBegin;
        bool sawTopLevelParameters = false;
        bool sawAssignmentBeforeParameters = false;
        bool sawParenthesizedDeclarator = false;
        bool constructorInitializer = false;
        bool invalidFunctionSuffix = false;
        bool sawTrailingReturnType = false;
        TokenIndex functionTry = std::numeric_limits<TokenIndex>::max();

        while (!AtEnd()) {
            auto kind = CurrentKind();
            if (kind == TK::Keyword_requires && Cursor_ != start &&
                Kind(PreviousSignificant(Cursor_)) == TK::Keyword_requires)
            {
                auto requiresEnd = SkipRequiresExpression(Cursor_);
                if (requiresEnd != Cursor_ + 1) {
                    Cursor_ = requiresEnd;
                    continue;
                }
            }
            // Parentheses inside a return type's template arguments (for
            // example a pointer-to-member-function type) are not the
            // function's parameter list.
            if (kind == TK::Operator_Less && !sawTopLevelParameters &&
                !sawAssignmentBeforeParameters && Cursor_ > start &&
                Kind(PreviousSignificant(Cursor_)) == TK::Identifier)
            {
                auto after = SkipTemplateArguments(
                    Cursor_,
                    Tree_.Tokens_.Size());
                if (after > Cursor_ && after < Tree_.Tokens_.Size() &&
                    (Kind(after - 1) == TK::Operator_Greater ||
                        Kind(after - 1) == TK::Operator_RightShift))
                {
                    Cursor_ = after;
                    continue;
                }
            }
            if (kind == TK::Operator_Assign && !sawTopLevelParameters) {
                auto previous = PreviousSignificant(Cursor_);
                if (Kind(previous) != TK::Keyword_operator) {
                    sawAssignmentBeforeParameters = true;
                }
                ++Cursor_;
                continue;
            }
            if (kind == TK::Operator_OpenParenthesis) {
                auto begin = Cursor_;
                auto previous = PreviousSignificant(Cursor_);
                ConsumeBalanced();
                if (!sawTopLevelParameters && !sawParenthesizedDeclarator &&
                    IsParenthesizedDeclarator(begin, Cursor_))
                {
                    sawParenthesizedDeclarator = true;
                    for (auto nested = NextSignificant(begin + 1);
                        nested + 1 < Cursor_;
                        nested = NextSignificant(nested + 1))
                    {
                        if (Kind(nested) != TK::Operator_OpenParenthesis ||
                            Kind(PreviousSignificant(nested)) !=
                                TK::Identifier)
                        {
                            continue;
                        }
                        auto nestedEnd = FindBalancedEnd(nested);
                        if (nestedEnd ==
                            std::numeric_limits<TokenIndex>::max() ||
                            nestedEnd >= Cursor_)
                        {
                            continue;
                        }
                        parameterBegin = nested;
                        parameterEnd = nestedEnd;
                        sawTopLevelParameters = true;
                        break;
                    }
                    continue;
                }
                bool isOperatorName = Kind(previous) == TK::Keyword_operator;
                bool isAttribute = Kind(previous) == TK::Keyword_alignas ||
                    Text(previous) == "__attribute__" ||
                    Text(previous) == "__declspec";
                if (!sawAssignmentBeforeParameters && !sawTopLevelParameters &&
                    !isOperatorName && !isAttribute)
                {
                    parameterBegin = begin;
                    parameterEnd = Cursor_;
                    if (!sawParenthesizedDeclarator) {
                        sawTopLevelParameters = true;
                    }
                }
                continue;
            }
            if (kind == TK::Operator_OpenBracket) {
                ConsumeBalanced();
                continue;
            }
            if (sawTopLevelParameters && !sawTrailingReturnType &&
                Cursor_ == NextSignificant(parameterEnd) &&
                !IsFunctionSuffixStart(Cursor_))
            {
                auto* node = MakeNode(NodeKind::UnparsedSyntax, start);
                node->Tokens.End = Cursor_;
                return node;
            }
            if (sawTopLevelParameters &&
                kind == TK::Operator_IndirectClassMemberAccess)
            {
                bool hasPlaceholderAuto = false;
                for (auto index = start; index < parameterBegin; ++index) {
                    hasPlaceholderAuto |= Kind(index) == TK::Keyword_auto;
                }
                invalidFunctionSuffix |= !hasPlaceholderAuto;
                sawTrailingReturnType |= hasPlaceholderAuto;
            }
            if (kind == TK::Keyword_try && sawTopLevelParameters) {
                functionTry = Cursor_++;
                continue;
            }
            if (kind == TK::Operator_Colon && sawTopLevelParameters) {
                constructorInitializer = true;
                ++Cursor_;
                continue;
            }
            if (kind == TK::Operator_OpenBrace) {
                if (sawTopLevelParameters && !sawAssignmentBeforeParameters) {
                    // A braced member initializer follows an identifier.  The
                    // function body after it follows '}' or ')'.
                    auto previous = PreviousSignificant(Cursor_);
                    if (constructorInitializer && (Kind(
                        previous) == TK::Identifier ||
                            Kind(previous) == TK::Operator_Greater ||
                            Kind(previous) == TK::Operator_RightShift))
                    {
                        ConsumeBalanced();
                        continue;
                    }
                    return ParseFunctionDefinition(
                        start,
                        parameterBegin,
                        parameterEnd,
                        constructorInitializer,
                        functionTry);
                }
                ConsumeBalanced();
                continue;
            }
            if (kind == TK::Operator_Semicolon) {
                ++Cursor_;
                // A top-level lambda argument or an empty argument slot
                // cannot be a function parameter declaration. At namespace
                // scope these are typically macro invocations: parse a call
                // rather than inventing overlapping declarator syntax.
                if (sawTopLevelParameters &&
                    (HasMacroLikeLambdaArgument(parameterBegin, parameterEnd) ||
                        HasEmptyArgumentSlot(parameterBegin, parameterEnd)))
                {
                    auto expressionCursor = start;
                    auto* expression = ParseExpressionSyntax(
                        expressionCursor,
                        Cursor_ - 1);
                    auto* node = MakeNode(
                        expression && expressionCursor == Cursor_ - 1 &&
                            expression->Kind == NodeKind::CallExpression
                            ? NodeKind::ExpressionStatement
                            : NodeKind::UnparsedSyntax,
                        start);
                    node->Tokens.End = Cursor_;
                    if (node->Kind == NodeKind::ExpressionStatement) {
                        Append(*node, *expression);
                    }
                    AddDirectiveChildrenInGap(*node, start, Cursor_);
                    return node;
                }
                auto nodeKind = invalidFunctionSuffix
                    ? NodeKind::UnparsedSyntax
                    : sawTopLevelParameters &&
                    !sawAssignmentBeforeParameters
                        ? NodeKind::FunctionDeclaration
                        : NodeKind::Declaration;
                auto* node = MakeNode(nodeKind, start);
                node->Tokens.End = Cursor_;
                std::optional<TokenRange> parameters;
                if (parameterBegin != std::numeric_limits<TokenIndex>::max()) {
                    parameters = TokenRange{
                        .Begin = parameterBegin,
                        .End = parameterEnd,
                    };
                }
                if (parameters) {
                    AddLambdaArguments(
                        *node,
                        parameters->Begin,
                        parameters->End);
                }
                AddDeclarationSyntax(*node, start, Cursor_, parameters);
                AddDirectiveChildrenInGap(*node, start, Cursor_);
                return node;
            }
            if (kind == TK::Operator_CloseBrace) {
                if (Kind(NextSignificant(start)) == TK::Identifier) {
                    invalidFunctionSuffix = true;
                } else {
                    ErrorAt(start, "expected semicolon after declaration");
                }
                break;
            }
            ++Cursor_;
        }

        auto* node = MakeNode(NodeKind::Declaration, start);
        node->Tokens.End = Cursor_;
        AddDeclarationSyntax(*node, start, Cursor_);
        AddDirectiveChildrenInGap(*node, start, Cursor_);
        return node;
    }

    TokenIndex PreviousSignificant(TokenIndex index) const
    {
        while (index != 0) {
            --index;
            if (Kind(index) != TK::Comment) {
                return index;
            }
        }
        return 0;
    }

    Node* ParseFunctionDefinition(
        TokenIndex start,
        TokenIndex parameterBegin,
        TokenIndex parameterEnd,
        bool constructorInitializer,
        TokenIndex functionTry)
    {
        auto* node = MakeNode(NodeKind::FunctionDefinition, start);
        auto parameters = TokenRange{
            .Begin = parameterBegin,
            .End = parameterEnd,
        };
        if (constructorInitializer) {
            AddSingleDeclaratorDeclarationSyntax(
                *node,
                start,
                Cursor_,
                parameters);
        } else {
            AddDeclarationSyntax(*node, start, Cursor_, parameters);
        }
        AddDirectiveChildrenInGap(*node, start, Cursor_);
        Node* bodyParent = node;
        Node* tryBlock = nullptr;
        if (functionTry < Tree_.Tokens_.Size()) {
            tryBlock = MakeNode(NodeKind::FunctionTryBlock, functionTry);
            bodyParent = tryBlock;
        }
        Append(*bodyParent, *ParseCompoundStatement(node));

        while (SignificantKind(Cursor_) == TK::Keyword_catch) {
            Cursor_ = NextSignificant(Cursor_);
            Append(*bodyParent, *ParseCatchClause());
        }
        if (tryBlock) {
            tryBlock->Tokens.End = Cursor_;
            Append(*node, *tryBlock);
        }
        if (CurrentKind() == TK::Operator_Semicolon) {
            ++Cursor_;
        }
        node->Tokens.End = Cursor_;
        return node;
    }

    void RegisterParameterValues(const Node& node)
    {
        if (node.Kind == NodeKind::ParameterDeclaration) {
            RegisterDeclaredValues(node);
            return;
        }
        for (auto* child = node.FirstChild; child; child = child->NextSibling) {
            RegisterParameterValues(*child);
        }
    }

    Node* ParseCompoundStatement(const Node* functionDeclaration = nullptr)
    {
        auto start = Cursor_;
        auto* node = MakeNode(NodeKind::CompoundStatement, start);
        if (CurrentKind() != TK::Operator_OpenBrace) {
            ErrorAt(Cursor_, "expected compound statement");
            return node;
        }
        ++Cursor_;
        PushScope();
        PushConditionalBraceScope();
        if (functionDeclaration) {
            RegisterParameterValues(*functionDeclaration);
        }
        while (!AtEnd()) {
            if (CurrentKind() == TK::Operator_CloseBrace) {
                if (ConsumeConditionalClosingBrace(*node)) {
                    continue;
                }
                break;
            }
            if (ParseTrivia(*node, true)) {
                continue;
            }
            auto before = Cursor_;
            Append(*node, *ParseStatement());
            if (Cursor_ == before) {
                ++Cursor_;
            }
        }
        PopConditionalBraceScope();
        PopScope();
        if (CurrentKind() == TK::Operator_CloseBrace) {
            ++Cursor_;
        } else {
            ErrorAt(start, "unterminated compound statement");
            AddMissingToken(*node, Cursor_);
        }
        node->Tokens.End = Cursor_;
        return node;
    }

    Node* ParseStatement()
    {
        auto first = NextSignificant(Cursor_);
        switch (Kind(first)) {
            case TK::Operator_OpenBrace:
                Cursor_ = first;
                return ParseCompoundStatement();
            case TK::Keyword_if:
                return ParseIfStatement();
            case TK::Keyword_switch:
                return ParseControlledStatement(NodeKind::SwitchStatement);
            case TK::Keyword_while:
                return ParseControlledStatement(NodeKind::WhileStatement);
            case TK::Keyword_for:
                return ParseControlledStatement(NodeKind::ForStatement);
            case TK::Keyword_do:
                return ParseDoWhileStatement();
            case TK::Keyword_try:
                return ParseTryStatement();
            case TK::Keyword_return:
            case TK::Keyword_co_return:
                return ParseExpressionKeywordStatement(
                    NodeKind::ReturnStatement);
            case TK::Keyword_co_yield:
                return ParseExpressionKeywordStatement(
                    NodeKind::CoYieldStatement);
            case TK::Keyword_break:
                return ParseToSemicolon(NodeKind::BreakStatement);
            case TK::Keyword_continue:
                return ParseToSemicolon(NodeKind::ContinueStatement);
            case TK::Keyword_goto:
                return ParseToSemicolon(NodeKind::GotoStatement);
            case TK::Keyword_throw:
                return ParseExpressionKeywordStatement(
                    NodeKind::ThrowStatement);
            case TK::Keyword_using:
                return ParseUsingDeclaration();
            case TK::Keyword_typedef:
                return ParseTypedefDeclaration();
            case TK::Keyword_static_assert:
                return ParseToSemicolon(NodeKind::StaticAssertDeclaration);
            case TK::Keyword_asm:
                return ParseToSemicolon(NodeKind::AsmDeclaration);
            case TK::Keyword_template:
            case TK::Keyword_class:
            case TK::Keyword_struct:
            case TK::Keyword_union:
            case TK::Keyword_enum:
                return ParseDeclaration();
            case TK::Keyword_case:
            case TK::Keyword_default:
                return ParseLabel(NodeKind::CaseLabel);
            case TK::Operator_Semicolon:
                return ParseToSemicolon(NodeKind::ExpressionStatement);
            default:
                break;
        }
        if (Kind(first) == TK::Identifier &&
            SignificantKind(first + 1) == TK::Operator_Colon)
        {
            return ParseLabel(NodeKind::LabelStatement);
        }
        return ParseSimpleStatement();
    }

    Node* ParseIfStatement()
    {
        auto start = Cursor_;
        auto* node = MakeNode(NodeKind::IfStatement, start);
        Cursor_ = NextSignificant(Cursor_) + 1;
        ParseTriviaBeforeChild(*node);
        if (CurrentKind() == TK::Keyword_constexpr) {
            ++Cursor_;
        }
        if (CurrentKind() == TK::Keyword_consteval) {
            ++Cursor_;
        } else if (CurrentKind() == TK::Operator_Not &&
            Kind(Cursor_ + 1) == TK::Keyword_consteval)
        {
            Cursor_ += 2;
        } else {
            ParseControlHeader(*node, NodeKind::Condition);
        }
        bool conditionalBody = ParseTriviaBeforeChild(*node);
        if (!conditionalBody && !AtEnd()) {
            Append(*node, *ParseStatement());
        }
        ParseTriviaBeforeChild(*node);
        if (CurrentKind() == TK::Keyword_else) {
            ++Cursor_;
            bool conditionalElse = ParseTriviaBeforeChild(*node);
            if (!conditionalElse && !AtEnd()) {
                Append(*node, *ParseStatement());
            }
        }
        node->Tokens.End = Cursor_;
        return node;
    }

    Node* ParseControlledStatement(NodeKind kind)
    {
        auto start = Cursor_;
        auto* node = MakeNode(kind, start);
        Cursor_ = NextSignificant(Cursor_) + 1;
        ParseTriviaBeforeChild(*node);
        if (kind == NodeKind::ForStatement &&
            CurrentKind() == TK::Keyword_co_await)
        {
            ++Cursor_;
        }
        ParseControlHeader(
            *node,
            kind == NodeKind::ForStatement
                ? NodeKind::ForHeader
                : NodeKind::Condition);
        bool conditionalBody = ParseTriviaBeforeChild(*node);
        if (!conditionalBody && !AtEnd()) {
            Append(*node, *ParseStatement());
        }
        node->Tokens.End = Cursor_;
        return node;
    }

    void AddExpressionRange(Node& node, size_t begin, size_t end)
    {
        auto expressionCursor = NextSignificant(begin);
        if (expressionCursor >= end) {
            return;
        }
        if (auto* expression = ParseExpressionSyntax(expressionCursor, end)) {
            Append(node, *expression);
        }
        AddUnparsedSyntax(node, expressionCursor, end);
    }

    void AddForHeaderSyntax(Node& header, size_t begin, size_t end)
    {
        std::array<size_t, 2> semicolons{
            Tree_.Tokens_.Size(),
            Tree_.Tokens_.Size(),
        };
        size_t semicolonCount = 0;
        size_t rangeColon = Tree_.Tokens_.Size();
        int delimiterDepth = 0;
        int conditionalDepth = 0;
        for (auto i = begin; i < end; ++i) {
            auto kind = Kind(i);
            if (IsOpenDelimiter(kind)) {
                ++delimiterDepth;
            } else if (kind == TK::Operator_CloseParenthesis ||
                kind == TK::Operator_CloseBracket ||
                kind == TK::Operator_CloseBrace)
            {
                --delimiterDepth;
            } else if (delimiterDepth == 0) {
                if (kind == TK::Operator_Conditional) {
                    ++conditionalDepth;
                } else if (kind == TK::Operator_Colon && conditionalDepth > 0) {
                    --conditionalDepth;
                } else if (kind ==
                    TK::Operator_Colon && conditionalDepth == 0)
                {
                    rangeColon = i;
                } else if (kind ==
                    TK::Operator_Semicolon && semicolonCount < 2)
                {
                    semicolons[semicolonCount++] = i;
                }
            }
        }

        if (semicolonCount == 0 && rangeColon < end) {
            auto* declaration = MakeNode(NodeKind::RangeForDeclaration, begin);
            declaration->Tokens.End = rangeColon;
            AddDeclarationSyntax(*declaration, begin, rangeColon);
            Append(header, *declaration);
            auto* separator = MakeNode(NodeKind::RangeForSeparator, rangeColon);
            separator->Tokens.End = rangeColon + 1;
            Append(header, *separator);
            auto* expression =
                MakeNode(NodeKind::RangeForExpression, rangeColon + 1);
            expression->Tokens.End = end;
            AddExpressionRange(*expression, rangeColon + 1, end);
            Append(header, *expression);
            return;
        }

        if (semicolonCount != 2) {
            return;
        }
        auto* initializer = MakeNode(NodeKind::ForInitStatement, begin);
        initializer->Tokens.End = semicolons[0];
        if (begin < semicolons[0]) {
            if (ClassifySimpleStatement(begin) ==
                NodeKind::DeclarationStatement)
            {
                AddDeclarationSyntax(*initializer, begin, semicolons[0]);
            } else {
                AddExpressionRange(*initializer, begin, semicolons[0]);
            }
        }
        Append(header, *initializer);
        auto* firstSeparator = MakeNode(NodeKind::ListSeparator, semicolons[0]);
        firstSeparator->Tokens.End = semicolons[0] + 1;
        Append(header, *firstSeparator);

        auto* condition = MakeNode(NodeKind::ForCondition, semicolons[0] + 1);
        condition->Tokens.End = semicolons[1];
        AddExpressionRange(*condition, semicolons[0] + 1, semicolons[1]);
        Append(header, *condition);
        auto* secondSeparator =
            MakeNode(NodeKind::ListSeparator, semicolons[1]);
        secondSeparator->Tokens.End = semicolons[1] + 1;
        Append(header, *secondSeparator);

        auto* iteration = MakeNode(NodeKind::ForIteration, semicolons[1] + 1);
        iteration->Tokens.End = end;
        AddExpressionRange(*iteration, semicolons[1] + 1, end);
        Append(header, *iteration);
    }

    void ParseControlHeader(Node& parent, NodeKind kind)
    {
        ParseTriviaBeforeChild(parent);
        if (CurrentKind() == TK::Identifier) {
            auto next = NextSignificant(Cursor_ + 1);
            if (Kind(next) == TK::Operator_OpenParenthesis) {
                Cursor_ = next;
            }
        }
        if (CurrentKind() != TK::Operator_OpenParenthesis) {
            ErrorAt(Cursor_, "expected parenthesized control expression");
            return;
        }
        auto begin = Cursor_;
        ConsumeBalanced();
        auto* header = MakeNode(kind, begin);
        header->Tokens.End = Cursor_;
        if (kind == NodeKind::ForHeader) {
            AddForHeaderSyntax(*header, begin + 1, Cursor_ - 1);
            AddDirectiveChildrenInGap(*header, begin, Cursor_);
        } else if (kind == NodeKind::Condition && begin + 1 < Cursor_ - 1 &&
            ClassifySimpleStatement(begin + 1) ==
                NodeKind::ExpressionStatement)
        {
            auto expressionCursor = begin + 1;
            if (auto* expression =
                    ParseExpressionSyntax(expressionCursor, Cursor_ - 1))
            {
                Append(*header, *expression);
            }
            AddDirectiveChildrenInGap(*header, begin, Cursor_);
        } else {
            AddDirectiveChildren(*header, begin, Cursor_);
        }
        Append(parent, *header);
    }

    Node* ParseDoWhileStatement()
    {
        auto start = Cursor_;
        auto* node = MakeNode(NodeKind::DoWhileStatement, start);
        Cursor_ = NextSignificant(Cursor_) + 1;
        bool conditionalBody = ParseTriviaBeforeChild(*node);
        if (!conditionalBody && !AtEnd()) {
            Append(*node, *ParseStatement());
        }
        ParseTriviaBeforeChild(*node);
        if (CurrentKind() == TK::Keyword_while) {
            ++Cursor_;
            ParseControlHeader(*node, NodeKind::Condition);
            if (CurrentKind() == TK::Operator_Semicolon) {
                ++Cursor_;
            } else {
                ErrorAt(Cursor_, "expected semicolon after do-while");
            }
        } else {
            ErrorAt(Cursor_, "expected while after do statement");
        }
        node->Tokens.End = Cursor_;
        return node;
    }

    Node* ParseTryStatement()
    {
        auto start = Cursor_;
        auto* node = MakeNode(NodeKind::TryStatement, start);
        Cursor_ = NextSignificant(Cursor_) + 1;
        bool conditionalBody = ParseTriviaBeforeChild(*node);
        if (CurrentKind() == TK::Operator_OpenBrace) {
            Append(*node, *ParseCompoundStatement());
        } else if (!conditionalBody) {
            ErrorAt(Cursor_, "expected try compound statement");
        }
        ParseTriviaBeforeChild(*node);
        while (CurrentKind() == TK::Keyword_catch) {
            Append(*node, *ParseCatchClause());
            ParseTriviaBeforeChild(*node);
        }
        node->Tokens.End = Cursor_;
        return node;
    }

    Node* ParseCatchClause()
    {
        auto start = Cursor_;
        auto* node = MakeNode(NodeKind::CatchClause, start);
        ++Cursor_;
        ParseControlHeader(*node, NodeKind::Condition);
        bool conditionalBody = ParseTriviaBeforeChild(*node);
        if (CurrentKind() == TK::Operator_OpenBrace) {
            Append(*node, *ParseCompoundStatement());
        } else if (!conditionalBody) {
            ErrorAt(Cursor_, "expected catch compound statement");
        }
        node->Tokens.End = Cursor_;
        return node;
    }

    Node* ParseLabel(NodeKind kind)
    {
        auto start = Cursor_;
        auto* node = MakeNode(kind, start);
        int conditionalDepth = 0;
        while (!AtEnd()) {
            auto tokenKind = CurrentKind();
            if (tokenKind == TK::Operator_Conditional) {
                ++conditionalDepth;
            } else if (tokenKind == TK::Operator_Colon) {
                if (conditionalDepth == 0) {
                    ++Cursor_;
                    break;
                }
                --conditionalDepth;
            } else if (IsOpenDelimiter(tokenKind)) {
                ConsumeBalanced();
                continue;
            }
            ++Cursor_;
        }
        node->Tokens.End = Cursor_;
        AddDirectiveChildren(*node, start, Cursor_);
        return node;
    }

    [[nodiscard]] bool IsLiteralExpressionToken(TK kind) const
    {
        switch (kind) {
            case TK::Literal_UserDefinedInteger:
            case TK::Literal_BinaryInteger:
            case TK::Literal_OctalInteger:
            case TK::Literal_DecimalInteger:
            case TK::Literal_HexadecimalInteger:
            case TK::Literal_Character:
            case TK::Literal_String:
            case TK::Keyword_true:
            case TK::Keyword_false:
            case TK::Keyword_nullptr:
                return true;
            default:
                return false;
        }
    }

    [[nodiscard]] bool IsNamedCast(TK kind) const
    {
        return kind == TK::Keyword_const_cast ||
            kind == TK::Keyword_dynamic_cast ||
            kind == TK::Keyword_reinterpret_cast ||
            kind == TK::Keyword_static_cast;
    }

    [[nodiscard]] bool LooksLikeTypeId(size_t begin, size_t end) const
    {
        auto first = SkipAttributeSpecifiers(begin);
        if (first >= end ||
            (!IsTypeKeyword(Kind(first)) && !IsKnownType(first)))
        {
            return false;
        }
        for (auto i = first; i < end; ++i) {
            auto kind = Kind(i);
            if (kind == TK::Operator_OpenParenthesis ||
                kind == TK::Operator_OpenBracket)
            {
                auto after = FindBalancedEnd(i);
                if (after == std::numeric_limits<TokenIndex>::max() ||
                    after > end)
                {
                    return false;
                }
                i = after - 1;
                continue;
            }
            if (kind == TK::Operator_Less) {
                auto after = SkipTemplateArguments(i, end);
                if (after > i && after <= end &&
                    (Kind(after - 1) == TK::Operator_Greater ||
                        Kind(after - 1) == TK::Operator_RightShift))
                {
                    i = after - 1;
                    continue;
                }
            }
            // A known type followed by a top-level binary expression is
            // not a cast type-id: (T(1) << shift) - 1 is subtraction.
            if (GetBinaryOperatorInfo(kind).Precedence != 0 &&
                kind != TK::Operator_Star &&
                kind != TK::Operator_Ampersand &&
                kind != TK::Operator_And)
            {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] bool IsPrefixOperator(TK kind) const
    {
        return kind == TK::Operator_Plus || kind == TK::Operator_Minus ||
            kind == TK::Operator_Not || kind == TK::Operator_Complement ||
            kind == TK::Operator_Star || kind == TK::Operator_Ampersand ||
            kind == TK::Operator_Increment || kind == TK::Operator_Decrement ||
            kind == TK::Keyword_co_await || kind == TK::Keyword_sizeof ||
            kind == TK::Keyword_alignof;
    }

    struct BinaryOperatorInfo
    {
        int Precedence = 0;
        bool RightAssociative = false;
        NodeKind Kind = NodeKind::BinaryExpression;
    };

    [[nodiscard]] BinaryOperatorInfo GetBinaryOperatorInfo(TK kind) const
    {
        switch (kind) {
            case TK::Operator_Comma:
                return {.Precedence = 1};
            case TK::Operator_Assign:
            case TK::Operator_PlusAssign:
            case TK::Operator_MinusAssign:
            case TK::Operator_MultiplyAssign:
            case TK::Operator_DivideAssign:
            case TK::Operator_RemainderAssign:
            case TK::Operator_XorAssign:
            case TK::Operator_BitAndAssign:
            case TK::Operator_BitOrAssign:
            case TK::Operator_LeftShiftAssign:
            case TK::Operator_RightShiftAssign:
                return {
                .Precedence = 2,
                .RightAssociative = true,
                .Kind = NodeKind::AssignmentExpression,
            };
            case TK::Operator_Or:
                return {.Precedence = 4};
            case TK::Operator_And:
                return {.Precedence = 5};
            case TK::Operator_BitOr:
                return {.Precedence = 6};
            case TK::Operator_Xor:
                return {.Precedence = 7};
            case TK::Operator_Ampersand:
                return {.Precedence = 8};
            case TK::Operator_Equals:
            case TK::Operator_NotEquals:
                return {.Precedence = 9};
            case TK::Operator_Less:
            case TK::Operator_Greater:
            case TK::Operator_LessEquals:
            case TK::Operator_GreaterEquals:
            case TK::Operator_Spaceship:
                return {.Precedence = 10};
            case TK::Operator_LeftShift:
            case TK::Operator_RightShift:
                return {.Precedence = 11};
            case TK::Operator_Plus:
            case TK::Operator_Minus:
                return {.Precedence = 12};
            case TK::Operator_Star:
            case TK::Operator_Divide:
            case TK::Operator_Remainder:
                return {.Precedence = 13};
            default:
                return {};
        }
    }

    Node* ParseLambdaExpression(TokenIndex& cursor, TokenIndex end)
    {
        auto start = cursor;
        auto captureEnd = FindBalancedEnd(cursor);
        if (captureEnd == std::numeric_limits<TokenIndex>::max() ||
            captureEnd > end)
        {
            return nullptr;
        }
        auto* lambda = MakeNode(NodeKind::LambdaExpression, start);
        auto* captures = MakeNode(NodeKind::LambdaCaptureList, start);
        captures->Tokens.End = captureEnd;
        if (start + 1 < captureEnd - 1) {
            AddSeparatedElements(
                *captures,
                start + 1,
                captureEnd - 1,
                NodeKind::LambdaCapture);
        }
        AddDirectiveChildrenInGap(*captures, start, captureEnd);
        Append(*lambda, *captures);
        cursor = NextSignificant(captureEnd);

        if (Kind(cursor) == TK::Operator_Less) {
            auto templateEnd = SkipTemplateArguments(cursor, end);
            if (templateEnd <= end && templateEnd > cursor) {
                auto* parameters =
                    MakeNode(NodeKind::TemplateParameterList, cursor);
                parameters->Tokens.End = templateEnd;
                if (cursor + 1 < templateEnd - 1) {
                    AddSeparatedElements(
                        *parameters,
                        cursor + 1,
                        templateEnd - 1,
                        NodeKind::TemplateParameter);
                }
                Append(*lambda, *parameters);
                cursor = NextSignificant(templateEnd);
            }
        }
        if (Kind(cursor) == TK::Operator_OpenParenthesis) {
            auto parameterEnd = FindBalancedEnd(cursor);
            if (parameterEnd != std::numeric_limits<TokenIndex>::max() &&
                parameterEnd <= end)
            {
                auto* parameters = MakeNode(NodeKind::ParameterList, cursor);
                parameters->Tokens.End = parameterEnd;
                AddParameterDeclarations(
                    *parameters,
                    TokenRange{.Begin = cursor, .End = parameterEnd});
                AddDirectiveChildrenInGap(*parameters, cursor, parameterEnd);
                Append(*lambda, *parameters);
                cursor = NextSignificant(parameterEnd);
            }
        }

        while (cursor < end) {
            auto kind = Kind(cursor);
            if (kind == TK::Operator_OpenBrace) {
                auto bodyEnd = FindBalancedEnd(cursor);
                if (bodyEnd == std::numeric_limits<TokenIndex>::max() ||
                    bodyEnd > end)
                {
                    break;
                }
                auto savedCursor = Cursor_;
                Cursor_ = cursor;
                ++BranchParseDepth_;
                auto* body = ParseCompoundStatement();
                --BranchParseDepth_;
                Cursor_ = savedCursor;
                body->Kind = NodeKind::LambdaBody;
                Append(*lambda, *body);
                cursor = bodyEnd;
                lambda->Tokens.End = bodyEnd;
                return lambda;
            }
            if (kind == TK::Keyword_mutable || kind == TK::Keyword_constexpr ||
                kind == TK::Keyword_consteval || kind == TK::Keyword_static)
            {
                auto* qualifier = MakeNode(NodeKind::FunctionQualifier, cursor);
                qualifier->Tokens.End = cursor + 1;
                Append(*lambda, *qualifier);
                cursor = NextSignificant(cursor + 1);
                continue;
            }
            if (kind == TK::Keyword_noexcept) {
                auto specificationEnd = cursor + 1;
                auto opening = NextSignificant(specificationEnd);
                if (Kind(opening) == TK::Operator_OpenParenthesis) {
                    auto balancedEnd = FindBalancedEnd(opening);
                    if (balancedEnd != std::numeric_limits<TokenIndex>::max() &&
                        balancedEnd <= end)
                    {
                        specificationEnd = balancedEnd;
                    }
                }
                auto* specification =
                    MakeNode(NodeKind::ExceptionSpecification, cursor);
                specification->Tokens.End = specificationEnd;
                Append(*lambda, *specification);
                cursor = NextSignificant(specificationEnd);
                continue;
            }
            if (kind == TK::Operator_IndirectClassMemberAccess) {
                auto trailingEnd = cursor + 1;
                while (trailingEnd < end &&
                    Kind(trailingEnd) != TK::Keyword_requires &&
                    Kind(trailingEnd) != TK::Operator_OpenBrace)
                {
                    ++trailingEnd;
                }
                auto* trailing = MakeNode(NodeKind::TrailingReturnType, cursor);
                trailing->Tokens.End = trailingEnd;
                Append(*lambda, *trailing);
                cursor = NextSignificant(trailingEnd);
                continue;
            }
            if (kind == TK::Keyword_requires) {
                auto requiresEnd = cursor + 1;
                while (requiresEnd < end &&
                    Kind(requiresEnd) != TK::Operator_OpenBrace)
                {
                    ++requiresEnd;
                }
                auto* requiresClause =
                    MakeNode(NodeKind::RequiresClause, cursor);
                requiresClause->Tokens.End = requiresEnd;
                auto constraintBegin = NextSignificant(cursor + 1);
                if (constraintBegin < requiresEnd) {
                    auto* constraint = MakeNode(
                        NodeKind::ConstraintExpression,
                        constraintBegin);
                    constraint->Tokens.End = requiresEnd;
                    AddExpressionRange(
                        *constraint,
                        constraintBegin,
                        requiresEnd);
                    Append(*requiresClause, *constraint);
                }
                Append(*lambda, *requiresClause);
                cursor = NextSignificant(requiresEnd);
                continue;
            }
            if (kind == TK::Operator_OpenBracket &&
                Kind(cursor + 1) == TK::Operator_OpenBracket)
            {
                auto attributeEnd = FindBalancedEnd(cursor);
                if (attributeEnd == std::numeric_limits<TokenIndex>::max() ||
                    attributeEnd > end)
                {
                    break;
                }
                auto* attribute =
                    MakeNode(NodeKind::AttributeSpecifier, cursor);
                attribute->Tokens.End = attributeEnd;
                Append(*lambda, *attribute);
                cursor = NextSignificant(attributeEnd);
                continue;
            }
            cursor = NextSignificant(cursor + 1);
        }
        lambda->Tokens.End = cursor;
        return lambda;
    }

    Node* ParseFoldExpression(TokenIndex& cursor, TokenIndex closingEnd)
    {
        auto opening = cursor;
        auto closing = closingEnd - 1;
        size_t ellipsis = closing;
        int depth = 0;
        for (auto i = opening + 1; i < closing; ++i) {
            auto kind = Kind(i);
            if (depth == 0 && kind == TK::Operator_Less && i > opening + 1 &&
                (Kind(PreviousSignificant(i)) == TK::Identifier ||
                    Kind(PreviousSignificant(i)) == TK::Operator_Greater))
            {
                auto templateEnd = SkipTemplateArguments(i, closing);
                if (templateEnd > i + 1 && templateEnd <= closing) {
                    i = templateEnd - 1;
                    continue;
                }
            }
            if (IsOpenDelimiter(kind)) {
                ++depth;
            } else if (kind == TK::Operator_CloseParenthesis ||
                kind == TK::Operator_CloseBracket ||
                kind == TK::Operator_CloseBrace)
            {
                --depth;
            } else if (depth == 0 && kind == TK::Operator_Ellipsis) {
                ellipsis = i;
                break;
            }
        }
        if (ellipsis == closing) {
            return nullptr;
        }

        auto before = PreviousSignificant(ellipsis);
        auto after = NextSignificant(ellipsis + 1);
        bool operatorBefore = before > opening &&
            GetBinaryOperatorInfo(Kind(before)).Precedence != 0;
        bool operatorAfter = after < closing &&
            GetBinaryOperatorInfo(Kind(after)).Precedence != 0;
        if (!operatorBefore && !operatorAfter) {
            return nullptr;
        }

        auto* fold = MakeNode(NodeKind::FoldExpression, opening);
        fold->Tokens.End = closingEnd;
        auto leftEnd = operatorBefore ? before : ellipsis;
        auto leftBegin = NextSignificant(opening + 1);
        if (leftBegin < leftEnd) {
            auto* operand = MakeNode(NodeKind::FoldOperand, leftBegin);
            operand->Tokens.End = leftEnd;
            AddExpressionRange(*operand, leftBegin, leftEnd);
            Append(*fold, *operand);
        }
        if (operatorBefore) {
            auto* op = MakeNode(NodeKind::FoldOperator, before);
            op->Tokens.End = before + 1;
            Append(*fold, *op);
        }
        auto* dots = MakeNode(NodeKind::FoldEllipsis, ellipsis);
        dots->Tokens.End = ellipsis + 1;
        Append(*fold, *dots);
        if (operatorAfter) {
            auto* op = MakeNode(NodeKind::FoldOperator, after);
            op->Tokens.End = after + 1;
            Append(*fold, *op);
        }
        auto rightBegin = operatorAfter
            ? NextSignificant(after + 1)
            : NextSignificant(ellipsis + 1);
        if (rightBegin < closing) {
            auto* operand = MakeNode(NodeKind::FoldOperand, rightBegin);
            operand->Tokens.End = closing;
            AddExpressionRange(*operand, rightBegin, closing);
            Append(*fold, *operand);
        }
        cursor = closingEnd;
        return fold;
    }

    Node* ParseRequiresExpression(TokenIndex& cursor, TokenIndex end)
    {
        auto start = cursor;
        auto* expression = MakeNode(NodeKind::RequiresExpression, start);
        cursor = NextSignificant(cursor + 1);
        if (Kind(cursor) == TK::Operator_OpenParenthesis) {
            auto parameterEnd = FindBalancedEnd(cursor);
            if (parameterEnd == std::numeric_limits<TokenIndex>::max() ||
                parameterEnd > end)
            {
                expression->Tokens.End = cursor;
                return expression;
            }
            auto* parameters = MakeNode(NodeKind::ParameterList, cursor);
            parameters->Tokens.End = parameterEnd;
            AddParameterDeclarations(
                *parameters,
                TokenRange{.Begin = cursor, .End = parameterEnd});
            Append(*expression, *parameters);
            cursor = NextSignificant(parameterEnd);
        }
        if (Kind(cursor) != TK::Operator_OpenBrace) {
            expression->Tokens.End = cursor;
            return expression;
        }
        auto bodyEnd = FindBalancedEnd(cursor);
        if (bodyEnd ==
            std::numeric_limits<TokenIndex>::max() || bodyEnd > end)
        {
            expression->Tokens.End = cursor;
            return expression;
        }
        auto* body = MakeNode(NodeKind::RequirementBody, cursor);
        body->Tokens.End = bodyEnd;
        auto requirementBegin = NextSignificant(cursor + 1);
        int depth = 0;
        for (auto i = requirementBegin; i < bodyEnd - 1; ++i) {
            auto kind = Kind(i);
            if (IsOpenDelimiter(kind)) {
                ++depth;
            } else if (kind == TK::Operator_CloseParenthesis ||
                kind == TK::Operator_CloseBracket ||
                kind == TK::Operator_CloseBrace)
            {
                --depth;
            } else if (depth == 0 && kind == TK::Operator_Semicolon) {
                if (requirementBegin < i) {
                    auto* requirement =
                        MakeNode(NodeKind::Requirement, requirementBegin);
                    requirement->Tokens.End = i;
                    Append(*body, *requirement);
                }
                auto* separator = MakeNode(NodeKind::ListSeparator, i);
                separator->Tokens.End = i + 1;
                Append(*body, *separator);
                requirementBegin = NextSignificant(i + 1);
            }
        }
        if (requirementBegin < bodyEnd - 1) {
            auto* requirement =
                MakeNode(NodeKind::Requirement, requirementBegin);
            requirement->Tokens.End = bodyEnd - 1;
            Append(*body, *requirement);
        }
        AddDirectiveChildrenInGap(*body, cursor, bodyEnd);
        Append(*expression, *body);
        cursor = bodyEnd;
        expression->Tokens.End = bodyEnd;
        return expression;
    }

    Node* ParseNewExpression(TokenIndex& cursor, TokenIndex end)
    {
        auto start = cursor;
        auto* expression = MakeNode(NodeKind::NewExpression, start);
        cursor = NextSignificant(cursor + 1);

        if (Kind(cursor) == TK::Operator_OpenParenthesis) {
            auto parenthesizedEnd = FindBalancedEnd(cursor);
            auto following = NextSignificant(parenthesizedEnd);
            if (parenthesizedEnd != std::numeric_limits<TokenIndex>::max() &&
                parenthesizedEnd <= end && following < end && (IsTypeKeyword(
                    Kind(following)) ||
                    Kind(following) == TK::Identifier ||
                    Kind(following) == TK::Operator_ScopeResolution))
            {
                auto* placement = MakeNode(NodeKind::NewPlacement, cursor);
                placement->Tokens.End = parenthesizedEnd;
                AddArgumentList(*placement, cursor, parenthesizedEnd);
                Append(*expression, *placement);
                cursor = following;
            }
        }

        auto typeBegin = cursor;
        auto typeEnd = cursor;
        while (typeEnd < end) {
            auto kind = Kind(typeEnd);
            if (kind == TK::Operator_Star) {
                // A following array bound makes this a pointer new-type-id,
                // not multiplication with a bare '[' as its right operand.
                auto afterPointers = NextSignificant(typeEnd + 1);
                while (Kind(afterPointers) == TK::Operator_Star) {
                    afterPointers = NextSignificant(afterPointers + 1);
                }
                if (Kind(afterPointers) == TK::Operator_OpenBracket &&
                    afterPointers < end)
                {
                    typeEnd = afterPointers;
                    continue;
                }
            }
            if (kind == TK::Operator_OpenBracket) {
                auto balancedEnd = FindBalancedEnd(typeEnd);
                if (balancedEnd == std::numeric_limits<TokenIndex>::max() ||
                    balancedEnd > end)
                {
                    break;
                }
                typeEnd = balancedEnd;
                continue;
            }
            if (kind == TK::Operator_OpenParenthesis ||
                kind == TK::Operator_OpenBrace || kind == TK::Operator_Comma ||
                kind == TK::Operator_Semicolon ||
                GetBinaryOperatorInfo(kind).Precedence != 0)
            {
                break;
            }
            ++typeEnd;
        }
        if (typeBegin < typeEnd) {
            auto* type = MakeNode(NodeKind::NewTypeId, typeBegin);
            type->Tokens.End = typeEnd;
            AddTypeSyntaxChildren(*type, typeBegin, typeEnd);
            Append(*expression, *type);
        }
        cursor = NextSignificant(typeEnd);
        if (Kind(cursor) == TK::Operator_OpenParenthesis ||
            Kind(cursor) == TK::Operator_OpenBrace)
        {
            auto initializerEnd = FindBalancedEnd(cursor);
            if (initializerEnd != std::numeric_limits<TokenIndex>::max() &&
                initializerEnd <= end)
            {
                auto* initializer = MakeNode(NodeKind::NewInitializer, cursor);
                initializer->Tokens.End = initializerEnd;
                if (Kind(cursor) == TK::Operator_OpenParenthesis) {
                    AddArgumentList(*initializer, cursor, initializerEnd);
                } else {
                    AddBracedInitializerElements(
                        *initializer,
                        cursor,
                        initializerEnd);
                }
                Append(*expression, *initializer);
                cursor = initializerEnd;
            }
        }
        expression->Tokens.End = cursor;
        return expression;
    }

    Node* ParseDeleteExpression(TokenIndex& cursor, TokenIndex end)
    {
        auto start = cursor;
        auto* expression = MakeNode(NodeKind::DeleteExpression, start);
        cursor = NextSignificant(cursor + 1);
        if (Kind(cursor) == TK::Operator_OpenBracket &&
            Kind(NextSignificant(cursor + 1)) == TK::Operator_CloseBracket)
        {
            auto closing = NextSignificant(cursor + 1);
            auto* marker = MakeNode(NodeKind::DeleteArrayMarker, cursor);
            marker->Tokens.End = closing + 1;
            Append(*expression, *marker);
            cursor = NextSignificant(closing + 1);
        }
        if (auto* operand = ParseExpressionSyntax(cursor, end, 14)) {
            Append(*expression, *operand);
            expression->Tokens.End = operand->Tokens.End;
        } else {
            expression->Tokens.End = cursor;
        }
        return expression;
    }

    [[nodiscard]] bool IsUnambiguousTemplateUse(
        TokenIndex following,
        TokenIndex end,
        bool explicitTemplate) const
    {
        if (explicitTemplate ||
            Kind(following) == TK::Operator_ClassMemberAccess ||
            Kind(following) == TK::Operator_IndirectClassMemberAccess)
        {
            return true;
        }
        if (Kind(following) != TK::Operator_OpenParenthesis) {
            return false;
        }
        auto closingEnd = FindBalancedEnd(following);
        return closingEnd <= end &&
            NextSignificant(following + 1) == closingEnd - 1;
    }

    Node* ParseMemberName(TokenIndex& cursor, TokenIndex end)
    {
        cursor = NextSignificant(cursor);
        if (cursor >= end) {
            return nullptr;
        }

        bool explicitTemplate = Kind(cursor) == TK::Keyword_template;
        if (explicitTemplate) {
            cursor = NextSignificant(cursor + 1);
            if (cursor >= end) {
                return nullptr;
            }
        }

        auto nameBegin = cursor;
        if (Kind(cursor) == TK::Operator_Complement) {
            cursor = NextSignificant(cursor + 1);
            if (Kind(cursor) == TK::Identifier) {
                cursor = NextSignificant(cursor + 1);
            }
        } else if (Kind(cursor) == TK::Keyword_operator) {
            cursor = NextSignificant(cursor + 1);
            if (Kind(cursor) == TK::Operator_OpenParenthesis ||
                Kind(cursor) == TK::Operator_OpenBracket)
            {
                auto operatorEnd = FindBalancedEnd(cursor);
                cursor = operatorEnd !=
                    std::numeric_limits<TokenIndex>::max() &&
                    operatorEnd <= end
                    ? NextSignificant(operatorEnd)
                    : NextSignificant(cursor + 1);
            } else if (Kind(cursor) == TK::Keyword_new ||
                Kind(cursor) == TK::Keyword_delete)
            {
                cursor = NextSignificant(cursor + 1);
                if (Kind(cursor) == TK::Operator_OpenBracket) {
                    auto arrayEnd = FindBalancedEnd(cursor);
                    if (arrayEnd != std::numeric_limits<TokenIndex>::max() &&
                        arrayEnd <= end)
                    {
                        cursor = NextSignificant(arrayEnd);
                    }
                }
            } else if (Kind(cursor) == TK::Identifier ||
                IsTypeKeyword(Kind(cursor)))
            {
                do {
                    cursor = NextSignificant(cursor + 1);
                    if (Kind(cursor) == TK::Operator_Less) {
                        auto templateEnd = SkipTemplateArguments(cursor, end);
                        if (templateEnd <= cursor || templateEnd > end) {
                            break;
                        }
                        cursor = NextSignificant(templateEnd);
                    }
                    if (Kind(cursor) == TK::Operator_ScopeResolution) {
                        cursor = NextSignificant(cursor + 1);
                    }
                } while (Kind(cursor) == TK::Identifier ||
                    IsTypeKeyword(Kind(cursor)) ||
                    Kind(cursor) == TK::Operator_Star ||
                    Kind(cursor) == TK::Operator_Ampersand ||
                    Kind(cursor) == TK::Operator_And);
            } else {
                cursor = NextSignificant(cursor + 1);
            }
        } else {
            cursor = NextSignificant(cursor + 1);
        }

        auto* name = MakeNode(NodeKind::IdentifierExpression, nameBegin);
        name->Tokens.End = cursor;
        if (Kind(cursor) != TK::Operator_Less) {
            return name;
        }

        auto templateEnd = SkipTemplateArguments(cursor, end);
        auto closingKind =
            templateEnd > cursor ? Kind(templateEnd - 1) : TK::Invalid;
        auto following = NextSignificant(templateEnd);
        bool hasClosing = templateEnd <= end && (closingKind ==
            TK::Operator_Greater ||
            closingKind == TK::Operator_RightShift);
        bool hasTemplateUse =
            IsUnambiguousTemplateUse(following, end, explicitTemplate);
        if (!hasClosing) {
            return name;
        }
        if (!hasTemplateUse) {
            if (templateEnd != end && IsWordToken(Kind(following))) {
                return name;
            }
            auto* ambiguous = MakeNode(NodeKind::UnparsedSyntax, nameBegin);
            ambiguous->Tokens.End = templateEnd;
            cursor = templateEnd;
            return ambiguous;
        }

        auto* templateId = MakeNode(NodeKind::TemplateIdExpression, nameBegin);
        templateId->Tokens.End = templateEnd;
        Append(*templateId, *name);
        Append(*templateId, *MakeTemplateArgumentList(cursor, templateEnd));
        cursor = templateEnd;
        return templateId;
    }

    Node*
    ParseExpressionSyntax(TokenIndex& cursor, TokenIndex end, int minimum = 1)
    {
        cursor = NextSignificant(cursor);
        if (cursor >= end) {
            return nullptr;
        }

        auto start = cursor;
        Node* left = nullptr;
        auto kind = Kind(cursor);
        if (kind == TK::Keyword_requires) {
            left = ParseRequiresExpression(cursor, end);
        } else if (kind == TK::Keyword_new) {
            left = ParseNewExpression(cursor, end);
        } else if (kind == TK::Keyword_delete) {
            left = ParseDeleteExpression(cursor, end);
        } else if (IsNamedCast(kind)) {
            auto angleBegin = NextSignificant(cursor + 1);
            auto angleEnd = SkipTemplateArguments(angleBegin, end);
            auto opening = NextSignificant(angleEnd);
            if (Kind(angleBegin) == TK::Operator_Less && angleEnd <= end &&
                Kind(opening) == TK::Operator_OpenParenthesis)
            {
                auto closingEnd = FindBalancedEnd(opening);
                if (closingEnd != std::numeric_limits<TokenIndex>::max() &&
                    closingEnd <= end)
                {
                    auto* cast = MakeNode(NodeKind::CastExpression, start);
                    cast->Tokens.End = closingEnd;
                    auto* type = MakeNode(NodeKind::TypeId, angleBegin + 1);
                    type->Tokens.End = angleEnd - 1;
                    AddTypeSyntaxChildren(*type, angleBegin + 1, angleEnd - 1);
                    Append(*cast, *type);
                    auto operandCursor = opening + 1;
                    if (auto* operand = ParseExpressionSyntax(
                        operandCursor, closingEnd - 1))
                    {
                        Append(*cast, *operand);
                    }
                    cursor = closingEnd;
                    left = cast;
                }
            }
            if (!left) {
                left = MakeNode(NodeKind::Expression, cursor);
                left->Tokens.End = ++cursor;
            }
        } else if (IsPrefixOperator(kind)) {
            ++cursor;
            auto operandEnd = end;
            if (kind == TK::Keyword_sizeof || kind == TK::Keyword_alignof) {
                auto opening = NextSignificant(cursor);
                if (Kind(opening) == TK::Operator_OpenParenthesis) {
                    auto closingEnd = FindBalancedEnd(opening);
                    if (closingEnd != std::numeric_limits<TokenIndex>::max() &&
                        closingEnd <= end &&
                        LooksLikeTypeId(opening + 1, closingEnd - 1))
                    {
                        // sizeof(type) is complete at ')': a following -
                        // is binary subtraction, not a cast's unary operand.
                        operandEnd = closingEnd;
                    }
                }
            }
            auto* operand = ParseExpressionSyntax(cursor, operandEnd, 14);
            if (!operand) {
                // A dangling prefix operator cannot be the right operand
                // of a binary expression (`char**` in a type-like macro
                // argument is not `char * *<missing operand>`).
                return nullptr;
            }
            left = MakeNode(NodeKind::UnaryExpression, start);
            left->Tokens.End = operand->Tokens.End;
            Append(*left, *operand);
        } else if (kind == TK::Operator_OpenParenthesis) {
            auto closingEnd = FindBalancedEnd(cursor);
            if (closingEnd == std::numeric_limits<TokenIndex>::max() ||
                closingEnd > end)
            {
                closingEnd = end;
            }
            auto closing = closingEnd == 0 ? 0 : closingEnd - 1;
            auto innerBegin = NextSignificant(cursor + 1);
            auto statementEnd = Kind(innerBegin) == TK::Operator_OpenBrace
                ? FindBalancedEnd(innerBegin)
                : std::numeric_limits<TokenIndex>::max();
            if (statementEnd == closing) {
                auto savedCursor = Cursor_;
                Cursor_ = innerBegin;
                auto* statement =
                    MakeNode(NodeKind::StatementExpression, start);
                statement->Tokens.End = closingEnd;
                ++BranchParseDepth_;
                Append(*statement, *ParseCompoundStatement());
                --BranchParseDepth_;
                Cursor_ = savedCursor;
                cursor = closingEnd;
                left = statement;
            } else if (auto* fold = ParseFoldExpression(cursor, closingEnd)) {
                left = fold;
            } else if (closingEnd < end && LooksLikeTypeId(
                cursor + 1,
                closing))
            {
                auto* cast = MakeNode(NodeKind::CastExpression, start);
                auto* type = MakeNode(NodeKind::TypeId, cursor + 1);
                type->Tokens.End = closing;
                AddTypeSyntaxChildren(*type, cursor + 1, closing);
                Append(*cast, *type);
                cursor = closingEnd;
                if (auto* operand = ParseExpressionSyntax(cursor, end, 14)) {
                    Append(*cast, *operand);
                    cast->Tokens.End = operand->Tokens.End;
                } else {
                    cast->Tokens.End = closingEnd;
                }
                left = cast;
            } else {
                auto innerCursor = cursor + 1;
                auto* inner = ParseExpressionSyntax(innerCursor, closing);
                cursor = closingEnd;
                left = MakeNode(NodeKind::ParenthesizedExpression, start);
                left->Tokens.End = closingEnd;
                if (inner) {
                    Append(*left, *inner);
                }
            }
        } else if (kind == TK::Operator_OpenBracket) {
            left = ParseLambdaExpression(cursor, end);
        } else if (kind == TK::Operator_OpenBrace) {
            auto closingEnd = FindBalancedEnd(cursor);
            if (closingEnd == std::numeric_limits<TokenIndex>::max() ||
                closingEnd > end)
            {
                closingEnd = end;
            }
            cursor = closingEnd;
            left = MakeNode(NodeKind::BracedInitializer, start);
            left->Tokens.End = closingEnd;
            AddBracedInitializerElements(*left, start, closingEnd);
        } else {
            auto nodeKind = IsLiteralExpressionToken(kind)
                ? NodeKind::LiteralExpression
                : (kind == TK::Identifier ||
                kind == TK::Keyword_this
                       ? NodeKind::IdentifierExpression
                       : NodeKind::Expression);
            left = MakeNode(nodeKind, cursor);
            left->Tokens.End = ++cursor;
        }

        while (left && cursor < end) {
            cursor = NextSignificant(cursor);
            if (cursor >= end) {
                break;
            }
            kind = Kind(cursor);
            if (kind == TK::Operator_Less) {
                auto templateEnd = SkipTemplateArguments(cursor, end);
                auto closingKind =
                    templateEnd > cursor ? Kind(templateEnd - 1) : TK::Invalid;
                auto following = NextSignificant(templateEnd);
                bool hasClosing = templateEnd <= end && (closingKind ==
                    TK::Operator_Greater ||
                    closingKind == TK::Operator_RightShift);
                if (hasClosing &&
                    IsUnambiguousTemplateUse(following, end, false))
                {
                    auto* templateId =
                        MakeNode(NodeKind::TemplateIdExpression, start);
                    templateId->Tokens.End = templateEnd;
                    Append(*templateId, *left);
                    Append(
                        *templateId,
                        *MakeTemplateArgumentList(cursor, templateEnd));
                    cursor = templateEnd;
                    left = templateId;
                    continue;
                }
                if (hasClosing &&
                    (templateEnd == end || !IsWordToken(Kind(following))))
                {
                    auto* ambiguous = MakeNode(NodeKind::UnparsedSyntax, start);
                    ambiguous->Tokens.End = templateEnd;
                    cursor = templateEnd;
                    left = ambiguous;
                    continue;
                }
            }
            if (kind == TK::Operator_OpenParenthesis) {
                auto closingEnd = FindBalancedEnd(cursor);
                if (closingEnd == std::numeric_limits<TokenIndex>::max() ||
                    closingEnd > end)
                {
                    break;
                }
                auto* call = MakeNode(NodeKind::CallExpression, start);
                call->Tokens.End = closingEnd;
                Append(*call, *left);
                AddArgumentList(*call, cursor, closingEnd);
                cursor = closingEnd;
                left = call;
                continue;
            }
            if (kind == TK::Operator_OpenBracket) {
                auto closingEnd = FindBalancedEnd(cursor);
                if (closingEnd == std::numeric_limits<TokenIndex>::max() ||
                    closingEnd > end)
                {
                    break;
                }
                auto* subscript =
                    MakeNode(NodeKind::SubscriptExpression, start);
                subscript->Tokens.End = closingEnd;
                Append(*subscript, *left);
                auto indexCursor = cursor + 1;
                auto* index =
                    ParseExpressionSyntax(indexCursor, closingEnd - 1);
                if (index) {
                    Append(*subscript, *index);
                }
                cursor = closingEnd;
                left = subscript;
                continue;
            }
            if (kind == TK::Operator_ClassMemberAccess ||
                kind == TK::Operator_IndirectClassMemberAccess ||
                kind == TK::Operator_ScopeResolution)
            {
                auto memberCursor = NextSignificant(cursor + 1);
                auto* name = ParseMemberName(memberCursor, end);
                if (!name) {
                    break;
                }
                auto* member = MakeNode(NodeKind::MemberExpression, start);
                member->Tokens.End = memberCursor;
                Append(*member, *left);
                Append(*member, *name);
                cursor = memberCursor;
                left = member;
                continue;
            }
            if (kind == TK::Operator_Increment ||
                kind == TK::Operator_Decrement)
            {
                auto* unary = MakeNode(NodeKind::UnaryExpression, start);
                unary->Tokens.End = ++cursor;
                Append(*unary, *left);
                left = unary;
                continue;
            }
            if (kind == TK::Operator_Conditional && minimum <= 3) {
                ++cursor;
                auto* trueExpression = ParseExpressionSyntax(cursor, end, 1);
                cursor = NextSignificant(cursor);
                if (Kind(cursor) != TK::Operator_Colon) {
                    break;
                }
                ++cursor;
                auto* falseExpression = ParseExpressionSyntax(cursor, end, 3);
                auto* conditional =
                    MakeNode(NodeKind::ConditionalExpression, start);
                conditional->Tokens.End =
                    falseExpression ? falseExpression->Tokens.End : cursor;
                Append(*conditional, *left);
                if (trueExpression) {
                    Append(*conditional, *trueExpression);
                }
                if (falseExpression) {
                    Append(*conditional, *falseExpression);
                }
                left = conditional;
                continue;
            }

            auto binary = GetBinaryOperatorInfo(kind);
            if (binary.Precedence < minimum || binary.Precedence == 0) {
                break;
            }
            ++cursor;
            auto* right = ParseExpressionSyntax(
                cursor,
                end,
                binary.RightAssociative
                    ? binary.Precedence
                    : binary.Precedence + 1);
            if (!right) {
                break;
            }
            auto* expression = MakeNode(binary.Kind, start);
            expression->Tokens.End = right->Tokens.End;
            Append(*expression, *left);
            Append(*expression, *right);
            left = expression;
        }
        return left;
    }

    [[nodiscard]] bool
    LooksLikeUnknownTypeDeclaration(size_t first, size_t end) const
    {
        auto cursor = NextSignificant(first + 1);
        bool hasQualifiedType = false;
        while (cursor < end) {
            if (Kind(cursor) == TK::Operator_Less) {
                auto templateEnd = SkipTemplateArguments(cursor, end);
                auto closingKind =
                    templateEnd > cursor ? Kind(templateEnd - 1) : TK::Invalid;
                if (templateEnd <= cursor || templateEnd > end ||
                    (closingKind != TK::Operator_Greater &&
                        closingKind != TK::Operator_RightShift))
                {
                    break;
                }
                cursor = NextSignificant(templateEnd);
                hasQualifiedType = true;
                continue;
            }
            if (Kind(cursor) != TK::Operator_ScopeResolution) {
                break;
            }
            cursor = NextSignificant(cursor + 1);
            if (Kind(cursor) == TK::Keyword_template) {
                cursor = NextSignificant(cursor + 1);
            }
            if (cursor >= end || Kind(cursor) != TK::Identifier) {
                return false;
            }
            cursor = NextSignificant(cursor + 1);
            hasQualifiedType = true;
        }
        if (!hasQualifiedType) {
            return false;
        }
        while (cursor < end && (Kind(cursor) == TK::Keyword_const ||
                Kind(cursor) == TK::Keyword_volatile ||
                Kind(cursor) == TK::Operator_Star ||
                Kind(cursor) == TK::Operator_Ampersand ||
                Kind(cursor) == TK::Operator_And))
        {
            cursor = NextSignificant(cursor + 1);
        }
        return cursor < end && Kind(cursor) == TK::Identifier;
    }

    [[nodiscard]] NodeKind ClassifySimpleStatement(size_t start) const
    {
        auto first = SkipAttributeSpecifiers(start);
        while (Kind(first) == TK::Keyword_alignas &&
            SignificantKind(first + 1) == TK::Operator_OpenParenthesis)
        {
            auto end = FindBalancedEnd(NextSignificant(first + 1));
            if (end == std::numeric_limits<TokenIndex>::max()) {
                break;
            }
            first = SkipAttributeSpecifiers(end);
        }
        if (IsTypeKeyword(Kind(first)) || IsKnownType(first)) {
            return NodeKind::DeclarationStatement;
        }
        if (Kind(first) != TK::Identifier || LookupName(Text(first))) {
            return NodeKind::ExpressionStatement;
        }

        auto next = NextSignificant(first + 1);
        if (Kind(next) == TK::Identifier) {
            return NodeKind::AmbiguousStatement;
        }
        if (LooksLikeUnknownTypeDeclaration(first, Tree_.Tokens_.Size())) {
            return NodeKind::AmbiguousStatement;
        }
        if (Kind(next) == TK::Operator_Star ||
            Kind(next) == TK::Operator_Ampersand ||
            Kind(next) == TK::Operator_And)
        {
            auto name = NextSignificant(next + 1);
            if (Kind(name) == TK::Identifier) {
                return NodeKind::AmbiguousStatement;
            }
        }
        return NodeKind::ExpressionStatement;
    }

    void RegisterDeclaratorNames(const Node& declarator, NameKind nameKind)
    {
        for (auto* part = declarator.FirstChild;
            part;
            part = part->NextSibling)
        {
            if (part->Kind ==
                NodeKind::DeclaratorName && !part->Tokens.Empty())
            {
                AddName(Text(part->Tokens.Begin), nameKind);
            } else if (part->Kind == NodeKind::NestedDeclarator ||
                part->Kind == NodeKind::StructuredBinding)
            {
                RegisterDeclaratorNames(*part, nameKind);
            }
        }
    }

    void RegisterDeclaredNames(const Node& declaration, NameKind nameKind)
    {
        for (auto* list = declaration.FirstChild;
            list;
            list = list->NextSibling)
        {
            if (list->Kind != NodeKind::InitDeclaratorList) {
                continue;
            }
            for (auto* init = list->FirstChild;
                init;
                init = init->NextSibling)
            {
                if (init->Kind != NodeKind::InitDeclarator) {
                    continue;
                }
                for (auto* declarator = init->FirstChild;
                    declarator;
                    declarator = declarator->NextSibling)
                {
                    if (declarator->Kind != NodeKind::Declarator) {
                        continue;
                    }
                    RegisterDeclaratorNames(*declarator, nameKind);
                }
            }
        }
    }

    void RegisterDeclaredValues(const Node& declaration)
    {
        RegisterDeclaredNames(declaration, NameKind::Value);
    }

    Node* ParseSimpleStatement()
    {
        auto start = Cursor_;
        auto nodeKind = ClassifySimpleStatement(start);
        auto* node = MakeNode(nodeKind, start);

        while (!AtEnd()) {
            auto tokenKind = CurrentKind();
            if (tokenKind == TK::Operator_Semicolon) {
                ++Cursor_;
                break;
            }
            if (tokenKind == TK::Operator_CloseBrace) {
                if (Kind(NextSignificant(start)) == TK::Identifier) {
                    nodeKind = NodeKind::UnparsedSyntax;
                    node->Kind = nodeKind;
                } else {
                    ErrorAt(start, "expected semicolon after statement");
                }
                break;
            }
            if (IsOpenDelimiter(tokenKind)) {
                ConsumeBalanced();
            } else {
                ++Cursor_;
            }
        }
        node->Tokens.End = Cursor_;
        if (nodeKind == NodeKind::DeclarationStatement) {
            AddDeclarationSyntax(*node, start, Cursor_);
            AddDirectiveChildrenInGap(*node, start, Cursor_);
            RegisterDeclaredValues(*node);
        } else if (nodeKind == NodeKind::ExpressionStatement) {
            auto expressionEnd = Cursor_;
            if (expressionEnd > start &&
                Kind(expressionEnd - 1) == TK::Operator_Semicolon)
            {
                --expressionEnd;
            }
            auto expressionCursor = start;
            if (auto* expression =
                    ParseExpressionSyntax(expressionCursor, expressionEnd))
            {
                Append(*node, *expression);
            }
            AddUnparsedSyntax(*node, expressionCursor, expressionEnd);
            AddDirectiveChildrenInGap(*node, start, Cursor_);
        } else {
            if (nodeKind == NodeKind::AmbiguousStatement) {
                auto unknown = SkipAttributeSpecifiers(start);
                if (Kind(unknown) == TK::Identifier) {
                    auto* name = MakeNode(NodeKind::UnknownTypeName, unknown);
                    name->Tokens.End = unknown + 1;
                    Append(*node, *name);
                }
            }
            AddDirectiveChildrenInGap(*node, start, Cursor_);
        }
        return node;
    }

    bool ParseTriviaBeforeChild(Node& parent)
    {
        bool consumedConditional = false;
        bool advanced;
        do {
            advanced = false;
            while (CurrentKind() == TK::Comment ||
                CurrentKind() == TK::PreprocessingDirective)
            {
                consumedConditional |=
                    GetDirectiveKind(Cursor_) == DirectiveKind::If;
                ParseTrivia(parent, true);
                advanced = true;
            }
            if (CurrentKind() == TK::Operator_OpenBracket &&
                Kind(Cursor_ + 1) == TK::Operator_OpenBracket)
            {
                auto begin = Cursor_;
                ConsumeBalanced();
                auto* attribute = MakeNode(NodeKind::AttributeSpecifier, begin);
                attribute->Tokens.End = Cursor_;
                Append(parent, *attribute);
                advanced = true;
            }
        } while (advanced);
        return consumedConditional;
    }

    // Specialized parsers already add directive children in nested bodies.
    // Add only directives not covered by any existing child range.
    void AddDirectiveChildrenInGap(Node& parent, size_t begin, size_t end)
    {
        Node* previous = nullptr;
        auto* child = parent.FirstChild;
        for (auto i = Tree_.Tokens_.FindPreprocessingDirective(begin, end);
            i < end;
            i = Tree_.Tokens_.FindPreprocessingDirective(i + 1, end))
        {
            while (child && child->Tokens.End <= i) {
                previous = child;
                child = child->NextSibling;
            }
            if (child && child->Tokens.Begin <= i && i < child->Tokens.End) {
                continue;
            }

            auto* directive = MakeNode(NodeKind::PreprocessingDirective, i);
            directive->Tokens.End = i + 1;
            directive->NextSibling = child;
            if (previous) {
                previous->NextSibling = directive;
            } else {
                parent.FirstChild = directive;
            }
            if (!child) {
                parent.LastChild = directive;
            }
            previous = directive;
        }
    }

    TriviaAttachments& EnsureTriviaAttachments(Node& node)
    {
        if (node.TriviaIndex_ == std::numeric_limits<uint32_t>::max()) {
            node.TriviaIndex_ = Tree_.Trivia_.Add();
        }
        return Tree_.Trivia_[node.TriviaIndex_];
    }

    enum class TriviaPlacement
    {
        Leading,
        Trailing,
        Dangling,
    };

    void InsertTrivia(TriviaList& list, Trivia& trivia)
    {
        if (!list.First || trivia.Source.Begin < list.First->Source.Begin) {
            trivia.Next = list.First;
            list.First = &trivia;
            return;
        }
        auto* previous = list.First;
        while (previous->Next &&
            previous->Next->Source.Begin <= trivia.Source.Begin)
        {
            previous = previous->Next;
        }
        trivia.Next = previous->Next;
        previous->Next = &trivia;
    }

    void AttachComment(Node& node, TokenIndex token, TriviaPlacement placement)
    {
        auto& attachments = EnsureTriviaAttachments(node);
        TriviaList* list = nullptr;
        switch (placement) {
            case TriviaPlacement::Leading:
                list = &attachments.Leading;
                break;
            case TriviaPlacement::Trailing:
                list = &attachments.Trailing;
                break;
            case TriviaPlacement::Dangling:
                list = &attachments.Dangling;
                break;
        }
        auto text = Tree_.Tokens_[token].Text.View(Tree_.Input_);
        auto sourceBegin =
            static_cast<SourceOffset>(text.data() - Tree_.Input_.data());
        auto* trivia = detail::Make<Trivia>(
            *Tree_.Memory_,
            Trivia{
                .Kind = TriviaKind::Comment,
                .Tokens = {.Begin = token, .End = token + 1},
                .Source = {
                    .Begin = sourceBegin,
                    .End = sourceBegin
                            + static_cast<SourceOffset>(text.size()),
                },
            });
        InsertTrivia(*list, *trivia);
    }

    void AttachWhitespace(
        Node& node,
        std::string_view whitespace,
        TokenIndex token,
        TriviaPlacement placement)
    {
        if (whitespace.empty()) {
            return;
        }
        auto& attachments = EnsureTriviaAttachments(node);
        TriviaList* list = nullptr;
        switch (placement) {
            case TriviaPlacement::Leading:
                list = &attachments.Leading;
                break;
            case TriviaPlacement::Trailing:
                list = &attachments.Trailing;
                break;
            case TriviaPlacement::Dangling:
                list = &attachments.Dangling;
                break;
        }
        auto sourceBegin =
            static_cast<SourceOffset>(whitespace.data() - Tree_.Input_.data());
        auto* trivia = detail::Make<Trivia>(
            *Tree_.Memory_,
            Trivia{
                .Kind = TriviaKind::Whitespace,
                .Tokens = {.Begin = token, .End = token},
                .Source = {
                    .Begin = sourceBegin,
                    .End = sourceBegin
                            + static_cast<SourceOffset>(whitespace.size()),
                },
            });
        InsertTrivia(*list, *trivia);
    }

    [[nodiscard]] Node* NextSyntaxSibling(Node* node) const
    {
        for (; node; node = node->NextSibling) {
            if (node->Kind != NodeKind::Comment) {
                return node;
            }
        }
        return nullptr;
    }

    [[nodiscard]] uint32_t SourceLine(TokenIndex token) const
    {
        return Tree_.Tokens_[token].Line;
    }

    void AssociateCommentRange(
        Node& owner,
        TokenIndex begin,
        TokenIndex end,
        Node* previous,
        Node* next)
    {
        for (auto i = Tree_.Tokens_.FindComment(begin, end);
            i < end;
            i = Tree_.Tokens_.FindComment(i + 1, end))
        {
            if (previous && !previous->Tokens.Empty() &&
                SourceLine(previous->Tokens.End - 1) == SourceLine(i))
            {
                AttachComment(*previous, i, TriviaPlacement::Trailing);
            } else if (next) {
                AttachComment(*next, i, TriviaPlacement::Leading);
            } else {
                AttachComment(owner, i, TriviaPlacement::Dangling);
            }
        }
    }

    void AssociateComments(Node& node)
    {
        if (Tree_.Tokens_.FindComment(node.Tokens.Begin, node.Tokens.End) ==
            node.Tokens.End)
        {
            return;
        }
        TokenIndex gapBegin = node.Tokens.Begin;
        Node* previous = nullptr;
        for (auto* child = node.FirstChild; child; child = child->NextSibling) {
            auto* next = child->Kind == NodeKind::Comment
                ? NextSyntaxSibling(child->NextSibling)
                : child;
            if (gapBegin < child->Tokens.Begin) {
                AssociateCommentRange(
                    node,
                    gapBegin,
                    child->Tokens.Begin,
                    previous,
                    next);
            }
            if (child->Kind == NodeKind::Comment) {
                AssociateCommentRange(
                    node,
                    child->Tokens.Begin,
                    child->Tokens.End,
                    previous,
                    next);
            } else {
                AssociateComments(*child);
                previous = child;
            }
            gapBegin = std::max(gapBegin, child->Tokens.End);
        }
        if (gapBegin < node.Tokens.End) {
            AssociateCommentRange(
                node,
                gapBegin,
                node.Tokens.End,
                previous,
                nullptr);
        }
    }

    [[nodiscard]] bool ContainsBlankLine(std::string_view whitespace) const
    {
        int newlines = 0;
        for (size_t i = 0; i < whitespace.size(); ++i) {
            if (whitespace[i] == '\n' &&
                (i == 0 || whitespace[i - 1] != '\\'))
            {
                if (++newlines == 2) {
                    return true;
                }
            }
        }
        return false;
    }

    [[nodiscard]] bool IsTriviaContainer(NodeKind kind) const
    {
        return kind == NodeKind::TranslationUnit ||
            kind == NodeKind::NamespaceDeclaration ||
            kind == NodeKind::RecordDeclaration ||
            kind == NodeKind::EnumDeclaration ||
            kind == NodeKind::CompoundStatement ||
            kind == NodeKind::PreprocessingBranch ||
            kind == NodeKind::LambdaBody;
    }

    void MarkRangeRole(TokenIndex begin, TokenIndex end, PunctuationRole role)
    {
        end = std::min(end, Tree_.Tokens_.Size());
        for (auto i = begin; i < end; ++i) {
            if (Tree_.Tokens_.Role(i) != PunctuationRole::None) {
                Tree_.Tokens_.SetRole(i, role);
            }
        }
    }

    void MarkDelimiterRole(
        const Node& node,
        TK openingKind,
        TK closingKind,
        PunctuationRole role)
    {
        if (!node.Tokens.Empty() && Kind(node.Tokens.Begin) == openingKind &&
            Kind(node.Tokens.End - 1) == closingKind)
        {
            Tree_.Tokens_.SetRole(node.Tokens.Begin, role);
            Tree_.Tokens_.SetRole(node.Tokens.End - 1, role);
            return;
        }
        for (auto i = node.Tokens.Begin; i < node.Tokens.End; ++i) {
            if (Kind(i) != openingKind) {
                continue;
            }
            auto closingEnd = FindBalancedEnd(i);
            if (closingEnd <= node.Tokens.End && closingEnd > i &&
                Kind(closingEnd - 1) == closingKind)
            {
                Tree_.Tokens_.SetRole(i, role);
                Tree_.Tokens_.SetRole(closingEnd - 1, role);
                return;
            }
        }
    }

    // Without an included type declaration, (Name) &value and
    // (Name) *value can be either casts of unary expressions or binary
    // operations on a parenthesized name. Do not assign binary spacing or
    // layout to the operator until the distinction is known.
    [[nodiscard]] bool IsAmbiguousCastOperator(
        const Node& left,
        TK operatorKind) const
    {
        if ((operatorKind != TK::Operator_Ampersand &&
                operatorKind != TK::Operator_Star) ||
            left.Kind != NodeKind::ParenthesizedExpression ||
            left.Tokens.End < left.Tokens.Begin + 3)
        {
            return false;
        }
        const auto end = left.Tokens.End - 1;
        if (Kind(left.Tokens.Begin) != TK::Operator_OpenParenthesis ||
            Kind(end) != TK::Operator_CloseParenthesis ||
            Kind(left.Tokens.Begin + 1) != TK::Identifier)
        {
            return false;
        }
        for (auto i = left.Tokens.Begin + 2; i < end; i += 2) {
            if (i + 1 >= end ||
                Kind(i) != TK::Operator_ScopeResolution ||
                Kind(i + 1) != TK::Identifier)
            {
                return false;
            }
        }
        return true;
    }

    void MarkPunctuationRoles(Node& node)
    {
        switch (node.Kind) {
            case NodeKind::PointerOperator:
            case NodeKind::MemberPointerOperator:
                for (auto i = node.Tokens.Begin; i < node.Tokens.End; ++i) {
                    if (Kind(i) == TK::Operator_Star) {
                        Tree_.Tokens_.SetRole(
                            i,
                            PunctuationRole::PointerDeclarator);
                    } else if (Kind(i) == TK::Operator_Ampersand ||
                    Kind(i) == TK::Operator_And)
                {
                        Tree_.Tokens_.SetRole(
                            i,
                            PunctuationRole::ReferenceDeclarator);
                    }
                }
                break;
            case NodeKind::UnaryExpression:
                MarkRangeRole(
                node.Tokens.Begin,
                node.FirstChild
                    ? node.FirstChild->Tokens.Begin
                    : node.Tokens.End,
                PunctuationRole::UnaryOperator);
                if (node.FirstChild) {
                    MarkRangeRole(
                        node.FirstChild->Tokens.End,
                        node.Tokens.End,
                        PunctuationRole::UnaryOperator);
                }
                break;
            case NodeKind::BinaryExpression:
            case NodeKind::AssignmentExpression:
                if (node.FirstChild && node.FirstChild->NextSibling) {
                    auto begin = node.FirstChild->Tokens.End;
                    auto end = node.FirstChild->NextSibling->Tokens.Begin;
                    for (auto i = begin; i < end; ++i) {
                        auto kind = Kind(i);
                        auto role = node.Kind == NodeKind::AssignmentExpression
                            ? PunctuationRole::AssignmentOperator
                            : PunctuationRole::BinaryOperator;
                        if (i == node.FirstChild->Tokens.End &&
                        IsAmbiguousCastOperator(*node.FirstChild, kind))
                    {
                            role = PunctuationRole::AmbiguousOperator;
                        } else if (kind == TK::Operator_Less ||
                        kind == TK::Operator_Greater ||
                        kind == TK::Operator_LessEquals ||
                        kind == TK::Operator_GreaterEquals ||
                        kind == TK::Operator_Spaceship)
                    {
                            role = PunctuationRole::ComparisonOperator;
                        } else if (kind == TK::Operator_LeftShift ||
                        kind == TK::Operator_RightShift)
                    {
                            role = PunctuationRole::ShiftOperator;
                        }
                        if (GetBinaryOperatorInfo(kind).Precedence != 0) {
                            Tree_.Tokens_.SetRole(i, role);
                        }
                    }
                }
                break;
            case NodeKind::ConditionalExpression:
                for (auto i = node.Tokens.Begin; i < node.Tokens.End; ++i) {
                    if (Kind(i) == TK::Operator_Conditional ||
                    Kind(i) == TK::Operator_Colon)
                {
                        Tree_.Tokens_.SetRole(
                            i,
                            PunctuationRole::ConditionalDelimiter);
                    }
                }
                break;
            case NodeKind::TemplateArgumentList:
            case NodeKind::TemplateParameterList:
                MarkRangeRole(
                    node.Tokens.Begin,
                    std::min(node.Tokens.Begin + 1, node.Tokens.End),
                    PunctuationRole::TemplateDelimiter);
                if (!node.Tokens.Empty()) {
                    Tree_.Tokens_.SetRole(
                        node.Tokens.End - 1,
                        PunctuationRole::TemplateDelimiter);
                }
                break;
            case NodeKind::ListSeparator:
                MarkRangeRole(
                    node.Tokens.Begin,
                    node.Tokens.End,
                    PunctuationRole::ListSeparator);
                break;
            case NodeKind::BaseClause:
                if (!node.Tokens.Empty()) {
                    Tree_.Tokens_.SetRole(
                        node.Tokens.Begin,
                        PunctuationRole::BaseDelimiter);
                }
                break;
            case NodeKind::BitField:
                if (!node.Tokens.Empty()) {
                    Tree_.Tokens_.SetRole(
                        node.Tokens.Begin,
                        PunctuationRole::BitFieldDelimiter);
                }
                break;
            case NodeKind::ConstructorInitializerList:
                if (!node.Tokens.Empty()) {
                    Tree_.Tokens_.SetRole(
                        node.Tokens.Begin,
                        PunctuationRole::ConstructorInitializerDelimiter);
                }
                break;
            case NodeKind::RangeForSeparator:
                MarkRangeRole(
                    node.Tokens.Begin,
                    node.Tokens.End,
                    PunctuationRole::ConditionDelimiter);
                break;
            case NodeKind::CaseLabel:
            case NodeKind::LabelStatement:
            case NodeKind::AccessSpecifier:
                for (auto i = node.Tokens.Begin; i < node.Tokens.End; ++i) {
                    if (Kind(i) == TK::Operator_Colon) {
                        Tree_.Tokens_.SetRole(
                            i,
                            PunctuationRole::LabelDelimiter);
                    }
                }
                break;
            case NodeKind::CompoundStatement:
            case NodeKind::RequirementBody:
                MarkDelimiterRole(
                    node,
                    TK::Operator_OpenBrace,
                    TK::Operator_CloseBrace,
                    PunctuationRole::BlockDelimiter);
                break;
            case NodeKind::RecordDeclaration:
                MarkDelimiterRole(
                    node,
                    TK::Operator_OpenBrace,
                    TK::Operator_CloseBrace,
                    PunctuationRole::RecordDelimiter);
                break;
            case NodeKind::BracedInitializer:
            case NodeKind::InitializerList:
            case NodeKind::NewInitializer:
                MarkDelimiterRole(
                    node,
                    TK::Operator_OpenBrace,
                    TK::Operator_CloseBrace,
                    PunctuationRole::InitializerDelimiter);
                break;
            case NodeKind::AttributeSpecifier:
                MarkDelimiterRole(
                    node,
                    TK::Operator_OpenBracket,
                    TK::Operator_CloseBracket,
                    PunctuationRole::AttributeDelimiter);
                break;
            case NodeKind::LambdaCaptureList:
            case NodeKind::LambdaBody:
                MarkDelimiterRole(
                node,
                node.Kind == NodeKind::LambdaCaptureList
                    ? TK::Operator_OpenBracket
                    : TK::Operator_OpenBrace,
                node.Kind == NodeKind::LambdaCaptureList
                    ? TK::Operator_CloseBracket
                    : TK::Operator_CloseBrace,
                PunctuationRole::LambdaDelimiter);
                break;
            case NodeKind::ArgumentList:
            case NodeKind::NewPlacement:
                MarkDelimiterRole(
                    node,
                    TK::Operator_OpenParenthesis,
                    TK::Operator_CloseParenthesis,
                    PunctuationRole::CallDelimiter);
                break;
            case NodeKind::ParameterList:
            case NodeKind::NestedDeclarator:
            case NodeKind::ArrayDeclarator:
            case NodeKind::StructuredBinding:
                MarkDelimiterRole(
                node,
                node.Kind == NodeKind::ArrayDeclarator ||
                node.Kind == NodeKind::StructuredBinding
                    ? TK::Operator_OpenBracket
                    : TK::Operator_OpenParenthesis,
                node.Kind == NodeKind::ArrayDeclarator ||
                node.Kind == NodeKind::StructuredBinding
                    ? TK::Operator_CloseBracket
                    : TK::Operator_CloseParenthesis,
                PunctuationRole::DeclarationDelimiter);
                break;
            case NodeKind::Condition:
            case NodeKind::ForHeader:
                MarkDelimiterRole(
                    node,
                    TK::Operator_OpenParenthesis,
                    TK::Operator_CloseParenthesis,
                    PunctuationRole::ConditionDelimiter);
                break;
            case NodeKind::ParenthesizedExpression:
            case NodeKind::StatementExpression:
            case NodeKind::FoldExpression:
                MarkDelimiterRole(
                    node,
                    TK::Operator_OpenParenthesis,
                    TK::Operator_CloseParenthesis,
                    PunctuationRole::GroupingDelimiter);
                break;
            case NodeKind::CastExpression:
                MarkDelimiterRole(
                    node,
                    TK::Operator_OpenParenthesis,
                    TK::Operator_CloseParenthesis,
                    PunctuationRole::CastDelimiter);
                break;
            case NodeKind::SubscriptExpression: {
                // A nested subscript's first '[' belongs to its base, not
                // this expression. Mark the bracket after that base instead.
                const auto* base = node.Child();
                auto open = base ? base->Tokens.End : node.Tokens.End;
                if (open < node.Tokens.End &&
                Kind(open) == TK::Operator_OpenBracket &&
                FindBalancedEnd(open) == node.Tokens.End)
            {
                    Tree_.Tokens_.SetRole(open, PunctuationRole::CallDelimiter);
                    Tree_.Tokens_.SetRole(
                        node.Tokens.End - 1,
                        PunctuationRole::CallDelimiter);
                }
                break;
            }
            case NodeKind::FoldOperator:
            case NodeKind::FoldEllipsis:
                MarkRangeRole(
                    node.Tokens.Begin,
                    node.Tokens.End,
                    PunctuationRole::BinaryOperator);
                break;
            default:
                break;
        }
    }

    [[nodiscard]] bool MayContainTriviaContainer(NodeKind kind) const
    {
        switch (kind) {
            case NodeKind::TranslationUnit:
            case NodeKind::PreprocessingConditional:
            case NodeKind::PreprocessingBranch:
            case NodeKind::NamespaceDeclaration:
            case NodeKind::RecordDeclaration:
            case NodeKind::EnumDeclaration:
            case NodeKind::TemplateDeclaration:
            case NodeKind::ExportDeclaration:
            case NodeKind::LinkageSpecification:
            case NodeKind::FunctionDefinition:
            case NodeKind::CompoundStatement:
            case NodeKind::IfStatement:
            case NodeKind::SwitchStatement:
            case NodeKind::WhileStatement:
            case NodeKind::DoWhileStatement:
            case NodeKind::ForStatement:
            case NodeKind::TryStatement:
            case NodeKind::CatchClause:
            case NodeKind::LambdaExpression:
            case NodeKind::LambdaBody:
                return true;
            default:
                return false;
        }
    }

    void AssociateBlankLines(Node& node)
    {
        if (IsTriviaContainer(node.Kind)) {
            for (auto* child = node.FirstChild;
                child;
                child = child->NextSibling)
            {
                if (!child->Tokens.Empty()) {
                    auto whitespace =
                        Tree_.WhitespaceBefore(child->Tokens.Begin);
                    if (ContainsBlankLine(whitespace)) {
                        auto* target = child->Kind == NodeKind::Comment
                            ? NextSyntaxSibling(child->NextSibling)
                            : child;
                        if (target) {
                            AttachWhitespace(
                                *target,
                                whitespace,
                                child->Tokens.Begin,
                                TriviaPlacement::Leading);
                        } else {
                            AttachWhitespace(
                                node,
                                whitespace,
                                child->Tokens.Begin,
                                TriviaPlacement::Dangling);
                        }
                    }
                }
            }
            if (!node.Tokens.Empty()) {
                auto closing = node.Tokens.End - 1;
                auto closingKind = Kind(closing);
                if (closingKind == TK::Operator_CloseBrace ||
                    closingKind == TK::Operator_CloseBracket)
                {
                    auto whitespace = Tree_.WhitespaceBefore(closing);
                    if (ContainsBlankLine(whitespace)) {
                        AttachWhitespace(
                            node,
                            whitespace,
                            closing,
                            TriviaPlacement::Dangling);
                    }
                } else if (node.Kind == NodeKind::TranslationUnit &&
                    node.Tokens.End == Tree_.Tokens_.Size())
                {
                    auto whitespace = Tree_.WhitespaceAfter(closing);
                    if (ContainsBlankLine(whitespace)) {
                        AttachWhitespace(
                            node,
                            whitespace,
                            node.Tokens.End,
                            TriviaPlacement::Dangling);
                    }
                }
            }
        }
        for (auto* child = node.FirstChild; child; child = child->NextSibling) {
            if (child->Kind != NodeKind::Comment &&
                MayContainTriviaContainer(child->Kind))
            {
                AssociateBlankLines(*child);
            }
        }
    }

    SyntaxTree& Tree_;
    ErrorHandler& ErrorHandler_;
    std::pmr::monotonic_buffer_resource ScratchMemory_{
        detail::InitialMemoryResourceSize};
    std::pmr::vector<std::string_view> RecordNames_{&ScratchMemory_};
    NameScope* CurrentScope_ = nullptr;
    Node* NextNode_ = nullptr;
    size_t RemainingNodes_ = 0;
    TokenIndex Cursor_ = 0;
    TokenIndex ParseLimit_ = 0;
    size_t BranchParseDepth_ = 0;
    static constexpr size_t MaximumParseDepth = 256;
    std::array<size_t, MaximumParseDepth> ConditionalOpenBraces_{};
    size_t ConditionalBraceScopeDepth_ = 0;
    size_t ConditionalBraceScopeOverflow_ = 0;
    bool HasDelimiterMap_ = true;
};

SyntaxTree
ParseContext::Run(util::PaddedSourceView input, ErrorHandler errorHandler)
{
    SyntaxTree tree(input.View());
    ParseContext context(tree, errorHandler);
    context.Build();
    return tree;
}

SyntaxTree Parse(util::PaddedSourceView input, ErrorHandler errorHandler)
{
    HIT_FMT_PROFILE_PHASE("parser.total");
    return ParseContext::Run(input, std::move(errorHandler));
}

} // namespace parser
