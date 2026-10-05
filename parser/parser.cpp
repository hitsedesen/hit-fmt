#include "parser.hpp"

#include "operator_info.hpp"

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

TokenIndex detail::TokenPool::FindComment(
    TokenIndex begin,
    TokenIndex end) const
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
    return kind == TK::Identifier ||
        (kind >= TK::Literal_UserDefinedInteger &&
            kind <= TK::Literal_String) ||
        (kind >= TK::Keyword_alignas && kind <= TK::Keyword_while);
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
    : Memory_(std::make_unique<std::pmr::monotonic_buffer_resource>(
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
            if (index >= TokenCount() || token.Kind != Kind(index) || source
                .View()
                .substr(token.Text.Begin, token.Text.End - token.Text.Begin) !=
                TokenText(index))
            {
                return std::nullopt;
            }
            const auto gap = source
                .View()
                .substr(previousEnd, token.Text.Begin - previousEnd);
            const auto oldGap = WhitespaceBefore(index);
            // Escaped newlines between tokens must stay byte-for-byte intact;
            // those inside a token are checked by comparing token spellings.
            if ((gap.find("\\\n") != std::string_view::npos ||
                oldGap.find("\\\n") != std::string_view::npos) && gap != oldGap)
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
    const auto tail = source
        .View()
        .substr(previousEnd);
    const auto oldTail = index ? WhitespaceAfter(index - 1) : Input_;
    if (((tail.find("\\\n") != std::string_view::npos ||
        oldTail.find("\\\n") != std::string_view::npos) && tail != oldTail) ||
        (previousCommentOrDirective &&
            (tail.find('\n') != std::string_view::npos) !=
                (oldTail.find('\n') != std::string_view::npos)))
    {
        return std::nullopt;
    }
    if (error || index != TokenCount()) {
        return std::nullopt;
    }
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

std::optional<SyntaxTree> SyntaxTree::RebindWhitespaceGaps(
    util::PaddedSourceView source) const
{
    HIT_FMT_PROFILE_PHASE("parser.rebind_gaps");
    if (source.Size > util::MaxSourceSize) {
        return std::nullopt;
    }
    std::byte buffer[512];
    std::pmr::monotonic_buffer_resource scratch(buffer, sizeof(buffer));
    std::pmr::vector<WhitespaceEdit> edits(&scratch);
    const auto input = source.View();
    util::SourceOffset oldEnd = 0;
    size_t newEnd = 0;
    auto isWhitespace = [] (char c) {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' ||
            c == '\v';
    };
    for (TokenIndex index = 0; index < TokenCount(); ++index) {
        const auto text = Tokens_[index].Text;
        auto newBegin = newEnd;
        while (newBegin < input.size() && isWhitespace(input[newBegin])) {
            ++newBegin;
        }
        const auto spelling = text.View(Input_);
        if (spelling.size() > input.size() - newBegin ||
            input.substr(newBegin, spelling.size()) != spelling)
        {
            return std::nullopt;
        }
        const auto gap = input.substr(newEnd, newBegin - newEnd);
        if (gap != Input_.substr(oldEnd, text.Begin - oldEnd)) {
            edits.push_back({oldEnd, text.Begin, gap});
        }
        oldEnd = text.End;
        newEnd = newBegin + spelling.size();
    }
    const auto tail = input.substr(newEnd);
    if (tail != Input_.substr(oldEnd)) {
        edits.push_back(
            {oldEnd, static_cast<util::SourceOffset>(Input_.size()), tail});
    }
    return RebindWhitespaceEdits(source, edits);
}

std::optional<SyntaxTree> SyntaxTree::RebindMatchingTokens(
    util::PaddedSourceView source) const
{
    HIT_FMT_PROFILE_PHASE("parser.rebind_matching_tokens");
    if (source.Size > util::MaxSourceSize) {
        return std::nullopt;
    }
    SyntaxTree rebound(source.View());
    bool error = false;
    tokenizer::Tokenizer lexer(source,
        [&] (tokenizer::TokenizationError) { error = true; });
    TokenIndex index = 0;
    for (auto token : lexer) {
        if (index >= TokenCount() || token.Kind != Kind(index) ||
            (token.Kind != tokenizer::TokenKind::Comment && source
                .View()
                .substr(token.Text.Begin, token.Text.Size()) !=
                TokenText(index)))
        {
            return std::nullopt;
        }
        rebound.Tokens_.Add(token);
        ++index;
    }
    if (error || index != TokenCount()) {
        return std::nullopt;
    }
    rebound.Tokens_.Seal();
    for (TokenIndex i = 0; i < TokenCount(); ++i) {
        rebound.Tokens_.SetRole(i, Role(i));
        rebound.Tokens_.SetMatchingDelimiter(i, MatchingDelimiter(i));
    }
    rebound.Root_ = Root_;
    rebound.Original_ = this;
    return rebound;
}

std::optional<SyntaxTree> SyntaxTree::RebindReorderedComments(
    util::PaddedSourceView source) const
{
    HIT_FMT_PROFILE_PHASE("parser.rebind_reordered_comments");
    if (source.Size > util::MaxSourceSize || TokenCount() < 2) {
        return std::nullopt;
    }
    bool error = false;
    tokenizer::Tokenizer lexer(source,
        [&] (tokenizer::TokenizationError) { error = true; });
    std::vector<tokenizer::Token> tokens;
    for (auto token : lexer) {
        tokens.push_back(token);
    }
    if (error || tokens.size() != TokenCount()) {
        return std::nullopt;
    }
    auto same = [&] (TokenIndex oldIndex, TokenIndex newIndex) {
        const auto& token = tokens[newIndex];
        return token.Kind == Kind(oldIndex) &&
            (token.Kind == tokenizer::TokenKind::Comment || source
                .View()
                .substr(token.Text.Begin, token.Text.Size()) ==
                TokenText(oldIndex));
    };
    auto brace = [] (tokenizer::TokenKind kind) {
        return kind == tokenizer::TokenKind::Operator_OpenBrace ||
            kind == tokenizer::TokenKind::Operator_CloseBrace;
    };
    std::vector<TokenIndex> mapped(TokenCount());
    std::vector<bool> movedComment(TokenCount(), false);
    size_t swaps = 0;
    for (TokenIndex i = 0; i < TokenCount();) {
        if (same(i, i)) {
            mapped[i] = i;
            ++i;
        } else if (i + 1 < TokenCount() &&
            ((brace(Kind(i)) && Kind(i + 1) == tokenizer::TokenKind::Comment) ||
                (Kind(i) == tokenizer::TokenKind::Comment &&
                    brace(Kind(i + 1)))) && same(i, i + 1) && same(i + 1, i))
        {
            mapped[i] = i + 1;
            mapped[i + 1] = i;
            movedComment[Kind(i) == tokenizer::TokenKind::Comment ? i : i + 1] =
                true;
            ++swaps;
            i += 2;
        } else {
            return std::nullopt;
        }
    }
    if (!swaps) {
        return std::nullopt;
    }
    SyntaxTree rebound(source.View());
    for (auto token : tokens) {
        rebound.Tokens_.Add(token);
    }
    rebound.Tokens_.Seal();
    for (TokenIndex i = 0; i < TokenCount(); ++i) {
        rebound.Tokens_.SetRole(mapped[i], Role(i));
        auto match = MatchingDelimiter(i);
        if (match != std::numeric_limits<TokenIndex>::max()) {
            if (match >= TokenCount()) {
                return std::nullopt;
            }
            match = mapped[match];
        }
        rebound.Tokens_.SetMatchingDelimiter(mapped[i], match);
    }
    auto* root = detail::Make<Node>(*rebound.Memory_);
    root->Kind = Root().Kind;
    root->FunctionKind_ = Root().FunctionKind_;
    root->Tokens = {0, TokenCount()};
    std::byte buffer[1024];
    std::pmr::monotonic_buffer_resource scratch(buffer, sizeof(buffer));
    std::pmr::vector<std::pair<const Node*, Node*>> pending(&scratch);
    pending.emplace_back(&Root(), root);
    std::pmr::vector<Node*> moved(&scratch);
    while (!pending.empty()) {
        auto [original, clone] = pending.back();
        pending.pop_back();
        for (auto* child = original->Child(); child; child = child->NextSibling)
        {
            auto* copy = detail::Make<Node>(*rebound.Memory_);
            copy->Kind = child->Kind;
            copy->FunctionKind_ = child->FunctionKind_;
            if (child->Tokens.Empty()) {
                const auto index = child->Tokens.Begin;
                auto position = index < TokenCount()
                    ? mapped[index]
                    : TokenCount();
                copy->Tokens = {position, position};
            } else {
                copy->Tokens = {
                    mapped[child->Tokens.Begin],
                    mapped[child->Tokens.End - 1] + 1,
                };
                if (copy->Tokens.Begin >= copy->Tokens.End) {
                    return std::nullopt;
                }
            }
            if (child->Kind == NodeKind::Comment &&
                child->Tokens.End == child->Tokens.Begin + 1 &&
                movedComment[child->Tokens.Begin])
            {
                moved.push_back(copy);
            } else {
                if (clone->LastChild) {
                    clone->LastChild->NextSibling = copy;
                } else {
                    clone->FirstChild = copy;
                }
                clone->LastChild = copy;
            }
            pending.emplace_back(child, copy);
        }
    }
    for (auto* comment : moved) {
        const auto position = comment->Tokens.Begin;
        auto* parent = root;
        while (true) {
            Node* containing = nullptr;
            for (auto* child = parent->FirstChild;
                child;
                child = child->NextSibling)
            {
                if (child->Kind != NodeKind::Comment &&
                    child->Tokens.Begin <= position &&
                    position < child->Tokens.End &&
                    (child->FirstChild ||
                        child->Kind == NodeKind::CompoundStatement))
                {
                    containing = child;
                    break;
                }
            }
            if (!containing) {
                break;
            }
            parent = containing;
        }
        Node* previous = nullptr;
        auto* next = parent->FirstChild;
        while (next && next->Tokens.Begin <= position) {
            previous = next;
            next = next->NextSibling;
        }
        if (parent->Kind == NodeKind::FunctionDefinition && previous &&
            previous->Kind == NodeKind::InitDeclaratorList && next &&
            next->Kind == NodeKind::CompoundStatement &&
            next->Tokens.Begin == position + 1 &&
            previous->Tokens.End == position)
        {
            // Between a function declarator and its body the parser owns
            // the comment as header trivia, not as a comment child.
            auto* header = previous;
            while (header) {
                header->Tokens.End = position + 1;
                Node* tail = nullptr;
                for (auto* child = header->FirstChild;
                    child;
                    child = child->NextSibling)
                {
                    if (child->Tokens.End == position &&
                        (child->Kind == NodeKind::InitDeclarator ||
                            child->Kind == NodeKind::Declarator ||
                            child->Kind == NodeKind::FunctionDeclarator))
                    {
                        tail = child;
                    }
                }
                header = tail;
            }
            continue;
        }
        comment->NextSibling = next;
        if (previous) {
            previous->NextSibling = comment;
        } else {
            parent->FirstChild = comment;
        }
        if (!next) {
            parent->LastChild = comment;
        }
    }
    rebound.Root_ = root;
    return rebound;
}

std::optional<SyntaxTree> SyntaxTree::RebindSortedIncludes(
    util::PaddedSourceView source) const
{
    HIT_FMT_PROFILE_PHASE("parser.rebind_sorted_includes");
    if (source.Size > util::MaxSourceSize) {
        return std::nullopt;
    }
    bool error = false;
    tokenizer::Tokenizer lexer(source,
        [&] (tokenizer::TokenizationError) { error = true; });
    std::vector<tokenizer::Token> tokens;
    for (auto token : lexer) {
        tokens.push_back(token);
    }
    if (error || tokens.size() != TokenCount()) {
        return std::nullopt;
    }
    bool changed = false;
    for (TokenIndex i = 0; i < TokenCount();) {
        if (tokens[i].Kind != Kind(i)) {
            return std::nullopt;
        }
        if (Kind(i) != tokenizer::TokenKind::PreprocessingDirective) {
            if (source
                .View()
                .substr(tokens[i].Text.Begin, tokens[i].Text.Size()) !=
                TokenText(i))
            {
                return std::nullopt;
            }
            ++i;
            continue;
        }
        auto end = i;
        std::vector<std::string_view> before;
        std::vector<std::string_view> after;
        while (end < TokenCount() &&
            Kind(end) == tokenizer::TokenKind::PreprocessingDirective &&
            tokens[end].Kind == Kind(end))
        {
            before.push_back(TokenText(end));
            after.push_back(source
                .View()
                .substr(tokens[end].Text.Begin, tokens[end].Text.Size()));
            ++end;
        }
        auto sortedBefore = before;
        auto sortedAfter = after;
        std::ranges::sort(sortedBefore);
        std::ranges::sort(sortedAfter);
        if (sortedBefore != sortedAfter) {
            return std::nullopt;
        }
        changed |= before != after;
        i = end;
    }
    if (!changed) {
        return std::nullopt;
    }
    SyntaxTree rebound(source.View());
    for (auto token : tokens) {
        rebound.Tokens_.Add(token);
    }
    rebound.Tokens_.Seal();
    for (TokenIndex i = 0; i < TokenCount(); ++i) {
        rebound.Tokens_.SetRole(i, Role(i));
        rebound.Tokens_.SetMatchingDelimiter(i, MatchingDelimiter(i));
    }
    rebound.Root_ = Root_;
    rebound.Original_ = this;
    return rebound;
}

std::optional<SyntaxTree> SyntaxTree::RebindWhitespaceEdits(
    util::PaddedSourceView source,
    std::span<const WhitespaceEdit> edits) const
{
    HIT_FMT_PROFILE_PHASE("parser.rebind_edits");
    if (source.Size > util::MaxSourceSize) {
        return std::nullopt;
    }
    SyntaxTree rebound(source.View());
    int64_t lineDelta = 0;
    int64_t columnDelta = 0;
    uint32_t previousOldLine = 0;
    util::Position previousNewPosition{.Line = 1, .Column = 1};
    auto asciiWhitespace = [] (std::string_view gap) {
        return gap.find_first_not_of(" \t\r\n\f\v") == std::string_view::npos;
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
            source
                .View()
                .substr(newSpan, unchanged.size()) != unchanged)
        {
            return std::nullopt;
        }
        newSpan += unchanged.size();
        if (edit.Text.size() > source.Size - newSpan || source
            .View()
            .substr(newSpan, edit.Text.size()) != edit.Text)
        {
            return std::nullopt;
        }
        newSpan += edit.Text.size();
        oldSpan = edit.End;
    }
    if (newSpan > source.Size || source
        .View()
        .substr(newSpan) != Input_.substr(oldSpan))
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
        bool gapEdited = false;
        while (editIndex < edits.size() && edits[editIndex].Begin <= oldBegin) {
            const auto& edit = edits[editIndex++];
            gapEdited = true;
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
        // Unedited spans were already compared byte-for-byte above.
        const auto oldGap = gapEdited
            ? Input_.substr(oldCursor, oldBegin - oldCursor)
            : std::string_view{};
        const auto newGap = gapEdited
            ? source
                .View()
                .substr(newCursor, newBegin - newCursor)
            : std::string_view{};
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
        if (oldPosition.Line != previousOldLine) {
            columnDelta = 0;
        }
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
                        std::ranges::any_of(
                            previous,
                            [] (unsigned char c) {
                                return c >= 0x80;
                            }))
                    {
                        return std::nullopt;
                    }
                    previousEndColumn =
                        previousNewPosition.Column + previous.size();
                }
                columnDelta = previousEndColumn +
                    static_cast<int64_t>(newGap.size()) - oldPosition.Column;
            } else {
                columnDelta += static_cast<int64_t>(newGap.size()) -
                    static_cast<int64_t>(oldGap.size());
            }
        }
        const auto line = static_cast<int64_t>(oldPosition.Line) + lineDelta;
        const auto column =
            static_cast<int64_t>(oldPosition.Column) + columnDelta;
        if (line < 1 || column < 1 ||
            line > std::numeric_limits<uint32_t>::max() ||
            column > std::numeric_limits<uint32_t>::max())
        {
            return std::nullopt;
        }
        rebound.Tokens_.Add({
            .Kind = token.Kind,
            .Position = {
                .Line = static_cast<uint32_t>(line),
                .Column = static_cast<uint32_t>(column),
            },
            .Text = {
                .Begin = static_cast<util::SourceOffset>(newBegin),
                .End = static_cast<util::SourceOffset>(newBegin + size),
            },
        });
        previousNewPosition = {
            .Line = static_cast<uint32_t>(line),
            .Column = static_cast<uint32_t>(column),
        };
        previousOldLine = oldPosition.Line;
        oldCursor = oldEnd;
        newCursor = newBegin + size;
        previousCommentOrDirective = commentOrDirective;
    }
    size_t lastEditEnd = oldCursor;
    while (editIndex < edits.size()) {
        const auto& edit = edits[editIndex++];
        if (edit.Begin < lastEditEnd || edit.End < edit.Begin ||
            edit.End > Input_.size() || edit.Text.size() > util::MaxSourceSize)
        {
            HIT_FMT_PROFILE_EVENT("edits.invalid_tail_edit");
            return std::nullopt;
        }
        delta +=
            static_cast<int64_t>(edit.Text.size()) - (edit.End - edit.Begin);
        lastEditEnd = edit.End;
    }
    if (static_cast<int64_t>(Input_.size()) + delta !=
        static_cast<int64_t>(source.Size))
    {
        HIT_FMT_PROFILE_EVENT("edits.length_mismatch");
        return std::nullopt;
    }
    const auto oldTail = Input_.substr(oldCursor);
    const auto newTail = source
        .View()
        .substr(newCursor);
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

std::optional<SyntaxTree> SyntaxTree::RebindRemovedNullStatements(
    util::PaddedSourceView source,
    std::span<const TokenIndex> removed) const
{
    HIT_FMT_PROFILE_PHASE("parser.rebind_removed_null");
    if (removed.empty() || source.Size > util::MaxSourceSize) {
        return std::nullopt;
    }
    std::vector<bool> deleted(TokenCount(), false);
    for (auto index : removed) {
        if (index >= TokenCount() || deleted[index] ||
            Kind(index) != tokenizer::TokenKind::Operator_Semicolon)
        {
            return std::nullopt;
        }
        deleted[index] = true;
    }
    std::vector<TokenIndex> mapped(TokenCount() + 1);
    TokenIndex remaining = 0;
    for (TokenIndex index = 0; index < TokenCount(); ++index) {
        mapped[index] = remaining;
        remaining += !deleted[index];
    }
    mapped[TokenCount()] = remaining;

    bool error = false;
    tokenizer::Tokenizer lexer(source,
        [&] (tokenizer::TokenizationError) { error = true; });
    SyntaxTree rebound(source.View());
    std::vector<TokenIndex> originals;
    originals.reserve(remaining);
    TokenIndex oldIndex = 0;
    for (auto token : lexer) {
        while (oldIndex < TokenCount() && deleted[oldIndex]) {
            ++oldIndex;
        }
        if (oldIndex == TokenCount() || token.Kind != Kind(oldIndex) || source
            .View()
            .substr(token.Text.Begin, token.Text.Size()) != TokenText(oldIndex))
        {
            return std::nullopt;
        }
        originals.push_back(oldIndex++);
        rebound.Tokens_.Add(token);
    }
    while (oldIndex < TokenCount() && deleted[oldIndex]) {
        ++oldIndex;
    }
    if (error || oldIndex != TokenCount() || originals.size() != remaining) {
        return std::nullopt;
    }
    rebound.Tokens_.Seal();
    for (TokenIndex index = 0; index < remaining; ++index) {
        const auto original = originals[index];
        rebound.Tokens_.SetRole(index, Role(original));
        const auto match = MatchingDelimiter(original);
        if (match != std::numeric_limits<TokenIndex>::max() &&
            (match >= TokenCount() || deleted[match]))
        {
            return std::nullopt;
        }
        rebound.Tokens_.SetMatchingDelimiter(
            index,
            match == std::numeric_limits<TokenIndex>::max()
            ? match
            : mapped[match]);
    }

    auto* root = detail::Make<Node>(*rebound.Memory_);
    root->Kind = Root().Kind;
    root->FunctionKind_ = Root().FunctionKind_;
    root->Tokens = {mapped[Root().Tokens.Begin], mapped[Root().Tokens.End]};
    std::byte buffer[1024];
    std::pmr::monotonic_buffer_resource scratch(buffer, sizeof(buffer));
    std::pmr::vector<std::pair<const Node*, Node*>> pending(&scratch);
    pending.emplace_back(&Root(), root);
    while (!pending.empty()) {
        auto [original, clone] = pending.back();
        pending.pop_back();
        for (auto* child = original->Child(); child; child = child->NextSibling)
        {
            const auto first = child->Tokens.Begin;
            const auto last = child->Tokens.End;
            if (first > last || last > TokenCount()) {
                return std::nullopt;
            }
            if (last == first + 1 && deleted[first] &&
                (child->Kind == NodeKind::ExpressionStatement ||
                    child->Kind == NodeKind::EmptyDeclaration))
            {
                continue;
            }
            if (first < last && mapped[first] == mapped[last]) {
                return std::nullopt;
            }
            auto* copy = detail::Make<Node>(*rebound.Memory_);
            copy->Kind = child->Kind;
            copy->FunctionKind_ = child->FunctionKind_;
            copy->Tokens = {mapped[first], mapped[last]};
            if (clone->LastChild) {
                clone->LastChild->NextSibling = copy;
            } else {
                clone->FirstChild = copy;
            }
            clone->LastChild = copy;
            pending.emplace_back(child, copy);
        }
    }
    rebound.Root_ = root;
    return rebound;
}

std::optional<SyntaxTree> SyntaxTree::RebindControlBraces(
    util::PaddedSourceView source,
    std::span<const InsertedControlBlock> blocks) const
{
    HIT_FMT_PROFILE_PHASE("parser.rebind_control_braces");
    if (blocks.empty() || source.Size > util::MaxSourceSize) {
        return std::nullopt;
    }
    struct Insertion
    {
        TokenIndex At;
        size_t Block;
        bool Closing;
    };
    std::vector<Insertion> insertions;
    std::vector<bool> removed(TokenCount(), false);
    insertions.reserve(blocks.size() * 2);
    for (size_t i = 0; i < blocks.size(); ++i) {
        const auto* body = blocks[i].Body;
        if (!body || body->Tokens.Empty() || body->Tokens.End > TokenCount() ||
            (blocks[i].ReplacesNull &&
                (body->Tokens.End != body->Tokens.Begin + 1 ||
                    Kind(body->Tokens.Begin) !=
                        tokenizer::TokenKind::Operator_Semicolon ||
                    removed[body->Tokens.Begin])))
        {
            return std::nullopt;
        }
        insertions.push_back({body->Tokens.Begin, i, false});
        insertions.push_back({body->Tokens.End, i, true});
        if (blocks[i].ReplacesNull) {
            removed[body->Tokens.Begin] = true;
        }
    }
    std::ranges::sort(
        insertions,
        [&] (const Insertion& left, const Insertion& right) {
            if (left.At != right.At) {
                return left.At < right.At;
            }
            if (left.Closing != right.Closing) {
                return left.Closing;
            }
            return left.Closing
                ? blocks[left.Block].Body->Tokens.Begin >
                    blocks[right.Block].Body->Tokens.Begin
                : blocks[left.Block].Body->Tokens.End >
                    blocks[right.Block].Body->Tokens.End;
        });
    // Node beginnings exclude injected opening braces; node endings include
    // closing braces belonging to the surrounding control statement.
    std::vector<TokenIndex> begins(TokenCount() + 1);
    std::vector<TokenIndex> ends(TokenCount() + 1);
    std::vector<TokenIndex> originalTokens(TokenCount());
    std::vector<TokenIndex> openings(blocks.size());
    std::vector<TokenIndex> closings(blocks.size());
    SyntaxTree rebound(source.View());
    bool error = false;
    tokenizer::Tokenizer lexer(source,
        [&] (tokenizer::TokenizationError) { error = true; });
    size_t nextInsertion = 0;
    for (TokenIndex i = 0; i <= TokenCount(); ++i) {
        while (nextInsertion < insertions.size() &&
            insertions[nextInsertion].At == i &&
            insertions[nextInsertion].Closing)
        {
            auto token = lexer.Get();
            if (token.Kind != tokenizer::TokenKind::Operator_CloseBrace) {
                return std::nullopt;
            }
            auto index = rebound.Tokens_.Size();
            closings[insertions[nextInsertion].Block] = index;
            rebound.Tokens_.Add(token);
            ++nextInsertion;
        }
        ends[i] = rebound.Tokens_.Size();
        while (nextInsertion < insertions.size() &&
            insertions[nextInsertion].At == i)
        {
            auto token = lexer.Get();
            if (token.Kind != tokenizer::TokenKind::Operator_OpenBrace) {
                return std::nullopt;
            }
            auto index = rebound.Tokens_.Size();
            openings[insertions[nextInsertion].Block] = index;
            rebound.Tokens_.Add(token);
            ++nextInsertion;
        }
        begins[i] = rebound.Tokens_.Size();
        if (i == TokenCount()) {
            break;
        }
        if (removed[i]) {
            continue;
        }
        originalTokens[i] = rebound.Tokens_.Size();
        auto token = lexer.Get();
        if (token.Kind != Kind(i) || source
            .View()
            .substr(token.Text.Begin, token.Text.Size()) != TokenText(i))
        {
            return std::nullopt;
        }
        rebound.Tokens_.Add(token);
    }
    if (error || nextInsertion != insertions.size() ||
        lexer.Get().Kind != tokenizer::TokenKind::EndOfFile)
    {
        return std::nullopt;
    }
    rebound.Tokens_.Seal();
    for (TokenIndex i = 0; i < TokenCount(); ++i) {
        if (removed[i]) {
            continue;
        }
        const auto index = originalTokens[i];
        rebound.Tokens_.SetRole(index, Role(i));
        auto match = MatchingDelimiter(i);
        if (match != std::numeric_limits<TokenIndex>::max()) {
            if (match >= TokenCount() || removed[match]) {
                return std::nullopt;
            }
            match = originalTokens[match];
        }
        rebound.Tokens_.SetMatchingDelimiter(index, match);
    }
    for (size_t i = 0; i < blocks.size(); ++i) {
        rebound.Tokens_.SetRole(openings[i], PunctuationRole::BlockDelimiter);
        rebound.Tokens_.SetRole(closings[i], PunctuationRole::BlockDelimiter);
        rebound.Tokens_.SetMatchingDelimiter(openings[i], closings[i]);
        rebound.Tokens_.SetMatchingDelimiter(closings[i], openings[i]);
    }

    auto* root = detail::Make<Node>(*rebound.Memory_);
    root->Kind = Root().Kind;
    root->FunctionKind_ = Root().FunctionKind_;
    root->Tokens = {begins[Root().Tokens.Begin], ends[Root().Tokens.End]};
    std::byte buffer[1024];
    std::pmr::monotonic_buffer_resource scratch(buffer, sizeof(buffer));
    std::pmr::vector<std::pair<const Node*, Node*>> pending(&scratch);
    pending.emplace_back(&Root(), root);
    std::vector<bool> wrapped(blocks.size(), false);
    while (!pending.empty()) {
        auto [original, clone] = pending.back();
        pending.pop_back();
        for (auto* child = original->Child(); child; child = child->NextSibling)
        {
            size_t block = 0;
            while (block < blocks.size() && blocks[block].Body != child) {
                ++block;
            }
            Node* parent = clone;
            if (block < blocks.size()) {
                if (wrapped[block]) {
                    return std::nullopt;
                }
                wrapped[block] = true;
                auto* wrapper = detail::Make<Node>(*rebound.Memory_);
                wrapper->Kind = NodeKind::CompoundStatement;
                wrapper->Tokens = {openings[block], closings[block] + 1};
                if (clone->LastChild) {
                    clone->LastChild->NextSibling = wrapper;
                } else {
                    clone->FirstChild = wrapper;
                }
                clone->LastChild = wrapper;
                parent = wrapper;
                if (blocks[block].ReplacesNull) {
                    continue;
                }
            }
            auto* copy = detail::Make<Node>(*rebound.Memory_);
            copy->Kind = child->Kind;
            copy->FunctionKind_ = child->FunctionKind_;
            copy->Tokens = {
                begins[child->Tokens.Begin],
                block < blocks.size()
                    ? closings[block]
                    : ends[child->Tokens.End],
            };
            if (parent->LastChild) {
                parent->LastChild->NextSibling = copy;
            } else {
                parent->FirstChild = copy;
            }
            parent->LastChild = copy;
            pending.emplace_back(child, copy);
        }
    }
    if (std::ranges::find(wrapped, false) != wrapped.end()) {
        return std::nullopt;
    }
    rebound.Root_ = root;
    return rebound;
}

SyntaxTree SyntaxTree::TokenizeOnly(util::PaddedSourceView source)
{
    SyntaxTree tree(source.View());
    tokenizer::Tokenizer lexer(source,
        [] (tokenizer::TokenizationError) { });
    for (auto token : lexer) {
        tree.Tokens_.Add(token);
    }
    tree.Tokens_.Seal();
    return tree;
}

SyntaxTree SyntaxTree::CloneSyntax(util::PaddedSourceView source) const
{
    ASSERT(source.View() == Source());
    SyntaxTree clone(source.View());
    for (TokenIndex i = 0; i < TokenCount(); ++i) {
        clone.Tokens_.Add(Token(i));
    }
    clone.Tokens_.Seal();
    for (TokenIndex i = 0; i < TokenCount(); ++i) {
        clone.Tokens_.SetRole(i, Role(i));
        clone.Tokens_.SetMatchingDelimiter(i, MatchingDelimiter(i));
    }
    auto* root = detail::Make<Node>(*clone.Memory_);
    root->Kind = Root().Kind;
    root->FunctionKind_ = Root().FunctionKind_;
    root->Tokens = Root().Tokens;
    std::byte buffer[1024];
    std::pmr::monotonic_buffer_resource scratch(buffer, sizeof(buffer));
    std::pmr::vector<std::pair<const Node*, Node*>> pending(&scratch);
    pending.emplace_back(&Root(), root);
    while (!pending.empty()) {
        auto [original, copy] = pending.back();
        pending.pop_back();
        for (auto* child = original->Child(); child; child = child->NextSibling)
        {
            auto* next = detail::Make<Node>(*clone.Memory_);
            next->Kind = child->Kind;
            next->FunctionKind_ = child->FunctionKind_;
            next->Tokens = child->Tokens;
            if (copy->LastChild) {
                copy->LastChild->NextSibling = next;
            } else {
                copy->FirstChild = next;
            }
            copy->LastChild = next;
            pending.emplace_back(child, next);
        }
    }
    clone.Root_ = root;
    return clone;
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
    if (Original_) {
        return nullptr;
    }
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
    [[nodiscard]] static SyntaxTree Run(
        util::PaddedSourceView input,
        ErrorHandler errorHandler,
        const ParseOptions& options);

    ParseContext(
        SyntaxTree& tree,
        ErrorHandler& errorHandler,
        const ParseOptions& options)
        : Tree_(tree)
        , ErrorHandler_(errorHandler)
    {
        FunctionBodyMacroNames_.reserve(options.FunctionBodyMacroNames.size());
        for (const auto& name : options.FunctionBodyMacroNames) {
            FunctionBodyMacroNames_.push_back(name);
        }
        std::sort(
            FunctionBodyMacroNames_.begin(),
            FunctionBodyMacroNames_.end());
        ClassMacroPairs_.reserve(options.ClassMacroNames.size());
        ClassEndMacroNames_.reserve(options.ClassMacroNames.size());
        for (std::string_view pair : options.ClassMacroNames) {
            const auto colon = pair.find(':');
            // Public preparation validates pairs. Do not infer a class from
            // incomplete metadata supplied directly to the parser.
            if (colon == std::string_view::npos || colon == 0 ||
                colon + 1 == pair.size())
            {
                continue;
            }
            ClassMacroPairs_
                .push_back({pair.substr(0, colon), pair.substr(colon + 1)});
            ClassEndMacroNames_.push_back(pair.substr(colon + 1));
        }
        std::sort(ClassMacroPairs_.begin(), ClassMacroPairs_.end());
        std::sort(ClassEndMacroNames_.begin(), ClassEndMacroNames_.end());
    }

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

    struct NameScope;

    struct NameBinding
    {
        std::string_view Name;
        NameKind Kind;
        NameBinding* Next = nullptr;
        NameScope* Members = nullptr;
    };

    struct NameScope
    {
        NameScope* Parent = nullptr;
        NameBinding* Bindings = nullptr;
    };

    void PushScope()
    {
        CurrentScope_ =
            detail::Make<NameScope>(
                ScratchMemory_,
                NameScope{.Parent = CurrentScope_});
    }

    void PopScope()
    {
        ASSERT(CurrentScope_)
        CurrentScope_ = CurrentScope_->Parent;
    }

    NameBinding* AddName(std::string_view name, NameKind kind)
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
        return binding;
    }

    [[nodiscard]] const NameBinding* FindName(
        const NameScope& scope,
        std::string_view name) const
    {
        for (auto* binding = scope.Bindings; binding; binding = binding->Next) {
            if (binding->Name == name) {
                return binding;
            }
        }
        return nullptr;
    }

    [[nodiscard]] const NameBinding* LookupBinding(
        std::string_view name,
        bool global = false) const
    {
        if (global) {
            auto* scope = CurrentScope_;
            while (scope->Parent) {
                scope = scope->Parent;
            }
            return FindName(*scope, name);
        }
        for (auto* scope = CurrentScope_; scope; scope = scope->Parent) {
            if (auto* binding = FindName(*scope, name)) {
                return binding;
            }
        }
        return nullptr;
    }

    [[nodiscard]] std::optional<NameKind> LookupName(
        std::string_view name) const
    {
        if (auto* binding = LookupBinding(name)) {
            return binding->Kind;
        }
        return std::nullopt;
    }

    [[nodiscard]] bool IsKnownQualifiedType(
        TokenIndex first,
        TokenIndex end) const
    {
        auto* binding = LookupBinding(Text(first));
        auto scope = NextSignificant(first + 1);
        if (!binding || binding->Kind != NameKind::Type) {
            return false;
        }
        if (Kind(scope) != TK::Operator_ScopeResolution) {
            return false;
        }
        do {
            auto member = NextSignificant(scope + 1);
            if (member >= end || Kind(member) != TK::Identifier ||
                !binding->Members)
            {
                return false;
            }
            binding = FindName(*binding->Members, Text(member));
            if (!binding || binding->Kind != NameKind::Type) {
                return false;
            }
            scope = NextSignificant(member + 1);
            if (Kind(scope) == TK::Operator_Less) {
                return false; // Member specializations may be in includes.
            }
        } while (scope < end && Kind(scope) == TK::Operator_ScopeResolution);
        return true;
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
                openings.push_back(
                    {.Index = index, .Closing = ClosingDelimiter(token.Kind)});
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
            NextNode_ = static_cast<Node*>(Tree_.Memory_
                ->allocate(sizeof(Node) * NodesPerBlock, alignof(Node)));
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
            for (auto roleKind :
                {
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

    void ParseDeclarationSequence(
        Node& parent,
        TK closing,
        bool macroRecord = false)
    {
        PushConditionalBraceScope();
        while (!AtEnd()) {
            if (macroRecord && CurrentKind() == TK::Identifier &&
                std::binary_search(
                    ClassEndMacroNames_.begin(),
                    ClassEndMacroNames_.end(),
                    Text(Cursor_)) &&
                ClassMacroInvocationEnd(Cursor_, Text(Cursor_)) !=
                    std::numeric_limits<TokenIndex>::max())
            {
                break;
            }
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
        if (!macroRecord && closing != TK::EndOfFile && AtEnd()) {
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
        while (!text.empty() &&
            (text.front() == ' ' || text.front() == '\t' ||
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
                        } else if (nestedDepth == 0 &&
                            (nestedKind == DirectiveKind::ElseIf ||
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
                        ConditionalOpenBraces_[ConditionalBraceScopeDepth_ -
                            1] += static_cast<size_t>(braceDelta);
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

    [[nodiscard]] TokenIndex ClassMacroInvocationEnd(
        TokenIndex first,
        std::string_view name) const
    {
        constexpr auto none = std::numeric_limits<TokenIndex>::max();
        if (Kind(first) != TK::Identifier || Text(first) != name) {
            return none;
        }
        const auto arguments = NextSignificant(first + 1);
        if (Kind(arguments) != TK::Operator_OpenParenthesis) {
            return none;
        }
        const auto end = FindBalancedEnd(arguments);
        if (end == none || end > ParseLimit_) {
            return none;
        }
        for (auto token = first + 1; token < end; ++token) {
            if (Kind(token) == TK::PreprocessingDirective) {
                return none;
            }
        }
        return end;
    }

    Node* TryParseClassMacro()
    {
        if (ClassMacroPairs_.empty()) {
            return nullptr;
        }
        const auto first = NextSignificant(Cursor_);
        if (Kind(first) != TK::Identifier) {
            return nullptr;
        }
        const auto pair = std::lower_bound(
            ClassMacroPairs_.begin(),
            ClassMacroPairs_.end(),
            Text(first),
            [] (const auto& pair, std::string_view name) {
                return pair.first < name;
            });
        if (pair == ClassMacroPairs_.end() || pair->first != Text(first)) {
            return nullptr;
        }
        const auto beginEnd = ClassMacroInvocationEnd(first, pair->first);
        if (beginEnd == std::numeric_limits<TokenIndex>::max()) {
            return nullptr;
        }
        // Macro nesting is independent of physical delimiter nesting. Bound
        // it explicitly rather than allowing input-dependent stack growth.
        if (MacroRecordDepth_ == MaximumParseDepth) {
            auto* node = MakeNode(NodeKind::UnparsedSyntax, Cursor_);
            ErrorAt(first, "macro class nesting is too deep");
            node->Tokens.End = Cursor_ = beginEnd;
            return node;
        }
        auto* node = MakeNode(NodeKind::MacroRecordDeclaration, Cursor_);
        auto* header = MakeNode(NodeKind::MacroRecordBegin, first);
        header->Tokens.End = beginEnd;
        Append(*node, *header);
        Cursor_ = beginEnd;
        PushScope();
        // No expanded class identity is known. Hide an enclosing record name
        // and keep member bindings local to this generated class.
        RecordNames_.push_back({});
        ++MacroRecordDepth_;
        ParseDeclarationSequence(*node, TK::Operator_CloseBrace, true);
        --MacroRecordDepth_;
        RecordNames_.pop_back();
        PopScope();
        const auto end = ClassMacroInvocationEnd(Cursor_, pair->second);
        if (end == std::numeric_limits<TokenIndex>::max()) {
            ErrorAt(first, "expected matching class end macro");
            AddMissingToken(*node, Cursor_);
        } else {
            auto* footer = MakeNode(NodeKind::MacroRecordEnd, Cursor_);
            Cursor_ = end;
            if (CurrentKind() == TK::Operator_Semicolon) {
                ++Cursor_;
            }
            footer->Tokens.End = Cursor_;
            Append(*node, *footer);
        }
        node->Tokens.End = Cursor_;
        return node;
    }

    Node* TryParseFunctionBodyMacro()
    {
        if (FunctionBodyMacroNames_.empty()) {
            return nullptr;
        }
        const auto first = NextSignificant(Cursor_);
        auto macro = first;
        // Function-like macros can follow storage and function specifiers.
        // The complete header remains opaque, including these prefixes.
        while (Kind(macro) == TK::Keyword_static ||
            Kind(macro) == TK::Keyword_extern ||
            Kind(macro) == TK::Keyword_inline ||
            Kind(macro) == TK::Keyword_constexpr ||
            Kind(macro) == TK::Keyword_consteval ||
            Kind(macro) == TK::Keyword_friend)
        {
            macro = NextSignificant(macro + 1);
        }
        if (Kind(macro) != TK::Identifier ||
            !std::binary_search(
                FunctionBodyMacroNames_.begin(),
                FunctionBodyMacroNames_.end(),
                Text(macro)))
        {
            return nullptr;
        }
        const auto arguments = NextSignificant(macro + 1);
        if (Kind(arguments) != TK::Operator_OpenParenthesis) {
            return nullptr;
        }
        const auto headerEnd = FindBalancedEnd(arguments);
        if (headerEnd == std::numeric_limits<TokenIndex>::max() ||
            headerEnd > ParseLimit_)
        {
            return nullptr;
        }
        for (auto token = arguments; token < headerEnd; ++token) {
            if (Kind(token) == TK::PreprocessingDirective) {
                return nullptr;
            }
        }
        const auto body = NextSignificant(headerEnd);
        if (Kind(body) != TK::Operator_OpenBrace) {
            return nullptr;
        }
        const auto bodyEnd = FindBalancedEnd(body);
        if (bodyEnd == std::numeric_limits<TokenIndex>::max() ||
            bodyEnd > ParseLimit_)
        {
            return nullptr;
        }
        auto* node = MakeNode(NodeKind::MacroFunctionDefinition, Cursor_);
        auto* header = MakeNode(NodeKind::MacroFunctionHeader, first);
        header->Tokens.End = headerEnd;
        Append(*node, *header);
        Cursor_ = headerEnd;
        ParseTriviaBeforeChild(*node);
        // Macro arguments are opaque, not C++ parameter declarations. Do not
        // register their names or infer the expanded function's identity.
        Append(*node, *ParseCompoundStatement());
        node->Tokens.End = Cursor_;
        return node;
    }

    Node* ParseDeclaration()
    {
        if (auto* macro = TryParseClassMacro()) {
            return macro;
        }
        if (auto* macro = TryParseFunctionBodyMacro()) {
            return macro;
        }
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
            case TK::Operator_CloseBracket: {
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

    void AddModuleDeclarationSyntax(
        Node& declaration,
        size_t begin,
        size_t end,
        bool import)
    {
        auto nameBegin = NextSignificant(begin + 1);
        auto nameEnd = end;
        if (nameEnd > nameBegin && Kind(nameEnd - 1) == TK::Operator_Semicolon)
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

    void AddConceptDeclarationSyntax(
        Node& declaration,
        size_t begin,
        size_t end)
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
        } else if (kind == NodeKind::UsingDeclaration) {
            AddUsingDeclarationSyntax(*node, start, Cursor_);
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
                if (auto* expression =
                    ParseExpressionSyntax(expressionCursor, expressionEnd))
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
            while (typeBegin < end &&
                (Kind(typeBegin) == TK::Keyword_public ||
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
        if (kind == NodeKind::TemplateArgument ||
            kind == NodeKind::TemplateParameter)
        {
            AddTypeSyntaxChildren(element, begin, end);
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
        unsigned conditionalMiddle = 0;
        size_t explicitMemberWork = 4096;
        for (auto i = begin; i <= end; ++i) {
            auto kind = Kind(i);
            if (i < end && delimiterDepth == 0 && kind == TK::Keyword_template)
            {
                const auto nameEnd = ArgumentExplicitMemberNameEnd(
                    i,
                    begin,
                    end,
                    explicitMemberWork);
                if (nameEnd) {
                    // Consume only the operator name here; this list's normal
                    // angle tracking still owns the following template span.
                    i = *nameEnd - 1;
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
                } else if (trackAngles && delimiterDepth == 0) {
                    // Balanced operand interiors cannot open or close the
                    // template nesting that owns this list's separators.
                    if (kind == TK::Operator_Less) {
                        ++angleDepth;
                    } else if (kind == TK::Operator_Greater && angleDepth > 0) {
                        --angleDepth;
                    } else if (kind == TK::Operator_RightShift &&
                        angleDepth > 0)
                    {
                        angleDepth = std::max(0, angleDepth - 2);
                    }
                }
            }
            if (i < end && delimiterDepth == 0 && angleDepth == 0 &&
                !AdvanceConditionalMiddle(kind, conditionalMiddle))
            {
                i = end;
            }
            bool separator =
                i < end && kind == TK::Operator_Comma && delimiterDepth == 0 &&
                    angleDepth == 0 && conditionalMiddle == 0;
            if (i != end && !separator) {
                continue;
            }
            if (elementBegin < i) {
                auto* element = MakeNode(elementKind, elementBegin);
                // Nested template lists may share one indivisible >> token.
                // Include it in the last element so its nested list can own
                // the complete token; the outer list shares that closer.
                const auto elementEnd = (elementKind ==
                    NodeKind::TemplateArgument ||
                    elementKind == NodeKind::TemplateParameter) && i == end &&
                    Kind(i) == TK::Operator_RightShift && angleDepth > 0
                    ? i + 1
                    : i;
                element->Tokens.End = elementEnd;
                if (conditionalMiddle != 0) {
                    AddUnparsedSyntax(*element, elementBegin, i);
                } else {
                    AddSeparatedElementSyntax(
                        *element,
                        elementKind,
                        elementBegin,
                        elementEnd);
                }
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

    Node* MakeTemplateList(NodeKind kind, size_t opening, size_t end)
    {
        auto* arguments = MakeNode(kind, opening);
        arguments->Tokens.End = end;
        if (end <= opening + 1 ||
            (Kind(end - 1) != TK::Operator_Greater &&
                Kind(end - 1) != TK::Operator_RightShift))
        {
            AddUnparsedSyntax(*arguments, opening + 1, end);
            return arguments;
        }
        if (TemplateListDepth_ >= 64) {
            AddUnparsedSyntax(*arguments, opening + 1, end - 1);
            return arguments;
        }
        ++TemplateListDepth_;
        if (opening + 1 < end - 1) {
            AddSeparatedElements(
                *arguments,
                opening + 1,
                end - 1,
                kind == NodeKind::TemplateParameterList
                ? NodeKind::TemplateParameter
                : NodeKind::TemplateArgument);
        }
        bool expectingArgument = true;
        bool malformed = false;
        for (auto* child = arguments->FirstChild;
            child;
            child = child->NextSibling)
        {
            if (child->Kind == NodeKind::ListSeparator) {
                malformed |= expectingArgument;
                expectingArgument = true;
            } else {
                expectingArgument = false;
            }
        }
        if (malformed || (arguments->FirstChild && expectingArgument)) {
            arguments->FirstChild = arguments->LastChild = nullptr;
            AddUnparsedSyntax(*arguments, opening + 1, end - 1);
        }
        AddDirectiveChildrenInGap(*arguments, opening, end);
        --TemplateListDepth_;
        return arguments;
    }

    Node* MakeTemplateArgumentList(size_t opening, size_t end)
    {
        return MakeTemplateList(NodeKind::TemplateArgumentList, opening, end);
    }

    Node* MakeTemplateParameterList(size_t opening, size_t end)
    {
        return MakeTemplateList(NodeKind::TemplateParameterList, opening, end);
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
        auto name = NextSignificant(NextSignificant(start) + 1);
        auto assign = SkipAttributeSpecifiers(name + 1);
        if (Kind(name) == TK::Identifier && Kind(assign) == TK::Operator_Assign)
        {
            AddName(Text(name), NameKind::Type);
        }
        return node;
    }

    void AddUsingDeclarationSyntax(Node& node, size_t begin, size_t end)
    {
        const auto name = NextSignificant(NextSignificant(begin) + 1);
        const auto assign = SkipAttributeSpecifiers(name + 1);
        if (Kind(name) != TK::Identifier || Kind(assign) != TK::Operator_Assign)
        {
            return;
        }
        const auto typeBegin = NextSignificant(assign + 1);
        if (end > typeBegin && Kind(end - 1) == TK::Operator_Semicolon) {
            --end;
        }
        if (typeBegin < end) {
            auto* type = MakeNode(NodeKind::TypeId, typeBegin);
            type->Tokens.End = end;
            AddTypeSyntaxChildren(*type, typeBegin, end);
            Append(node, *type);
        }
    }

    Node* ParseTypedefDeclaration()
    {
        auto start = Cursor_;
        auto* node = ParseToSemicolon(NodeKind::TypedefDeclaration);
        auto declarationBegin = NextSignificant(start + 1);
        // A typedef can name a function type: `typedef int Callback(int*);`.
        // Its parentheses are parameters, never a direct initializer.
        std::optional<TokenRange> parameters;
        auto declarator =
            FindDeclarationSpecifiersEnd(declarationBegin, node->Tokens.End);
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
            if (Kind(i) != TK::Keyword_typename && Kind(i) != TK::Keyword_class)
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
        bool closedHeader = false;

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
            closedHeader = depth == 0;
            if (depth > 0) {
                ErrorAt(start, "unterminated template parameter list");
            }
        }

        auto parametersEnd = Cursor_;
        if (parametersBegin < parametersEnd &&
            Kind(parametersBegin) == TK::Operator_Less)
        {
            if (closedHeader) {
                Append(
                    *node,
                    *MakeTemplateParameterList(parametersBegin, parametersEnd));
            } else {
                auto* parameters =
                    MakeNode(NodeKind::TemplateParameterList, parametersBegin);
                parameters->Tokens.End = parametersEnd;
                AddUnparsedSyntax(
                    *parameters,
                    parametersBegin + 1,
                    parametersEnd);
                Append(*node, *parameters);
            }
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
        if (declaration &&
            (declaration->Kind == NodeKind::RecordDeclaration ||
                declaration->Kind == NodeKind::EnumDeclaration))
        {
            auto name = declaration->Kind == NodeKind::RecordDeclaration
                ? FindDeclaredRecordName(declaration->Tokens.Begin)
                : RecordHeadName{
                    FindDeclaredTypeName(declaration->Tokens.Begin, true),
                    false,
                };
            if (!name.IsQualified && name.Token < Tree_.Tokens_.Size() &&
                (declaration->Kind != NodeKind::RecordDeclaration ||
                    (name.HasTerminalName && !name.HasOpaquePrefix)))
            {
                AddName(Text(name.Token), NameKind::Type);
            }
        } else if (declaration &&
            declaration->Kind == NodeKind::UsingDeclaration)
        {
            auto name =
                NextSignificant(NextSignificant(declaration->Tokens.Begin) + 1);
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
        // Boundary eligibility is not proof of valid constraint syntax.
        // Recovery permits only strong declaration prefixes, not
        // identifier-pair guesses.
        bool constraintMayEnd = false;
        bool recoveredBoundary = false;
        while (!AtEnd()) {
            auto kind = CurrentKind();
            if (kind == TK::Comment || kind == TK::Invalid) {
                // Invalid bytes allow a recovery boundary but remain in the
                // constraint. Comments do not change operand state.
                if (kind == TK::Invalid) {
                    constraintMayEnd = false;
                    recoveredBoundary = true;
                }
                ++Cursor_;
                continue;
            }
            if (kind == TK::Keyword_requires) {
                recoveredBoundary = false;
                // The nested requires-expression owns its optional parameters
                // and requirement body, not the following declaration's body.
                // Only scan delimiters here; AddExpressionRange builds it once.
                auto expressionEnd = SkipRequiresExpression(Cursor_);
                constraintMayEnd =
                    expressionEnd > Cursor_ + 1 && expressionEnd <= ParseLimit_;
                if (constraintMayEnd) {
                    Cursor_ = expressionEnd;
                } else {
                    ErrorAt(Cursor_, "expected complete requirement body");
                    ++Cursor_;
                }
                continue;
            }
            auto next = NextSignificant(Cursor_ + 1);
            const bool boundaryEligible = constraintMayEnd || recoveredBoundary;
            bool startsDeclaration =
                angleDepth == 0 &&
                    ((boundaryEligible &&
                        (IsTypeKeyword(kind) || kind == TK::Keyword_class ||
                            kind == TK::Keyword_struct ||
                            kind == TK::Keyword_union ||
                            kind == TK::Keyword_enum ||
                            kind == TK::Keyword_using ||
                            kind == TK::Keyword_concept)) ||
                        (constraintMayEnd && kind == TK::Identifier &&
                            (Kind(next) == TK::Identifier ||
                                Kind(next) == TK::Keyword_operator ||
                                Kind(next) == TK::Operator_OpenParenthesis)));
            // An adjacent return-type prefix follows a finished constraint,
            // never an operator awaiting its operand. Recovery alone retains
            // the stronger pointer/reference requirement.
            if (!startsDeclaration && angleDepth == 0 && boundaryEligible &&
                kind == TK::Identifier &&
                (Kind(next) == TK::Operator_Less ||
                    Kind(next) == TK::Operator_ScopeResolution ||
                    Kind(next) == TK::Operator_Star ||
                    Kind(next) == TK::Operator_Ampersand ||
                    Kind(next) == TK::Operator_And))
            {
                auto declarator =
                    FindDeclarationSpecifiersEnd(Cursor_, ParseLimit_, true);
                bool hasPointerOrReference = false;
                while (declarator < ParseLimit_ &&
                    (Kind(declarator) == TK::Operator_Star ||
                        Kind(declarator) == TK::Operator_Ampersand ||
                        Kind(declarator) == TK::Operator_And ||
                        Kind(declarator) == TK::Keyword_const ||
                        Kind(declarator) == TK::Keyword_volatile))
                {
                    hasPointerOrReference |=
                        Kind(declarator) == TK::Operator_Star ||
                            Kind(declarator) == TK::Operator_Ampersand ||
                            Kind(declarator) == TK::Operator_And;
                    declarator = NextSignificant(declarator + 1);
                }
                startsDeclaration = Kind(declarator) == TK::Keyword_operator ||
                    (hasPointerOrReference &&
                        Kind(declarator) == TK::Identifier &&
                        Kind(NextSignificant(declarator + 1)) ==
                            TK::Operator_OpenParenthesis);
                if (constraintMayEnd) {
                    // Only the declarator-id suffix is inspected here. Its
                    // parameters and body remain owned by ParseDeclaration.
                    while (Kind(declarator) == TK::Identifier) {
                        auto suffix = NextSignificant(declarator + 1);
                        if (Kind(suffix) == TK::Operator_Less) {
                            suffix = NextSignificant(SkipTemplateArguments(
                                suffix,
                                ParseLimit_,
                                true));
                        }
                        if (Kind(suffix) != TK::Operator_ScopeResolution) {
                            startsDeclaration =
                                Kind(suffix) == TK::Operator_OpenParenthesis;
                            break;
                        }
                        declarator = NextSignificant(suffix + 1);
                    }
                    startsDeclaration |=
                        Kind(declarator) == TK::Keyword_operator;
                }
            }
            if (startsDeclaration || kind == TK::Operator_Semicolon ||
                kind == TK::Operator_OpenBrace)
            {
                break;
            }

            recoveredBoundary = false;
            if (IsOpenDelimiter(kind)) {
                ConsumeBalanced();
                constraintMayEnd = true;
                continue;
            }
            constraintMayEnd =
                kind == TK::Identifier || IsLiteralExpressionToken(kind);
            if (kind == TK::Operator_Less) {
                ++angleDepth;
                constraintMayEnd = false;
            } else if (kind == TK::Operator_Greater && angleDepth > 0) {
                --angleDepth;
                constraintMayEnd = angleDepth == 0;
            } else if (kind == TK::Operator_RightShift && angleDepth > 0) {
                angleDepth = std::max(0, angleDepth - 2);
                constraintMayEnd = angleDepth == 0;
            }
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

    [[nodiscard]] size_t FindDeclaredTypeName(
        size_t start,
        bool isEnum = false) const
    {
        auto index = NextSignificant(start) + 1;
        if (isEnum &&
            (Kind(index) == TK::Keyword_class ||
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

    struct RecordHeadName
    {
        size_t Token;
        bool IsQualified;
        bool HasOpaquePrefix = false;
        bool HasTerminalName = false;
    };

    [[nodiscard]] RecordHeadName FindDeclaredRecordName(size_t start) const
    {
        const auto end = Tree_.Tokens_.Size();
        auto skipAttributes = [&] (size_t index) -> size_t {
            while (index < end) {
                auto after = index;
                if (IsExtensionAttribute(index)) {
                    after = FindExtensionAttributeEnd(index, end);
                } else if ((Kind(index) == TK::Operator_OpenBracket &&
                    Kind(index + 1) == TK::Operator_OpenBracket) ||
                    Kind(index) == TK::Keyword_alignas)
                {
                    auto opening = Kind(index) == TK::Keyword_alignas
                        ? NextSignificant(index + 1)
                        : index;
                    after = FindBalancedEnd(opening);
                } else {
                    break;
                }
                if (after == std::numeric_limits<TokenIndex>::max() ||
                    after <= index || after >= end)
                {
                    return end;
                }
                index = NextSignificant(after);
            }
            return index;
        };
        auto name = skipAttributes(NextSignificant(start + 1));
        bool isQualified = false;
        bool opaquePrefix = false;
        if (Kind(name) == TK::Operator_ScopeResolution) {
            isQualified = true;
            name = NextSignificant(name + 1);
        }
        while (name < end && Kind(name) == TK::Identifier) {
            auto next = NextSignificant(name + 1);
            bool templateId = Kind(next) == TK::Operator_Less;
            if (templateId) {
                next = NextSignificant(SkipTemplateArguments(next, end));
                if (next >= end) {
                    break;
                }
            }
            if (Kind(next) == TK::Operator_ScopeResolution) {
                auto component = NextSignificant(next + 1);
                if (Kind(component) == TK::Keyword_template) {
                    component = NextSignificant(component + 1);
                }
                if (Kind(component) != TK::Identifier) {
                    break;
                }
                isQualified = true;
                name = component;
                continue;
            }
            next = skipAttributes(next);
            if (!isQualified && !templateId && Kind(next) == TK::Identifier &&
                Text(next) != "final")
            {
                // Only bare prefix components can be opaque decorations.
                // The terminal record-id must still meet a head boundary.
                opaquePrefix = true;
                name = next;
                continue;
            }
            if (Kind(next) == TK::Identifier && Text(next) == "final") {
                next = skipAttributes(NextSignificant(next + 1));
            }
            const auto kind = Kind(next);
            const bool terminal = kind == TK::Operator_OpenBrace ||
                kind == TK::Operator_Colon || kind == TK::Operator_Semicolon;
            return {name, isQualified, opaquePrefix, terminal};
        }
        return {end, isQualified, opaquePrefix, false};
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
        const auto recordName = FindDeclaredRecordName(start);
        const auto typeName = recordName.Token;
        auto* node = MakeNode(NodeKind::RecordDeclaration, start);
        bool sawBaseClause = false;
        bool unknownHeader =
            recordName.HasOpaquePrefix || !recordName.HasTerminalName;
        bool ambiguousRecordForm = recordName.HasOpaquePrefix;
        bool invalidHeadAngles = false;
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
                invalidHeadAngles |= angleDepth == 0;
                angleDepth = std::max(0, angleDepth - 1);
                ++Cursor_;
            } else if (CurrentKind() == TK::Operator_RightShift) {
                invalidHeadAngles |= angleDepth < 2;
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
                ambiguousRecordForm = true;
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
        const auto headEnd = sawBaseClause ? baseClauseBegin : Cursor_;
        unknownHeader |= invalidHeadAngles || angleDepth != 0;
        if (ambiguousRecordForm && CurrentKind() != TK::Operator_OpenBrace) {
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
            // A qualified definition injects its terminal name only inside
            // the record; it does not introduce a surrounding binding.
            NameBinding* record = nullptr;
            if (!unknownHeader && !recordName.IsQualified &&
                typeName < Tree_.Tokens_.Size())
            {
                record = AddName(Text(typeName), NameKind::Type);
            }
            ++Cursor_;
            PushScope();
            if (record) {
                record->Members = CurrentScope_;
            }
            // Opaque decorations do not prove a type binding. A structurally
            // delimited record-id can still identify this local owner only.
            RecordNames_.push_back(recordName.HasTerminalName &&
                !invalidHeadAngles && angleDepth == 0 &&
                typeName < Tree_.Tokens_.Size()
                ? Text(typeName)
                : std::string_view{});
            if (!unknownHeader && typeName < Tree_.Tokens_.Size()) {
                AddName(Text(typeName), NameKind::Type)->Members =
                    CurrentScope_;
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
            if (!recordName.IsQualified && !node->FirstChild &&
                typeName < Tree_.Tokens_.Size())
            {
                auto type = Text(typeName);
                auto* previous = FindName(*CurrentScope_, type);
                auto* binding = AddName(type, NameKind::Type);
                if (previous && previous->Kind == NameKind::Type) {
                    binding->Members = previous->Members;
                }
            }
        } else {
            ErrorAt(start, "expected semicolon after record declaration");
            AddMissingToken(*node, Cursor_);
        }
        node->Tokens.End = Cursor_;
        if (!unknownHeader && angleDepth == 0) {
            Node head;
            for (auto i = start + 1; i < headEnd; ++i) {
                if (Kind(i) == TK::Keyword_alignas) {
                    if (auto* operand = MakeAlignmentOperand(i, headEnd)) {
                        Append(head, *operand);
                        i = operand->Tokens.End - 1;
                    }
                } else if (IsOpenDelimiter(Kind(i))) {
                    auto balancedEnd = FindBalancedEnd(i);
                    if (balancedEnd > headEnd) {
                        break;
                    }
                    if (Kind(i) == TK::Operator_OpenBracket &&
                        Kind(i + 1) == TK::Operator_OpenBracket &&
                        Tree_.Tokens_.MatchingDelimiter(i + 1) ==
                            balancedEnd - 2)
                    {
                        auto* attribute =
                            MakeNode(NodeKind::AttributeSpecifier, i);
                        attribute->Tokens.End = balancedEnd;
                        Append(head, *attribute);
                    }
                    i = balancedEnd - 1;
                } else if (Kind(i) == TK::Operator_Less &&
                    Kind(PreviousSignificant(i)) == TK::Identifier)
                {
                    auto end = SkipTemplateArguments(i, headEnd);
                    if (end > i + 1 && end <= headEnd &&
                        (Kind(end - 1) == TK::Operator_Greater ||
                            Kind(end - 1) == TK::Operator_RightShift))
                    {
                        Append(head, *MakeTemplateArgumentList(i, end));
                        i = end - 1;
                    }
                }
            }
            if (head.FirstChild) {
                head.LastChild->NextSibling = node->FirstChild;
                node->FirstChild = head.FirstChild;
                if (!node->LastChild) {
                    node->LastChild = head.LastChild;
                }
            }
        }
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
        return text == "__cdecl" || text == "__stdcall" ||
            text == "__fastcall" || text == "__thiscall" ||
            text == "__vectorcall" || text == "__regcall" ||
            text == "__clrcall";
    }

    [[nodiscard]] size_t FindExtensionAttributeEnd(
        size_t index,
        size_t end) const
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
            kind == TK::Keyword_explicit || kind == TK::Keyword_friend ||
            kind == TK::Keyword_virtual;
    }

    [[nodiscard]] size_t SkipTemplateArguments(
        size_t index,
        size_t end,
        bool requireComplete = false,
        bool* exactClosing = nullptr) const
    {
        if (exactClosing) {
            *exactClosing = false;
        }
        if (Kind(index) != TK::Operator_Less) {
            return index;
        }
        int depth = 0;
        for (; index < end; ++index) {
            if (Kind(index) == TK::Operator_Less) {
                ++depth;
            } else if (Kind(index) == TK::Operator_Greater) {
                if (--depth == 0) {
                    if (exactClosing) {
                        *exactClosing = true;
                    }
                    return index + 1;
                }
            } else if (Kind(index) == TK::Operator_RightShift) {
                depth -= 2;
                if (depth <= 0) {
                    if (exactClosing) {
                        *exactClosing = depth == 0;
                    }
                    return requireComplete && depth < 0 ? end : index + 1;
                }
            } else if (IsOpenDelimiter(Kind(index))) {
                auto balancedEnd = FindBalancedEnd(index);
                if (balancedEnd == std::numeric_limits<TokenIndex>::max() ||
                    (requireComplete && balancedEnd > end))
                {
                    return end;
                }
                index = balancedEnd - 1;
            }
        }
        return end;
    }

    [[nodiscard]] bool LooksLikeConstructorDeclarator(
        size_t index,
        size_t end,
        FunctionKind* functionKind = nullptr,
        std::optional<TokenRange> constructorParameters = std::nullopt) const
    {
        bool localOwner = Kind(index) != TK::Operator_ScopeResolution;
        if (!localOwner) {
            index = NextSignificant(index + 1);
        }
        std::string_view owner = RecordNames_.empty()
            ? std::string_view{} : RecordNames_.back();
        bool exactOwner = true;
        while (index < end) {
            bool destructor = Kind(index) == TK::Operator_Complement;
            auto name = destructor ? NextSignificant(index + 1) : index;
            if (name >= end || Kind(name) != TK::Identifier) {
                return false;
            }
            auto next = NextSignificant(name + 1);
            const bool templateId =
                !destructor && Kind(next) == TK::Operator_Less;
            if (templateId) {
                bool exactClosing = false;
                auto after =
                    SkipTemplateArguments(next, end, false, &exactClosing);
                exactOwner &= exactClosing;
                if (after >= end ||
                    (Kind(after - 1) != TK::Operator_Greater &&
                        Kind(after - 1) != TK::Operator_RightShift))
                {
                    return false;
                }
                next = NextSignificant(after);
            }
            if (Kind(next) == TK::Operator_OpenParenthesis) {
                size_t after;
                if (templateId) {
                    // The declaration owner selected this delimiter-inclusive
                    // range before head classification. End is one past ')',
                    // not the end of the inner parameter payload. A grouped
                    // declarator's earlier pair must not become parameters.
                    if (!localOwner || !exactOwner || !constructorParameters ||
                        constructorParameters->Begin != next ||
                        constructorParameters->End <= next + 1 ||
                        constructorParameters->End > end)
                    {
                        return false;
                    }
                    auto closing = constructorParameters->End - 1;
                    if (Kind(closing) != TK::Operator_CloseParenthesis ||
                        Tree_.Tokens_.MatchingDelimiter(next) != closing ||
                        Tree_.Tokens_.MatchingDelimiter(closing) != next)
                    {
                        return false;
                    }
                    after = constructorParameters->End;
                } else {
                    after = FindBalancedEnd(next);
                }
                bool matches = !owner.empty() && Text(name) == owner &&
                    after != std::numeric_limits<TokenIndex>::max() &&
                    after <= end;
                if (matches && exactOwner && functionKind) {
                    *functionKind = destructor
                        ? FunctionKind::Destructor
                        : FunctionKind::Constructor;
                }
                // A qualified terminal name followed directly by parameters
                // has no return type. Its qualifier may be an alias defined in
                // an include; retain the declarator without claiming that the
                // terminal name proves a constructor/destructor identity.
                return matches ||
                    (!localOwner && !templateId &&
                        after != std::numeric_limits<TokenIndex>::max() &&
                        after <= end);
            }
            if (destructor || next >= end ||
                Kind(next) != TK::Operator_ScopeResolution)
            {
                return false;
            }
            localOwner = false;
            owner = Text(name);
            index = NextSignificant(next + 1);
            if (Kind(index) == TK::Keyword_template) {
                index = NextSignificant(index + 1);
            }
        }
        return false;
    }

    [[nodiscard]] size_t SkipDeclarationTypeName(
        size_t index,
        size_t end,
        bool requireComplete,
        FunctionKind* functionKind = nullptr) const
    {
        if (index >= end) {
            return index;
        }
        if (Kind(index) == TK::Operator_ScopeResolution) {
            auto component = NextSignificant(index + 1);
            if (component >= end || Kind(component) != TK::Identifier) {
                return index;
            }
            index = component;
        }
        if (Kind(index) != TK::Identifier) {
            return index;
        }
        if (functionKind) {
            *functionKind = FunctionKind::Ordinary;
        }
        index = NextSignificant(index + 1);
        if (index < end && Kind(index) == TK::Operator_Less) {
            bool exactClosing = false;
            index = NextSignificant(SkipTemplateArguments(
                index,
                end,
                requireComplete,
                &exactClosing));
            if (functionKind && !exactClosing) {
                *functionKind = FunctionKind::Unknown;
            }
        }
        while (index < end && Kind(index) == TK::Operator_ScopeResolution) {
            auto component = NextSignificant(index + 1);
            if (component >= end || Kind(component) != TK::Identifier) {
                break;
            }
            index = NextSignificant(component + 1);
            if (index < end && Kind(index) == TK::Operator_Less) {
                bool exactClosing = false;
                index = NextSignificant(SkipTemplateArguments(
                    index,
                    end,
                    requireComplete,
                    &exactClosing));
                if (functionKind && !exactClosing) {
                    *functionKind = FunctionKind::Unknown;
                }
            }
        }
        return index;
    }

    [[nodiscard]] size_t FindDeclarationSpecifiersEnd(
        size_t begin,
        size_t end,
        bool requireComplete = false,
        FunctionKind* functionKind = nullptr,
        std::optional<TokenRange> constructorParameters = std::nullopt) const
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
            if (!foundType && kind == TK::Identifier) {
                const auto next = NextSignificant(index + 1);
                const auto typeEnd = SkipDeclarationTypeName(next, end, true);
                const auto afterType = Kind(typeEnd);
                // An opaque modifier may precede a complete type followed by
                // a declarator. A qualified function name ends at '(' and a
                // member-pointer qualifier at '::*'; neither proves a type.
                if (next < end &&
                    (IsDeclarationSpecifier(Kind(next)) ||
                        (typeEnd > next && typeEnd < end &&
                            (afterType == TK::Identifier ||
                                afterType == TK::Keyword_operator ||
                                afterType == TK::Operator_Star ||
                                afterType == TK::Operator_Ampersand ||
                                afterType == TK::Operator_And))))
                {
                    index = next;
                    continue;
                }
            }
            break;
        }

        if (index >= end) {
            return index;
        }
        if (foundType || Kind(index) == TK::Keyword_operator) {
            if (functionKind) {
                *functionKind = FunctionKind::Ordinary;
            }
            return index;
        }
        if (LooksLikeConstructorDeclarator(
            index,
            end,
            functionKind,
            constructorParameters) || Kind(index) == TK::Operator_Complement)
        {
            return index;
        }
        // Consume a user-defined, qualified type name and optional template
        // arguments. The following token begins the declarator.
        return SkipDeclarationTypeName(
            index,
            end,
            requireComplete,
            functionKind);
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
        int braceDepth = 0;
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
            } else if (parenthesisDepth == 0 && bracketDepth == 0 &&
                (!functionDeclarator || braceDepth == 0) &&
                (kind == TK::Operator_Assign ||
                    (!functionDeclarator && kind == TK::Operator_OpenBrace)))
            {
                if (kind == TK::Operator_Assign &&
                    Kind(PreviousSignificant(i)) == TK::Keyword_operator)
                {
                    continue;
                }
                return i;
            } else if (kind == TK::Operator_OpenBrace) {
                // Assignments in a constructor's braced member initializer
                // belong to that expression, not to the declarator suffix.
                ++braceDepth;
            } else if (kind == TK::Operator_CloseBrace) {
                --braceDepth;
            }
        }
        return end;
    }

    enum class ListFragmentKind
    {
        Arguments,
        Parameters,
        Initializers
    };

    enum class ListFragmentState
    {
        Empty,
        Item,
        AfterItem,
        Constructor,
        Complete
    };

    // Parse complete list entries in each branch, without choosing a macro
    // configuration or expanding combinations of nested conditionals. Scanning
    // and nesting share a small proof budget; uncertain fragments remain local.
    bool AddListFragments(
        Node& parent,
        TokenIndex begin,
        TokenIndex end,
        ListFragmentKind kind,
        ListFragmentState& state,
        size_t& work,
        unsigned depth = 0)
    {
        if (depth == 32) {
            return false;
        }
        auto cursor = NextSignificant(begin);
        while (cursor < end) {
            if (work == 0) {
                return false;
            }
            --work;
            if (Kind(cursor) == TK::PreprocessingDirective) {
                if (GetDirectiveKind(cursor) == DirectiveKind::Other) {
                    auto* directive =
                        MakeNode(NodeKind::PreprocessingDirective, cursor);
                    directive->Tokens.End = cursor + 1;
                    Append(parent, *directive);
                    cursor = NextSignificant(cursor + 1);
                    continue;
                }
                if (GetDirectiveKind(cursor) != DirectiveKind::If) {
                    return false;
                }
                auto* conditional =
                    MakeNode(NodeKind::PreprocessingConditional, cursor);
                const auto incoming = state;
                auto merged = state;
                bool first = true;
                bool hasElse = false;
                bool mismatch = false;
                bool terminal = true;
                const auto merge = [&] (ListFragmentState branch) {
                    terminal &= branch == ListFragmentState::AfterItem ||
                        branch == ListFragmentState::Complete ||
                        (branch == ListFragmentState::Constructor &&
                            kind == ListFragmentKind::Initializers) ||
                        (branch == ListFragmentState::Empty &&
                            kind != ListFragmentKind::Initializers);
                    if (first) {
                        merged = branch;
                        first = false;
                    } else if ((merged == ListFragmentState::Empty &&
                        branch == ListFragmentState::Item) ||
                        (merged == ListFragmentState::Item &&
                            branch == ListFragmentState::Empty))
                    {
                        // An optional entry ending in a comma still leaves
                        // every path expecting the next common list entry.
                        merged = ListFragmentState::Item;
                    } else if (merged != branch) {
                        mismatch = true;
                    }
                };
                while (cursor < end) {
                    const auto directiveKind = GetDirectiveKind(cursor);
                    if (directiveKind != DirectiveKind::If &&
                        directiveKind != DirectiveKind::ElseIf &&
                        directiveKind != DirectiveKind::Else)
                    {
                        return false;
                    }
                    hasElse |= directiveKind == DirectiveKind::Else;
                    auto* branch =
                        MakeNode(NodeKind::PreprocessingBranch, cursor);
                    auto* directive =
                        MakeNode(NodeKind::PreprocessingDirective, cursor);
                    directive->Tokens.End = cursor + 1;
                    Append(*branch, *directive);
                    auto branchEnd = cursor + 1;
                    unsigned nested = 0;
                    for (; branchEnd < end; ++branchEnd) {
                        if (work == 0) {
                            return false;
                        }
                        --work;
                        const auto boundary = GetDirectiveKind(branchEnd);
                        if (boundary == DirectiveKind::If) {
                            ++nested;
                        } else if (boundary == DirectiveKind::EndIf) {
                            if (nested == 0) {
                                break;
                            }
                            --nested;
                        } else if (nested == 0 &&
                            (boundary == DirectiveKind::ElseIf ||
                                boundary == DirectiveKind::Else))
                        {
                            break;
                        }
                    }
                    if (branchEnd == end) {
                        return false;
                    }
                    auto branchState = incoming;
                    PushScope();
                    ++BranchParseDepth_;
                    const bool parsed = AddListFragments(
                        *branch,
                        cursor + 1,
                        branchEnd,
                        kind,
                        branchState,
                        work,
                        depth + 1);
                    --BranchParseDepth_;
                    PopScope();
                    if (!parsed) {
                        return false;
                    }
                    merge(branchState);
                    branch->Tokens.End = branchEnd;
                    Append(*conditional, *branch);
                    cursor = branchEnd;
                    if (GetDirectiveKind(cursor) == DirectiveKind::EndIf) {
                        auto* closing =
                            MakeNode(NodeKind::PreprocessingDirective, cursor);
                        closing->Tokens.End = ++cursor;
                        Append(*conditional, *closing);
                        break;
                    }
                    if (hasElse) {
                        return false;
                    }
                }
                if (!hasElse) {
                    merge(incoming);
                }
                conditional->Tokens.End = cursor;
                cursor = NextSignificant(cursor);
                if (mismatch) {
                    // Different arities can meet at a closer (or a constructor
                    // body), but cannot establish one continuation before it.
                    if (cursor != end || !terminal) {
                        return false;
                    }
                    merged = ListFragmentState::Complete;
                }
                state = merged;
                Append(parent, *conditional);
                continue;
            }
            if (kind == ListFragmentKind::Initializers &&
                state == ListFragmentState::Constructor &&
                Kind(cursor) == TK::Operator_Colon)
            {
                auto* list =
                    MakeNode(NodeKind::ConstructorInitializerList, cursor);
                list->Tokens.End = end;
                state = ListFragmentState::Empty;
                if (!AddListFragments(
                    *list,
                    cursor + 1,
                    end,
                    kind,
                    state,
                    work,
                    depth + 1) || state == ListFragmentState::Empty)
                {
                    return false;
                }
                Append(parent, *list);
                return true;
            }
            if (Kind(cursor) == TK::Operator_Comma) {
                if (state != ListFragmentState::AfterItem) {
                    return false;
                }
                auto* comma = MakeNode(NodeKind::ListSeparator, cursor);
                comma->Tokens.End = cursor + 1;
                Append(parent, *comma);
                state = ListFragmentState::Item;
                cursor = NextSignificant(cursor + 1);
                continue;
            }
            if (state != ListFragmentState::Empty &&
                state != ListFragmentState::Item)
            {
                return false;
            }
            const auto itemEnd = ArgumentExpressionEnd(cursor, end, work);
            if (itemEnd == cursor || work == 0) {
                return false;
            }
            const auto itemKind = kind == ListFragmentKind::Parameters
                ? NodeKind::ParameterDeclaration
                : kind == ListFragmentKind::Arguments
                    ? NodeKind::Argument
                    : NodeKind::ConstructorInitializer;
            auto* item = MakeNode(itemKind, cursor);
            item->Tokens.End = itemEnd;
            if (kind == ListFragmentKind::Parameters) {
                AddDeclarationSyntax(*item, cursor, itemEnd);
                if (!item->FirstChild) {
                    return false;
                }
            } else {
                auto expressionCursor = cursor;
                auto* expression =
                    ParseExpressionSyntax(expressionCursor, itemEnd, 2);
                if (!expression || expression->Tokens.End > itemEnd) {
                    return false;
                }
                expressionCursor = NextSignificant(expressionCursor);
                if (kind == ListFragmentKind::Initializers &&
                    (expression->Kind == NodeKind::IdentifierExpression ||
                        expression->Kind == NodeKind::MemberExpression ||
                        expression->Kind == NodeKind::TemplateIdExpression) &&
                    Kind(expressionCursor) == TK::Operator_OpenBrace &&
                    ArgumentPairEnd(expressionCursor, itemEnd) == itemEnd)
                {
                    Append(*item, *expression);
                    auto* braced =
                        MakeNode(NodeKind::BracedInitializer, expressionCursor);
                    braced->Tokens.End = itemEnd;
                    AddBracedInitializerElements(
                        *braced,
                        expressionCursor,
                        itemEnd);
                    Append(*item, *braced);
                } else {
                    if (expressionCursor != itemEnd ||
                        (kind == ListFragmentKind::Initializers &&
                            expression->Kind != NodeKind::CallExpression &&
                            expression->Kind !=
                                NodeKind::TypeConstructionExpression))
                    {
                        return false;
                    }
                    Append(*item, *expression);
                }
            }
            Append(parent, *item);
            state = ListFragmentState::AfterItem;
            cursor = NextSignificant(itemEnd);
        }
        return true;
    }

    bool AddConditionalListFragments(
        Node& parent,
        TokenIndex begin,
        TokenIndex end,
        ListFragmentKind kind,
        bool constructor = false)
    {
        size_t work = 4096;
        auto state = constructor
            ? ListFragmentState::Constructor
            : ListFragmentState::Empty;
        const auto valid =
            AddListFragments(parent, begin, end, kind, state, work) &&
                state != ListFragmentState::Item &&
                (kind != ListFragmentKind::Initializers ||
                    state != ListFragmentState::Empty);
        if (!valid) {
            parent.FirstChild = parent.LastChild = nullptr;
            AddUnparsedSyntax(parent, begin, end);
            AddDirectiveChildrenInGap(parent, begin, end);
        }
        return valid;
    }

    // Rejects empty/type-less parameter slots, not full grammar validity.
    bool AddParameterDeclarations(Node& list, TokenRange parameters)
    {
        if (Tree_.Tokens_.FindPreprocessingDirective(
            parameters.Begin + 1,
            parameters.End - 1) < parameters.End - 1)
        {
            return AddConditionalListFragments(
                list,
                parameters.Begin + 1,
                parameters.End - 1,
                ListFragmentKind::Parameters);
        }
        size_t segmentBegin = parameters.Begin + 1;
        int delimiterDepth = 0;
        int angleDepth = 0;
        bool hasParameterToken = false;
        bool sawSeparator = false;
        bool noEmptyParameterSlots = true;
        unsigned conditionalMiddle = 0;
        size_t explicitMemberWork = 4096;
        for (auto i = segmentBegin; i + 1 <= parameters.End; ++i) {
            bool atEnd = i + 1 == parameters.End;
            auto kind = Kind(i);
            if (!atEnd && delimiterDepth == 0 && kind == TK::Keyword_template) {
                const auto nameEnd = ArgumentExplicitMemberNameEnd(
                    i,
                    segmentBegin,
                    parameters.End - 1,
                    explicitMemberWork);
                if (nameEnd) {
                    const auto suffix =
                        ArgumentSignificant(*nameEnd, parameters.End - 1);
                    if (suffix < parameters.End - 1 &&
                        Kind(suffix) == TK::Operator_Less)
                    {
                        const auto after = ArgumentTemplateEnd(
                            suffix,
                            parameters.End - 1,
                            explicitMemberWork);
                        if (after) {
                            hasParameterToken = true;
                            i = *after - 1;
                            continue;
                        }
                    } else {
                        hasParameterToken = true;
                        i = *nameEnd - 1;
                        continue;
                    }
                }
                // No proved partition for this suffix; retain the remaining
                // lexical payload in the current parameter, not invented slots.
                auto* parameter =
                    MakeNode(NodeKind::ParameterDeclaration, segmentBegin);
                parameter->Tokens.End = parameters.End - 1;
                AddUnparsedSyntax(*parameter, segmentBegin, parameters.End - 1);
                Append(list, *parameter);
                return false;
            }
            if (!atEnd) {
                if (IsOpenDelimiter(kind)) {
                    ++delimiterDepth;
                } else if (kind == TK::Operator_CloseParenthesis ||
                    kind == TK::Operator_CloseBracket ||
                    kind == TK::Operator_CloseBrace)
                {
                    --delimiterDepth;
                } else if (delimiterDepth == 0) {
                    // Defaults nested inside delimiters do not change the
                    // template depth that owns the parameter separators.
                    if (kind == TK::Operator_Less) {
                        ++angleDepth;
                    } else if (kind == TK::Operator_Greater && angleDepth > 0) {
                        --angleDepth;
                    } else if (kind == TK::Operator_RightShift &&
                        angleDepth > 0)
                    {
                        angleDepth = std::max(0, angleDepth - 2);
                    }
                }
            }
            if (!atEnd && delimiterDepth == 0 && angleDepth == 0 &&
                !AdvanceConditionalMiddle(kind, conditionalMiddle))
            {
                i = parameters.End - 1;
                atEnd = true;
            }
            if (!atEnd &&
                (kind != TK::Operator_Comma || delimiterDepth != 0 ||
                    angleDepth != 0 || conditionalMiddle != 0))
            {
                hasParameterToken |= kind != TK::Comment;
                continue;
            }

            noEmptyParameterSlots &=
                hasParameterToken || (atEnd && !sawSeparator);
            if (segmentBegin < i) {
                auto* parameter =
                    MakeNode(NodeKind::ParameterDeclaration, segmentBegin);
                parameter->Tokens.End = i;
                const auto firstKind = Kind(NextSignificant(segmentBegin));
                // A pointer/reference operator cannot supply a parameter's
                // missing type. The payload may be unexpanded macro arguments.
                const bool missingType = firstKind == TK::Operator_Star ||
                    firstKind == TK::Operator_Ampersand ||
                    firstKind == TK::Operator_And;
                if (conditionalMiddle != 0 || missingType) {
                    AddUnparsedSyntax(*parameter, segmentBegin, i);
                    noEmptyParameterSlots = false;
                } else {
                    AddDeclarationSyntax(*parameter, segmentBegin, i);
                }
                Append(list, *parameter);
            }
            if (!atEnd) {
                auto* comma = MakeNode(NodeKind::ListSeparator, i);
                comma->Tokens.End = i + 1;
                Append(list, *comma);
                sawSeparator = true;
            }
            segmentBegin = i + 1;
            hasParameterToken = false;
        }
        return noEmptyParameterSlots;
    }

    [[nodiscard]] bool HasEmptyArgumentSlot(
        TokenIndex opening,
        TokenIndex closingEnd) const
    {
        bool missingArgument = false;
        bool expectingArgument = true;
        bool sawSeparator = false;
        unsigned conditionalMiddle = 0;
        size_t explicitMemberWork = 4096;
        for (auto i = NextSignificant(opening + 1);
            i + 1 < closingEnd;
            i = NextSignificant(i + 1))
        {
            if (Kind(i) == TK::Keyword_template) {
                const auto nameEnd = ArgumentExplicitMemberNameEnd(
                    i,
                    opening + 1,
                    closingEnd - 1,
                    explicitMemberWork);
                if (nameEnd) {
                    const auto suffix =
                        ArgumentSignificant(*nameEnd, closingEnd - 1);
                    if (suffix < closingEnd - 1 &&
                        Kind(suffix) == TK::Operator_Less)
                    {
                        const auto after = ArgumentTemplateEnd(
                            suffix,
                            closingEnd - 1,
                            explicitMemberWork);
                        if (!after) {
                            return true;
                        }
                        i = *after - 1;
                    } else {
                        i = *nameEnd - 1;
                    }
                    expectingArgument = false;
                    continue;
                }
                return true; // Unknown partition requires recovered syntax.
            }
            if (!AdvanceConditionalMiddle(Kind(i), conditionalMiddle)) {
                return false;
            }
            if (Kind(i) == TK::Operator_Comma && conditionalMiddle == 0) {
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

    [[nodiscard]] bool HasTopLevelSemicolonInBraces(
        TokenIndex opening,
        TokenIndex closingEnd) const
    {
        for (auto i = opening + 1; i < closingEnd - 1; ++i) {
            if (Kind(i) == TK::Operator_Semicolon) {
                return true;
            }
            if (IsOpenDelimiter(Kind(i))) {
                auto nestedEnd = FindBalancedEnd(i);
                if (nestedEnd <= closingEnd - 1) {
                    i = nestedEnd - 1;
                }
            }
        }
        return false;
    }

    void AddArgumentList(Node& parent, size_t opening, size_t closingEnd)
    {
        auto* arguments = MakeNode(NodeKind::ArgumentList, opening);
        arguments->Tokens.End = closingEnd;
        if (Tree_.Tokens_
            .FindPreprocessingDirective(opening + 1, closingEnd - 1) <
            closingEnd - 1)
        {
            AddConditionalListFragments(
                *arguments,
                opening + 1,
                closingEnd - 1,
                ListFragmentKind::Arguments);
            Append(parent, *arguments);
            return;
        }
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
        size_t boundaryWork = 4096;
        while (argumentCursor + 1 < closingEnd) {
            auto before = argumentCursor;
            const auto argumentEnd =
                ArgumentExpressionEnd(before, closingEnd - 1, boundaryWork);
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
                    if (HasTopLevelSemicolonInBraces(before, braceEnd)) {
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
                const auto savedLimit = ParseLimit_;
                ParseLimit_ = std::min(ParseLimit_, argumentEnd);
                expression =
                    ParseExpressionSyntax(argumentCursor, argumentEnd, 2);
                ParseLimit_ = savedLimit;
            }
            // Parsing may consume a dangling operator without representing
            // it in the returned expression. Recover from the first token
            // not actually owned, not just the advanced parse cursor.
            const bool validExpression = expression &&
                before <= argumentCursor && argumentCursor <= argumentEnd &&
                before <= expression->Tokens.Begin &&
                expression->Tokens.Begin <= expression->Tokens.End &&
                expression->Tokens.End <= argumentEnd &&
                expression->Tokens.End <= argumentCursor;
            const auto recoveryBegin = validExpression
                ? std::min(expression->Tokens.End, argumentCursor)
                : before;
            const bool trailingCommentsOnly = validExpression &&
                ArgumentSignificant(recoveryBegin, argumentEnd) == argumentEnd;
            auto* argument = MakeNode(NodeKind::Argument, before);
            argument->Tokens.End =
                trailingCommentsOnly ? expression->Tokens.End : argumentEnd;
            if (validExpression) {
                Append(*argument, *expression);
            }
            if (!trailingCommentsOnly) {
                AddUnparsedSyntax(*argument, recoveryBegin, argumentEnd);
            }
            Append(*arguments, *argument);
            argumentCursor = NextSignificant(argumentEnd);
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
            // A designated member is one list element, including its
            // designator and assignment. Parse the value as an expression
            // while keeping the complete element available to list reflow.
            TokenIndex afterDesignator = before;
            if (Kind(before) == TK::Operator_ClassMemberAccess &&
                Kind(NextSignificant(before + 1)) == TK::Identifier)
            {
                afterDesignator =
                    NextSignificant(NextSignificant(before + 1) + 1);
            } else if (Kind(before) == TK::Operator_OpenBracket) {
                auto closing = Tree_.Tokens_.MatchingDelimiter(before);
                if (closing > before && closing < closingEnd - 1) {
                    afterDesignator = NextSignificant(closing + 1);
                }
            }
            if (afterDesignator != before &&
                Kind(afterDesignator) == TK::Operator_Assign)
            {
                elementCursor = NextSignificant(afterDesignator + 1);
            }
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
        const auto modeledEnd = list->LastChild
            ? list->LastChild->Tokens.End
            : opening + 1;
        AddUnparsedSyntax(*list, modeledEnd, closingEnd - 1);
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
        if (Kind(begin) == TK::Operator_OpenParenthesis &&
            ArgumentPairEnd(begin, end) == end)
        {
            AddArgumentList(initializer, begin, end);
            return;
        }
        if (auto* expression = ParseExpressionSyntax(expressionBegin, end)) {
            Append(initializer, *expression);
        }
        AddUnparsedSyntax(initializer, expressionBegin, end);
    }

    [[nodiscard]] bool AddStructuredBindingSyntax(
        Node& initDeclarator,
        size_t begin,
        size_t end)
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
        if (Tree_.Tokens_.FindPreprocessingDirective(begin + 1, end) < end) {
            AddConditionalListFragments(
                *list,
                begin + 1,
                end,
                ListFragmentKind::Initializers);
            return;
        }

        size_t initializerBegin = begin + 1;
        int delimiterDepth = 0;
        for (auto i = initializerBegin; i <= end; ++i) {
            auto kind = Kind(i);
            if (i < end && delimiterDepth == 0 && kind == TK::Operator_Less) {
                auto templateEnd = SkipTemplateArguments(i, end);
                auto afterTemplate = NextSignificant(templateEnd);
                if (templateEnd > i + 1 && afterTemplate < end &&
                    (Kind(afterTemplate) == TK::Operator_OpenParenthesis ||
                        Kind(afterTemplate) == TK::Operator_OpenBrace ||
                        Kind(afterTemplate) == TK::Operator_ScopeResolution))
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
            if (i != end && (kind != TK::Operator_Comma || delimiterDepth != 0))
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
                } else if (hasTemplateName) {
                    // A mem-initializer-id is a type/member name, not a
                    // comparison expression. Model its initializer separately
                    // so argument breaks can take priority over type breaks.
                    auto opening = initializerBegin;
                    while (opening < i && !IsOpenDelimiter(Kind(opening))) {
                        if (Kind(opening) == TK::Operator_Less) {
                            const auto templateEnd =
                                SkipTemplateArguments(opening, i);
                            if (templateEnd <= opening + 1 || templateEnd > i) {
                                break;
                            }
                            opening = templateEnd;
                        } else {
                            ++opening;
                        }
                    }
                    const auto openingKind = Kind(opening);
                    const auto closingEnd = IsOpenDelimiter(openingKind)
                        ? FindBalancedEnd(opening)
                        : std::numeric_limits<TokenIndex>::max();
                    const auto suffix = closingEnd <= i
                        ? NextSignificant(closingEnd)
                        : i;
                    const bool complete = suffix >= i ||
                        (Kind(suffix) == TK::Operator_Ellipsis &&
                            NextSignificant(suffix + 1) >= i);
                    if ((openingKind == TK::Operator_OpenParenthesis ||
                        openingKind == TK::Operator_OpenBrace) &&
                        closingEnd <= i && complete)
                    {
                        auto* construction = MakeNode(
                            openingKind == TK::Operator_OpenParenthesis
                            ? NodeKind::CallExpression
                            : NodeKind::TypeConstructionExpression,
                            initializerBegin);
                        construction->Tokens.End = closingEnd;
                        auto* name =
                            MakeNode(NodeKind::TypeId, initializerBegin);
                        name->Tokens.End = opening;
                        AddTypeSyntaxChildren(*name, initializerBegin, opening);
                        Append(*construction, *name);
                        if (openingKind == TK::Operator_OpenParenthesis) {
                            AddArgumentList(*construction, opening, closingEnd);
                        } else {
                            auto* braced =
                                MakeNode(NodeKind::BracedInitializer, opening);
                            braced->Tokens.End = closingEnd;
                            AddBracedInitializerElements(
                                *braced,
                                opening,
                                closingEnd);
                            Append(*construction, *braced);
                        }
                        Append(*initializer, *construction);
                    } else {
                        AddTypeSyntaxChildren(
                            *initializer,
                            initializerBegin,
                            opening);
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

    [[nodiscard]] std::optional<TokenIndex> AddFunctionSuffixSyntax(
        Node& function,
        size_t begin,
        size_t end,
        bool constructor)
    {
        auto index = NextSignificant(begin);
        while (index < end) {
            auto kind = Kind(index);
            if (kind == TK::PreprocessingDirective) {
                if (constructor) {
                    auto* suffix =
                        MakeNode(NodeKind::PreprocessingBranch, index);
                    suffix->Tokens.End = end;
                    AddConditionalListFragments(
                        *suffix,
                        index,
                        end,
                        ListFragmentKind::Initializers,
                        true);
                    // The wrapper is only a builder; publish its actual nodes.
                    for (auto* child = suffix->FirstChild; child;) {
                        auto* next = child->NextSibling;
                        child->NextSibling = nullptr;
                        Append(function, *child);
                        child = next;
                    }
                } else {
                    AddUnparsedSyntax(function, index, end);
                    AddDirectiveChildrenInGap(function, index, end);
                }
                return index;
            }
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
                (kind == TK::Identifier &&
                    (Text(index) == "final" || Text(index) == "override")))
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
                AddTypeSyntaxChildren(*trailing, index + 1, trailingEnd);
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
                return index;
            }
            index = NextSignificant(index + 1);
        }
        return std::nullopt;
    }

    Node* MakeAlignmentOperand(size_t keyword, size_t end)
    {
        const auto opening = NextSignificant(keyword + 1);
        const auto after = Kind(opening) == TK::Operator_OpenParenthesis
            ? FindBalancedEnd(opening)
            : std::numeric_limits<TokenIndex>::max();
        if (after > end || after > ParseLimit_) {
            return nullptr;
        }
        auto* operand = MakeNode(NodeKind::ParenthesizedExpression, opening);
        operand->Tokens.End = after;
        const auto begin = NextSignificant(opening + 1);
        auto operandEnd = after - 1;
        const auto last = PreviousSignificant(operandEnd);
        if (Kind(last) == TK::Operator_Ellipsis) {
            operandEnd = last;
        }
        // Alignment accepts a type-id or a constant expression, optionally
        // followed by a pack ellipsis. Do not scan expression angles as types.
        if (LooksLikeTypeId(begin, operandEnd)) {
            auto* type = MakeNode(NodeKind::TypeId, begin);
            type->Tokens.End = operandEnd;
            AddTypeSyntaxChildren(*type, begin, operandEnd);
            Append(*operand, *type);
        } else {
            AddExpressionRange(*operand, begin, operandEnd);
        }
        return operand;
    }

    void AddTypeSyntaxChildren(Node& node, size_t begin, size_t end)
    {
        std::optional<bool> knownType;
        for (auto i = begin; i < end; ++i) {
            if (Kind(i) == TK::Keyword_alignas) {
                if (auto* operand = MakeAlignmentOperand(i, end)) {
                    Append(node, *operand);
                    i = operand->Tokens.End - 1;
                    continue;
                }
            }
            if (Kind(i) == TK::Keyword_decltype) {
                const auto opening = NextSignificant(i + 1);
                const auto after = Kind(opening) == TK::Operator_OpenParenthesis
                    ? FindBalancedEnd(opening)
                    : std::numeric_limits<TokenIndex>::max();
                if (after <= end) {
                    const auto first = NextSignificant(opening + 1);
                    if (Kind(first) == TK::Keyword_auto &&
                        NextSignificant(first + 1) == after - 1)
                    {
                        i = after - 1;
                        continue;
                    }
                    // The operand is an expression: let expression syntax
                    // distinguish nested template calls from comparisons.
                    auto* type = MakeNode(NodeKind::TypeId, i);
                    type->Tokens.End = after;
                    auto* operand =
                        MakeNode(NodeKind::ParenthesizedExpression, opening);
                    operand->Tokens.End = after;
                    AddExpressionRange(*operand, opening + 1, after - 1);
                    Append(*type, *operand);
                    Append(node, *type);
                    i = after - 1;
                    continue;
                }
            }
            if ((node.Kind == NodeKind::TemplateArgument ||
                node.Kind == NodeKind::TemplateParameter) &&
                IsOpenDelimiter(Kind(i)))
            {
                if (!knownType) {
                    knownType = node.Kind == NodeKind::TemplateArgument &&
                        LooksLikeTypeId(begin, end);
                }
                if (*knownType && Kind(i) == TK::Operator_OpenParenthesis) {
                    continue;
                }
                // Non-type argument interiors are opaque here. Comparisons
                // and shifts in parentheses must not become nested type lists.
                const auto closingEnd = FindBalancedEnd(i);
                if (closingEnd > end) {
                    break;
                }
                i = closingEnd - 1;
                continue;
            }
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
                    for (auto pointer = i; pointer < afterPointers; ++pointer) {
                        if (Kind(pointer) != TK::Operator_Star) {
                            continue;
                        }
                        auto* part =
                            MakeNode(NodeKind::PointerOperator, pointer);
                        part->Tokens.End = pointer + 1;
                        Append(node, *part);
                    }
                    i = afterPointers - 1;
                }
            } else if (Kind(i) == TK::Operator_Less) {
                auto templateEnd = SkipTemplateArguments(i, end);
                if (templateEnd <= end && templateEnd > i &&
                    (Kind(templateEnd - 1) == TK::Operator_Greater ||
                        Kind(templateEnd - 1) == TK::Operator_RightShift))
                {
                    const bool templateParameters = node.Kind ==
                        NodeKind::TemplateParameter &&
                        Kind(PreviousSignificant(i)) == TK::Keyword_template;
                    Append(
                        node,
                        *MakeTemplateList(
                            templateParameters
                            ? NodeKind::TemplateParameterList
                            : NodeKind::TemplateArgumentList,
                            i,
                            templateEnd));
                    i = templateEnd - 1;
                }
            }
        }
    }

    [[nodiscard]] std::optional<TokenRange> InferNestedFunctionParameters(
        size_t begin,
        size_t end) const
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
        return TokenRange{.Begin = suffixBegin, .End = suffixEnd};
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

    void AddQualifiedTemplateSyntax(Node& core, size_t begin, size_t name)
    {
        // Only a complete, independently proven function head reaches here.
        // A balanced angle span followed by :: is not proof in an expression.
        auto** link = &core.FirstChild;
        for (auto i = begin; i < name; ++i) {
            if (*link && i >= (*link)->Tokens.Begin) {
                i = (*link)->Tokens.End - 1;
                link = &(*link)->NextSibling;
                continue;
            }
            if (Kind(i) != TK::Identifier) {
                continue;
            }
            const auto opening = NextSignificant(i + 1);
            if (Kind(opening) != TK::Operator_Less) {
                continue;
            }
            bool exactClosing = false;
            const auto after =
                SkipTemplateArguments(opening, name, false, &exactClosing);
            const auto next = NextSignificant(after);
            if (exactClosing && next < name &&
                Kind(next) == TK::Operator_ScopeResolution)
            {
                auto* arguments = MakeTemplateArgumentList(opening, after);
                MarkPunctuationRoles(*arguments);
                arguments->NextSibling = *link;
                *link = arguments;
                if (!arguments->NextSibling) {
                    core.LastChild = arguments;
                }
                link = &arguments->NextSibling;
            }
            i = after - 1;
        }
    }

    FunctionKind AddDeclaratorSyntax(
        Node& initDeclarator,
        size_t begin,
        size_t end,
        std::optional<TokenRange> parameters,
        bool ambiguousSuffix = false,
        bool inferNestedParameters = true,
        FunctionKind functionKind = FunctionKind::Unknown)
    {
        const bool hasExplicitParameters = parameters.has_value();
        if (parameters &&
            !(begin <= parameters->Begin &&
                parameters->Begin < parameters->End && parameters->End <= end))
        {
            parameters.reset();
        }
        if (inferNestedParameters &&
            (!parameters || parameters->Begin == begin))
        {
            if (auto nested = InferNestedFunctionParameters(begin, end)) {
                parameters = nested;
            }
        }
        bool isFunction = parameters && !parameters->Empty();
        if (!isFunction &&
            AddStructuredBindingSyntax(initDeclarator, begin, end))
        {
            return FunctionKind::Unknown;
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
                        auto* initializer = MakeNode(
                            ambiguousSuffix
                            ? NodeKind::AmbiguousDeclaratorSuffix
                            : NodeKind::Initializer,
                            afterName);
                        initializer->Tokens.End = end;
                        if (ambiguousSuffix) {
                            AddDirectiveChildrenInGap(
                                *initializer,
                                afterName,
                                end);
                        } else {
                            AddInitializerExpression(
                                *initializer,
                                afterName,
                                end);
                        }
                        Append(initDeclarator, *initializer);
                    }
                    return FunctionKind::Unknown;
                }
            }
        }

        auto name = end;
        auto nameArgumentsBegin = begin;
        auto nameArgumentsEnd = begin;
        if (isFunction && functionKind == FunctionKind::Constructor &&
            Kind(begin) == TK::Identifier)
        {
            auto next = NextSignificant(begin + 1);
            if (Kind(next) == TK::Operator_Less) {
                bool exactClosing = false;
                auto after = SkipTemplateArguments(
                    next,
                    parameters->Begin,
                    false,
                    &exactClosing);
                if (exactClosing && NextSignificant(after) == parameters->Begin)
                {
                    name = begin;
                    nameArgumentsBegin = next;
                    nameArgumentsEnd = after;
                }
            }
        }
        if (name == end) {
            name = FindDeclaratorName(begin, end, parameters);
        }
        auto initializerStart =
            FindInitializerStart(begin, end, name, isFunction);
        auto declaratorEnd = initializerStart;
        if (isFunction && parameters->End > declaratorEnd) {
            parameters.reset();
            isFunction = false;
            nameArgumentsEnd = begin;
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
        // An explicitly selected parameter list is never a grouped declarator,
        // even when its payload contains pointer/reference operators.
        if (Kind(begin) == TK::Operator_OpenParenthesis &&
            (!parameters || begin != parameters->Begin))
        {
            auto balancedEnd = FindBalancedEnd(begin);
            if (balancedEnd != std::numeric_limits<TokenIndex>::max() &&
                balancedEnd <= declaratorEnd &&
                ((begin < name && name < balancedEnd) ||
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
                if (kind == TK::Operator_Star ||
                    kind == TK::Operator_Ampersand || kind == TK::Operator_And)
                {
                    auto* pointer = MakeNode(NodeKind::PointerOperator, i);
                    pointer->Tokens.End = i + 1;
                    Append(*core, *pointer);
                }
            }
        }

        bool knownHead =
            hasExplicitParameters && isFunction && name < parameters->Begin &&
                Kind(parameters->End - 1) == TK::Operator_CloseParenthesis;
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
            size_t qualifiedArgumentsEnd = coreBegin;
            for (auto i = coreBegin; i < name; ++i) {
                if (i < qualifiedArgumentsEnd) {
                    continue;
                }
                auto kind = Kind(i);
                if (kind == TK::Operator_Star ||
                    kind == TK::Operator_Ampersand || kind == TK::Operator_And)
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
                        knownHead = false;
                        break;
                    }
                    auto* attribute = MakeNode(NodeKind::AttributeSpecifier, i);
                    attribute->Tokens.End = attributeEnd;
                    Append(*core, *attribute);
                    i = attributeEnd - 1;
                } else if (kind == TK::Identifier) {
                    auto next = NextSignificant(i + 1);
                    if (Kind(next) == TK::Operator_Less) {
                        bool exactClosing = false;
                        qualifiedArgumentsEnd = SkipTemplateArguments(
                            next,
                            name,
                            false,
                            &exactClosing);
                        knownHead &= exactClosing;
                        next = NextSignificant(qualifiedArgumentsEnd);
                        if (!exactClosing && hasExplicitParameters) {
                            // Comparisons inside template arguments can make
                            // a lexical angle scan inconclusive. Preserve the
                            // qualifier instead of assigning template roles.
                            auto* uncertain =
                                MakeNode(NodeKind::UnparsedSyntax, i);
                            uncertain->Tokens.End = qualifiedArgumentsEnd;
                            Append(*core, *uncertain);
                        }
                    }
                    knownHead &= next < name &&
                        Kind(next) == TK::Operator_ScopeResolution;
                } else if (kind != TK::Operator_ScopeResolution &&
                    kind != TK::Operator_Complement &&
                    kind != TK::Keyword_template && kind != TK::Keyword_const &&
                    kind != TK::Keyword_volatile && kind != TK::Comment)
                {
                    knownHead = false;
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
            if (nameArgumentsEnd != begin) {
                Append(
                    *core,
                    *MakeTemplateArgumentList(
                        nameArgumentsBegin,
                        nameArgumentsEnd));
            }
            if (isFunction && nameKind != NodeKind::OperatorFunctionName) {
                knownHead &= NextSignificant(
                    nameArgumentsEnd != begin ? nameArgumentsEnd : name + 1) ==
                    parameters->Begin;
            }
            if (nameKind == NodeKind::DestructorName) {
                knownHead &= functionKind == FunctionKind::Destructor;
            }
        }

        Node* function = nullptr;
        auto suffixScanEnd = declaratorEnd;
        if (isFunction) {
            function =
                MakeNode(NodeKind::FunctionDeclarator, parameters->Begin);
            function->Tokens.End = declaratorEnd;
            auto* parameterList =
                MakeNode(NodeKind::ParameterList, parameters->Begin);
            parameterList->Tokens.End = parameters->End;
            knownHead &= AddParameterDeclarations(*parameterList, *parameters);
            AddDirectiveChildrenInGap(
                *parameterList,
                parameters->Begin,
                parameters->End);
            Append(*function, *parameterList);
            if (auto initializerColon = AddFunctionSuffixSyntax(
                *function,
                parameters->End,
                declaratorEnd,
                functionKind == FunctionKind::Constructor))
            {
                // Constructor initializers belong to the function, not its
                // array declarator suffixes.
                suffixScanEnd = *initializerColon;
            }
        }

        {
            int delimiterDepth = 0;
            auto suffixBegin = nameArgumentsEnd != begin
                ? nameArgumentsEnd
                : (name < declaratorEnd
                    ? name + 1
                    : (nestedEnd != begin ? nestedEnd : begin));
            for (auto i = suffixBegin; i < suffixScanEnd; ++i) {
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

        if (function) {
            Append(*declarator, *function);
        }

        if (initializerStart < end) {
            auto* initializer = MakeNode(
                ambiguousSuffix
                ? NodeKind::AmbiguousDeclaratorSuffix
                : NodeKind::Initializer,
                initializerStart);
            initializer->Tokens.End = end;
            if (ambiguousSuffix) {
                AddDirectiveChildrenInGap(*initializer, initializerStart, end);
            } else {
                AddInitializerExpression(*initializer, initializerStart, end);
            }
            Append(initDeclarator, *initializer);
        }
        if (knownHead && functionKind != FunctionKind::Unknown) {
            AddQualifiedTemplateSyntax(*core, coreBegin, name);
        }
        return knownHead ? functionKind : FunctionKind::Unknown;
    }

    enum class ParameterTypeEvidence
    {
        Type,
        Value,
        Unknown,
    };

    [[nodiscard]] ParameterTypeEvidence StartsStatementParameterType(
        TokenIndex first,
        TokenIndex end) const
    {
        auto type = first;
        while (Kind(type) == TK::Keyword_const ||
            Kind(type) == TK::Keyword_volatile)
        {
            type = NextSignificant(type + 1);
        }
        auto typeKind = Kind(type);
        bool explicitType = IsFundamentalTypeKeyword(typeKind) ||
            typeKind == TK::Keyword_typename ||
            typeKind == TK::Keyword_struct || typeKind == TK::Keyword_class ||
            typeKind == TK::Keyword_union || typeKind == TK::Keyword_enum;
        auto binding = typeKind == TK::Identifier
            ? LookupName(Text(type))
            : std::nullopt;
        if (!explicitType && (!binding || *binding != NameKind::Type)) {
            return typeKind == TK::Identifier && !binding
                ? ParameterTypeEvidence::Unknown
                : ParameterTypeEvidence::Value;
        }
        if (binding) {
            auto afterType = NextSignificant(type + 1);
            bool templateId = Kind(afterType) == TK::Operator_Less;
            if (templateId) {
                afterType =
                    NextSignificant(SkipTemplateArguments(afterType, end));
            }
            if (Kind(afterType) == TK::Operator_ScopeResolution) {
                // An included specialization can change a member's role.
                if (templateId || !IsKnownQualifiedType(type, end)) {
                    return ParameterTypeEvidence::Unknown;
                }
            }
        }
        return ParameterTypeEvidence::Type;
    }

    [[nodiscard]] std::optional<TokenRange> InferStatementParameters(
        TokenIndex begin,
        TokenIndex end,
        bool& ambiguous,
        bool proveDirectInitializer = false) const
    {
        ambiguous = false;
        size_t proofWork = 4096;
        const auto uncertain = [&] () -> std::optional<TokenRange> {
            ambiguous = true;
            return std::nullopt;
        };
        auto initializer = [&] () -> std::optional<TokenRange> {
            ambiguous = false;
            return std::nullopt;
        };
        const auto proofParameterType =
            [&] (TokenIndex first, TokenIndex limit) {
                while (first < limit &&
                    (Kind(first) == TK::Keyword_const ||
                        Kind(first) == TK::Keyword_volatile))
                {
                    if (proofWork == 0) {
                        return ParameterTypeEvidence::Unknown;
                    }
                    --proofWork;
                    first = ArgumentSignificant(first + 1, limit);
                }
                if (first >= limit || proofWork == 0) {
                    return ParameterTypeEvidence::Unknown;
                }
                --proofWork;
                if (IsTypeKeyword(Kind(first))) {
                    return ParameterTypeEvidence::Type;
                }
                if (Kind(first) == TK::Identifier) {
                    const auto binding = LookupName(Text(first));
                    if (!binding) {
                        return ParameterTypeEvidence::Unknown;
                    }
                    return *binding == NameKind::Type
                        ? ParameterTypeEvidence::Type
                        : ParameterTypeEvidence::Value;
                }
                if (Kind(first) == TK::Operator_OpenParenthesis ||
                    Kind(first) == TK::Operator_OpenBracket ||
                    Kind(first) == TK::Operator_ScopeResolution)
                {
                    return ParameterTypeEvidence::Unknown;
                }
                return ParameterTypeEvidence::Value;
            };
        auto name = Kind(begin) == TK::Identifier
            ? begin
            : FindDeclaratorName(begin, end, std::nullopt);
        if (name >= end || Kind(name) != TK::Identifier) {
            return proveDirectInitializer ? uncertain() : std::nullopt;
        }
        auto opening = NextSignificant(name + 1);
        if (Kind(opening) != TK::Operator_OpenParenthesis &&
            Kind(begin) == TK::Operator_OpenParenthesis)
        {
            if (auto nested = InferNestedFunctionParameters(begin, end)) {
                opening = nested->Begin;
            }
        }
        if (Kind(opening) != TK::Operator_OpenParenthesis) {
            return proveDirectInitializer ? uncertain() : std::nullopt;
        }
        auto closing = proveDirectInitializer
            ? ArgumentPairEnd(opening, end)
                .value_or(std::numeric_limits<TokenIndex>::max())
            : FindBalancedEnd(opening);
        if (closing == std::numeric_limits<TokenIndex>::max() || closing > end)
        {
            return proveDirectInitializer ? uncertain() : std::nullopt;
        }
        auto first = NextSignificant(opening + 1);
        if (first == closing - 1) {
            return TokenRange{.Begin = opening, .End = closing};
        }
        if (NextSignificant(first + 1) == closing - 1) {
            if (Kind(first) == TK::Operator_Ellipsis) {
                return TokenRange{.Begin = opening, .End = closing};
            }
            auto evidence = proveDirectInitializer
                ? proofParameterType(first, closing - 1)
                : StartsStatementParameterType(first, closing - 1);
            if (evidence == ParameterTypeEvidence::Value) {
                return initializer();
            }
            ambiguous = evidence == ParameterTypeEvidence::Unknown;
            return ambiguous
                ? std::nullopt
                : std::optional<TokenRange>(
                    TokenRange{.Begin = opening, .End = closing});
        }

        // A parenthesized initializer can also start with an identifier.
        // Infer parameters only from types established in this file or
        // explicit type keywords. An unresolved name may instead be a value.
        while (first < closing - 1) {
            if (Kind(first) == TK::Operator_Ellipsis) {
                if (NextSignificant(first + 1) != closing - 1) {
                    return initializer();
                }
                return ambiguous
                    ? std::nullopt
                    : std::optional<TokenRange>(
                        TokenRange{.Begin = opening, .End = closing});
            }
            auto evidence = proveDirectInitializer
                ? proofParameterType(first, closing - 1)
                : StartsStatementParameterType(first, closing - 1);
            if (evidence == ParameterTypeEvidence::Value) {
                return initializer();
            }
            ambiguous |= evidence == ParameterTypeEvidence::Unknown;
            // Before a parameter's default argument, a declarator cannot
            // contain a value expression or a braced initializer. A type
            // followed by either one is still direct initialization.
            int depth = 0;
            std::array<TokenIndex, 256> nestedParameterStarts;
            bool sawDeclaratorParentheses = false;
            bool defaultArgument = false;
            unsigned defaultConditionalMiddle = 0;
            TokenIndex defaultExpressionBegin = closing - 1;
            auto cursor = first;
            for (; cursor < closing - 1; ++cursor) {
                if (proveDirectInitializer) {
                    if (proofWork == 0) {
                        return uncertain();
                    }
                    --proofWork;
                }
                auto kind = Kind(cursor);
                if (proveDirectInitializer && defaultArgument && depth == 0 &&
                    !AdvanceConditionalMiddle(kind, defaultConditionalMiddle))
                {
                    return uncertain();
                }
                if (kind == TK::Operator_Comma) {
                    if (depth == 0) {
                        if (defaultConditionalMiddle == 0) {
                            break;
                        }
                        continue;
                    }
                    auto nestedEvidence = proveDirectInitializer
                        ? proofParameterType(
                            nestedParameterStarts[depth - 1],
                            closing - 1)
                        : StartsStatementParameterType(
                            nestedParameterStarts[depth - 1],
                            closing - 1);
                    if (nestedEvidence == ParameterTypeEvidence::Value) {
                        return initializer();
                    }
                    ambiguous |=
                        nestedEvidence == ParameterTypeEvidence::Unknown;
                    nestedParameterStarts[depth - 1] =
                        NextSignificant(cursor + 1);
                }
                if (kind == TK::Operator_Assign) {
                    if (depth != 0) {
                        return initializer();
                    }
                    defaultArgument = true;
                    if (proveDirectInitializer) {
                        defaultExpressionBegin =
                            ArgumentSignificant(cursor + 1, closing - 1);
                    }
                }
                if (defaultArgument) {
                    if (proveDirectInitializer && kind == TK::Keyword_template)
                    {
                        const auto nameEnd = ArgumentExplicitMemberNameEnd(
                            cursor,
                            defaultExpressionBegin,
                            closing - 1,
                            proofWork);
                        if (!nameEnd) {
                            return uncertain();
                        }
                        const auto suffix =
                            ArgumentSignificant(*nameEnd, closing - 1);
                        if (suffix < closing - 1 &&
                            Kind(suffix) == TK::Operator_Less)
                        {
                            const auto after = ArgumentTemplateEnd(
                                suffix,
                                closing - 1,
                                proofWork);
                            if (!after) {
                                return uncertain();
                            }
                            cursor = *after - 1;
                        } else {
                            cursor = *nameEnd - 1;
                        }
                    } else if (proveDirectInitializer &&
                        kind == TK::Operator_Less &&
                        Kind(PreviousSignificant(cursor)) == TK::Identifier)
                    {
                        const auto after =
                            ArgumentTemplateEnd(cursor, closing - 1, proofWork);
                        if (!after ||
                            !ArgumentTemplateUse(
                                ArgumentSignificant(*after, closing - 1),
                                closing - 1))
                        {
                            return uncertain();
                        }
                        cursor = *after - 1;
                    } else if (IsOpenDelimiter(kind)) {
                        auto nested = proveDirectInitializer
                            ? ArgumentPairEnd(cursor, closing - 1)
                                .value_or(
                                    std::numeric_limits<TokenIndex>::max())
                            : FindBalancedEnd(cursor);
                        if (nested == std::numeric_limits<TokenIndex>::max() ||
                            nested >= closing)
                        {
                            return proveDirectInitializer
                                ? uncertain()
                                : std::nullopt;
                        }
                        const auto suffix = proveDirectInitializer
                            ? ArgumentSignificant(nested, closing - 1)
                            : closing - 1;
                        if (proveDirectInitializer &&
                            cursor == defaultExpressionBegin &&
                            kind == TK::Operator_OpenBracket &&
                            Kind(
                                ArgumentSignificant(cursor + 1, closing - 1)) !=
                                TK::Operator_OpenBracket &&
                            suffix < closing - 1 &&
                            Kind(suffix) == TK::Operator_Less)
                        {
                            const auto after = ArgumentTemplateEnd(
                                suffix,
                                closing - 1,
                                proofWork);
                            if (!after) {
                                return uncertain();
                            }
                            cursor = *after - 1;
                        } else {
                            cursor = nested - 1;
                        }
                    }
                    continue;
                }
                if (kind == TK::Keyword_noexcept &&
                    Kind(PreviousSignificant(cursor)) ==
                        TK::Operator_CloseParenthesis &&
                    SignificantKind(cursor + 1) == TK::Operator_OpenParenthesis)
                {
                    // The expression in an exception specification is not
                    // a parameter list, even when it contains commas.
                    auto nested = proveDirectInitializer
                        ? ArgumentPairEnd(
                            ArgumentSignificant(cursor + 1, closing - 1),
                            closing - 1)
                            .value_or(std::numeric_limits<TokenIndex>::max())
                        : FindBalancedEnd(NextSignificant(cursor + 1));
                    if (nested == std::numeric_limits<TokenIndex>::max() ||
                        nested >= closing)
                    {
                        return proveDirectInitializer
                            ? uncertain()
                            : std::nullopt;
                    }
                    cursor = nested - 1;
                } else if (kind == TK::Keyword_decltype &&
                    SignificantKind(cursor + 1) == TK::Operator_OpenParenthesis)
                {
                    auto nested = proveDirectInitializer
                        ? ArgumentPairEnd(
                            ArgumentSignificant(cursor + 1, closing - 1),
                            closing - 1)
                            .value_or(std::numeric_limits<TokenIndex>::max())
                        : FindBalancedEnd(NextSignificant(cursor + 1));
                    if (nested == std::numeric_limits<TokenIndex>::max() ||
                        nested >= closing)
                    {
                        return proveDirectInitializer
                            ? uncertain()
                            : std::nullopt;
                    }
                    cursor = nested - 1;
                } else if (kind == TK::Operator_Less) {
                    if (proveDirectInitializer) {
                        const auto previous = PreviousSignificant(cursor);
                        if (Kind(previous) != TK::Identifier ||
                            !IsKnownType(previous))
                        {
                            return uncertain();
                        }
                        const auto after =
                            ArgumentTemplateEnd(cursor, closing - 1, proofWork);
                        if (!after) {
                            return uncertain();
                        }
                        cursor = *after - 1;
                    } else {
                        auto after = SkipTemplateArguments(cursor, closing);
                        if (after > cursor && after < closing) {
                            cursor = after - 1;
                        }
                    }
                } else if (kind == TK::Operator_OpenBracket) {
                    auto nested = proveDirectInitializer
                        ? ArgumentPairEnd(cursor, closing - 1)
                            .value_or(std::numeric_limits<TokenIndex>::max())
                        : FindBalancedEnd(cursor);
                    if (nested == std::numeric_limits<TokenIndex>::max() ||
                        nested >= closing)
                    {
                        return proveDirectInitializer
                            ? uncertain()
                            : std::nullopt;
                    }
                    cursor = nested - 1;
                } else if (depth == 0 && sawDeclaratorParentheses &&
                    (kind == TK::Operator_Star ||
                        kind == TK::Operator_Ampersand ||
                        kind == TK::Operator_And))
                {
                    // A pointer operator cannot follow a completed
                    // parenthesized declarator or function suffix.
                    return initializer();
                } else if (depth > 0 && kind == TK::Identifier &&
                    (SignificantKind(cursor + 1) == TK::Operator_Star ||
                        SignificantKind(cursor + 1) == TK::Operator_Ampersand ||
                        SignificantKind(cursor + 1) == TK::Operator_And) &&
                    Kind(PreviousSignificant(cursor)) !=
                        TK::Operator_ScopeResolution)
                {
                    // Pointer operators precede a declarator name; a
                    // name followed by one is a binary expression.
                    return initializer();
                } else if (kind == TK::Operator_OpenBrace ||
                    (kind >= TK::Literal_UserDefinedInteger &&
                        kind <= TK::Literal_String) ||
                    kind == TK::Keyword_true || kind == TK::Keyword_false ||
                    kind == TK::Keyword_nullptr ||
                    kind == TK::Operator_Conditional ||
                    kind == TK::Operator_ClassMemberAccess ||
                    kind == TK::Operator_IndirectClassMemberAccess ||
                    kind == TK::Operator_Increment ||
                    kind == TK::Operator_Decrement)
                {
                    return initializer();
                } else if (GetBinaryOperatorInfo(kind).Precedence != 0 &&
                    kind != TK::Operator_Star &&
                    kind != TK::Operator_Ampersand &&
                    kind != TK::Operator_And && kind != TK::Operator_Comma &&
                    kind != TK::Operator_Assign)
                {
                    return initializer();
                } else if (kind == TK::Operator_OpenParenthesis) {
                    sawDeclaratorParentheses |= depth == 0;
                    if (depth == static_cast<int>(nestedParameterStarts.size()))
                    {
                        return proveDirectInitializer
                            ? uncertain()
                            : std::nullopt;
                    }
                    nestedParameterStarts[depth++] =
                        NextSignificant(cursor + 1);
                } else if (kind == TK::Operator_CloseParenthesis) {
                    --depth;
                }
            }
            if (proveDirectInitializer && defaultConditionalMiddle != 0) {
                return uncertain();
            }
            first = NextSignificant(cursor + 1);
            if (cursor == closing - 1) {
                break;
            }
            if (first == closing - 1) {
                return initializer();
            }
        }
        if (ambiguous) {
            return proveDirectInitializer ? uncertain() : std::nullopt;
        }
        return TokenRange{.Begin = opening, .End = closing};
    }

    void AddDeclarationSyntax(
        Node& declaration,
        size_t begin,
        size_t end,
        std::optional<TokenRange> parameters = std::nullopt,
        bool ungroupedConstructorInitializer = false,
        bool terminalConstructorEligible = false)
    {
        while (end > begin &&
            (Kind(end - 1) == TK::Operator_Semicolon ||
                Kind(end - 1) == TK::Operator_OpenBrace))
        {
            --end;
        }
        FunctionKind functionKind = FunctionKind::Unknown;
        const auto constructorParameters = terminalConstructorEligible &&
            (declaration.Kind == NodeKind::FunctionDeclaration ||
                declaration.Kind == NodeKind::FunctionDefinition)
            ? parameters
            : std::nullopt;
        auto specifiersEnd = FindDeclarationSpecifiersEnd(
            begin,
            end,
            false,
            &functionKind,
            constructorParameters);
        if (ungroupedConstructorInitializer && parameters &&
            !RecordNames_.empty() && RecordNames_
                .back()
                .empty() && specifiersEnd == parameters->Begin)
        {
            AddUnparsedSyntax(declaration, begin, end);
            return;
        }
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
                } else if (delimiterDepth == 0 && kind == TK::Operator_Less) {
                    ++angleDepth;
                } else if (delimiterDepth == 0 &&
                    kind == TK::Operator_Greater && angleDepth > 0)
                {
                    --angleDepth;
                } else if (delimiterDepth == 0 &&
                    kind == TK::Operator_RightShift && angleDepth > 0)
                {
                    angleDepth = std::max(0, angleDepth - 2);
                }
            }
            if (i != end &&
                (kind != TK::Operator_Comma || delimiterDepth != 0 ||
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
                bool ambiguousSuffix = false;
                if (!segmentParameters &&
                    declaration.Kind == NodeKind::DeclarationStatement)
                {
                    segmentParameters = InferStatementParameters(
                        segmentBegin,
                        segmentEnd,
                        ambiguousSuffix);
                }
                auto kind = AddDeclaratorSyntax(
                    *init,
                    segmentBegin,
                    segmentEnd,
                    segmentParameters,
                    ambiguousSuffix,
                    declaration.Kind != NodeKind::DeclarationStatement,
                    functionKind);
                if (segmentBegin == specifiersEnd && i == end &&
                    (declaration.Kind == NodeKind::FunctionDeclaration ||
                        declaration.Kind == NodeKind::FunctionDefinition))
                {
                    declaration.FunctionKind_ = kind;
                }
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
        TokenRange parameters,
        bool ungroupedConstructorInitializer,
        bool terminalConstructorEligible = false)
    {
        FunctionKind functionKind = FunctionKind::Unknown;
        auto specifiersEnd = FindDeclarationSpecifiersEnd(
            begin,
            end,
            false,
            &functionKind,
            terminalConstructorEligible &&
                declaration.Kind == NodeKind::FunctionDefinition
            ? std::optional<TokenRange>(parameters)
            : std::nullopt);
        if (ungroupedConstructorInitializer && !RecordNames_.empty() &&
            RecordNames_
                .back()
                .empty() && specifiersEnd == parameters.Begin)
        {
            AddUnparsedSyntax(declaration, begin, end);
            return;
        }
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
        auto kind = AddDeclaratorSyntax(
            *init,
            specifiersEnd,
            end,
            parameters,
            false,
            true,
            functionKind);
        if (declaration.Kind == NodeKind::FunctionDefinition) {
            declaration.FunctionKind_ = kind;
        }
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

    [[nodiscard]] TokenIndex SkipRecoveredRequiresExpression(
        TokenIndex start) const
    {
        // Mismatched interiors can leave populated outer pairs. A valid map
        // is required so cached advancement cannot hide their diagnostics.
        if (!HasDelimiterMap_) {
            return start + 1;
        }
        const auto limit = std::min(ParseLimit_, Tree_.Tokens_.Size());
        auto pairedEnd = [&] (TokenIndex opening, TK closing) -> TokenIndex {
            if (opening >= limit) {
                return std::numeric_limits<TokenIndex>::max();
            }
            const auto matching = Tree_.Tokens_.MatchingDelimiter(opening);
            if (matching <= opening || matching >= limit ||
                Kind(matching) != closing ||
                Tree_.Tokens_.MatchingDelimiter(matching) != opening)
            {
                return std::numeric_limits<TokenIndex>::max();
            }
            return matching + 1;
        };
        auto cursor = NextSignificant(start + 1);
        if (Kind(cursor) == TK::Operator_OpenParenthesis) {
            cursor = pairedEnd(cursor, TK::Operator_CloseParenthesis);
            if (cursor == std::numeric_limits<TokenIndex>::max()) {
                return start + 1;
            }
            cursor = NextSignificant(cursor);
        }
        if (Kind(cursor) != TK::Operator_OpenBrace) {
            return start + 1;
        }
        const auto end = pairedEnd(cursor, TK::Operator_CloseBrace);
        return end == std::numeric_limits<TokenIndex>::max() ? start + 1 : end;
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
        return kind == TK::Identifier &&
            (Text(index) == "final" || Text(index) == "override" ||
                IsExtensionAttribute(index) || IsCallingConvention(index));
    }

    // Only pending conditional middles own commas. Callers update this at
    // their separator depth, after consuming delimiter/template/operator facts.
    [[nodiscard]] static bool AdvanceConditionalMiddle(
        TK kind,
        unsigned& middle)
    {
        if (middle > 256) {
            return false;
        }
        if (kind == TK::Operator_Conditional) {
            if (++middle > 256) {
                return false;
            }
        } else if (kind == TK::Operator_Colon && middle != 0) {
            --middle;
        }
        return true;
    }

    [[nodiscard]] TokenIndex ArgumentSignificant(
        TokenIndex index,
        TokenIndex end) const
    {
        end = std::min(end, ParseLimit_);
        while (index < end && Kind(index) == TK::Comment) {
            ++index;
        }
        return std::min(index, end);
    }

    // These decisions use only cached pairs within the caller's expression
    // limit. An absent fact withholds authority; recovery never scans for it.
    [[nodiscard]] std::optional<TokenIndex> ArgumentPairEnd(
        TokenIndex opening,
        TokenIndex end) const
    {
        end = std::min(end, ParseLimit_);
        if (!HasDelimiterMap_ || opening >= end ||
            !IsOpenDelimiter(Kind(opening)))
        {
            return std::nullopt;
        }
        const auto match = Tree_.Tokens_.MatchingDelimiter(opening);
        if (match <= opening || match >= end ||
            Kind(match) != ClosingDelimiter(Kind(opening)) ||
            Tree_.Tokens_.MatchingDelimiter(match) != opening)
        {
            return std::nullopt;
        }
        return match + 1;
    }

    // Called only at a contextually introduced template opener, never for
    // every comparison angle. The shared budget includes unsuccessful probes.
    [[nodiscard]] std::optional<TokenIndex> ArgumentTemplateEnd(
        TokenIndex opening,
        TokenIndex end,
        size_t& work) const
    {
        end = std::min(end, ParseLimit_);
        unsigned depth = 0;
        unsigned conditionalMiddle = 0;
        TokenIndex explicitOpening = end;
        for (auto i = opening; i < end && work != 0; ++i) {
            --work;
            const auto kind = Kind(i);
            if (kind == TK::Keyword_template) {
                const auto nameEnd =
                    ArgumentExplicitMemberNameEnd(i, opening + 1, end, work);
                if (nameEnd) {
                    explicitOpening = ArgumentSignificant(*nameEnd, end);
                    i = *nameEnd - 1;
                    continue;
                }
            }
            if (!AdvanceConditionalMiddle(kind, conditionalMiddle)) {
                return std::nullopt;
            }
            if (kind == TK::Operator_Less) {
                if (i != opening && i != explicitOpening &&
                    Kind(PreviousSignificant(i)) != TK::Identifier)
                {
                    return std::nullopt;
                }
                if (++depth > 256) {
                    return std::nullopt;
                }
            } else if (kind == TK::Operator_Greater ||
                kind == TK::Operator_RightShift)
            {
                const unsigned count = kind == TK::Operator_RightShift ? 2 : 1;
                if (count > depth) {
                    return std::nullopt;
                }
                depth -= count;
                if (depth == 0) {
                    // A template argument cannot end inside a conditional
                    // middle. Such angles belong to a comparison expression.
                    return conditionalMiddle == 0
                        ? std::optional<TokenIndex>{i + 1}
                        : std::nullopt;
                }
            } else if (IsOpenDelimiter(kind)) {
                const auto after = ArgumentPairEnd(i, end);
                if (!after) {
                    return std::nullopt;
                }
                i = *after - 1;
            } else if (kind == TK::Operator_Semicolon ||
                kind == TK::Operator_CloseParenthesis ||
                kind == TK::Operator_CloseBracket ||
                kind == TK::Operator_CloseBrace)
            {
                return std::nullopt;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<TokenIndex> ArgumentOperatorNameEnd(
        TokenIndex name,
        TokenIndex end,
        size_t& work) const
    {
        end = std::min(end, ParseLimit_);
        if (name >= end || work == 0 || Kind(name) != TK::Keyword_operator) {
            return std::nullopt;
        }
        --work;
        const auto symbol = ArgumentSignificant(name + 1, end);
        if (symbol >= end) {
            return std::nullopt;
        }
        const auto kind = Kind(symbol);
        if (kind == TK::Operator_OpenParenthesis ||
            kind == TK::Operator_OpenBracket)
        {
            const auto pair = ArgumentPairEnd(symbol, end);
            if (!pair ||
                ArgumentSignificant(symbol + 1, *pair - 1) != *pair - 1)
            {
                return std::nullopt;
            }
            return pair;
        }
        if (GetBinaryOperatorInfo(kind).Precedence == 0 &&
            kind != TK::Operator_Not && kind != TK::Operator_Complement &&
            kind != TK::Operator_Increment && kind != TK::Operator_Decrement &&
            kind != TK::Operator_IndirectClassMemberAccess &&
            kind != TK::Operator_IndirectPointerToMemberDereference &&
            kind != TK::Keyword_co_await && kind != TK::Keyword_new &&
            kind != TK::Keyword_delete)
        {
            return std::nullopt;
        }
        const auto after = ArgumentSignificant(symbol + 1, end);
        if ((kind == TK::Keyword_new || kind == TK::Keyword_delete) &&
            after < end && Kind(after) == TK::Operator_OpenBracket)
        {
            const auto pair = ArgumentPairEnd(after, end);
            if (!pair || ArgumentSignificant(after + 1, *pair - 1) != *pair - 1)
            {
                return std::nullopt;
            }
            return pair;
        }
        return symbol + 1;
    }

    [[nodiscard]] std::optional<TokenIndex> ArgumentExplicitMemberNameEnd(
        TokenIndex introducer,
        TokenIndex begin,
        TokenIndex end,
        size_t& work) const
    {
        end = std::min(end, ParseLimit_);
        if (introducer <= begin || introducer >= end || work == 0 ||
            Kind(introducer) != TK::Keyword_template)
        {
            return std::nullopt;
        }
        --work;
        const auto access = PreviousSignificant(introducer);
        if (access < begin || access >= introducer) {
            return std::nullopt;
        }
        const auto kind = Kind(access);
        if (kind != TK::Operator_ScopeResolution &&
            kind != TK::Operator_ClassMemberAccess &&
            kind != TK::Operator_IndirectClassMemberAccess)
        {
            return std::nullopt;
        }
        const auto receiver = PreviousSignificant(access);
        if (receiver < begin || receiver >= access ||
            (Kind(receiver) != TK::Identifier &&
                (kind == TK::Operator_ScopeResolution ||
                    Kind(receiver) != TK::Keyword_this)))
        {
            return std::nullopt;
        }
        const auto name = ArgumentSignificant(introducer + 1, end);
        if (name >= end) {
            return std::nullopt;
        }
        if (Kind(name) == TK::Identifier) {
            return name + 1;
        }
        return ArgumentOperatorNameEnd(name, end, work);
    }

    [[nodiscard]] bool ArgumentTemplateUse(
        TokenIndex following,
        TokenIndex end,
        bool explicitTemplate = false) const
    {
        if (explicitTemplate) {
            return true;
        }
        if (following >= end) {
            return false;
        }
        const auto kind = Kind(following);
        if (kind == TK::Operator_ClassMemberAccess ||
            kind == TK::Operator_IndirectClassMemberAccess)
        {
            return true;
        }
        if (kind != TK::Operator_OpenParenthesis) {
            return false;
        }
        const auto pair = ArgumentPairEnd(following, end);
        return pair && ArgumentSignificant(following + 1, *pair) == *pair - 1;
    }

    [[nodiscard]] bool HasDirectLambdaArgument(
        TokenIndex opening,
        TokenIndex closingEnd) const
    {
        const auto pair = ArgumentPairEnd(opening, closingEnd);
        if (!pair || *pair != closingEnd ||
            Kind(opening) != TK::Operator_OpenParenthesis)
        {
            return false;
        }
        const auto end = closingEnd - 1;
        bool atStart = true;
        bool hasDefault = false;
        unsigned conditionalMiddle = 0;
        size_t work = 4096;
        for (auto i = ArgumentSignificant(opening + 1, end);
            i < end && work != 0;
            i = ArgumentSignificant(i + 1, end))
        {
            --work;
            const auto kind = Kind(i);
            if (kind == TK::Keyword_template) {
                const auto nameEnd =
                    ArgumentExplicitMemberNameEnd(i, opening + 1, end, work);
                if (!nameEnd) {
                    return false;
                }
                const auto suffix = ArgumentSignificant(*nameEnd, end);
                if (suffix < end && Kind(suffix) == TK::Operator_Less) {
                    const auto after = ArgumentTemplateEnd(suffix, end, work);
                    if (!after) {
                        return false;
                    }
                    i = *after - 1;
                } else {
                    i = *nameEnd - 1;
                }
                continue;
            }
            if (!AdvanceConditionalMiddle(kind, conditionalMiddle)) {
                return false;
            }
            if (kind == TK::Operator_Comma) {
                if (conditionalMiddle != 0) {
                    continue;
                }
                atStart = true;
                hasDefault = false;
                continue;
            }
            if (atStart && !hasDefault && kind == TK::Operator_OpenBracket &&
                Kind(ArgumentSignificant(i + 1, end)) !=
                    TK::Operator_OpenBracket && ArgumentPairEnd(i, end))
            {
                // Capture origin retires parameters; it proves no suffix/body.
                return true;
            }
            atStart = false;
            hasDefault |= kind == TK::Operator_Assign;
            if (IsOpenDelimiter(kind)) {
                const auto after = ArgumentPairEnd(i, end);
                if (!after) {
                    return false;
                }
                i = *after - 1;
            } else if (kind == TK::Operator_Less) {
                const auto previous = PreviousSignificant(i);
                if (Kind(previous) != TK::Identifier) {
                    return false;
                }
                const auto after = ArgumentTemplateEnd(i, end, work);
                if (!after) {
                    return false;
                }
                const auto following = ArgumentSignificant(*after, end);
                // Known declaration types and unambiguous expression use
                // certify the span; unresolved/comparison syntax cannot.
                if (!IsKnownType(previous) &&
                    !ArgumentTemplateUse(following, end))
                {
                    return false;
                }
                i = *after - 1;
            }
        }
        return false;
    }

    [[nodiscard]] TokenIndex ArgumentExpressionEnd(
        TokenIndex begin,
        TokenIndex end,
        size_t& work) const
    {
        end = std::min(end, ParseLimit_);
        unsigned conditionalMiddle = 0;
        for (auto i = begin; i < end && work != 0; ++i) {
            --work;
            const auto kind = Kind(i);
            if (kind == TK::PreprocessingDirective) {
                return i;
            }
            if (kind == TK::Keyword_template) {
                const auto nameEnd =
                    ArgumentExplicitMemberNameEnd(i, begin, end, work);
                if (!nameEnd) {
                    return end;
                }
                const auto suffix = ArgumentSignificant(*nameEnd, end);
                if (suffix < end && Kind(suffix) == TK::Operator_Less) {
                    const auto after = ArgumentTemplateEnd(suffix, end, work);
                    if (!after) {
                        return end;
                    }
                    i = *after - 1;
                } else {
                    i = *nameEnd - 1;
                }
                continue;
            }
            if (!AdvanceConditionalMiddle(kind, conditionalMiddle)) {
                return end;
            }
            if (kind == TK::Operator_Comma && conditionalMiddle == 0) {
                return i;
            }
            if (IsOpenDelimiter(kind)) {
                const auto after = ArgumentPairEnd(i, end);
                if (!after) {
                    return end;
                }
                const auto suffix = ArgumentSignificant(*after, end);
                if (i == begin && kind == TK::Operator_OpenBracket &&
                    Kind(ArgumentSignificant(i + 1, end)) !=
                        TK::Operator_OpenBracket && suffix < end &&
                    Kind(suffix) == TK::Operator_Less)
                {
                    // Only this immediate capture context introduces a lambda
                    // template-parameter span; its commas are not arguments.
                    const auto templateEnd =
                        ArgumentTemplateEnd(suffix, end, work);
                    if (!templateEnd) {
                        return end;
                    }
                    i = *templateEnd - 1;
                } else {
                    i = *after - 1;
                }
            } else if (kind == TK::Operator_Less &&
                Kind(PreviousSignificant(i)) == TK::Identifier)
            {
                const auto after = ArgumentTemplateEnd(i, end, work);
                if (after) {
                    const auto following = ArgumentSignificant(*after, end);
                    const bool parenthesizedSuffix = following < end &&
                        Kind(following) == TK::Operator_OpenParenthesis &&
                        ArgumentPairEnd(following, end).has_value();
                    if (ArgumentTemplateUse(following, end) ||
                        parenthesizedSuffix)
                    {
                        // A nonempty suffix can still be a comparison operand,
                        // so expression parsing retains the ambiguous angles.
                        // Either interpretation ends before the comma after
                        // the paired suffix; do not recover the whole tail.
                        i = *after - 1;
                    } else {
                        // Ambiguous template/comparison commas have no proven
                        // argument boundary. Keep the remainder protected.
                        return end;
                    }
                }
            }
        }
        return end;
    }

    // Terminal-ID permission uses cached facts only. Generic balanced
    // consumption/recovery remains independent and may not certify permission.
    [[nodiscard]] std::optional<TokenIndex> TerminalCachedPairEnd(
        TokenIndex opening,
        TK closing) const
    {
        if (!HasDelimiterMap_ || opening >= ParseLimit_ ||
            opening >= Tree_.Tokens_.Size() ||
            !IsOpenDelimiter(Kind(opening)) ||
            ClosingDelimiter(Kind(opening)) != closing)
        {
            return std::nullopt;
        }
        const auto match = Tree_.Tokens_.MatchingDelimiter(opening);
        if (match <= opening || match >= ParseLimit_ ||
            match >= Tree_.Tokens_.Size() || Kind(match) != closing ||
            Tree_.Tokens_.MatchingDelimiter(match) != opening)
        {
            return std::nullopt;
        }
        return match + 1;
    }

    [[nodiscard]] bool TerminalImmediateOperand(
        TokenIndex introducer,
        TokenIndex opening,
        TokenIndex consumedEnd,
        std::optional<TokenIndex> cachedEnd) const
    {
        // The loop's one-use pending state proves significant-token adjacency.
        return introducer < opening && cachedEnd && *cachedEnd == consumedEnd &&
            (Kind(introducer) == TK::Keyword_noexcept ||
                Kind(introducer) == TK::Keyword_throw ||
                IsExtensionAttribute(introducer));
    }

    [[nodiscard]] bool TerminalBracketAttribute(
        TokenIndex opening,
        std::optional<TokenIndex> outerEnd) const
    {
        if (!outerEnd || opening + 1 >= *outerEnd ||
            Kind(opening + 1) != TK::Operator_OpenBracket)
        {
            return false;
        }
        const auto innerEnd =
            TerminalCachedPairEnd(opening + 1, TK::Operator_CloseBracket);
        return innerEnd && *innerEnd == *outerEnd - 1;
    }

    Node* ParseGeneralDeclaration()
    {
        auto start = Cursor_;
        TokenIndex declaratorOrigin = std::numeric_limits<TokenIndex>::max();
        TokenIndex bareInvocationName = std::numeric_limits<TokenIndex>::max();
        bool recoveredHeader = false;
        // Structural closure inside the current opaque header, not accepted
        // parameter syntax, function classification, or binding authority.
        bool opaqueHeaderParentheses = false;
        bool opaqueRequiresClause = false;
        TokenIndex parameterBegin = std::numeric_limits<TokenIndex>::max();
        TokenIndex parameterEnd = parameterBegin;
        bool sawTopLevelParameters = false;
        bool sawAssignmentBeforeParameters = false;
        bool sawParenthesizedDeclarator = false;
        bool constructorInitializer = false;
        bool invalidFunctionSuffix = false;
        bool sawTrailingReturnType = false;
        TokenIndex functionTry = std::numeric_limits<TokenIndex>::max();
        enum class TerminalPending
        {
            None,
            OptionalException,
            RequiredOperand,
            DefaultDelete,
            Semicolon
        };
        bool terminalEligible = false;
        bool terminalDenied = false;
        auto terminalPending = TerminalPending::None;
        TokenIndex terminalIntroducer = std::numeric_limits<TokenIndex>::max();

        auto retireParameters = [&] {
            sawTopLevelParameters = false;
            sawParenthesizedDeclarator = false;
            parameterBegin = std::numeric_limits<TokenIndex>::max();
            parameterEnd = parameterBegin;
            terminalDenied = true;
            terminalEligible = false;
            terminalPending = TerminalPending::None;
            terminalIntroducer = std::numeric_limits<TokenIndex>::max();
        };

        while (!AtEnd()) {
            auto kind = CurrentKind();
            // Directives delimit header fragments; they are never suffix
            // operands. The suffix builder proves conditional initializer
            // lists.
            if (kind == TK::PreprocessingDirective && sawTopLevelParameters) {
                ++Cursor_;
                continue;
            }
            TokenIndex immediateOperand =
                std::numeric_limits<TokenIndex>::max();
            if (sawTopLevelParameters && !constructorInitializer &&
                !terminalDenied && kind != TK::Comment)
            {
                bool reservationToken = false;
                if (terminalPending == TerminalPending::DefaultDelete) {
                    terminalDenied = kind != TK::Keyword_default &&
                        kind != TK::Keyword_delete;
                    terminalPending = TerminalPending::Semicolon;
                    reservationToken = true;
                } else if (terminalPending == TerminalPending::Semicolon) {
                    terminalDenied = kind != TK::Operator_Semicolon;
                    reservationToken = true;
                } else if (terminalPending != TerminalPending::None) {
                    if (kind == TK::Operator_OpenParenthesis) {
                        immediateOperand = terminalIntroducer;
                    } else if (terminalPending ==
                        TerminalPending::RequiredOperand)
                    {
                        terminalDenied = true;
                    }
                    // Optional bare noexcept cannot claim a later pair.
                    terminalPending = TerminalPending::None;
                    terminalIntroducer = std::numeric_limits<TokenIndex>::max();
                }
                if (!terminalDenied && !reservationToken) {
                    if (kind == TK::Keyword_requires ||
                        kind == TK::Operator_IndirectClassMemberAccess)
                    {
                        terminalDenied = true;
                    } else if (kind == TK::Operator_Assign) {
                        terminalDenied = functionTry !=
                            std::numeric_limits<TokenIndex>::max();
                        if (!terminalDenied) {
                            terminalPending = TerminalPending::DefaultDelete;
                        }
                    } else if (kind == TK::Keyword_noexcept ||
                        kind == TK::Keyword_throw ||
                        IsExtensionAttribute(Cursor_))
                    {
                        terminalIntroducer = Cursor_;
                        terminalPending = kind == TK::Keyword_noexcept
                            ? TerminalPending::OptionalException
                            : TerminalPending::RequiredOperand;
                    } else if (kind != TK::Operator_OpenParenthesis &&
                        kind != TK::Operator_OpenBracket &&
                        kind != TK::Operator_OpenBrace &&
                        kind != TK::Operator_Semicolon &&
                        kind != TK::Operator_Colon && kind != TK::Keyword_try &&
                        kind != TK::Keyword_const &&
                        kind != TK::Keyword_volatile &&
                        kind != TK::Operator_Ampersand &&
                        kind != TK::Operator_And &&
                        !IsCallingConvention(Cursor_) &&
                        !(kind == TK::Identifier &&
                            (Text(Cursor_) == "final" ||
                                Text(Cursor_) == "override")))
                    {
                        terminalDenied = true;
                    }
                }
            }
            if (kind == TK::Keyword_requires && Cursor_ != start) {
                const bool opaqueSuffix = recoveredHeader &&
                    opaqueHeaderParentheses && !constructorInitializer &&
                    !sawAssignmentBeforeParameters;
                const bool requiresExpression = Kind(
                    PreviousSignificant(Cursor_)) == TK::Keyword_requires ||
                    (opaqueSuffix && opaqueRequiresClause);
                // The first suffix requires introduces the clause. Its
                // parenthesized constraint must not swallow the actual body.
                opaqueRequiresClause |= opaqueSuffix;
                if (requiresExpression) {
                    const auto requiresEnd = recoveredHeader
                        ? opaqueSuffix
                            ? SkipRecoveredRequiresExpression(Cursor_)
                            : Cursor_ + 1
                        : SkipRequiresExpression(Cursor_);
                    if (requiresEnd != Cursor_ + 1) {
                        Cursor_ = requiresEnd;
                        continue;
                    }
                }
            }
            // Parentheses inside a return type's template arguments (for
            // example a pointer-to-member-function type) are not the
            // function's parameter list.
            if (!recoveredHeader && kind == TK::Operator_Less &&
                !sawTopLevelParameters && !sawAssignmentBeforeParameters &&
                Cursor_ > start &&
                Kind(PreviousSignificant(Cursor_)) == TK::Identifier)
            {
                auto after =
                    SkipTemplateArguments(Cursor_, Tree_.Tokens_.Size());
                if (after > Cursor_ && after < Tree_.Tokens_.Size() &&
                    (Kind(after - 1) == TK::Operator_Greater ||
                        Kind(after - 1) == TK::Operator_RightShift))
                {
                    Cursor_ = after;
                    continue;
                }
            }
            if (!recoveredHeader && kind == TK::Operator_Assign &&
                !sawTopLevelParameters)
            {
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
                const auto cachedEnd =
                    TerminalCachedPairEnd(begin, TK::Operator_CloseParenthesis);
                ConsumeBalanced();
                if (sawTopLevelParameters && !constructorInitializer &&
                    !terminalDenied &&
                    !TerminalImmediateOperand(
                        immediateOperand,
                        begin,
                        Cursor_,
                        cachedEnd))
                {
                    terminalDenied = true;
                }
                if (recoveredHeader && constructorInitializer &&
                    (AtEnd() ||
                        (Cursor_ > begin &&
                            Tree_.Tokens_.MatchingDelimiter(begin) ==
                                Cursor_ - 1)))
                {
                    // An opaque initializer operand is not a parameter list.
                    // Diagnosed exhaustion grants no closure or binding facts.
                    continue;
                }
                if (!recoveredHeader && !sawAssignmentBeforeParameters &&
                    !sawTopLevelParameters && !sawParenthesizedDeclarator &&
                    Kind(previous) == TK::Identifier &&
                    declaratorOrigin == std::numeric_limits<TokenIndex>::max())
                {
                    declaratorOrigin = FindDeclarationSpecifiersEnd(
                        start,
                        Tree_.Tokens_.Size());
                }
                if (!sawAssignmentBeforeParameters && !sawTopLevelParameters &&
                    !sawParenthesizedDeclarator &&
                    Kind(previous) == TK::Identifier &&
                    (begin == declaratorOrigin ||
                        previous == bareInvocationName))
                {
                    const auto following = NextSignificant(Cursor_);
                    const auto suffix = Kind(following);
                    if (cachedEnd && *cachedEnd == Cursor_ &&
                        suffix != TK::Identifier &&
                        !IsFunctionSuffixStart(following))
                    {
                        // A closed bare invocation cannot own the next
                        // declaration prefix, even after adjacent invocations
                        // made the header opaque. Keep identifier-led headers
                        // and possible function suffixes conservatively opaque.
                        auto* node = MakeNode(NodeKind::UnparsedSyntax, start);
                        node->Tokens.End = Cursor_;
                        AddDirectiveChildrenInGap(*node, start, Cursor_);
                        return node;
                    }
                    if (suffix == TK::Identifier &&
                        !IsFunctionSuffixStart(following))
                    {
                        // Adjacent bare invocations retain the unknown origin;
                        // they provide neither type nor parameter bindings.
                        bareInvocationName = SignificantKind(following + 1) ==
                            TK::Operator_OpenParenthesis
                            ? following
                            : std::numeric_limits<TokenIndex>::max();
                        recoveredHeader = true;
                        opaqueHeaderParentheses = Cursor_ > begin &&
                            Tree_.Tokens_.MatchingDelimiter(begin) ==
                                Cursor_ - 1;
                        opaqueRequiresClause = false;
                        retireParameters();
                        continue;
                    }
                    if (IsFunctionSuffixStart(following) &&
                        suffix != TK::Operator_Semicolon &&
                        suffix != TK::Operator_OpenBrace &&
                        suffix != TK::Operator_Colon &&
                        suffix != TK::Operator_Assign &&
                        suffix != TK::Operator_IndirectClassMemberAccess &&
                        suffix != TK::Operator_OpenParenthesis &&
                        suffix != TK::Keyword_try)
                    {
                        if (suffix == TK::Keyword_const ||
                            (suffix == TK::Operator_OpenBracket &&
                                Kind(following + 1) ==
                                    TK::Operator_OpenBracket))
                        {
                            const auto followingOrigin =
                                FindDeclarationSpecifiersEnd(
                                    following,
                                    Tree_.Tokens_.Size());
                            const auto type =
                                PreviousSignificant(followingOrigin);
                            auto name = followingOrigin;
                            if (Kind(name) == TK::Operator_Star ||
                                Kind(name) == TK::Operator_Ampersand ||
                                Kind(name) == TK::Operator_And)
                            {
                                name = NextSignificant(name + 1);
                            }
                            if (followingOrigin > following &&
                                (IsFundamentalTypeKeyword(Kind(type)) ||
                                    Kind(type) == TK::Identifier) &&
                                Kind(name) == TK::Identifier &&
                                !IsFunctionSuffixStart(name))
                            {
                                auto* node =
                                    MakeNode(NodeKind::UnparsedSyntax, start);
                                node->Tokens.End = Cursor_;
                                AddDirectiveChildrenInGap(
                                    *node,
                                    start,
                                    Cursor_);
                                return node;
                            }
                        }
                        // A bare invocation provides no parameter bindings.
                        // Retain uncertain suffixes with it, not as functions.
                        recoveredHeader = true;
                        opaqueHeaderParentheses = Cursor_ > begin &&
                            Tree_.Tokens_.MatchingDelimiter(begin) ==
                                Cursor_ - 1;
                        opaqueRequiresClause = false;
                        retireParameters();
                        continue;
                    }
                }
                if (recoveredHeader) {
                    // Balanced opaque operands never reacquire parameters or
                    // grouped-declarator authority. Header selectors above
                    // retain only their original absolute token indices.
                    continue;
                }
                // Specifier operands cannot introduce grouped declarators
                // or function parameters, even when their syntax resembles one.
                if (Kind(previous) == TK::Keyword_decltype ||
                    Kind(previous) == TK::Keyword_alignas)
                {
                    continue;
                }
                if (!sawTopLevelParameters && !sawParenthesizedDeclarator &&
                    IsParenthesizedDeclarator(begin, Cursor_))
                {
                    sawParenthesizedDeclarator = true;
                    for (auto nested = NextSignificant(begin + 1);
                        nested + 1 < Cursor_;
                        nested = NextSignificant(nested + 1))
                    {
                        if (Kind(nested) != TK::Operator_OpenParenthesis ||
                            Kind(PreviousSignificant(nested)) != TK::Identifier)
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
                        const auto nestedCachedEnd = TerminalCachedPairEnd(
                            nested,
                            TK::Operator_CloseParenthesis);
                        terminalEligible =
                            nestedCachedEnd && *nestedCachedEnd == nestedEnd;
                        terminalDenied |= !terminalEligible;
                        break;
                    }
                    continue;
                }
                bool isOperatorName = Kind(previous) == TK::Keyword_operator;
                bool isAttribute = Text(previous) == "__attribute__" ||
                    Text(previous) == "__declspec";
                if (!sawAssignmentBeforeParameters && !sawTopLevelParameters &&
                    !isOperatorName && !isAttribute)
                {
                    parameterBegin = begin;
                    parameterEnd = Cursor_;
                    if (!sawParenthesizedDeclarator) {
                        sawTopLevelParameters = true;
                        terminalEligible = cachedEnd && *cachedEnd == Cursor_;
                        terminalDenied |= !terminalEligible;
                    }
                }
                continue;
            }
            if (kind == TK::Operator_OpenBracket) {
                const auto cachedEnd =
                    TerminalCachedPairEnd(Cursor_, TK::Operator_CloseBracket);
                if (sawTopLevelParameters && !constructorInitializer &&
                    !terminalDenied &&
                    !TerminalBracketAttribute(Cursor_, cachedEnd))
                {
                    terminalDenied = true;
                }
                ConsumeBalanced();
                if (sawTopLevelParameters && !constructorInitializer &&
                    (!cachedEnd || *cachedEnd != Cursor_))
                {
                    terminalDenied = true;
                }
                continue;
            }
            if (!recoveredHeader && sawTopLevelParameters &&
                !sawTrailingReturnType &&
                Cursor_ == NextSignificant(parameterEnd) &&
                !IsFunctionSuffixStart(Cursor_))
            {
                if (kind == TK::Identifier &&
                    Kind(PreviousSignificant(parameterBegin)) == TK::Identifier)
                {
                    // An unknown function-like header component is not a
                    // parameter list. Keep its return/header context opaque;
                    // no body may use bindings from these provisional tokens.
                    recoveredHeader = true;
                    opaqueHeaderParentheses = parameterEnd > parameterBegin &&
                        Tree_.Tokens_.MatchingDelimiter(parameterBegin) ==
                            parameterEnd - 1;
                    opaqueRequiresClause = false;
                    retireParameters();
                    continue;
                }
                auto* node = MakeNode(NodeKind::UnparsedSyntax, start);
                node->Tokens.End = Cursor_;
                return node;
            }
            if (!recoveredHeader && sawTopLevelParameters &&
                kind == TK::Operator_IndirectClassMemberAccess)
            {
                bool hasPlaceholderAuto = false;
                for (auto index = start; index < parameterBegin; ++index) {
                    hasPlaceholderAuto |= Kind(index) == TK::Keyword_auto;
                }
                invalidFunctionSuffix |= !hasPlaceholderAuto;
                sawTrailingReturnType |= hasPlaceholderAuto;
            }
            if (kind == TK::Keyword_try &&
                (sawTopLevelParameters || recoveredHeader))
            {
                functionTry = Cursor_++;
                continue;
            }
            if (kind == TK::Operator_Colon &&
                (sawTopLevelParameters ||
                    (recoveredHeader && opaqueHeaderParentheses)))
            {
                constructorInitializer = true;
                ++Cursor_;
                continue;
            }
            if (kind == TK::Operator_OpenBrace) {
                if ((sawTopLevelParameters ||
                    (recoveredHeader && opaqueHeaderParentheses)) &&
                    !sawAssignmentBeforeParameters && constructorInitializer)
                {
                    // Apply the initializer/body distinction before either
                    // parsed or opaque body handling. Recovery must not end at
                    // a braced member initializer.
                    const auto previous = Kind(PreviousSignificant(Cursor_));
                    if (previous == TK::Identifier ||
                        previous == TK::Operator_Greater ||
                        previous == TK::Operator_RightShift)
                    {
                        ConsumeBalanced();
                        continue;
                    }
                }
                if (recoveredHeader) {
                    ConsumeBalanced();
                    auto* node = MakeNode(NodeKind::UnparsedSyntax, start);
                    if (functionTry != std::numeric_limits<TokenIndex>::max()) {
                        while (SignificantKind(Cursor_) == TK::Keyword_catch) {
                            Cursor_ = NextSignificant(Cursor_);
                            Append(*node, *ParseCatchClause(true));
                        }
                    }
                    node->Tokens.End = Cursor_;
                    AddDirectiveChildrenInGap(*node, start, Cursor_);
                    return node;
                }
                if (sawTopLevelParameters && !sawAssignmentBeforeParameters) {
                    // A sole type-like identifier followed by parentheses
                    // has no function declarator name, unless the current
                    // record establishes a constructor. An opaque header
                    // cannot establish the body's bindings or scope either.
                    if (!constructorInitializer &&
                        functionTry == std::numeric_limits<TokenIndex>::max() &&
                        Kind(start) == TK::Identifier &&
                        NextSignificant(start + 1) == parameterBegin &&
                        NextSignificant(parameterEnd) == Cursor_ &&
                        Kind(parameterEnd - 1) ==
                            TK::Operator_CloseParenthesis &&
                        FindDeclarationSpecifiersEnd(start, Cursor_) ==
                            parameterBegin)
                    {
                        auto* node = MakeNode(NodeKind::Declaration, start);
                        ConsumeBalanced();
                        if (CurrentKind() == TK::Operator_Semicolon) {
                            ++Cursor_;
                        }
                        node->Tokens.End = Cursor_;
                        AddUnparsedSyntax(*node, start, Cursor_);
                        return node;
                    }
                    return ParseFunctionDefinition(
                        start,
                        parameterBegin,
                        parameterEnd,
                        constructorInitializer,
                        functionTry,
                        !sawParenthesizedDeclarator,
                        terminalEligible && !terminalDenied &&
                        !invalidFunctionSuffix &&
                        terminalPending == TerminalPending::None);
                }
                ConsumeBalanced();
                continue;
            }
            if (kind == TK::Operator_Semicolon) {
                // Complete the reservation only at this visited, in-limit
                // semicolon, before cursor advancement or syntax-end trimming.
                const bool terminalConstructorEligible = terminalEligible &&
                    !terminalDenied && !invalidFunctionSuffix &&
                    (terminalPending == TerminalPending::None ||
                        terminalPending == TerminalPending::Semicolon);
                ++Cursor_;
                if (recoveredHeader) {
                    auto* node = MakeNode(NodeKind::UnparsedSyntax, start);
                    node->Tokens.End = Cursor_;
                    AddDirectiveChildrenInGap(*node, start, Cursor_);
                    return node;
                }
                bool directInitializer = false;
                const bool directLambda = sawTopLevelParameters &&
                    HasDirectLambdaArgument(parameterBegin, parameterEnd);
                if (sawTopLevelParameters && !sawParenthesizedDeclarator &&
                    !constructorInitializer && !sawAssignmentBeforeParameters &&
                    NextSignificant(parameterEnd) == Cursor_ - 1)
                {
                    const auto name = declaratorOrigin;
                    if (name > start && name < parameterBegin &&
                        Kind(name) == TK::Identifier &&
                        NextSignificant(name + 1) == parameterBegin)
                    {
                        bool ambiguous = false;
                        directInitializer = directLambda ||
                            (!InferStatementParameters(
                                name,
                                parameterEnd,
                                ambiguous,
                                true) && !ambiguous);
                    }
                }
                if (directInitializer) {
                    retireParameters();
                }
                // A top-level lambda argument or an empty argument slot
                // cannot be a function parameter declaration. At namespace
                // scope these are typically macro invocations: parse a call
                // rather than inventing overlapping declarator syntax.
                if (sawTopLevelParameters &&
                    (directLambda ||
                        HasEmptyArgumentSlot(parameterBegin, parameterEnd)))
                {
                    auto expressionCursor = start;
                    auto* expression =
                        ParseExpressionSyntax(expressionCursor, Cursor_ - 1);
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
                    : sawTopLevelParameters && !sawAssignmentBeforeParameters
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
                AddDeclarationSyntax(
                    *node,
                    start,
                    Cursor_,
                    parameters,
                    constructorInitializer && !sawParenthesizedDeclarator,
                    terminalConstructorEligible);
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

        auto* node = MakeNode(
            recoveredHeader ? NodeKind::UnparsedSyntax : NodeKind::Declaration,
            start);
        node->Tokens.End = Cursor_;
        if (!recoveredHeader) {
            AddDeclarationSyntax(*node, start, Cursor_);
        }
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
        TokenIndex functionTry,
        bool ungroupedParameters,
        bool terminalConstructorEligible)
    {
        auto* node = MakeNode(NodeKind::FunctionDefinition, start);
        auto parameters =
            TokenRange{.Begin = parameterBegin, .End = parameterEnd};
        if (constructorInitializer) {
            AddSingleDeclaratorDeclarationSyntax(
                *node,
                start,
                Cursor_,
                parameters,
                ungroupedParameters,
                terminalConstructorEligible);
        } else {
            AddDeclarationSyntax(
                *node,
                start,
                Cursor_,
                parameters,
                false,
                terminalConstructorEligible);
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
        // Attributes after the completed body belong to the next statement,
        // not to this if's else lookahead. Unsupported attributed statements
        // must retain their required semicolon in the enclosing sequence.
        ParseTriviaBeforeChild(*node, false);
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
                } else if (kind == TK::Operator_Colon && conditionalDepth == 0)
                {
                    rangeColon = i;
                } else if (kind == TK::Operator_Semicolon && semicolonCount < 2)
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

    bool AddConditionInitSyntax(Node& header, size_t begin, size_t end)
    {
        size_t separator = end;
        unsigned depth = 0;
        for (auto i = begin; i < end; ++i) {
            const auto kind = Kind(i);
            if (IsOpenDelimiter(kind)) {
                ++depth;
            } else if (kind == TK::Operator_CloseParenthesis ||
                kind == TK::Operator_CloseBracket ||
                kind == TK::Operator_CloseBrace)
            {
                if (depth == 0) {
                    return false;
                }
                --depth;
            } else if (depth == 0 && kind == TK::Operator_Semicolon) {
                if (separator != end) {
                    return false;
                }
                separator = i;
            }
        }
        if (separator == end || separator + 1 == end) {
            return false;
        }
        const auto first = NextSignificant(begin);
        const auto kind = first == separator
            ? NodeKind::ExpressionStatement
            : ClassifySimpleStatement(first);
        if (kind != NodeKind::DeclarationStatement &&
            kind != NodeKind::ExpressionStatement)
        {
            return false;
        }
        auto* initializer = MakeNode(kind, begin);
        initializer->Tokens.End = separator;
        if (kind == NodeKind::DeclarationStatement) {
            AddDeclarationSyntax(*initializer, begin, separator);
        } else {
            AddExpressionRange(*initializer, begin, separator);
        }
        Append(header, *initializer);
        // The semicolon stays a direct header token, with header-owned spacing
        // and a separate list boundary before the condition expression.
        AddConditionSyntax(header, separator + 1, end);
        return true;
    }

    [[nodiscard]] bool IsDeclarationCondition(size_t begin, size_t end) const
    {
        auto cursor = FindDeclarationSpecifiersEnd(begin, end, true);
        // A type-shaped prefix is not a declaration by itself: qualified
        // constants and template-id operands also start with known types.
        unsigned nested = 0;
        while (cursor < end) {
            cursor = SkipAttributeSpecifiers(cursor);
            const auto memberPointerEnd = FindMemberPointerEnd(cursor, end);
            if (memberPointerEnd > cursor) {
                cursor = NextSignificant(memberPointerEnd);
                continue;
            }
            const auto kind = Kind(cursor);
            if (kind == TK::Operator_OpenParenthesis) {
                const auto closingEnd = FindBalancedEnd(cursor);
                if (closingEnd > end) {
                    return false;
                }
                ++nested;
            } else if (kind != TK::Operator_Star &&
                kind != TK::Operator_Ampersand && kind != TK::Operator_And &&
                kind != TK::Keyword_const && kind != TK::Keyword_volatile)
            {
                break;
            }
            cursor = NextSignificant(cursor + 1);
        }
        if (cursor >= end || Kind(cursor) != TK::Identifier) {
            return false;
        }
        cursor = SkipAttributeSpecifiers(NextSignificant(cursor + 1));
        while (nested > 0 && Kind(cursor) == TK::Operator_CloseParenthesis) {
            --nested;
            cursor = NextSignificant(cursor + 1);
        }
        return nested == 0 && cursor < end &&
            (Kind(cursor) == TK::Operator_Assign ||
                Kind(cursor) == TK::Operator_OpenBrace);
    }

    void AddConditionSyntax(Node& header, size_t begin, size_t end)
    {
        if (!IsDeclarationCondition(begin, end)) {
            AddExpressionRange(header, begin, end);
            return;
        }
        const auto kind = ClassifySimpleStatement(begin);
        if (kind == NodeKind::ExpressionStatement) {
            AddExpressionRange(header, begin, end);
            return;
        }
        auto* declaration = MakeNode(kind, begin);
        declaration->Tokens.End = end;
        if (kind == NodeKind::DeclarationStatement) {
            AddDeclarationSyntax(*declaration, begin, end);
            RegisterDeclaredValues(*declaration);
        } else {
            // Unavailable type bindings remain ambiguous, not invented.
            declaration->Kind = NodeKind::AmbiguousStatement;
        }
        Append(header, *declaration);
    }

    void ParseControlHeader(Node& parent, NodeKind kind)
    {
        ParseTriviaBeforeChild(parent);
        if (CurrentKind() == TK::Identifier) {
            auto next = NextSignificant(Cursor_ + 1);
            if (Kind(next) == TK::Operator_OpenParenthesis) {
                // A macro-shaped header has no proven control delimiter.
                // Retain its whole spelling as recovery, not a Condition
                // whose opening gap would belong to the control keyword.
                auto begin = Cursor_;
                Cursor_ = next;
                ConsumeBalanced();
                AddUnparsedSyntax(parent, begin, Cursor_);
                return;
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
        if (parent.Kind == NodeKind::CatchClause) {
            if (Kind(NextSignificant(begin + 1)) != TK::Operator_Ellipsis) {
                AddDeclarationSyntax(*header, begin + 1, Cursor_ - 1);
                RegisterDeclaredValues(*header);
            }
            AddDirectiveChildrenInGap(*header, begin, Cursor_);
        } else if (kind == NodeKind::ForHeader) {
            AddForHeaderSyntax(*header, begin + 1, Cursor_ - 1);
            AddDirectiveChildrenInGap(*header, begin, Cursor_);
        } else if (kind == NodeKind::Condition &&
            (parent.Kind == NodeKind::IfStatement ||
                parent.Kind == NodeKind::SwitchStatement) &&
            AddConditionInitSyntax(*header, begin + 1, Cursor_ - 1))
        {
            AddDirectiveChildrenInGap(*header, begin, Cursor_);
        } else if (kind == NodeKind::Condition && begin + 1 < Cursor_ - 1) {
            AddConditionSyntax(*header, begin + 1, Cursor_ - 1);
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

    Node* ParseCatchClause(bool recovered = false)
    {
        auto start = Cursor_;
        auto* node = MakeNode(
            recovered ? NodeKind::UnparsedSyntax : NodeKind::CatchClause,
            start);
        ++Cursor_;
        bool conditionalBody = false;
        if (recovered) {
            Cursor_ = NextSignificant(Cursor_);
            if (CurrentKind() == TK::Operator_OpenParenthesis) {
                ConsumeBalanced();
            } else {
                ErrorAt(Cursor_, "expected catch parameter list");
            }
            while (CurrentKind() == TK::Comment) {
                ParseTrivia(*node, false);
            }
        } else {
            ParseControlHeader(*node, NodeKind::Condition);
            conditionalBody = ParseTriviaBeforeChild(*node);
        }
        if (CurrentKind() == TK::Operator_OpenBrace) {
            if (recovered) {
                ConsumeBalanced();
            } else {
                Append(*node, *ParseCompoundStatement());
            }
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
                kind != TK::Operator_Star && kind != TK::Operator_Ampersand &&
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

    Node* ParseLambdaExpression(TokenIndex& cursor, TokenIndex end)
    {
        end = std::min(end, ParseLimit_);
        auto start = cursor;
        auto capture = ArgumentPairEnd(cursor, end);
        auto captureEnd =
            capture.value_or(std::numeric_limits<TokenIndex>::max());
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
        cursor = ArgumentSignificant(captureEnd, end);

        size_t suffixWork = 4096;
        if (Kind(cursor) == TK::Operator_Less) {
            auto templateAfter = ArgumentTemplateEnd(cursor, end, suffixWork);
            auto templateEnd = templateAfter.value_or(cursor);
            if (templateEnd > cursor) {
                Append(
                    *lambda,
                    *MakeTemplateParameterList(cursor, templateEnd));
                cursor = ArgumentSignificant(templateEnd, end);
            }
        }
        Node* actualParameters = nullptr;
        if (Kind(cursor) == TK::Operator_OpenParenthesis) {
            auto parameterEnd = ArgumentPairEnd(cursor, end).value_or(cursor);
            if (parameterEnd > cursor) {
                auto* parameters = MakeNode(NodeKind::ParameterList, cursor);
                parameters->Tokens.End = parameterEnd;
                AddParameterDeclarations(
                    *parameters,
                    TokenRange{.Begin = cursor, .End = parameterEnd});
                AddDirectiveChildrenInGap(*parameters, cursor, parameterEnd);
                actualParameters = parameters;
                Append(*lambda, *parameters);
                cursor = ArgumentSignificant(parameterEnd, end);
            }
        }

        while (cursor < end && suffixWork != 0) {
            --suffixWork;
            auto kind = Kind(cursor);
            if (kind == TK::Operator_OpenBrace) {
                auto bodyEnd = ArgumentPairEnd(cursor, end).value_or(cursor);
                if (bodyEnd == cursor) {
                    break;
                }
                auto savedCursor = Cursor_;
                auto savedLimit = ParseLimit_;
                Cursor_ = cursor;
                ParseLimit_ = bodyEnd;
                ++BranchParseDepth_;
                // Requires-local parameters and defaults are not lambda
                // parameters and must not introduce body bindings.
                auto* body = ParseCompoundStatement(actualParameters);
                --BranchParseDepth_;
                ParseLimit_ = savedLimit;
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
                cursor = ArgumentSignificant(cursor + 1, end);
                continue;
            }
            if (kind == TK::Keyword_noexcept) {
                auto specificationEnd = cursor + 1;
                auto opening = ArgumentSignificant(specificationEnd, end);
                if (Kind(opening) == TK::Operator_OpenParenthesis) {
                    const auto balancedEnd = ArgumentPairEnd(opening, end);
                    if (!balancedEnd) {
                        break;
                    }
                    specificationEnd = *balancedEnd;
                }
                auto* specification =
                    MakeNode(NodeKind::ExceptionSpecification, cursor);
                specification->Tokens.End = specificationEnd;
                Append(*lambda, *specification);
                cursor = ArgumentSignificant(specificationEnd, end);
                continue;
            }
            if (kind == TK::Operator_IndirectClassMemberAccess) {
                auto trailingEnd = ArgumentSignificant(cursor + 1, end);
                bool hasType = false;
                bool namedType = false;
                bool needsComponent = false;
                while (trailingEnd < end && suffixWork != 0) {
                    --suffixWork;
                    const auto typeKind = Kind(trailingEnd);
                    if (typeKind == TK::Keyword_requires ||
                        typeKind == TK::Operator_OpenBrace)
                    {
                        break;
                    }
                    if (typeKind == TK::Keyword_decltype) {
                        const auto opening =
                            ArgumentSignificant(trailingEnd + 1, end);
                        const auto after =
                            Kind(opening) == TK::Operator_OpenParenthesis
                            ? ArgumentPairEnd(opening, end)
                            : std::nullopt;
                        if (!after) {
                            break;
                        }
                        hasType = true;
                        namedType = true;
                        needsComponent = false;
                        trailingEnd = ArgumentSignificant(*after, end);
                        continue;
                    }
                    if (typeKind == TK::Operator_Less &&
                        Kind(PreviousSignificant(trailingEnd)) ==
                            TK::Identifier)
                    {
                        const auto after =
                            ArgumentTemplateEnd(trailingEnd, end, suffixWork);
                        if (!after) {
                            break;
                        }
                        trailingEnd = ArgumentSignificant(*after, end);
                        continue;
                    }
                    if (typeKind == TK::Operator_ScopeResolution) {
                        const auto next =
                            ArgumentSignificant(trailingEnd + 1, end);
                        if (next >= end || Kind(next) != TK::Identifier) {
                            break;
                        }
                        needsComponent = true;
                    } else if (typeKind == TK::Identifier) {
                        if (hasType && !needsComponent) {
                            break;
                        }
                        hasType = true;
                        namedType = true;
                        needsComponent = false;
                    } else if (IsFundamentalTypeKeyword(typeKind)) {
                        if (namedType || needsComponent) {
                            break;
                        }
                        hasType = true;
                    } else if (typeKind == TK::Keyword_typename ||
                        typeKind == TK::Keyword_struct ||
                        typeKind == TK::Keyword_class ||
                        typeKind == TK::Keyword_enum ||
                        typeKind == TK::Keyword_union)
                    {
                        if (hasType) {
                            break;
                        }
                        needsComponent = true;
                    } else if (typeKind == TK::Keyword_const ||
                        typeKind == TK::Keyword_volatile)
                    {
                        if (needsComponent) {
                            break;
                        }
                    } else if (typeKind == TK::Operator_Star ||
                        typeKind == TK::Operator_Ampersand ||
                        typeKind == TK::Operator_And)
                    {
                        if (!hasType || needsComponent) {
                            break;
                        }
                    } else {
                        break;
                    }
                    trailingEnd = ArgumentSignificant(trailingEnd + 1, end);
                }
                if (!hasType || needsComponent || suffixWork == 0 ||
                    (Kind(trailingEnd) != TK::Operator_OpenBrace &&
                        Kind(trailingEnd) != TK::Keyword_requires))
                {
                    break;
                }
                auto* trailing = MakeNode(NodeKind::TrailingReturnType, cursor);
                trailing->Tokens.End = trailingEnd;
                AddTypeSyntaxChildren(*trailing, cursor + 1, trailingEnd);
                Append(*lambda, *trailing);
                cursor = ArgumentSignificant(trailingEnd, end);
                continue;
            }
            if (kind == TK::Keyword_requires) {
                auto requiresEnd = cursor + 1;
                while (requiresEnd < end && suffixWork != 0 &&
                    Kind(requiresEnd) != TK::Operator_OpenBrace)
                {
                    --suffixWork;
                    if (Kind(requiresEnd) == TK::Keyword_requires) {
                        auto operand =
                            ArgumentSignificant(requiresEnd + 1, end);
                        if (operand < end &&
                            Kind(operand) == TK::Operator_OpenParenthesis)
                        {
                            const auto after = ArgumentPairEnd(operand, end);
                            if (!after) {
                                break;
                            }
                            operand = ArgumentSignificant(*after, end);
                        }
                        const auto after = operand < end &&
                            Kind(operand) == TK::Operator_OpenBrace
                            ? ArgumentPairEnd(operand, end)
                            : std::nullopt;
                        if (!after) {
                            break;
                        }
                        requiresEnd = *after;
                    } else if (Kind(requiresEnd) == TK::Keyword_template) {
                        const auto nameEnd = ArgumentExplicitMemberNameEnd(
                            requiresEnd,
                            cursor + 1,
                            end,
                            suffixWork);
                        if (!nameEnd) {
                            break;
                        }
                        const auto suffix = ArgumentSignificant(*nameEnd, end);
                        if (suffix < end && Kind(suffix) == TK::Operator_Less) {
                            const auto after =
                                ArgumentTemplateEnd(suffix, end, suffixWork);
                            if (!after) {
                                break;
                            }
                            requiresEnd = *after;
                        } else {
                            requiresEnd = *nameEnd;
                        }
                    } else if (Kind(requiresEnd) == TK::Operator_Less) {
                        // An operand's template boundary owns its braces; an
                        // unproved angle never licenses a later lambda body.
                        if (Kind(PreviousSignificant(requiresEnd)) !=
                            TK::Identifier)
                        {
                            break;
                        }
                        const auto after =
                            ArgumentTemplateEnd(requiresEnd, end, suffixWork);
                        if (!after) {
                            break;
                        }
                        requiresEnd = *after;
                    } else if (IsOpenDelimiter(Kind(requiresEnd))) {
                        const auto after = ArgumentPairEnd(requiresEnd, end);
                        if (!after) {
                            break;
                        }
                        requiresEnd = *after;
                    } else if (Kind(requiresEnd) == TK::Operator_Comma ||
                        Kind(requiresEnd) == TK::Operator_Semicolon)
                    {
                        break;
                    } else {
                        ++requiresEnd;
                    }
                }
                if (suffixWork == 0 || requiresEnd >= end ||
                    ArgumentSignificant(cursor + 1, end) == requiresEnd ||
                    Kind(requiresEnd) != TK::Operator_OpenBrace)
                {
                    break;
                }
                auto* requiresClause =
                    MakeNode(NodeKind::RequiresClause, cursor);
                requiresClause->Tokens.End = requiresEnd;
                auto constraintBegin = ArgumentSignificant(cursor + 1, end);
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
                cursor = ArgumentSignificant(requiresEnd, end);
                continue;
            }
            if (kind == TK::Operator_OpenBracket &&
                Kind(ArgumentSignificant(cursor + 1, end)) ==
                    TK::Operator_OpenBracket)
            {
                auto attributeEnd =
                    ArgumentPairEnd(cursor, end).value_or(cursor);
                if (attributeEnd == cursor) {
                    break;
                }
                auto* attribute =
                    MakeNode(NodeKind::AttributeSpecifier, cursor);
                attribute->Tokens.End = attributeEnd;
                Append(*lambda, *attribute);
                cursor = ArgumentSignificant(attributeEnd, end);
                continue;
            }
            break;
        }
        // Unsupported/missing suffixes are locally protected, not certified
        // by finding a later brace. The caller supplied this argument's end.
        ErrorAt(cursor, "expected lambda body or supported suffix");
        AddUnparsedSyntax(*lambda, cursor, end);
        if (cursor >= end) {
            AddMissingToken(*lambda, end);
        }
        cursor = end;
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
        if (bodyEnd == std::numeric_limits<TokenIndex>::max() || bodyEnd > end)
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
                parenthesizedEnd <= end && following < end &&
                (IsTypeKeyword(Kind(following)) ||
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
        if (typeBegin == typeEnd) {
            // Without a new-type-id, parentheses cannot be an initializer.
            // Parenthesized type-ids are conservatively retained here too;
            // this producer does not establish their type ownership.
            if (Kind(cursor) == TK::Operator_OpenParenthesis ||
                Kind(cursor) == TK::Operator_OpenBrace)
            {
                auto recoveryEnd = FindBalancedEnd(cursor);
                if (recoveryEnd != std::numeric_limits<TokenIndex>::max() &&
                    recoveryEnd <= end)
                {
                    // A parenthesized unknown type may have one initializer
                    // suffix. It is not a postfix call on a proven expression.
                    auto suffix = NextSignificant(recoveryEnd);
                    if (Kind(cursor) == TK::Operator_OpenParenthesis &&
                        suffix < end &&
                        (Kind(suffix) == TK::Operator_OpenParenthesis ||
                            Kind(suffix) == TK::Operator_OpenBrace))
                    {
                        auto suffixEnd = FindBalancedEnd(suffix);
                        if (suffixEnd !=
                            std::numeric_limits<TokenIndex>::max() &&
                            suffixEnd <= end)
                        {
                            recoveryEnd = suffixEnd;
                        }
                    }
                    AddUnparsedSyntax(*expression, cursor, recoveryEnd);
                    cursor = recoveryEnd;
                } else {
                    AddUnparsedSyntax(*expression, cursor, end);
                    cursor = end;
                }
            } else {
                AddMissingToken(*expression, cursor);
            }
            expression->Tokens.End = cursor;
            return expression;
        }
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
            AddMissingToken(*expression, cursor);
            expression->Tokens.End = cursor;
        }
        return expression;
    }

    [[nodiscard]] bool IsTypeTemplateArgument(
        TokenIndex firstArgument,
        TokenIndex following) const
    {
        auto afterArgument = NextSignificant(firstArgument + 1);
        const bool knownTypeArgument = IsKnownType(firstArgument);
        bool sharedCloser = false;
        if (knownTypeArgument && Kind(afterArgument) == TK::Operator_Less) {
            // A locally known type remains a type when specialized. Do not
            // mistake its nested angles for a competing comparison, and keep
            // a shared >> available as the enclosing argument's terminator.
            bool exactClosing = false;
            const auto nestedEnd = SkipTemplateArguments(
                afterArgument,
                following,
                false,
                &exactClosing);
            sharedCloser = nestedEnd > afterArgument &&
                Kind(nestedEnd - 1) == TK::Operator_RightShift &&
                NextSignificant(nestedEnd) == following && !exactClosing;
            if (exactClosing || sharedCloser) {
                afterArgument = NextSignificant(nestedEnd);
            }
        }
        return (knownTypeArgument ||
            IsFundamentalTypeKeyword(Kind(firstArgument))) &&
            (sharedCloser || Kind(afterArgument) == TK::Operator_Comma ||
                Kind(afterArgument) == TK::Operator_Greater ||
                Kind(afterArgument) == TK::Operator_RightShift);
    }

    [[nodiscard]] bool HasTypeTemplateArgument(
        TokenIndex opening,
        TokenIndex following) const
    {
        auto first = NextSignificant(opening + 1);
        if (IsTypeTemplateArgument(first, following)) {
            return true;
        }
        // A definite type in any top-level argument rules out comparison
        // syntax. Nested lists, operand delimiters, and conditional middles
        // must not expose their commas as the enclosing list's separators.
        size_t work = 4096;
        unsigned depth = 1;
        unsigned middle = 0;
        for (auto i = first; i < following && work != 0; ++i) {
            --work;
            const auto kind = Kind(i);
            if (IsOpenDelimiter(kind)) {
                const auto after = ArgumentPairEnd(i, following);
                if (!after) {
                    return false;
                }
                i = *after - 1;
            } else if (kind == TK::Operator_Less) {
                if (++depth > 256) {
                    return false;
                }
            } else if (kind == TK::Operator_Greater ||
                kind == TK::Operator_RightShift)
            {
                const unsigned count = kind == TK::Operator_RightShift ? 2 : 1;
                if (count >= depth) {
                    return false;
                }
                depth -= count;
            } else if (depth == 1) {
                if (!AdvanceConditionalMiddle(kind, middle)) {
                    return false;
                }
                if (kind == TK::Operator_Comma && middle == 0 &&
                    IsTypeTemplateArgument(NextSignificant(i + 1), following))
                {
                    return true;
                }
            }
        }
        return false;
    }

    [[nodiscard]] bool IsUnambiguousTemplateUse(
        TokenIndex following,
        TokenIndex end,
        bool explicitTemplate,
        TokenIndex name,
        TokenIndex opening) const
    {
        // Local type bindings rule out the competing comparison expression.
        // Included or dependent names alone still do not prove template use.
        const bool unqualifiedTypeName =
            NextSignificant(name + 1) == opening && IsKnownType(name);
        if (explicitTemplate || unqualifiedTypeName ||
            HasTypeTemplateArgument(opening, following) ||
            Kind(following) == TK::Operator_ClassMemberAccess ||
            Kind(following) == TK::Operator_IndirectClassMemberAccess)
        {
            return true;
        }
        if (Kind(following) == TK::Operator_OpenBrace) {
            // A braced initializer cannot be a comparison's right operand;
            // it proves the preceding balanced angle list is a template-id.
            return FindBalancedEnd(following) <= end;
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

        const auto memberBegin = cursor;
        bool explicitTemplate = Kind(cursor) == TK::Keyword_template;
        size_t templateWork = 4096;
        if (explicitTemplate) {
            cursor = NextSignificant(cursor + 1);
            if (cursor >= end) {
                return nullptr;
            }
        }

        if (explicitTemplate) {
            const auto nameKind = Kind(cursor);
            bool supportedName = nameKind == TK::Identifier;
            if (nameKind == TK::Keyword_operator) {
                supportedName =
                    ArgumentOperatorNameEnd(cursor, end, templateWork)
                        .has_value();
                const auto symbol = ArgumentSignificant(cursor + 1, end);
                if (symbol < end) {
                    supportedName =
                        supportedName || Kind(symbol) == TK::Identifier ||
                            IsTypeKeyword(Kind(symbol));
                }
            }
            if (!supportedName) {
                ErrorAt(memberBegin, "expected member template name");
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
                    std::numeric_limits<TokenIndex>::max() && operatorEnd <= end
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
                        auto templateEnd = explicitTemplate
                            ? ArgumentTemplateEnd(cursor, end, templateWork)
                                .value_or(cursor)
                            : SkipTemplateArguments(cursor, end);
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

        auto templateEnd = explicitTemplate
            ? ArgumentTemplateEnd(cursor, end, templateWork).value_or(cursor)
            : SkipTemplateArguments(cursor, end);
        auto closingKind = templateEnd > cursor
            ? Kind(templateEnd - 1)
            : TK::Invalid;
        auto following = NextSignificant(templateEnd);
        bool hasClosing = templateEnd <= end &&
            (closingKind == TK::Operator_Greater ||
                closingKind == TK::Operator_RightShift);
        bool hasTemplateUse = IsUnambiguousTemplateUse(
            following,
            end,
            explicitTemplate,
            nameBegin,
            cursor);
        if (!hasClosing) {
            if (explicitTemplate) {
                ErrorAt(memberBegin, "unterminated member template arguments");
                auto* unparsed =
                    MakeNode(NodeKind::UnparsedSyntax, memberBegin);
                unparsed->Tokens.End = end;
                cursor = end;
                return unparsed;
            }
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

    [[nodiscard]] bool IsTypeConstructionName(const Node& name) const
    {
        const auto* node = &name;
        while (node) {
            if (node->Kind == NodeKind::IdentifierExpression) {
                return Kind(node->Tokens.Begin) == TK::Identifier ||
                    Kind(node->Tokens.Begin) == TK::Operator_ScopeResolution;
            }
            if (node->Kind == NodeKind::Expression) {
                return node->Tokens.Size() == 1 &&
                    IsFundamentalTypeKeyword(Kind(node->Tokens.Begin));
            }
            if (node->Kind == NodeKind::CallExpression) {
                return Kind(node->Tokens.Begin) == TK::Keyword_decltype;
            }
            if (node->Kind == NodeKind::TemplateIdExpression) {
                node = node->FirstChild;
                continue;
            }
            if (node->Kind == NodeKind::MemberExpression && node->FirstChild &&
                Kind(NextSignificant(node->FirstChild->Tokens.End)) ==
                    TK::Operator_ScopeResolution)
            {
                node = node->FirstChild;
                continue;
            }
            return false;
        }
        return false;
    }

    Node* ParseExpressionSyntax(
        TokenIndex& cursor,
        TokenIndex end,
        int minimum = 1)
    {
        cursor = NextSignificant(cursor);
        if (cursor >= end) {
            return nullptr;
        }

        auto start = cursor;
        Node* left = nullptr;
        auto kind = Kind(cursor);
        if (kind == TK::PreprocessingDirective) {
            return nullptr;
        }
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
                    auto* typeArguments =
                        MakeNode(NodeKind::TemplateArgumentList, angleBegin);
                    typeArguments->Tokens.End = angleEnd;
                    auto* type = MakeNode(NodeKind::TypeId, angleBegin + 1);
                    type->Tokens.End =
                        Kind(angleEnd - 1) == TK::Operator_RightShift
                        ? angleEnd
                        : angleEnd - 1;
                    if (type->Tokens.Empty()) {
                        AddMissingToken(*type, type->Tokens.Begin);
                    } else {
                        AddTypeSyntaxChildren(
                            *type,
                            angleBegin + 1,
                            type->Tokens.End);
                    }
                    Append(*typeArguments, *type);
                    Append(*cast, *typeArguments);
                    auto operandCursor = opening + 1;
                    if (auto* operand =
                        ParseExpressionSyntax(operandCursor, closingEnd - 1))
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
        } else if (kind == TK::Keyword_sizeof &&
            Kind(NextSignificant(cursor + 1)) == TK::Operator_Ellipsis)
        {
            auto opening = NextSignificant(NextSignificant(cursor + 1) + 1);
            auto closingEnd = Kind(opening) == TK::Operator_OpenParenthesis
                ? FindBalancedEnd(opening)
                : std::numeric_limits<TokenIndex>::max();
            left = MakeNode(NodeKind::UnaryExpression, start);
            if (closingEnd != std::numeric_limits<TokenIndex>::max() &&
                closingEnd <= end)
            {
                // sizeof... has one pack name, not a call argument list.
                // Keep the proven prefix separate from operand recovery.
                auto* operand =
                    MakeNode(NodeKind::ParenthesizedExpression, opening);
                operand->Tokens.End = closingEnd;
                auto name = NextSignificant(opening + 1);
                if (name == closingEnd - 1) {
                    AddMissingToken(*operand, name);
                } else if (Kind(name) == TK::Identifier &&
                    NextSignificant(name + 1) == closingEnd - 1)
                {
                    auto* identifier =
                        MakeNode(NodeKind::IdentifierExpression, name);
                    identifier->Tokens.End = name + 1;
                    Append(*operand, *identifier);
                } else {
                    AddUnparsedSyntax(*operand, name, closingEnd - 1);
                }
                Append(*left, *operand);
                cursor = closingEnd;
            } else {
                auto prefixEnd = opening;
                AddUnparsedSyntax(*left, start, prefixEnd);
                cursor = prefixEnd;
                AddMissingToken(*left, cursor);
            }
            left->Tokens.End = cursor;
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
            // `({ ... })` can also wrap a braced initializer in a direct
            // initializer. A top-level semicolon distinguishes the GNU
            // statement expression from an initializer list.
            if (statementEnd == closing &&
                HasTopLevelSemicolonInBraces(innerBegin, statementEnd))
            {
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
            } else if (closingEnd < end && LooksLikeTypeId(cursor + 1, closing))
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
                AddUnparsedSyntax(*left, innerCursor, closing);
            }
        } else if (kind == TK::Operator_ScopeResolution) {
            const auto name = NextSignificant(cursor + 1);
            if (name < end && Kind(name) == TK::Identifier) {
                cursor = name + 1;
                left = MakeNode(NodeKind::IdentifierExpression, start);
                left->Tokens.End = cursor;
            } else {
                left = MakeNode(NodeKind::UnparsedSyntax, start);
                left->Tokens.End = ++cursor;
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
                : (kind == TK::Identifier || kind == TK::Keyword_this
                    ? NodeKind::IdentifierExpression
                    : NodeKind::Expression);
            left = MakeNode(nodeKind, cursor);
            left->Tokens.End = ++cursor;
            if (kind == TK::Literal_String) {
                // Adjacent string tokens form one primary expression. Keep
                // intervening comments in its range, but not trailing trivia.
                auto next = NextSignificant(cursor);
                while (next < end && Kind(next) == TK::Literal_String) {
                    cursor = next + 1;
                    left->Tokens.End = cursor;
                    next = NextSignificant(cursor);
                }
            }
        }

        while (left && cursor < end) {
            cursor = NextSignificant(cursor);
            if (cursor >= end) {
                break;
            }
            kind = Kind(cursor);
            if (kind == TK::Operator_Less) {
                auto templateEnd = SkipTemplateArguments(cursor, end);
                auto closingKind = templateEnd > cursor
                    ? Kind(templateEnd - 1)
                    : TK::Invalid;
                auto following = NextSignificant(templateEnd);
                bool hasClosing = templateEnd <= end &&
                    (closingKind == TK::Operator_Greater ||
                        closingKind == TK::Operator_RightShift);
                if (hasClosing &&
                    IsUnambiguousTemplateUse(
                        following,
                        end,
                        false,
                        start,
                        cursor))
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
            if (kind == TK::Operator_OpenBrace && IsTypeConstructionName(*left))
            {
                const auto closingEnd = FindBalancedEnd(cursor);
                if (closingEnd == std::numeric_limits<TokenIndex>::max() ||
                    closingEnd > end)
                {
                    break;
                }
                auto* construction =
                    MakeNode(NodeKind::TypeConstructionExpression, start);
                construction->Tokens.End = closingEnd;
                Append(*construction, *left);
                auto* initializer =
                    MakeNode(NodeKind::BracedInitializer, cursor);
                initializer->Tokens.End = closingEnd;
                AddBracedInitializerElements(*initializer, cursor, closingEnd);
                Append(*construction, *initializer);
                cursor = closingEnd;
                left = construction;
                continue;
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
                auto* name = memberCursor < end &&
                    (Kind(memberCursor) == TK::Identifier ||
                        Kind(memberCursor) == TK::Keyword_template ||
                        Kind(memberCursor) == TK::Keyword_operator ||
                        Kind(memberCursor) == TK::Operator_Complement)
                    ? ParseMemberName(memberCursor, end)
                    : nullptr;
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
            auto nodeKind = binary.Precedence == 2
                ? NodeKind::AssignmentExpression
                : NodeKind::BinaryExpression;
            auto* expression = MakeNode(nodeKind, start);
            expression->Tokens.End = right->Tokens.End;
            Append(*expression, *left);
            Append(*expression, *right);
            left = expression;
        }
        return left;
    }

    [[nodiscard]] bool LooksLikeUnknownTypeDeclaration(
        size_t first,
        size_t end) const
    {
        bool hasQualifiedType = Kind(first) == TK::Operator_ScopeResolution;
        if (hasQualifiedType) {
            first = NextSignificant(first + 1);
            if (first >= end || Kind(first) != TK::Identifier) {
                return false;
            }
        }
        auto cursor = NextSignificant(first + 1);
        while (cursor < end) {
            if (Kind(cursor) == TK::Operator_Less) {
                auto templateEnd = SkipTemplateArguments(cursor, end);
                auto closingKind = templateEnd > cursor
                    ? Kind(templateEnd - 1)
                    : TK::Invalid;
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
        while (cursor < end &&
            (Kind(cursor) == TK::Keyword_const ||
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
        if (IsTypeKeyword(Kind(first))) {
            return NodeKind::DeclarationStatement;
        }
        const bool global = Kind(first) == TK::Operator_ScopeResolution;
        const auto name = global ? NextSignificant(first + 1) : first;
        const auto* binding = Kind(name) == TK::Identifier
            ? LookupBinding(Text(name), global)
            : nullptr;
        if (binding && binding->Kind == NameKind::Type) {
            return NodeKind::DeclarationStatement;
        }
        if (Kind(name) != TK::Identifier || binding) {
            return NodeKind::ExpressionStatement;
        }

        auto next = NextSignificant(name + 1);
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
        for (auto* part = declarator.FirstChild; part; part = part->NextSibling)
        {
            if (part->Kind == NodeKind::DeclaratorName && !part->Tokens.Empty())
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
            for (auto* init = list->FirstChild; init; init = init->NextSibling)
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
                if (Kind(unknown) == TK::Operator_ScopeResolution) {
                    unknown = NextSignificant(unknown + 1);
                }
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

    bool ParseTriviaBeforeChild(Node& parent, bool allowAttributes = true)
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
            if (allowAttributes && CurrentKind() == TK::Operator_OpenBracket &&
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
                    .End = sourceBegin + static_cast<SourceOffset>(text.size()),
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
                    .End = sourceBegin +
                        static_cast<SourceOffset>(whitespace.size()),
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
            if (previous && previous->Kind == NodeKind::ListSeparator && next &&
                Text(i).starts_with("/*"))
            {
                // A block comment after a list separator describes the next
                // item, even when it shares a line with the separator. Line
                // comments retain their original line-based attachment.
                AttachComment(*next, i, TriviaPlacement::Leading);
            } else if (previous && !previous->Tokens.Empty() &&
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
            if (whitespace[i] == '\n' && (i == 0 || whitespace[i - 1] != '\\'))
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
            kind == NodeKind::MacroRecordDeclaration ||
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
            if (i + 1 >= end || Kind(i) != TK::Operator_ScopeResolution ||
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
                        Tree_.Tokens_
                            .SetRole(i, PunctuationRole::PointerDeclarator);
                    } else if (Kind(i) == TK::Operator_Ampersand ||
                        Kind(i) == TK::Operator_And)
                    {
                        Tree_.Tokens_
                            .SetRole(i, PunctuationRole::ReferenceDeclarator);
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
                        Tree_.Tokens_
                            .SetRole(i, PunctuationRole::ConditionalDelimiter);
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
                        Tree_.Tokens_
                            .SetRole(i, PunctuationRole::LabelDelimiter);
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
            case NodeKind::MacroRecordDeclaration:
            case NodeKind::EnumDeclaration:
            case NodeKind::TemplateDeclaration:
            case NodeKind::ExportDeclaration:
            case NodeKind::LinkageSpecification:
            case NodeKind::FunctionDefinition:
            case NodeKind::MacroFunctionDefinition:
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
        detail::InitialMemoryResourceSize,
    };
    std::pmr::vector<std::string_view> RecordNames_{&ScratchMemory_};
    std::pmr::vector<std::string_view> FunctionBodyMacroNames_{&ScratchMemory_};
    std::pmr::vector<std::pair<std::string_view, std::string_view>>
        ClassMacroPairs_{&ScratchMemory_};
    std::pmr::vector<std::string_view> ClassEndMacroNames_{&ScratchMemory_};
    size_t MacroRecordDepth_ = 0;
    NameScope* CurrentScope_ = nullptr;
    Node* NextNode_ = nullptr;
    size_t RemainingNodes_ = 0;
    TokenIndex Cursor_ = 0;
    TokenIndex ParseLimit_ = 0;
    size_t BranchParseDepth_ = 0;
    unsigned TemplateListDepth_ = 0;
    static constexpr size_t MaximumParseDepth = 256;
    std::array<size_t, MaximumParseDepth> ConditionalOpenBraces_{};
    size_t ConditionalBraceScopeDepth_ = 0;
    size_t ConditionalBraceScopeOverflow_ = 0;
    bool HasDelimiterMap_ = true;
};

SyntaxTree ParseContext::Run(
    util::PaddedSourceView input,
    ErrorHandler errorHandler,
    const ParseOptions& options)
{
    SyntaxTree tree(input.View());
    ParseContext context(tree, errorHandler, options);
    context.Build();
    return tree;
}

SyntaxTree Parse(
    util::PaddedSourceView input,
    ErrorHandler errorHandler,
    const ParseOptions& options)
{
    HIT_FMT_PROFILE_PHASE("parser.total");
    return ParseContext::Run(input, std::move(errorHandler), options);
}

} // namespace parser
