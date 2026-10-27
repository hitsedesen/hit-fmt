#include "tokenizer.hpp"

#include "token.hpp"

#include <unicode/code_point.hpp>
#include <unicode/utf8.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <type_traits>
#include <utility>

#if defined(_MSC_VER) && defined(_M_X64)
#include <intrin.h>
#endif

namespace tokenizer {

using util::SourceOffset;

using detail::AsciiRun;

std::string_view GetTokenKindName(TokenKind kind)
{
    constexpr auto Names = std::to_array<std::string_view>({
#define TOKEN_KIND(value, name) {name},
        TOKEN_KINDS
#undef TOKEN_KIND
    });

    return Names[static_cast<std::underlying_type_t<TokenKind>>(kind)];
}

std::string_view GetTokenizationErrorDescription(TokenizationErrorKind kind)
{
    constexpr auto Descriptions = std::to_array<std::string_view>({
#define TOKENIZATION_ERROR_KIND(value, description) {description},
        TOKENIZATION_ERROR_KINDS
#undef TOKENIZATION_ERROR_KIND
    });

    using Underlying = std::underlying_type_t<TokenizationErrorKind>;
    return Descriptions[static_cast<Underlying>(kind)];
}

namespace {

constexpr uint32_t AsciiLimit = 0x80;

constexpr uint64_t IdentifierKey(std::string_view text)
{
    return text.size() | (static_cast<uint64_t>(text[0]) << 8) | (static_cast<uint64_t>(text[1]) << 16) |
        (static_cast<uint64_t>(text.size() > 4 ? text[4] : '\0') << 24) | (static_cast<uint64_t>(text.back()) << 32);
}

constexpr size_t KeywordSlot(uint64_t key)
{
    return (key * 0x1ed83522862e78afULL) >> 54;
}

struct KeywordLookup
{
    struct Text
    {
        uint64_t First;
        uint64_t Second;
        uint8_t Length;
    };

    std::array<Text, static_cast<size_t>(TokenKind::Keyword_while) + 1> Words;
    std::array<TokenKind, 1024> Slots;
};

constexpr auto Keywords = [] {
    KeywordLookup table{};
    auto add = [&] (std::string_view text, TokenKind kind) {
        const auto slot = KeywordSlot(IdentifierKey(text));
        if (table.Slots[slot] != TokenKind::Invalid) {
            throw "keyword hash collision";
        }
        table.Slots[slot] = kind;
        auto& word = table.Words[static_cast<size_t>(kind)];
        word.Length = static_cast<uint8_t>(text.size());
        for (size_t i = 0; i < text.size(); ++i) {
            auto& part = i < 8 ? word.First : word.Second;
            part |= static_cast<uint64_t>(text[i]) << ((i % 8) * 8);
        }
    };
#define TOKENIZER_KEYWORD(text, kind) add(text, TokenKind::kind)
    TOKENIZER_KEYWORD("and", Operator_And);
    TOKENIZER_KEYWORD("bitor", Operator_BitOr);
    TOKENIZER_KEYWORD("or", Operator_Or);
    TOKENIZER_KEYWORD("xor", Operator_Xor);
    TOKENIZER_KEYWORD("compl", Operator_Complement);
    TOKENIZER_KEYWORD("bitand", Operator_Ampersand);
    TOKENIZER_KEYWORD("and_eq", Operator_BitAndAssign);
    TOKENIZER_KEYWORD("or_eq", Operator_BitOrAssign);
    TOKENIZER_KEYWORD("xor_eq", Operator_XorAssign);
    TOKENIZER_KEYWORD("not", Operator_Not);
    TOKENIZER_KEYWORD("not_eq", Operator_NotEquals);
    TOKENIZER_KEYWORD("alignas", Keyword_alignas);
    TOKENIZER_KEYWORD("alignof", Keyword_alignof);
    TOKENIZER_KEYWORD("asm", Keyword_asm);
    TOKENIZER_KEYWORD("auto", Keyword_auto);
    TOKENIZER_KEYWORD("bool", Keyword_bool);
    TOKENIZER_KEYWORD("break", Keyword_break);
    TOKENIZER_KEYWORD("case", Keyword_case);
    TOKENIZER_KEYWORD("catch", Keyword_catch);
    TOKENIZER_KEYWORD("char", Keyword_char);
    TOKENIZER_KEYWORD("char8_t", Keyword_char8_t);
    TOKENIZER_KEYWORD("char16_t", Keyword_char16_t);
    TOKENIZER_KEYWORD("char32_t", Keyword_char32_t);
    TOKENIZER_KEYWORD("class", Keyword_class);
    TOKENIZER_KEYWORD("concept", Keyword_concept);
    TOKENIZER_KEYWORD("const", Keyword_const);
    TOKENIZER_KEYWORD("consteval", Keyword_consteval);
    TOKENIZER_KEYWORD("constexpr", Keyword_constexpr);
    TOKENIZER_KEYWORD("constinit", Keyword_constinit);
    TOKENIZER_KEYWORD("const_cast", Keyword_const_cast);
    TOKENIZER_KEYWORD("continue", Keyword_continue);
    TOKENIZER_KEYWORD("co_await", Keyword_co_await);
    TOKENIZER_KEYWORD("co_return", Keyword_co_return);
    TOKENIZER_KEYWORD("co_yield", Keyword_co_yield);
    TOKENIZER_KEYWORD("decltype", Keyword_decltype);
    TOKENIZER_KEYWORD("default", Keyword_default);
    TOKENIZER_KEYWORD("delete", Keyword_delete);
    TOKENIZER_KEYWORD("do", Keyword_do);
    TOKENIZER_KEYWORD("double", Keyword_double);
    TOKENIZER_KEYWORD("dynamic_cast", Keyword_dynamic_cast);
    TOKENIZER_KEYWORD("else", Keyword_else);
    TOKENIZER_KEYWORD("enum", Keyword_enum);
    TOKENIZER_KEYWORD("explicit", Keyword_explicit);
    TOKENIZER_KEYWORD("export", Keyword_export);
    TOKENIZER_KEYWORD("extern", Keyword_extern);
    TOKENIZER_KEYWORD("false", Keyword_false);
    TOKENIZER_KEYWORD("float", Keyword_float);
    TOKENIZER_KEYWORD("for", Keyword_for);
    TOKENIZER_KEYWORD("friend", Keyword_friend);
    TOKENIZER_KEYWORD("goto", Keyword_goto);
    TOKENIZER_KEYWORD("if", Keyword_if);
    TOKENIZER_KEYWORD("inline", Keyword_inline);
    TOKENIZER_KEYWORD("int", Keyword_int);
    TOKENIZER_KEYWORD("long", Keyword_long);
    TOKENIZER_KEYWORD("mutable", Keyword_mutable);
    TOKENIZER_KEYWORD("namespace", Keyword_namespace);
    TOKENIZER_KEYWORD("new", Keyword_new);
    TOKENIZER_KEYWORD("noexcept", Keyword_noexcept);
    TOKENIZER_KEYWORD("nullptr", Keyword_nullptr);
    TOKENIZER_KEYWORD("operator", Keyword_operator);
    TOKENIZER_KEYWORD("private", Keyword_private);
    TOKENIZER_KEYWORD("protected", Keyword_protected);
    TOKENIZER_KEYWORD("public", Keyword_public);
    TOKENIZER_KEYWORD("register", Keyword_register);
    TOKENIZER_KEYWORD("reinterpret_cast", Keyword_reinterpret_cast);
    TOKENIZER_KEYWORD("requires", Keyword_requires);
    TOKENIZER_KEYWORD("return", Keyword_return);
    TOKENIZER_KEYWORD("short", Keyword_short);
    TOKENIZER_KEYWORD("signed", Keyword_signed);
    TOKENIZER_KEYWORD("sizeof", Keyword_sizeof);
    TOKENIZER_KEYWORD("static", Keyword_static);
    TOKENIZER_KEYWORD("static_assert", Keyword_static_assert);
    TOKENIZER_KEYWORD("static_cast", Keyword_static_cast);
    TOKENIZER_KEYWORD("struct", Keyword_struct);
    TOKENIZER_KEYWORD("switch", Keyword_switch);
    TOKENIZER_KEYWORD("template", Keyword_template);
    TOKENIZER_KEYWORD("this", Keyword_this);
    TOKENIZER_KEYWORD("thread_local", Keyword_thread_local);
    TOKENIZER_KEYWORD("throw", Keyword_throw);
    TOKENIZER_KEYWORD("true", Keyword_true);
    TOKENIZER_KEYWORD("try", Keyword_try);
    TOKENIZER_KEYWORD("typedef", Keyword_typedef);
    TOKENIZER_KEYWORD("typeid", Keyword_typeid);
    TOKENIZER_KEYWORD("typename", Keyword_typename);
    TOKENIZER_KEYWORD("union", Keyword_union);
    TOKENIZER_KEYWORD("unsigned", Keyword_unsigned);
    TOKENIZER_KEYWORD("using", Keyword_using);
    TOKENIZER_KEYWORD("virtual", Keyword_virtual);
    TOKENIZER_KEYWORD("void", Keyword_void);
    TOKENIZER_KEYWORD("volatile", Keyword_volatile);
    TOKENIZER_KEYWORD("wchar_t", Keyword_wchar_t);
    TOKENIZER_KEYWORD("while", Keyword_while);
#undef TOKENIZER_KEYWORD
    return table;
}();

#if defined(_MSC_VER)
__forceinline
#else
inline
#endif
TokenKind ClassifyIdentifier(std::string_view identifier)
{
    if (identifier.size() < 2 || identifier.size() > 16 || identifier.front() < 'a' || identifier.front() > 'z') {
        return TokenKind::Identifier;
    }
    const auto kind = Keywords.Slots[KeywordSlot(IdentifierKey(identifier))];
    const auto& candidate = Keywords.Words[static_cast<size_t>(kind)];
    if (candidate.Length != identifier.size()) {
        return TokenKind::Identifier;
    }
    uint64_t first;
    // Identifier views borrow the padded source, including short final words.
    std::memcpy(&first, identifier.data(), sizeof(first));
    if constexpr (std::endian::native == std::endian::big) {
        first =
            ((first & 0x00000000000000ffULL) << 56) | ((first & 0x000000000000ff00ULL) << 40) |
                ((first & 0x0000000000ff0000ULL) << 24) | ((first & 0x00000000ff000000ULL) << 8) |
                ((first & 0x000000ff00000000ULL) >> 8) | ((first & 0x0000ff0000000000ULL) >> 24) |
                ((first & 0x00ff000000000000ULL) >> 40) | ((first & 0xff00000000000000ULL) >> 56);
    }
    if (identifier.size() <= 8) {
        const auto shift = static_cast<unsigned>((8 - identifier.size()) * 8);
        return ((first ^ candidate.First) << shift) == 0 ? kind : TokenKind::Identifier;
    }
    if (first != candidate.First) {
        return TokenKind::Identifier;
    }
    uint64_t second = 0;
    for (size_t i = 8; i < identifier.size(); ++i) {
        second |= static_cast<uint64_t>(static_cast<unsigned char>(identifier[i])) << ((i - 8) * 8);
    }
    return second == candidate.Second ? kind : TokenKind::Identifier;
}

[[nodiscard]] constexpr bool IsSpace(unsigned char byte) noexcept
{
    return byte == ' ' || byte == '\t' || byte == '\r' || byte == '\f' || byte == '\v';
}

[[nodiscard]] constexpr bool IsOctalDigit(unicode::CodePoint cp) noexcept
{
    return cp >= '0' && cp <= '7';
}

[[nodiscard]] constexpr bool IsDigit(unicode::CodePoint cp) noexcept
{
    return cp >= '0' && cp <= '9';
}

[[nodiscard]] constexpr bool IsHexDigit(unicode::CodePoint cp) noexcept
{
    return IsDigit(cp) || (cp >= 'a' && cp <= 'f') || (cp >= 'A' && cp <= 'F');
}

[[nodiscard]] constexpr bool IsNonDigit(unicode::CodePoint cp) noexcept
{
    return (cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z') || cp == '_';
}

[[nodiscard]] constexpr bool IsAsciiIdentifierStart(unsigned char byte) noexcept
{
    return (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') || byte == '_';
}

[[nodiscard]] constexpr bool IsAlphaNumeric(unsigned char value) noexcept
{
    return IsDigit(value) || (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') || value == '_';
}

constexpr uint64_t LowBits(unsigned count) noexcept
{
    return count == 64 ? ~uint64_t{0} : (uint64_t{1} << count) - 1;
}

[[nodiscard]] unsigned FirstBit(uint64_t bits) noexcept
{
    assert(bits != 0);
#if defined(_MSC_VER) && defined(_M_X64)
    unsigned long index;
    _BitScanForward64(&index, bits);
    return index;
#else
    return static_cast<unsigned>(std::countr_zero(bits));
#endif
}

constexpr uint16_t OperatorKey(unsigned char first, unsigned char second)
{
    return static_cast<uint16_t>(first | (second << 8));
}

constexpr auto SingleOperators = [] {
    std::array<TokenKind, 128> kinds{};
    kinds['{'] = TokenKind::Operator_OpenBrace;
    kinds['}'] = TokenKind::Operator_CloseBrace;
    kinds['['] = TokenKind::Operator_OpenBracket;
    kinds[']'] = TokenKind::Operator_CloseBracket;
    kinds['('] = TokenKind::Operator_OpenParenthesis;
    kinds[')'] = TokenKind::Operator_CloseParenthesis;
    kinds[';'] = TokenKind::Operator_Semicolon;
    kinds[':'] = TokenKind::Operator_Colon;
    kinds['?'] = TokenKind::Operator_Conditional;
    kinds['.'] = TokenKind::Operator_ClassMemberAccess;
    kinds['~'] = TokenKind::Operator_Complement;
    kinds['!'] = TokenKind::Operator_Not;
    kinds['+'] = TokenKind::Operator_Plus;
    kinds['-'] = TokenKind::Operator_Minus;
    kinds['*'] = TokenKind::Operator_Star;
    kinds['/'] = TokenKind::Operator_Divide;
    kinds['%'] = TokenKind::Operator_Remainder;
    kinds['^'] = TokenKind::Operator_Xor;
    kinds['&'] = TokenKind::Operator_Ampersand;
    kinds['|'] = TokenKind::Operator_BitOr;
    kinds['='] = TokenKind::Operator_Assign;
    kinds['<'] = TokenKind::Operator_Less;
    kinds['>'] = TokenKind::Operator_Greater;
    kinds[','] = TokenKind::Operator_Comma;
    return kinds;
}();

#if defined(_MSC_VER)
__forceinline
#else
inline
#endif
TokenKind ClassifyOperator(const unsigned char* text, unsigned length)
{
    if (length == 1) {
        return SingleOperators[*text];
    }
    if (length == 3) {
        switch (*text) {
            case '.':
                return TokenKind::Operator_Ellipsis;
            case '-':
                return TokenKind::Operator_IndirectPointerToMemberDereference;
            case '<':
                return text[1] == '<' ? TokenKind::Operator_LeftShiftAssign : TokenKind::Operator_Spaceship;
            default:
                return TokenKind::Operator_RightShiftAssign;
        }
    }
    switch (OperatorKey(text[0], text[1])) {
        case OperatorKey(':', ':'):
            return TokenKind::Operator_ScopeResolution;
        case OperatorKey(':', '>'):
            return TokenKind::Operator_CloseBracket;
        case OperatorKey('.', '*'):
            return TokenKind::Operator_PointerToMemberDereference;
        case OperatorKey('-', '>'):
            return TokenKind::Operator_IndirectClassMemberAccess;
        case OperatorKey('!', '='):
            return TokenKind::Operator_NotEquals;
        case OperatorKey('+', '='):
            return TokenKind::Operator_PlusAssign;
        case OperatorKey('+', '+'):
            return TokenKind::Operator_Increment;
        case OperatorKey('-', '='):
            return TokenKind::Operator_MinusAssign;
        case OperatorKey('-', '-'):
            return TokenKind::Operator_Decrement;
        case OperatorKey('*', '='):
            return TokenKind::Operator_MultiplyAssign;
        case OperatorKey('/', '='):
            return TokenKind::Operator_DivideAssign;
        case OperatorKey('%', '='):
            return TokenKind::Operator_RemainderAssign;
        case OperatorKey('^', '='):
            return TokenKind::Operator_XorAssign;
        case OperatorKey('&', '='):
            return TokenKind::Operator_BitAndAssign;
        case OperatorKey('&', '&'):
            return TokenKind::Operator_And;
        case OperatorKey('|', '='):
            return TokenKind::Operator_BitOrAssign;
        case OperatorKey('|', '|'):
            return TokenKind::Operator_Or;
        case OperatorKey('=', '='):
            return TokenKind::Operator_Equals;
        case OperatorKey('<', ':'):
            return TokenKind::Operator_OpenBracket;
        case OperatorKey('<', '%'):
            return TokenKind::Operator_OpenBrace;
        case OperatorKey('<', '='):
            return TokenKind::Operator_LessEquals;
        case OperatorKey('<', '<'):
            return TokenKind::Operator_LeftShift;
        case OperatorKey('>', '='):
            return TokenKind::Operator_GreaterEquals;
        case OperatorKey('>', '>'):
            return TokenKind::Operator_RightShift;
        default:
            return TokenKind::Invalid;
    }
}

[[nodiscard]] bool IsIdStart(unicode::CodePoint cp)
{
    if (IsNonDigit(cp)) {
        return true;
    }
    if (cp < AsciiLimit) {
        return false;
    }
    return unicode::IsXIDStart(cp);
}

[[nodiscard]] bool IsIdContinuation(unicode::CodePoint cp)
{
    if (IsNonDigit(cp) || IsDigit(cp)) {
        return true;
    }
    if (cp < AsciiLimit) {
        return false;
    }
    return unicode::IsXIDContinue(cp);
}

constexpr std::array<std::string_view, 4> EncodingPrefixes{"u8", "u", "U", "L"};

constexpr std::array<std::string_view, 5> RawEncodingPrefixes{"R", "u8R", "uR", "UR", "LR"};

constexpr std::array<std::string_view, 8> FloatingPointSuffixes{"", "f", "l", "f16", "f32", "f64", "f128", "bf16"};

constexpr std::array<std::string_view, 11> IntegerSuffixes{
    "",
    "u",
    "ul",
    "ull",
    "uz",
    "l",
    "lu",
    "ll",
    "llu",
    "z",
    "zu",
};

[[nodiscard]] constexpr bool EqualsIgnoringAsciiCase(std::string_view lhs, std::string_view rhs) noexcept
{
    if (lhs.size() != rhs.size()) {
        return false;
    }

    auto lower = [] (char value) {
        if (value >= 'A' && value <= 'Z') {
            return static_cast<char>(value + ('a' - 'A'));
        }
        return value;
    };
    for (SourceOffset i = 0; i < lhs.size(); ++i) {
        if (lower(lhs[i]) != lower(rhs[i])) {
            return false;
        }
    }
    return true;
}

} // namespace

namespace detail {

template <typename Owner>
inline unicode::CodePoint TokenizerCursor<Owner>::CurrentCodePoint()
{
    auto byte = PeekByte();
    if (byte < AsciiLimit) [[likely]] {
        return byte;
    }
    return CurrentUnicodeCodePoint();
}

template <typename Owner>
inline unicode::CodePoint TokenizerCursor<Owner>::CurrentUnicodeCodePoint()
{
    LoadCurrentUnicodeCodePoint();
    return CachedUnicodeValue_;
}

template <typename Owner>
inline void TokenizerCursor<Owner>::LoadCurrentUnicodeCodePoint()
{
    if (CachedUnicodeOffset_ != CurrentCodePointOffset_) {
        DecodeCurrentUnicodeCodePoint();
    }
}

template <typename Owner>
inline bool TokenizerCursor<Owner>::AtEnd() const noexcept
{
    return CurrentCodePointOffset_ == GetOwner().Input_.Size;
}

template <typename Owner>
inline unsigned char TokenizerCursor<Owner>::ByteAt(SourceOffset offset) const noexcept
{
    return ByteData()[offset];
}

template <typename Owner>
inline unsigned char TokenizerCursor<Owner>::PeekByte(SourceOffset lookahead) const noexcept
{
    return ByteAt(CurrentCodePointOffset_ + lookahead);
}

template <typename Owner>
inline const unsigned char* TokenizerCursor<Owner>::CurrentByteAddress() const noexcept
{
    return ByteData() + CurrentCodePointOffset_;
}

template <typename Owner>
inline void TokenizerCursor<Owner>::ConsumeUnicodeCodePoint()
{
    auto& owner = GetOwner();
    owner.CurrentTokenSize_ += CachedUnicodeSize_;
    CurrentCodePointOffset_ += CachedUnicodeSize_;
    ++CurrentCodePointPosition_.Column;
    owner.IsNewLine_ = false;
}

template <typename Owner>
inline void TokenizerCursor<Owner>::ConsumeCodePoint(unicode::CodePoint value, NewlineHandling newlineHandling)
{
    if (value < AsciiLimit) [[likely]] {
        ConsumeAsciiByte(static_cast<unsigned char>(value), newlineHandling);
    } else {
        ConsumeUnicodeCodePoint();
    }
}

template <typename Owner>
inline void TokenizerCursor<Owner>::ConsumeCodePoint(NewlineHandling newlineHandling)
{
    ConsumeCodePoint(CurrentCodePoint(), newlineHandling);
}

template <typename Owner>
inline const unsigned char* TokenizerCursor<Owner>::ByteData() const noexcept
{
    return reinterpret_cast<const unsigned char*>(GetOwner().Input_.Data);
}

template <typename Owner>
inline Owner& TokenizerCursor<Owner>::GetOwner() noexcept
{
    return static_cast<Owner&>(*this);
}

template <typename Owner>
inline const Owner& TokenizerCursor<Owner>::GetOwner() const noexcept
{
    return static_cast<const Owner&>(*this);
}

template <typename Owner>
void TokenizerCursor<Owner>::ConsumeAsciiByte(unsigned char byte, NewlineHandling newlineHandling)
{
    auto& owner = GetOwner();
    ++owner.CurrentTokenSize_;
    ++CurrentCodePointOffset_;
    if (byte == '\n') {
        ++CurrentCodePointPosition_.Line;
        CurrentCodePointPosition_.Column = 1;
        if (newlineHandling == NewlineHandling::Track) {
            owner.IsNewLine_ = true;
        }
    } else {
        ++CurrentCodePointPosition_.Column;
        if (!IsSpace(byte)) {
            owner.IsNewLine_ = false;
        }
    }
}

} // namespace detail

Tokenizer::Tokenizer(util::PaddedSourceView input, std::function<void(TokenizationError)> errorHandler)
    : Input_(input)
    , ErrorHandler_(std::move(errorHandler))
{
    if (Input_.Size > util::MaxSourceSize) {
        static constexpr auto EmptySource = util::PadSource("");
        Input_ = {.Data = EmptySource.data(), .Size = 0};
        OnError(TokenizationError{
            .Kind = TokenizationErrorKind::InputTooLarge,
            .TokenStart = {.Line = 1, .Column = 1},
            .ErrorPosition = {.Line = 1, .Column = 1},
            .TokenEnd = {.Line = 1, .Column = 1},
        });
    }
}

void Tokenizer::LoadBlock(SourceOffset offset) noexcept
{
    const auto blockOffset = offset & ~(detail::AsciiBlock::Size - 1);
    if (blockOffset != BlockOffset_) {
        BlockOffset_ = blockOffset;
        ClassifyBlock_(reinterpret_cast<const unsigned char*>(Input_.Data) + blockOffset, Block_);
    }
}

template <detail::AsciiRun Kind, bool TrackNonSpace>
inline const unsigned char* Tokenizer::ScanAscii(const unsigned char* cursor, bool* nonSpace) noexcept
{
    static_assert(util::SourcePaddingSize >= detail::AsciiBlock::Size);
    const auto* data = reinterpret_cast<const unsigned char*>(Input_.Data);
    auto offset = static_cast<SourceOffset>(cursor - data);
    while (true) {
        LoadBlock(offset);
        const auto within = offset - BlockOffset_;
        const auto continuations = Block_[Kind] >> within;
        const auto count = static_cast<SourceOffset>(
            std::countr_one(continuations));
        if constexpr (TrackNonSpace) {
            const auto consumed = count == detail::AsciiBlock::Size ? ~uint64_t{0} : (uint64_t{1} << count) - 1;
            *nonSpace |= (consumed & ~(Block_[detail::AsciiRun::Whitespace] >> within)) != 0;
        }
        offset += count;
        if (count < detail::AsciiBlock::Size - within) {
            return data + offset;
        }
    }
}

inline const unsigned char* Tokenizer::ScanAsciiQuotedLiteral(const unsigned char* cursor, unsigned char quote) noexcept
{
    return quote == '"' ? ScanAscii<detail::AsciiRun::String>(cursor) : ScanAscii<detail::AsciiRun::Character>(cursor);
}

template <>
void detail::TokenizerCursor<Tokenizer>::DecodeCurrentUnicodeCodePoint()
{
    auto first = unicode::utf8::GetFirstCodePointFromPaddedInput(GetOwner().Input_.Data + CurrentCodePointOffset_);
    if (!first) {
        GetOwner().OnError(TokenizationErrorKind::InvalidUtf8CodePoint);
        ++EncodingErrorCount_;
        CachedUnicodeValue_ = InvalidCodePoint;
        CachedUnicodeSize_ = 1;
    } else {
        CachedUnicodeValue_ = first->Value;
        CachedUnicodeSize_ = static_cast<uint8_t>(first->Size);
    }
    CachedUnicodeOffset_ = CurrentCodePointOffset_;
}

Token Tokenizer::FinishToken(TokenKind kind) const noexcept
{
    return Token{
        .Kind = kind,
        .Position = CurrentTokenPosition_,
        .Text = {.Begin = CurrentTokenOffset_, .End = CurrentTokenOffset_ + CurrentTokenSize_},
    };
}

SourceOffset Tokenizer::FindLineCommentEnd(SourceOffset offset) noexcept
{
    while (true) {
        LoadBlock(offset);
        const auto within = offset - BlockOffset_;
        const auto stops = (Block_.Newlines | Block_.NonAscii | Block_.Nulls) >> within;
        if (stops != 0) {
            const auto end = offset + static_cast<SourceOffset>(std::countr_zero(stops));
            return ByteAt(end) >= AsciiLimit ? std::numeric_limits<SourceOffset>::max() : end;
        }
        offset = BlockOffset_ + detail::AsciiBlock::Size;
    }
}

SourceOffset Tokenizer::FindBlockCommentEnd(SourceOffset offset) noexcept
{
    while (true) {
        LoadBlock(offset);
        const auto within = offset - BlockOffset_;
        const auto closers = Block_.Stars & (Block_.Slashes >> 1);
        const auto stops = (closers | Block_.NonAscii | Block_.Nulls) >> within;
        if (stops != 0) {
            const auto end = offset + static_cast<SourceOffset>(std::countr_zero(stops));
            return (closers & (uint64_t{1} << (end - BlockOffset_))) != 0
                ? end + 2
                : std::numeric_limits<SourceOffset>::max();
        }
        const auto end = BlockOffset_ + detail::AsciiBlock::Size;
        if ((Block_.Stars >> 63) != 0 && ByteAt(end) == '/') {
            return end + 1;
        }
        offset = end;
    }
}

SourceOffset Tokenizer::FindQuotedEnd(SourceOffset offset, bool character) noexcept
{
    while (true) {
        LoadBlock(offset);
        const auto within = offset - BlockOffset_;
        const auto delimiters = character ? Block_.Apostrophes : Block_.Quotes;
        const auto stops =
            (delimiters | Block_.Backslashes | Block_.Newlines | Block_.NonAscii | Block_.Nulls) >> within;
        if (stops != 0) {
            const auto end = offset + static_cast<SourceOffset>(std::countr_zero(stops));
            return (delimiters & (uint64_t{1} << (end - BlockOffset_))) != 0
                ? end + 1
                : std::numeric_limits<SourceOffset>::max();
        }
        offset = BlockOffset_ + detail::AsciiBlock::Size;
    }
}

Token Tokenizer::Refill()
{
    PendingIndex_ = 0;
    PendingCount_ = 0;
    while (true) {
        const bool more = ReadBlock();
        if (PendingCount_ != 0) {
            return Pending_[PendingIndex_++];
        }
        if (!more) {
            return ReadComplexToken();
        }
    }
}

bool Tokenizer::ReadBlock()
{
    constexpr auto Width = detail::AsciiBlock::Size;
    LoadBlock(CurrentCodePointOffset_);
    const auto base = BlockOffset_;
    const auto limit = std::min<SourceOffset>(base + Width, Input_.Size);
    const auto* data = reinterpret_cast<const unsigned char*>(Input_.Data);
    const auto words = Block_.Identifiers;
    const auto digits = Block_.Digits;
    const auto newlines = Block_.Newlines;
    const auto whitespace = Block_.Whitespace | newlines;
    const auto wordStarts = words & ~(words << 1);
    auto wordEnds = words & ~(words >> 1) & ~LowBits(CurrentCodePointOffset_ - base);
    const auto punctuation = Block_.Punctuation;
    const auto singles = Block_.Singles;
    const auto quotes = Block_.Quotes;
    const auto apostrophes = Block_.Apostrophes;
    const auto backslashes = Block_.Backslashes;
    const auto nonAscii = Block_.NonAscii;
    const auto nulls = Block_.Nulls;
    const auto pairs = Block_.Pairs;
    const auto triples = Block_.Triples;
    auto cursor = CurrentCodePointOffset_;
    auto position = CurrentCodePointPosition_;
    auto isNewLine = IsNewLine_;
    auto remainingLines = newlines & ~LowBits(cursor - base);
    unsigned count = 0;
    auto starts = (wordStarts | (~words & ~whitespace)) & ~LowBits(cursor - base) & LowBits(limit - base);
    // A continuation arriving from another block starts at the current cursor.
    if (cursor < limit && (words & (uint64_t{1} << (cursor - base))) != 0) {
        starts |= uint64_t{1} << (cursor - base);
    }

    auto advance = [&] (SourceOffset end) {
        if (end <= limit) {
            const auto begin = cursor - base;
            const auto stop = end - base;
            const auto range = LowBits(stop) & ~LowBits(begin);
            const auto lines = newlines & range;
            if (lines != 0) {
                position.Line += static_cast<uint32_t>(std::popcount(lines));
                const auto last = 63u - static_cast<unsigned>(std::countl_zero(lines));
                position.Column = stop - last;
                isNewLine = (range & ~LowBits(last + 1) & ~whitespace) == 0;
            } else {
                position.Column += stop - begin;
                if ((range & ~whitespace) != 0) {
                    isNewLine = false;
                }
            }
            cursor = end;
            return;
        }
        while (cursor < end) {
            LoadBlock(cursor);
            const auto begin = cursor - BlockOffset_;
            const auto stop = std::min<SourceOffset>(end - BlockOffset_, Width);
            const auto range = LowBits(stop) & ~LowBits(begin);
            const auto lines = Block_.Newlines & range;
            if (lines != 0) {
                position.Line += static_cast<uint32_t>(std::popcount(lines));
                const auto last = 63u - static_cast<unsigned>(std::countl_zero(lines));
                position.Column = stop - last;
                isNewLine = (range & ~LowBits(last + 1) & ~(Block_[AsciiRun::Whitespace] | Block_.Newlines)) == 0;
            } else {
                position.Column += stop - begin;
                if ((range & ~Block_[AsciiRun::Whitespace]) != 0) {
                    isNewLine = false;
                }
            }
            cursor = BlockOffset_ + stop;
        }
    };

    // At most one token starts at each byte in this block.
    while (starts != 0) {
        const auto index = FirstBit(starts);
        const auto start = base + index;
        const auto bit = uint64_t{1} << index;
        const auto skippedLines = remainingLines & LowBits(index);
        if (skippedLines != 0) {
            remainingLines &= ~skippedLines;
            position.Line += static_cast<uint32_t>(std::popcount(skippedLines));
            position.Column = index - (63u - static_cast<unsigned>(std::countl_zero(skippedLines)));
            isNewLine = true;
        } else {
            position.Column += start - cursor;
        }
        cursor = start;
        auto end = start;
        auto consumedStarts = starts & (starts - 1);
        TokenKind kind;
        if ((words & bit) != 0) {
            const auto ends = wordEnds;
            if (ends != 0) {
                end = base + FirstBit(ends) + 1;
                wordEnds &= wordEnds - 1;
            }
            if (end == 0 || end == start || (end == base + Width && IsAlphaNumeric(data[end]))) {
                end = static_cast<SourceOffset>(ScanAscii<AsciiRun::Identifier>(data + end) - data);
            }
            const auto next = data[end];
            if (next >= AsciiLimit || next == '"' || next == '\'') {
                break;
            }
            if ((digits & bit) != 0) {
                // Compound numeric spellings retain their validating path.
                const auto length = end - start;
                if (end > limit || next == '.' || (data[start] == '0' && length > 1) ||
                    ((digits >> index) & LowBits(length)) != LowBits(length))
                {
                    break;
                }
                kind = data[start] == '0' ? TokenKind::Literal_OctalInteger : TokenKind::Literal_DecimalInteger;
            } else {
                kind = ClassifyIdentifier(Input_.substr(start, end - start));
            }
        } else if ((singles & bit) != 0) {
            end = start + 1;
            kind = SingleOperators[data[start]];
        } else if ((punctuation & bit) != 0) {
            const auto byte = data[start];
            const auto next = data[start + 1];
            if (byte == '/' && (next == '/' || next == '*')) {
                end = next == '/' ? FindLineCommentEnd(start + 2) : FindBlockCommentEnd(start + 2);
                if (end > Input_.Size) {
                    break;
                }
                kind = TokenKind::Comment;
            } else {
                if ((byte == '%' && next == ':' && isNewLine) ||
                    (byte == '.' && (IsDigit(next) || (next == '.' && data[start + 2] != '.'))))
                {
                    break;
                }
                unsigned length = (triples & bit) != 0 ? 3 : (pairs & bit) != 0 ? 2 : 1;
                if (index >= Width - 2) {
                    // Keep compound symbols intact across block boundaries.
                    detail::AsciiBlock boundary;
                    ClassifyBlock_(data + start, boundary);
                    length = (boundary.Triples & 1) != 0 ? 3 : (boundary.Pairs & 1) != 0 ? 2 : 1;
                }
                if (byte == '<' && next == ':' && data[start + 2] == ':' && data[start + 3] != ':' &&
                    data[start + 3] != '>')
                {
                    length = 1;
                }
                end = start + length;
                if (data[end] >= AsciiLimit) {
                    break;
                }
                kind = ClassifyOperator(data + start, length);
            }
        } else if (((quotes | apostrophes) & bit) != 0) {
            const bool character = (apostrophes & bit) != 0;
            const auto delimiters = character ? apostrophes : quotes;
            const auto stops = (delimiters | backslashes | newlines | nonAscii | nulls) & ~LowBits(index + 1);
            if (stops == 0) {
                end = FindQuotedEnd(start + 1, character);
            } else {
                const auto close = FirstBit(stops);
                end = (delimiters & (uint64_t{1} << close)) != 0
                    ? base + close + 1
                    : std::numeric_limits<SourceOffset>::max();
            }
            if (end > Input_.Size || (character && end == start + 2) || IsAsciiIdentifierStart(data[end]) ||
                data[end] >= AsciiLimit)
            {
                break;
            }
            kind = character ? TokenKind::Literal_Character : TokenKind::Literal_String;
        } else {
            break;
        }

        auto& token = Pending_[count++];
        token.Kind = kind;
        token.Position = position;
        token.Text.Begin = start;
        token.Text.End = end;
        if (kind == TokenKind::Comment) {
            advance(end);
        } else {
            position.Column += end - start;
            isNewLine = false;
        }
        cursor = end;
        if (end >= limit) {
            break;
        }
        if (((words | singles) & bit) == 0) {
            const auto remaining = ~LowBits(end - base);
            consumedStarts &= remaining;
            wordEnds &= remaining;
            remainingLines &= remaining;
        }
        starts = consumedStarts;
    }
    PendingCount_ = static_cast<uint8_t>(count);
    if (count != 0) {
        const auto& last = Pending_[count - 1];
        CurrentTokenOffset_ = last.Text.Begin;
        CurrentTokenPosition_ = last.Position;
        CurrentTokenSize_ = last.Text.Size();
    }
    if (starts == 0 && cursor < limit) {
        advance(limit);
    }
    CurrentCodePointOffset_ = cursor;
    CurrentCodePointPosition_ = position;
    IsNewLine_ = isNewLine;
    return cursor == limit && cursor < Input_.Size;
}

Token Tokenizer::ReadComplexToken()
{
    auto byte = PeekByte();
    if (byte <= 0x20 || byte >= AsciiLimit || byte == '\\') {
        SkipWhitespace();
        byte = PeekByte();
    }

    CurrentTokenPosition_ = CurrentCodePointPosition_;
    CurrentTokenOffset_ = CurrentCodePointOffset_;
    CurrentTokenSize_ = 0;

    if (byte == '\0') [[unlikely]] {
        if (IsEndOfFile_ || CurrentCodePointOffset_ >= Input_.Size) {
            IsEndOfFile_ = true;
            return FinishToken(TokenKind::EndOfFile);
        }
        ConsumeAsciiByte(byte);
        OnError({
            .Kind = TokenizationErrorKind::UnexpectedNullCharacter,
            .TokenStart = CurrentTokenPosition_,
            .ErrorPosition = CurrentTokenPosition_,
            .TokenEnd = CurrentCodePointPosition_,
        });
        return FinishToken(TokenKind::Invalid);
    }
    if (IsAsciiIdentifierStart(byte)) {
        return ParseIdentifier();
    }
    if (IsDigit(byte)) {
        return ParseNumber();
    }
    TokenKind kind;
    switch (byte) {
        case '\'':
            return ParseCharacterLiteral();
        case '"':
            return ParseStringLiteral();
        case '{':
            kind = TokenKind::Operator_OpenBrace;
            break;
        case '}':
            kind = TokenKind::Operator_CloseBrace;
            break;
        case '[':
            kind = TokenKind::Operator_OpenBracket;
            break;
        case ']':
            kind = TokenKind::Operator_CloseBracket;
            break;
        case '(':
            kind = TokenKind::Operator_OpenParenthesis;
            break;
        case ')':
            kind = TokenKind::Operator_CloseParenthesis;
            break;
        case ';':
            kind = TokenKind::Operator_Semicolon;
            break;
        case '?':
            kind = TokenKind::Operator_Conditional;
            break;
        case '~':
            kind = TokenKind::Operator_Complement;
            break;
        case ',':
            kind = TokenKind::Operator_Comma;
            break;
        default: {
            if (byte < AsciiLimit) {
                return ParseAsciiOperator(byte);
            }
            auto cur = CurrentUnicodeCodePoint();
            if (unicode::IsXIDStart(cur)) {
                return ParseIdentifier();
            }
            ConsumeUnicodeCodePoint();
            if (cur != InvalidCodePoint) {
                OnError(TokenizationErrorKind::UnexpectedToken);
            }
            return FinishToken(TokenKind::Invalid);
        }
    }
    ++CurrentCodePointOffset_;
    ++CurrentCodePointPosition_.Column;
    CurrentTokenSize_ = 1;
    IsNewLine_ = false;
    return FinishToken(kind);
}

Token Tokenizer::ParseAsciiOperator(unsigned char byte)
{
    unicode::CodePoint cur = byte;
    switch (byte) {
        case '#': {
            if (!IsNewLine_) {
                ConsumeAsciiByte(byte);
                OnError(TokenizationErrorKind::UnexpectedToken);
                return FinishToken(TokenKind::Invalid);
            }
            return ParsePreprocessingDirective();
        }
        case ':': {
            ConsumeAsciiByte(byte);
            cur = CurrentCodePoint();
            switch (cur) {
                case ':':
                    ConsumeCodePoint();
                    return FinishToken(TokenKind::Operator_ScopeResolution);
                case '>':
                    ConsumeCodePoint();
                    return FinishToken(TokenKind::Operator_CloseBracket);
                default:
                    return FinishToken(TokenKind::Operator_Colon);
            }
        }
        case '.': {
            if (IsDigit(PeekByte(1))) [[unlikely]] {
                return ParseNumber();
            }
            ConsumeAsciiByte(byte);
            cur = CurrentCodePoint();
            switch (cur) {
                case '.': {
                    ConsumeCodePoint();
                    cur = CurrentCodePoint();
                    if (cur == '.') {
                        ConsumeCodePoint();
                        return FinishToken(TokenKind::Operator_Ellipsis);
                    }
                    OnError(TokenizationErrorKind::UnfinishedEllipsis);
                    return FinishToken(TokenKind::Invalid);
                }
                case '*':
                    ConsumeCodePoint();
                    return FinishToken(TokenKind::Operator_PointerToMemberDereference);
                default:
                    return FinishToken(TokenKind::Operator_ClassMemberAccess);
            }
        }
        case '-': {
            ConsumeAsciiByte(byte);
            cur = CurrentCodePoint();
            switch (cur) {
                case '>':
                    ConsumeCodePoint();
                    cur = CurrentCodePoint();
                    if (cur == '*') {
                        ConsumeCodePoint();
                        using Kind = TokenKind;
                        auto kind = Kind::Operator_IndirectPointerToMemberDereference;
                        return FinishToken(kind);
                    }
                    return FinishToken(TokenKind::Operator_IndirectClassMemberAccess);
                case '=':
                    ConsumeCodePoint();
                    return FinishToken(TokenKind::Operator_MinusAssign);
                case '-':
                    ConsumeCodePoint();
                    return FinishToken(TokenKind::Operator_Decrement);
                default:
                    return FinishToken(TokenKind::Operator_Minus);
            }
        }
        case '!': {
            ConsumeAsciiByte(byte);
            cur = CurrentCodePoint();
            if (cur == '=') {
                ConsumeCodePoint();
                return FinishToken(TokenKind::Operator_NotEquals);
            }
            return FinishToken(TokenKind::Operator_Not);
        }
        case '+': {
            ConsumeAsciiByte(byte);
            cur = CurrentCodePoint();
            if (cur == '=') {
                ConsumeCodePoint();
                return FinishToken(TokenKind::Operator_PlusAssign);
            }
            if (cur == '+') {
                ConsumeCodePoint();
                return FinishToken(TokenKind::Operator_Increment);
            }
            return FinishToken(TokenKind::Operator_Plus);
        }
        case '*': {
            ConsumeAsciiByte(byte);
            cur = CurrentCodePoint();
            if (cur == '=') {
                ConsumeCodePoint();
                return FinishToken(TokenKind::Operator_MultiplyAssign);
            }
            return FinishToken(TokenKind::Operator_Star);
        }
        case '/': {
            ConsumeAsciiByte(byte);
            cur = CurrentCodePoint();
            switch (cur) {
                case '=':
                    ConsumeCodePoint();
                    return FinishToken(TokenKind::Operator_DivideAssign);
                case '/':
                    return ParseInlineComment();
                case '*':
                    return ParseMultilineComment();
                default:
                    return FinishToken(TokenKind::Operator_Divide);
            }
        }
        case '%': {
            const bool startsLine = IsNewLine_;
            ConsumeAsciiByte(byte);
            cur = CurrentCodePoint();
            if (cur == '=') {
                ConsumeCodePoint();
                return FinishToken(TokenKind::Operator_RemainderAssign);
            }
            if (startsLine) {
                if (cur == ':') {
                    ConsumeCodePoint();
                    return ParsePreprocessingDirective();
                }
            }
            return FinishToken(TokenKind::Operator_Remainder);
        }
        case '^': {
            ConsumeAsciiByte(byte);
            cur = CurrentCodePoint();
            if (cur == '=') {
                ConsumeCodePoint();
                return FinishToken(TokenKind::Operator_XorAssign);
            }
            return FinishToken(TokenKind::Operator_Xor);
        }
        case '&': {
            ConsumeAsciiByte(byte);
            cur = CurrentCodePoint();
            switch (cur) {
                case '=':
                    ConsumeCodePoint();
                    return FinishToken(TokenKind::Operator_BitAndAssign);
                case '&':
                    ConsumeCodePoint();
                    return FinishToken(TokenKind::Operator_And);
                default:
                    return FinishToken(TokenKind::Operator_Ampersand);
            }
        }
        case '|': {
            ConsumeAsciiByte(byte);
            cur = CurrentCodePoint();
            switch (cur) {
                case '=':
                    ConsumeCodePoint();
                    return FinishToken(TokenKind::Operator_BitOrAssign);
                case '|':
                    ConsumeCodePoint();
                    return FinishToken(TokenKind::Operator_Or);
                default:
                    return FinishToken(TokenKind::Operator_BitOr);
            }
        }
        case '=': {
            ConsumeAsciiByte(byte);
            cur = CurrentCodePoint();
            if (cur == '=') {
                ConsumeCodePoint();
                return FinishToken(TokenKind::Operator_Equals);
            }
            return FinishToken(TokenKind::Operator_Assign);
        }
        case '<': {
            ConsumeAsciiByte(byte);
            cur = CurrentCodePoint();
            switch (cur) {
                case ':':
                    // [lex.pptoken] has a special maximal-munch exception for
                    // <:: when the following character is neither ':' nor '>'.
                    // In that case this is '<' followed by '::', not the '<:'
                    // digraph followed by ':'.
                    if (PeekByte() == ':' && PeekByte(1) == ':' && PeekByte(2) != ':' && PeekByte(2) != '>') {
                        return FinishToken(TokenKind::Operator_Less);
                    }
                    ConsumeCodePoint();
                    return FinishToken(TokenKind::Operator_OpenBracket);
                case '%':
                    ConsumeCodePoint();
                    return FinishToken(TokenKind::Operator_OpenBrace);
                case '=': {
                    ConsumeCodePoint();
                    cur = CurrentCodePoint();
                    if (cur == '>') {
                        ConsumeCodePoint();
                        return FinishToken(TokenKind::Operator_Spaceship);
                    }
                    return FinishToken(TokenKind::Operator_LessEquals);
                }
                case '<': {
                    ConsumeCodePoint();
                    cur = CurrentCodePoint();
                    if (cur == '=') {
                        ConsumeCodePoint();
                        return FinishToken(TokenKind::Operator_LeftShiftAssign);
                    }
                    return FinishToken(TokenKind::Operator_LeftShift);
                }
                default:
                    return FinishToken(TokenKind::Operator_Less);
            }
            break;
        }
        case '>': {
            ConsumeAsciiByte(byte);
            cur = CurrentCodePoint();
            switch (cur) {
                case '=':
                    ConsumeCodePoint();
                    return FinishToken(TokenKind::Operator_GreaterEquals);
                case '>': {
                    ConsumeCodePoint();
                    cur = CurrentCodePoint();
                    if (cur == '=') {
                        ConsumeCodePoint();
                        return FinishToken(TokenKind::Operator_RightShiftAssign);
                    }
                    return FinishToken(TokenKind::Operator_RightShift);
                }
                default:
                    return FinishToken(TokenKind::Operator_Greater);
            }
        }
        default:
            ConsumeAsciiByte(byte);
            OnError(TokenizationErrorKind::UnexpectedToken);
            return FinishToken(TokenKind::Invalid);
    }
}

bool Tokenizer::IsFileStart() const noexcept
{
    return CurrentCodePointOffset_ == 0;
}

bool Tokenizer::TrySkipNewline()
{
    const auto* begin = CurrentByteAddress();
    const auto* cursor = ScanAscii<AsciiRun::Whitespace>(begin);
    auto lastByte = *cursor;
    auto size = static_cast<SourceOffset>(cursor - begin);
    CurrentCodePointOffset_ += size;
    CurrentCodePointPosition_.Column += size;
    CurrentTokenSize_ += size;
    if (lastByte == '\n') {
        ConsumeAsciiByte(lastByte, NewlineHandling::Ignore);
        return true;
    }
    if (lastByte >= AsciiLimit) {
        LoadCurrentUnicodeCodePoint();
    }
    return false;
}

void Tokenizer::SkipWhitespace()
{
    bool skippedNewline;
    do {
        skippedNewline = false;
        unsigned char lastByte;
        while (true) {
            const auto* begin = CurrentByteAddress();
            const auto* cursor = ScanAscii<AsciiRun::Whitespace>(begin);
            lastByte = *cursor;
            auto size = static_cast<SourceOffset>(cursor - begin);
            CurrentCodePointOffset_ += size;
            CurrentCodePointPosition_.Column += size;
            if (lastByte != '\n') {
                break;
            }
            ++CurrentCodePointOffset_;
            ++CurrentCodePointPosition_.Line;
            CurrentCodePointPosition_.Column = 1;
            IsNewLine_ = true;
        }
        if (lastByte >= AsciiLimit) {
            auto cur = CurrentUnicodeCodePoint();
            if (IsFileStart() && cur == unicode::utf8::ByteOrderMark) {
                ConsumeUnicodeCodePoint();
                skippedNewline = true;
                continue;
            }
        }
        if (lastByte == '\\') {
            ConsumeAsciiByte(lastByte);
            skippedNewline = TrySkipNewline();
        }
    } while (skippedNewline);
}

std::string_view Tokenizer::CurrentTokenText() const noexcept
{
    return Input_.substr(CurrentTokenOffset_, CurrentTokenSize_);
}

Token Tokenizer::ParseIdentifier()
{
    // Get has already checked the first character with IsIdStart.
    unsigned char lastByte;
    while (true) {
        const auto* begin = CurrentByteAddress();
        const auto* cursor = ScanAscii<AsciiRun::Identifier>(begin);
        lastByte = *cursor;
        auto size = static_cast<SourceOffset>(cursor - begin);
        CurrentCodePointOffset_ += size;
        CurrentCodePointPosition_.Column += size;
        CurrentTokenSize_ += size;
        if (size != 0) {
            IsNewLine_ = false;
        }
        if (lastByte < AsciiLimit || !IsIdContinuation(CurrentUnicodeCodePoint())) {
            break;
        }
        ConsumeUnicodeCodePoint();
    }
    auto cur = lastByte < AsciiLimit ? unicode::CodePoint(lastByte) : CurrentUnicodeCodePoint();

    if (cur == '\'') {
        if (std::ranges::find(EncodingPrefixes, CurrentTokenText()) != EncodingPrefixes.end()) {
            return ParseCharacterLiteral();
        }
    } else if (cur == '"') {
        if (std::ranges::find(EncodingPrefixes, CurrentTokenText()) != EncodingPrefixes.end()) {
            return ParseStringLiteral();
        }
        if (std::ranges::find(RawEncodingPrefixes, CurrentTokenText()) != RawEncodingPrefixes.end()) {
            return ParseRawStringLiteral();
        }
    }

    return FinishToken(ClassifyIdentifier(CurrentTokenText()));
}

bool Tokenizer::ConsumeEscapeSequence()
{
    ConsumeCodePoint();
    auto cur = CurrentCodePoint();

    switch (cur) {
        case '\'':
        case '"':
        case '?':
        case '\\':
        case 'a':
        case 'b':
        case 'f':
        case 'n':
        case 'r':
        case 't':
        case 'v':
            ConsumeCodePoint();
            return true;
        case '\n':
            ConsumeCodePoint(NewlineHandling::Ignore);
            return true;
        case '\r':
            if (PeekByte() == '\r' && PeekByte(1) == '\n') {
                ConsumeCodePoint();
                ConsumeCodePoint(NewlineHandling::Ignore);
                return true;
            }
            break;
        default:
            break;
    }

    if (IsOctalDigit(cur)) {
        for (int count = 0; count < 3 && IsOctalDigit(CurrentCodePoint()); ++count) {
            ConsumeCodePoint();
        }
        return true;
    }

    auto consumeBracedDigits = [&] (auto isDigit) {
        ConsumeCodePoint();
        bool hasDigits = false;
        while (isDigit(CurrentCodePoint())) {
            ConsumeCodePoint();
            hasDigits = true;
        }
        if (CurrentCodePoint() != '}') {
            return false;
        }
        ConsumeCodePoint();
        return hasDigits;
    };

    if (cur == 'x') {
        ConsumeCodePoint();
        if (CurrentCodePoint() == '{') {
            if (consumeBracedDigits(IsHexDigit)) {
                return true;
            }
        } else if (IsHexDigit(CurrentCodePoint())) {
            while (IsHexDigit(CurrentCodePoint())) {
                ConsumeCodePoint();
            }
            return true;
        }
        OnError(TokenizationErrorKind::InvalidEscapeSequence);
        return false;
    }

    if (cur == 'o') {
        ConsumeCodePoint();
        if (CurrentCodePoint() == '{' && consumeBracedDigits(IsOctalDigit)) {
            return true;
        }
        OnError(TokenizationErrorKind::InvalidEscapeSequence);
        return false;
    }

    if (cur == 'u' || cur == 'U') {
        auto marker = cur;
        ConsumeCodePoint();
        uint32_t value = 0;
        bool valid = true;
        if (marker == 'u' && CurrentCodePoint() == '{') {
            ConsumeCodePoint();
            int digits = 0;
            while (IsHexDigit(CurrentCodePoint())) {
                auto digit = CurrentCodePoint();
                value = value > 0x10ffff / 16
                    ? 0x110000
                    : value * 16 +
                        (IsDigit(digit)
                            ? static_cast<uint32_t>(digit) - '0'
                            : (static_cast<uint32_t>(digit) | 0x20) - 'a' + 10);
                ConsumeCodePoint();
                ++digits;
            }
            valid = digits != 0 && CurrentCodePoint() == '}';
            if (CurrentCodePoint() == '}') {
                ConsumeCodePoint();
            }
        } else {
            int expectedDigits = marker == 'u' ? 4 : 8;
            for (int i = 0; i < expectedDigits; ++i) {
                auto digit = CurrentCodePoint();
                if (!IsHexDigit(digit)) {
                    valid = false;
                    break;
                }
                value = value * 16 +
                    (IsDigit(digit)
                        ? static_cast<uint32_t>(digit) - '0'
                        : (static_cast<uint32_t>(digit) | 0x20) - 'a' + 10);
                ConsumeCodePoint();
            }
        }
        if (!valid || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) {
            OnError(TokenizationErrorKind::InvalidUniversalCharacter);
            return false;
        }
        return true;
    }

    if (cur == 'N') {
        ConsumeCodePoint();
        if (CurrentCodePoint() == '{') {
            ConsumeCodePoint();
            bool hasName = false;
            while (CurrentCodePoint() != '}' && CurrentCodePoint() != '\n' && CurrentCodePoint() != '\0') {
                ConsumeCodePoint();
                hasName = true;
            }
            if (hasName && CurrentCodePoint() == '}') {
                ConsumeCodePoint();
                return true;
            }
        }
        OnError(TokenizationErrorKind::InvalidUniversalCharacter);
        return false;
    }

    if (cur != '\0' && cur != '\n') {
        ConsumeCodePoint();
    }
    OnError(TokenizationErrorKind::InvalidEscapeSequence);
    return false;
}

void Tokenizer::ConsumeLiteralSuffix()
{
    auto cur = CurrentCodePoint();
    if (!IsIdStart(cur)) {
        return;
    }
    // A directly attached identifier is part of the literal token, not a
    // separate expression. Unlike numeric suffixes, digit separators are
    // not allowed here.
    while (true) {
        const auto* begin = CurrentByteAddress();
        const auto* cursor = ScanAscii<AsciiRun::Identifier>(begin);
        auto lastByte = *cursor;
        auto size = static_cast<SourceOffset>(cursor - begin);
        CurrentCodePointOffset_ += size;
        CurrentCodePointPosition_.Column += size;
        CurrentTokenSize_ += size;
        if (size != 0) {
            IsNewLine_ = false;
        }
        if (lastByte < AsciiLimit || !IsIdContinuation(CurrentUnicodeCodePoint())) {
            break;
        }
        ConsumeUnicodeCodePoint();
    }
}

Token Tokenizer::ParseCharacterLiteral()
{
    auto prefix = CurrentTokenText();
    auto encodingErrorCount = EncodingErrorCount_;
    ConsumeAsciiByte('\'');
    bool valid = true;
    uint32_t characterCount = 0;
    unsigned char lastByte;
    while (true) {
        const auto* begin = CurrentByteAddress();
        const auto* cursor = ScanAsciiQuotedLiteral(begin, '\'');
        lastByte = *cursor;
        auto size = static_cast<SourceOffset>(cursor - begin);
        CurrentCodePointOffset_ += size;
        CurrentCodePointPosition_.Column += size;
        CurrentTokenSize_ += size;
        characterCount += size;
        if (size != 0) {
            IsNewLine_ = false;
        }
        if (lastByte >= AsciiLimit) {
            LoadCurrentUnicodeCodePoint();
            ConsumeUnicodeCodePoint();
            ++characterCount;
            continue;
        }
        if (lastByte == '\\') {
            bool lineSplice = PeekByte(1) == '\n' || (PeekByte(1) == '\r' && PeekByte(2) == '\n');
            valid &= ConsumeEscapeSequence();
            characterCount += !lineSplice;
            continue;
        }
        break;
    }
    valid &= encodingErrorCount == EncodingErrorCount_;
    if (lastByte != '\'') {
        OnError(TokenizationErrorKind::UnterminatedCharacterLiteral);
        return FinishToken(TokenKind::Invalid);
    }
    ConsumeAsciiByte(lastByte);
    if (characterCount == 0) {
        OnError(TokenizationErrorKind::EmptyCharacterLiteral);
        valid = false;
    }
    if ((prefix == "u8" || prefix == "u" || prefix == "U") && characterCount > 1) {
        OnError(TokenizationErrorKind::TooManyCharactersInCharacterLiteral);
        valid = false;
    }
    ConsumeLiteralSuffix();
    return FinishToken(valid ? TokenKind::Literal_Character : TokenKind::Invalid);
}

Token Tokenizer::ParseStringLiteral()
{
    auto encodingErrorCount = EncodingErrorCount_;
    ConsumeAsciiByte('"');
    bool valid = true;
    unsigned char lastByte;
    while (true) {
        const auto* begin = CurrentByteAddress();
        const auto* cursor = ScanAsciiQuotedLiteral(begin, '"');
        lastByte = *cursor;
        auto size = static_cast<SourceOffset>(cursor - begin);
        CurrentCodePointOffset_ += size;
        CurrentCodePointPosition_.Column += size;
        CurrentTokenSize_ += size;
        if (size != 0) {
            IsNewLine_ = false;
        }
        if (lastByte >= AsciiLimit) {
            LoadCurrentUnicodeCodePoint();
            ConsumeUnicodeCodePoint();
            continue;
        }
        if (lastByte == '\\') {
            valid &= ConsumeEscapeSequence();
            continue;
        }
        break;
    }
    valid &= encodingErrorCount == EncodingErrorCount_;
    if (lastByte != '"') {
        OnError(TokenizationErrorKind::UnterminatedStringLiteral);
        return FinishToken(TokenKind::Invalid);
    }
    ConsumeAsciiByte(lastByte);
    ConsumeLiteralSuffix();
    return FinishToken(valid ? TokenKind::Literal_String : TokenKind::Invalid);
}

Token Tokenizer::ParseRawStringLiteral()
{
    auto cur = CurrentCodePoint();
    auto next = [&] {
        ConsumeCodePoint(cur);
        cur = CurrentCodePoint();
    };
    next();

    auto delimiterStart = CurrentCodePointOffset_;
    auto encodingErrorCount = EncodingErrorCount_;
    uint32_t delimiterLength = 0;
    bool valid = true;
    bool reportedInvalidDelimiter = false;
    bool reportedLongDelimiter = false;
    while (!AtEnd() && cur != '(' && cur != '\n') {
        auto value = static_cast<uint32_t>(cur);
        if ((value < 0x21 || value > 0x7e || cur == ')' || cur == '\\') && !reportedInvalidDelimiter) {
            OnError(TokenizationErrorKind::InvalidRawStringDelimiter);
            reportedInvalidDelimiter = true;
            valid = false;
        }
        ++delimiterLength;
        if (delimiterLength > 16 && !reportedLongDelimiter) {
            OnError(TokenizationErrorKind::RawStringDelimiterTooLong);
            reportedLongDelimiter = true;
            valid = false;
        }
        next();
    }

    if (AtEnd() || cur != '(') {
        OnError(TokenizationErrorKind::UnterminatedRawStringLiteral);
        return FinishToken(TokenKind::Invalid);
    }

    auto delimiter = Input_.substr(delimiterStart, CurrentCodePointOffset_ - delimiterStart);
    next();

    // A raw string may contain newlines and embedded NUL bytes.
    while (true) {
        const auto* begin = CurrentByteAddress();
        bool nonSpace = false;
        const auto* cursor = ScanAscii<AsciiRun::RawString, true>(begin, &nonSpace);
        auto lastByte = *cursor;
        auto size = static_cast<SourceOffset>(cursor - begin);
        CurrentCodePointOffset_ += size;
        CurrentCodePointPosition_.Column += size;
        CurrentTokenSize_ += size;
        if (nonSpace) {
            IsNewLine_ = false;
        }
        if (AtEnd()) {
            break;
        }
        cur = lastByte < AsciiLimit ? unicode::CodePoint(lastByte) : CurrentUnicodeCodePoint();
        if (cur == ')') {
            auto suffix = Input_.substr(CurrentCodePointOffset_ + 1);
            if (suffix.starts_with(delimiter) && suffix.size() > delimiter.size() && suffix[delimiter.size()] == '"') {
                auto closingDelimiterEnd = CurrentCodePointOffset_ + 2 + static_cast<SourceOffset>(delimiter.size());
                do {
                    ConsumeCodePoint();
                } while (CurrentCodePointOffset_ < closingDelimiterEnd);
                valid &= encodingErrorCount == EncodingErrorCount_;
                ConsumeLiteralSuffix();
                return FinishToken(valid ? TokenKind::Literal_String : TokenKind::Invalid);
            }
        }
        next();
    }

    OnError(TokenizationErrorKind::UnterminatedRawStringLiteral);
    return FinishToken(TokenKind::Invalid);
}

Token Tokenizer::ParseNumber()
{
    const auto* begin = CurrentByteAddress();
    const auto* cursor = begin;
    bool hexadecimal = *cursor == '0' && (cursor[1] == 'x' || cursor[1] == 'X');
    bool binary = *cursor == '0' && (cursor[1] == 'b' || cursor[1] == 'B');
    bool leadingZero = *cursor == '0';
    if (hexadecimal || binary) {
        cursor += 2;
    }
    bool valid = true;
    unsigned char digitBits = 0;
    auto parseDigits = [&] (auto isDigit, bool useCachedBlock = false) {
        if (useCachedBlock) {
            const auto offset = static_cast<SourceOffset>(cursor - reinterpret_cast<const unsigned char*>(Input_.Data));
            const auto within = offset - BlockOffset_;
            if (within < detail::AsciiBlock::Size) {
                const auto digits = static_cast<uint32_t>(Block_.Digits >> within);
                const auto separators = static_cast<uint32_t>(Block_.Apostrophes >> within);
                const auto available = std::min<unsigned>(32, detail::AsciiBlock::Size - within);
                const auto full = ~uint32_t{0} >> (32 - available);
                if ((digits | separators) != full) {
                    const auto length = static_cast<unsigned>(std::countr_one(digits | separators));
                    const auto consumed = (uint32_t{1} << length) - 1;
                    const auto quotes = separators & consumed;
                    valid &= (quotes & ((quotes << 1) | 1)) == 0;
                    valid &= length == 0 || (quotes & (uint32_t{1} << (length - 1))) == 0;
                    cursor += length;
                    return static_cast<uint32_t>((digits & consumed) != 0);
                }
            }
        }
        uint32_t count = 0;
        bool separator = false;
        while (true) {
            auto byte = *cursor;
            if (isDigit(byte)) {
                digitBits |= byte;
                ++count;
                separator = false;
            } else if (byte == '\'') {
                valid &= count != 0 && !separator;
                separator = true;
            } else {
                break;
            }
            ++cursor;
        }
        valid &= !separator;
        return count;
    };
    auto digits = hexadecimal ? parseDigits(IsHexDigit) : parseDigits(IsDigit, !binary && !leadingZero);
    bool floating = !binary &&
        (*cursor == '.' || (hexadecimal ? *cursor == 'p' || *cursor == 'P' : *cursor == 'e' || *cursor == 'E'));
    TokenKind kind;
    if (floating) {
        kind = hexadecimal ? TokenKind::Literal_HexadecimalFloatingPoint : TokenKind::Literal_DecimalFloatingPoint;
        if (*cursor == '.') {
            ++cursor;
            digits += hexadecimal ? parseDigits(IsHexDigit) : parseDigits(IsDigit, true);
        }
        valid &= digits != 0;
        bool exponent = hexadecimal ? *cursor == 'p' || *cursor == 'P' : *cursor == 'e' || *cursor == 'E';
        if (exponent) {
            ++cursor;
            if (*cursor == '+' || *cursor == '-') {
                ++cursor;
            }
            auto exponentDigits = parseDigits(IsDigit, true);
            valid &= exponentDigits != 0;
        } else if (hexadecimal) {
            valid = false;
        }
    } else if (hexadecimal) {
        kind = TokenKind::Literal_HexadecimalInteger;
        valid &= digits != 0;
    } else if (binary) {
        kind = TokenKind::Literal_BinaryInteger;
        valid &= digits != 0 && (digitBits & 0x0e) == 0;
    } else if (leadingZero) {
        kind = TokenKind::Literal_OctalInteger;
        valid &= (digitBits & 0x08) == 0;
    } else {
        kind = TokenKind::Literal_DecimalInteger;
    }
    if (!valid) [[unlikely]] {
        // A later dot/exponent selects the separator diagnostic kind. Keep
        // exact streaming diagnostics without buffering arbitrary errors:
        // only malformed bodies use the validating, code-point-based path.
        return floating ? DiagnoseFloatingPoint() : DiagnoseInteger();
    }

    auto size = static_cast<SourceOffset>(cursor - begin);
    CurrentCodePointOffset_ += size;
    CurrentCodePointPosition_.Column += size;
    CurrentTokenSize_ += size;
    IsNewLine_ = false;
    auto suffixStart = CurrentTokenSize_;
    auto cur = CurrentCodePoint();
    if (IsIdStart(cur)) {
        while (true) {
            const auto* suffixBegin = CurrentByteAddress();
            const auto* suffixCursor = ScanAscii<AsciiRun::Identifier>(suffixBegin);
            auto lastByte = *suffixCursor;
            auto suffixSize = static_cast<SourceOffset>(suffixCursor - suffixBegin);
            CurrentCodePointOffset_ += suffixSize;
            CurrentCodePointPosition_.Column += suffixSize;
            CurrentTokenSize_ += suffixSize;
            if (suffixSize != 0) {
                IsNewLine_ = false;
            }
            if (lastByte >= AsciiLimit) {
                if (IsIdContinuation(CurrentUnicodeCodePoint())) {
                    ConsumeUnicodeCodePoint();
                    continue;
                }
            } else if (!floating && lastByte == '\'' && IsDigit(ByteAt(CurrentCodePointOffset_ - 1)) &&
                IsDigit(PeekByte(1)))
            {
                ConsumeAsciiByte(lastByte);
                continue;
            }
            break;
        }
    }
    auto suffix = CurrentTokenText().substr(suffixStart);
    if (floating) {
        if (!std::ranges::any_of(
            FloatingPointSuffixes,
            [&] (auto candidate) {
                return EqualsIgnoringAsciiCase(suffix, candidate);
            }))
        {
            return FinishToken(TokenKind::Literal_UserDefinedFloatingPoint);
        }
    } else if (!std::ranges::any_of(
        IntegerSuffixes,
        [&] (auto candidate) {
            return EqualsIgnoringAsciiCase(suffix, candidate);
        }))
    {
        return FinishToken(TokenKind::Literal_UserDefinedInteger);
    }
    return FinishToken(kind);
}

Token Tokenizer::DiagnoseFloatingPoint()
{
    auto cur = CurrentCodePoint();
    bool valid = true;
    auto next = [&] {
        ConsumeCodePoint(cur);
        cur = CurrentCodePoint();
    };
    auto parseDigits = [&] (auto isDigit) {
        uint32_t count = 0;
        bool previousSeparator = false;
        bool lastSeparatorReported = false;
        while (isDigit(cur) || cur == '\'') {
            if (cur == '\'') {
                bool invalidSeparator = count == 0 || previousSeparator;
                if (invalidSeparator) {
                    OnError(TokenizationErrorKind::InvalidDigitSeparator);
                    valid = false;
                }
                previousSeparator = true;
                lastSeparatorReported = invalidSeparator;
            } else {
                ++count;
                previousSeparator = false;
                lastSeparatorReported = false;
            }
            next();
        }
        if (previousSeparator && !lastSeparatorReported) {
            OnError(TokenizationErrorKind::InvalidDigitSeparator);
            valid = false;
        }
        return count;
    };
    auto parseExponent = [&] {
        next();
        if (cur == '+' || cur == '-') {
            next();
        }
        if (parseDigits(IsDigit) == 0) {
            OnError(TokenizationErrorKind::NoDigitsInFloatingPointExponent);
            valid = false;
        }
    };

    TokenKind kind;
    if (cur == '0' && (PeekByte(1) == 'x' || PeekByte(1) == 'X')) {
        kind = TokenKind::Literal_HexadecimalFloatingPoint;
        next();
        next();
        auto digitCount = parseDigits(IsHexDigit);
        if (cur == '.') {
            next();
            digitCount += parseDigits(IsHexDigit);
        }
        if (digitCount == 0) {
            OnError(TokenizationErrorKind::NoDigitsInHexadecimalInteger);
            valid = false;
        }
        if (cur == 'p' || cur == 'P') {
            parseExponent();
        } else {
            OnError(TokenizationErrorKind::
                    HexadecimalFloatingPointRequiresExponent);
            valid = false;
        }
    } else {
        kind = TokenKind::Literal_DecimalFloatingPoint;
        if (cur == '.') {
            next();
            parseDigits(IsDigit);
        } else {
            parseDigits(IsDigit);
            if (cur == '.') {
                next();
                parseDigits(IsDigit);
            }
        }
        if (cur == 'e' || cur == 'E') {
            parseExponent();
        }
    }

    auto suffixStart = CurrentTokenSize_;
    if (IsIdStart(cur)) {
        do {
            next();
        } while (IsIdContinuation(cur));
    }
    if (!valid) {
        return FinishToken(TokenKind::Invalid);
    }

    auto suffix = CurrentTokenText().substr(suffixStart);
    if (!std::ranges::any_of(
        FloatingPointSuffixes,
        [&] (auto candidate) {
            return EqualsIgnoringAsciiCase(suffix, candidate);
        }))
    {
        return FinishToken(TokenKind::Literal_UserDefinedFloatingPoint);
    }
    return FinishToken(kind);
}

Token Tokenizer::DiagnoseInteger()
{
    TokenKind kind = TokenKind::Invalid;
    auto cur = CurrentCodePoint();
    ConsumeCodePoint(cur);
    uint32_t parsedDigits = 1;
    auto prev = cur;
    cur = CurrentCodePoint();
    auto next = [&] {
        ConsumeCodePoint(cur);
        prev = cur;
        cur = CurrentCodePoint();
    };
    if (prev == '0') {
        switch (cur) {
            case 'b':
            case 'B': {
                kind = TokenKind::Literal_BinaryInteger;
                next();
                parsedDigits = 0;
                while (cur == '0' || cur == '1' || cur == '\'') {
                    if (cur == '\'') {
                        if (prev == '\'') {
                            OnError(TokenizationErrorKind::
                                    NoDigitsBetweenDigitSeparators);
                            kind = TokenKind::Invalid;
                        } else if (parsedDigits == 0) {
                            OnError(TokenizationErrorKind::
                                    DigitSeparatorAtStartOfBinaryInteger);
                            kind = TokenKind::Invalid;
                        }
                    } else {
                        parsedDigits++;
                    }
                    next();
                }
                if (IsDigit(cur)) {
                    OnError(TokenizationErrorKind::InvalidDigitInBinaryInteger);
                    kind = TokenKind::Invalid;
                    while (IsDigit(cur) || cur == '\'') {
                        next();
                    }
                }
                if (parsedDigits == 0) {
                    OnError(TokenizationErrorKind::NoDigitsInBinaryInteger);
                    kind = TokenKind::Invalid;
                } else if (prev == '\'') {
                    OnError(TokenizationErrorKind::
                            DigitSeparatorAtEndOfBinaryInteger);
                    kind = TokenKind::Invalid;
                }
                break;
            }
            case 'x':
            case 'X': {
                kind = TokenKind::Literal_HexadecimalInteger;
                parsedDigits = 0;
                next();
                while (IsHexDigit(cur) || cur == '\'') {
                    if (cur == '\'') {
                        if (prev == '\'') {
                            OnError(TokenizationErrorKind::
                                    NoDigitsBetweenDigitSeparators);
                            kind = TokenKind::Invalid;
                        } else if (parsedDigits == 0) {
                            OnError(TokenizationErrorKind::
                                    DigitSeparatorAtStartOfHexadecimalInteger);
                            kind = TokenKind::Invalid;
                        }
                    } else {
                        parsedDigits++;
                    }
                    next();
                }
                if (parsedDigits == 0) {
                    OnError(TokenizationErrorKind::NoDigitsInHexadecimalInteger);
                    kind = TokenKind::Invalid;
                } else if (prev == '\'') {
                    OnError(TokenizationErrorKind::
                            DigitSeparatorAtEndOfHexadecimalInteger);
                    kind = TokenKind::Invalid;
                }
                break;
            }
            default: {
                kind = TokenKind::Literal_OctalInteger;
                while (IsOctalDigit(cur) || cur == '\'') {
                    if (cur == '\'') {
                        if (prev == '\'') {
                            OnError(TokenizationErrorKind::
                                    NoDigitsBetweenDigitSeparators);
                            kind = TokenKind::Invalid;
                        }
                    } else {
                        parsedDigits++;
                    }
                    next();
                }
                if (IsDigit(cur)) {
                    OnError(TokenizationErrorKind::InvalidDigitInOctalInteger);
                    kind = TokenKind::Invalid;
                    while (IsDigit(cur) || cur == '\'') {
                        next();
                    }
                }
                if (prev == '\'') {
                    OnError(TokenizationErrorKind::
                            DigitSeparatorAtEndOfOctalInteger);
                    kind = TokenKind::Invalid;
                }
                break;
            }
        }
    } else {
        kind = TokenKind::Literal_DecimalInteger;
        while (IsDigit(cur) || cur == '\'') {
            if (cur == '\'') {
                if (prev == '\'') {
                    OnError(TokenizationErrorKind::NoDigitsBetweenDigitSeparators);
                    kind = TokenKind::Invalid;
                }
            } else {
                parsedDigits++;
            }
            ConsumeCodePoint(cur);
            prev = cur;
            cur = CurrentCodePoint();
        }
        if (prev == '\'') {
            OnError(TokenizationErrorKind::DigitSeparatorAtEndOfDecimalInteger);
            return FinishToken(TokenKind::Invalid);
        }
    }

    auto suffixStart = CurrentTokenSize_;

    if (IsIdStart(cur)) {
        auto isDigitSeparator = [&] {
            return cur == '\'' && CurrentCodePointOffset_ != 0 && IsDigit(ByteAt(CurrentCodePointOffset_ - 1)) &&
                IsDigit(PeekByte(1));
        };
        do {
            next();
        } while (IsIdContinuation(cur) || isDigitSeparator());
    }

    if (kind == TokenKind::Invalid) {
        return FinishToken(kind);
    }

    auto suffix = CurrentTokenText().substr(suffixStart);
    if (!std::ranges::any_of(
        IntegerSuffixes,
        [&] (auto candidate) {
            return EqualsIgnoringAsciiCase(suffix, candidate);
        }))
    {
        return FinishToken(TokenKind::Literal_UserDefinedInteger);
    }
    return FinishToken(kind);
}

Token Tokenizer::ParseInlineComment()
{
    ConsumeAsciiByte('/');
    while (true) {
        const auto* begin = CurrentByteAddress();
        const auto* cursor = ScanAscii<AsciiRun::LineComment>(begin);
        auto lastByte = *cursor;
        auto size = static_cast<SourceOffset>(cursor - begin);
        CurrentCodePointOffset_ += size;
        CurrentCodePointPosition_.Column += size;
        CurrentTokenSize_ += size;
        if (size != 0) {
            IsNewLine_ = false;
        }
        if (lastByte < AsciiLimit) {
            break;
        }
        LoadCurrentUnicodeCodePoint();
        ConsumeUnicodeCodePoint();
    }
    return FinishToken(TokenKind::Comment);
}

Token Tokenizer::ParseMultilineComment()
{
    ConsumeAsciiByte('*');
    while (true) {
        const auto* begin = CurrentByteAddress();
        bool nonSpace = false;
        const auto* cursor = ScanAscii<AsciiRun::BlockComment, true>(begin, &nonSpace);
        auto lastByte = *cursor;
        auto size = static_cast<SourceOffset>(cursor - begin);
        CurrentCodePointOffset_ += size;
        CurrentCodePointPosition_.Column += size;
        CurrentTokenSize_ += size;
        if (nonSpace) {
            IsNewLine_ = false;
        }
        if (lastByte >= AsciiLimit) {
            LoadCurrentUnicodeCodePoint();
            ConsumeUnicodeCodePoint();
        } else if (lastByte == '\n') {
            ConsumeAsciiByte(lastByte);
        } else if (lastByte == '*') {
            ConsumeAsciiByte(lastByte);
            if (PeekByte() == '/') {
                ConsumeAsciiByte('/');
                return FinishToken(TokenKind::Comment);
            }
        } else {
            OnError(TokenizationErrorKind::UnterminatedMultilineComment);
            return FinishToken(TokenKind::Invalid);
        }
    }
}

Token Tokenizer::ParsePreprocessingDirective()
{
    auto cur = CurrentCodePoint();

    bool inBlockComment = false;
    while (cur != '\0') {
        const auto* begin = CurrentByteAddress();
        const auto* cursor = begin;
        bool nonSpace = false;
        unsigned char lastByte;
        if (inBlockComment) {
            cursor = ScanAscii<AsciiRun::BlockComment, true>(begin, &nonSpace);
        } else {
            cursor = ScanAscii<AsciiRun::Directive, true>(begin, &nonSpace);
        }
        lastByte = *cursor;
        auto size = static_cast<SourceOffset>(cursor - begin);
        CurrentCodePointOffset_ += size;
        CurrentCodePointPosition_.Column += size;
        CurrentTokenSize_ += size;
        if (nonSpace) {
            IsNewLine_ = false;
        }
        cur = lastByte < AsciiLimit ? unicode::CodePoint(lastByte) : CurrentUnicodeCodePoint();
        if (cur == '\0') {
            break;
        }
        if (inBlockComment) {
            if (cur == '*') {
                ConsumeCodePoint();
                cur = CurrentCodePoint();
                if (cur == '/') {
                    ConsumeCodePoint();
                    cur = CurrentCodePoint();
                    inBlockComment = false;
                }
            } else {
                ConsumeCodePoint();
                cur = CurrentCodePoint();
            }
            continue;
        }

        if (cur == '\n') {
            break;
        }
        if (cur == '\\') {
            ConsumeCodePoint();
            TrySkipNewline();
            cur = CurrentCodePoint();
            continue;
        }
        if (cur == '/') {
            ConsumeCodePoint();
            cur = CurrentCodePoint();
            if (cur == '*') {
                ConsumeCodePoint();
                cur = CurrentCodePoint();
                inBlockComment = true;
            } else if (cur == '/') {
                while (cur != '\n' && cur != '\0') {
                    const auto* commentBegin = CurrentByteAddress();
                    bool commentNonSpace = false;
                    const auto* commentCursor =
                        ScanAscii<AsciiRun::DirectiveComment, true>(commentBegin, &commentNonSpace);
                    auto lastByte = *commentCursor;
                    auto commentSize = static_cast<SourceOffset>(
                        commentCursor - commentBegin);
                    CurrentCodePointOffset_ += commentSize;
                    CurrentCodePointPosition_.Column += commentSize;
                    CurrentTokenSize_ += commentSize;
                    if (commentNonSpace) {
                        IsNewLine_ = false;
                    }
                    cur = lastByte < AsciiLimit ? unicode::CodePoint(lastByte) : CurrentUnicodeCodePoint();
                    if (cur == '\n' || cur == '\0') {
                        break;
                    }
                    ConsumeCodePoint(cur);
                    if (cur == '\\') {
                        TrySkipNewline();
                    }
                    cur = CurrentCodePoint();
                }
            }
            continue;
        }
        if (cur == 'R' && PeekByte(1) == '"') {
            ConsumeCodePoint(); // R
            ConsumeCodePoint(); // "
            auto delimiterStart = CurrentCodePointOffset_;
            cur = CurrentCodePoint();
            while (cur != '(' && cur != '\n' && cur != '\0') {
                ConsumeCodePoint();
                cur = CurrentCodePoint();
            }
            if (cur != '(') {
                continue;
            }
            auto delimiter = Input_.substr(delimiterStart, CurrentCodePointOffset_ - delimiterStart);
            ConsumeCodePoint();
            cur = CurrentCodePoint();
            while (true) {
                const auto* rawBegin = CurrentByteAddress();
                bool rawNonSpace = false;
                const auto* rawCursor = ScanAscii<AsciiRun::RawString, true>(rawBegin, &rawNonSpace);
                auto lastByte = *rawCursor;
                auto rawSize = static_cast<SourceOffset>(rawCursor - rawBegin);
                CurrentCodePointOffset_ += rawSize;
                CurrentCodePointPosition_.Column += rawSize;
                CurrentTokenSize_ += rawSize;
                if (rawNonSpace) {
                    IsNewLine_ = false;
                }
                cur = lastByte < AsciiLimit ? unicode::CodePoint(lastByte) : CurrentUnicodeCodePoint();
                if (AtEnd()) {
                    break;
                }
                if (cur == ')') {
                    auto suffix = Input_.substr(CurrentCodePointOffset_ + 1);
                    if (suffix.starts_with(delimiter) && suffix.size() > delimiter.size() &&
                        suffix[delimiter.size()] == '"')
                    {
                        ConsumeCodePoint();
                        for (SourceOffset i = 0; i <= delimiter.size(); ++i) {
                            ConsumeCodePoint();
                        }
                        cur = CurrentCodePoint();
                        break;
                    }
                }
                ConsumeCodePoint();
                cur = CurrentCodePoint();
            }
            continue;
        }
        bool digitSeparator = cur == '\'' && CurrentCodePointOffset_ != 0 &&
            IsAlphaNumeric(ByteAt(CurrentCodePointOffset_ - 1)) && IsAlphaNumeric(PeekByte(1));
        if ((cur == '\'' && !digitSeparator) || cur == '"') {
            auto quote = cur;
            ConsumeCodePoint();
            cur = CurrentCodePoint();
            while (cur != '\0' && cur != quote && cur != '\n') {
                const auto* literalBegin = CurrentByteAddress();
                const auto* literalCursor = ScanAsciiQuotedLiteral(literalBegin, static_cast<unsigned char>(quote));
                auto lastByte = *literalCursor;
                auto literalSize = static_cast<SourceOffset>(literalCursor - literalBegin);
                CurrentCodePointOffset_ += literalSize;
                CurrentCodePointPosition_.Column += literalSize;
                CurrentTokenSize_ += literalSize;
                if (literalSize != 0) {
                    IsNewLine_ = false;
                }
                cur = lastByte < AsciiLimit ? unicode::CodePoint(lastByte) : CurrentUnicodeCodePoint();
                if (cur == '\0' || cur == quote || cur == '\n') {
                    break;
                }
                if (cur == '\\') {
                    ConsumeCodePoint();
                    cur = CurrentCodePoint();
                    if (cur == '\0') {
                        break;
                    }
                }
                ConsumeCodePoint();
                cur = CurrentCodePoint();
            }
            if (cur == quote) {
                ConsumeCodePoint();
                cur = CurrentCodePoint();
            }
            continue;
        }

        ConsumeCodePoint();
        cur = CurrentCodePoint();
    }
    return FinishToken(TokenKind::PreprocessingDirective);
}

void Tokenizer::OnError(TokenizationError error)
{
    if (ErrorHandler_) {
        ErrorHandler_(std::move(error));
    } else {
        std::cerr << "Tokenization error at Ln " << error.ErrorPosition.Line << ", Col " << error.ErrorPosition.Column
            << ": " << GetTokenizationErrorDescription(error.Kind) << '\n';
    }
}

void Tokenizer::OnError(TokenizationErrorKind error)
{
    OnError({
        .Kind = error,
        .TokenStart = CurrentTokenPosition_,
        .ErrorPosition = CurrentCodePointPosition_,
        .TokenEnd = CurrentCodePointPosition_,
    });
}

} // namespace tokenizer
