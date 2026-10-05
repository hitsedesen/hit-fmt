#pragma once

#include <array>
#include <cstdint>
#include <format>
#include <string_view>
#include <utility>

#include <util/input.hpp>

namespace tokenizer {

#define TOKEN_KINDS                                                            \
    TOKEN_KIND(Invalid, "invalid")                                             \
    TOKEN_KIND(EndOfFile, "eof")                                               \
    TOKEN_KIND(PreprocessingDirective, "pp")                                   \
    TOKEN_KIND(Comment, "comment")                                             \
    TOKEN_KIND(Identifier, "identifier")                                       \
    TOKEN_KIND(Literal_UserDefinedInteger, "user_integer")                     \
    TOKEN_KIND(Literal_UserDefinedFloatingPoint, "user_float")                 \
    TOKEN_KIND(Literal_DecimalFloatingPoint, "float")                          \
    TOKEN_KIND(Literal_HexadecimalFloatingPoint, "hex_float")                  \
    TOKEN_KIND(Literal_BinaryInteger, "bin_int")                               \
    TOKEN_KIND(Literal_OctalInteger, "oct_int")                                \
    TOKEN_KIND(Literal_DecimalInteger, "int")                                  \
    TOKEN_KIND(Literal_HexadecimalInteger, "hex_int")                          \
    TOKEN_KIND(Literal_Character, "char")                                      \
    TOKEN_KIND(Literal_String, "str")                                          \
    TOKEN_KIND(Operator_OpenBrace, "op{")                                      \
    TOKEN_KIND(Operator_CloseBrace, "op}")                                     \
    TOKEN_KIND(Operator_OpenBracket, "op[")                                    \
    TOKEN_KIND(Operator_CloseBracket, "op]")                                   \
    TOKEN_KIND(Operator_OpenParenthesis, "op(")                                \
    TOKEN_KIND(Operator_CloseParenthesis, "op)")                               \
    TOKEN_KIND(Operator_Semicolon, "op;")                                      \
    TOKEN_KIND(Operator_Colon, "op:")                                          \
    TOKEN_KIND(Operator_Ellipsis, "op...")                                     \
    TOKEN_KIND(Operator_Conditional, "op?")                                    \
    TOKEN_KIND(Operator_ScopeResolution, "op::")                               \
    TOKEN_KIND(Operator_ClassMemberAccess, "op.")                              \
    TOKEN_KIND(Operator_PointerToMemberDereference, "op.*")                    \
    TOKEN_KIND(Operator_IndirectClassMemberAccess, "op->")                     \
    TOKEN_KIND(Operator_IndirectPointerToMemberDereference, "op->*")           \
    TOKEN_KIND(Operator_Complement, "op~")                                     \
    TOKEN_KIND(Operator_Not, "op!")                                            \
    TOKEN_KIND(Operator_Plus, "op+")                                           \
    TOKEN_KIND(Operator_Minus, "op-")                                          \
    TOKEN_KIND(Operator_Star, "op*") /*multiplication or dereference*/         \
    TOKEN_KIND(Operator_Divide, "op/")                                         \
    TOKEN_KIND(Operator_Remainder, "op%")                                      \
    TOKEN_KIND(Operator_Xor, "op^")                                            \
    TOKEN_KIND(Operator_Ampersand, "op&") /*bitand or take pointer*/           \
    TOKEN_KIND(Operator_BitOr, "op|")                                          \
    TOKEN_KIND(Operator_Assign, "op=")                                         \
    TOKEN_KIND(Operator_PlusAssign, "op+=")                                    \
    TOKEN_KIND(Operator_MinusAssign, "op-=")                                   \
    TOKEN_KIND(Operator_MultiplyAssign, "op*=")                                \
    TOKEN_KIND(Operator_DivideAssign, "op/=")                                  \
    TOKEN_KIND(Operator_RemainderAssign, "op%=")                               \
    TOKEN_KIND(Operator_XorAssign, "op^=")                                     \
    TOKEN_KIND(Operator_BitAndAssign, "op&=")                                  \
    TOKEN_KIND(Operator_BitOrAssign, "op|=")                                   \
    TOKEN_KIND(Operator_Equals, "op==")                                        \
    TOKEN_KIND(Operator_NotEquals, "op!=")                                     \
    TOKEN_KIND(Operator_Less, "op<")                                           \
    TOKEN_KIND(Operator_Greater, "op>")                                        \
    TOKEN_KIND(Operator_LessEquals, "op<=")                                    \
    TOKEN_KIND(Operator_GreaterEquals, "op>=")                                 \
    TOKEN_KIND(Operator_Spaceship, "<=>")                                      \
    TOKEN_KIND(Operator_And, "op&&")                                           \
    TOKEN_KIND(Operator_Or, "op||")                                            \
    TOKEN_KIND(Operator_LeftShift, "op<<")                                     \
    TOKEN_KIND(Operator_RightShift, "op>>")                                    \
    TOKEN_KIND(Operator_LeftShiftAssign, "op<<=")                              \
    TOKEN_KIND(Operator_RightShiftAssign, "op>>=")                             \
    TOKEN_KIND(Operator_Increment, "op++")                                     \
    TOKEN_KIND(Operator_Decrement, "op--")                                     \
    TOKEN_KIND(Operator_Comma, "op,")                                          \
    TOKEN_KIND(Keyword_alignas, "alignas")                                     \
    TOKEN_KIND(Keyword_alignof, "alignof")                                     \
    TOKEN_KIND(Keyword_asm, "asm")                                             \
    TOKEN_KIND(Keyword_auto, "auto")                                           \
    TOKEN_KIND(Keyword_bool, "bool")                                           \
    TOKEN_KIND(Keyword_break, "break")                                         \
    TOKEN_KIND(Keyword_case, "case")                                           \
    TOKEN_KIND(Keyword_catch, "catch")                                         \
    TOKEN_KIND(Keyword_char, "char")                                           \
    TOKEN_KIND(Keyword_char8_t, "char8_t")                                     \
    TOKEN_KIND(Keyword_char16_t, "char16_t")                                   \
    TOKEN_KIND(Keyword_char32_t, "char32_t")                                   \
    TOKEN_KIND(Keyword_class, "class")                                         \
    TOKEN_KIND(Keyword_concept, "concept")                                     \
    TOKEN_KIND(Keyword_const, "const")                                         \
    TOKEN_KIND(Keyword_consteval, "consteval")                                 \
    TOKEN_KIND(Keyword_constexpr, "constexpr")                                 \
    TOKEN_KIND(Keyword_constinit, "constinit")                                 \
    TOKEN_KIND(Keyword_const_cast, "const_cast")                               \
    TOKEN_KIND(Keyword_continue, "continue")                                   \
    TOKEN_KIND(Keyword_co_await, "co_await")                                   \
    TOKEN_KIND(Keyword_co_return, "co_return")                                 \
    TOKEN_KIND(Keyword_co_yield, "co_yield")                                   \
    TOKEN_KIND(Keyword_decltype, "decltype")                                   \
    TOKEN_KIND(Keyword_default, "default")                                     \
    TOKEN_KIND(Keyword_delete, "delete")                                       \
    TOKEN_KIND(Keyword_do, "do")                                               \
    TOKEN_KIND(Keyword_double, "double")                                       \
    TOKEN_KIND(Keyword_dynamic_cast, "dynamic_cast")                           \
    TOKEN_KIND(Keyword_else, "else")                                           \
    TOKEN_KIND(Keyword_enum, "enum")                                           \
    TOKEN_KIND(Keyword_explicit, "explicit")                                   \
    TOKEN_KIND(Keyword_export, "export")                                       \
    TOKEN_KIND(Keyword_extern, "extern")                                       \
    TOKEN_KIND(Keyword_false, "false")                                         \
    TOKEN_KIND(Keyword_float, "float")                                         \
    TOKEN_KIND(Keyword_for, "for")                                             \
    TOKEN_KIND(Keyword_friend, "friend")                                       \
    TOKEN_KIND(Keyword_goto, "goto")                                           \
    TOKEN_KIND(Keyword_if, "if")                                               \
    TOKEN_KIND(Keyword_inline, "inline")                                       \
    TOKEN_KIND(Keyword_int, "int")                                             \
    TOKEN_KIND(Keyword_long, "long")                                           \
    TOKEN_KIND(Keyword_mutable, "mutable")                                     \
    TOKEN_KIND(Keyword_namespace, "namespace")                                 \
    TOKEN_KIND(Keyword_new, "new")                                             \
    TOKEN_KIND(Keyword_noexcept, "noexcept")                                   \
    TOKEN_KIND(Keyword_nullptr, "nullptr")                                     \
    TOKEN_KIND(Keyword_operator, "operator")                                   \
    TOKEN_KIND(Keyword_private, "private")                                     \
    TOKEN_KIND(Keyword_protected, "protected")                                 \
    TOKEN_KIND(Keyword_public, "public")                                       \
    TOKEN_KIND(Keyword_register, "register")                                   \
    TOKEN_KIND(Keyword_reinterpret_cast, "reinterpret_cast")                   \
    TOKEN_KIND(Keyword_requires, "requires")                                   \
    TOKEN_KIND(Keyword_return, "return")                                       \
    TOKEN_KIND(Keyword_short, "short")                                         \
    TOKEN_KIND(Keyword_signed, "signed")                                       \
    TOKEN_KIND(Keyword_sizeof, "sizeof")                                       \
    TOKEN_KIND(Keyword_static, "static")                                       \
    TOKEN_KIND(Keyword_static_assert, "static_assert")                         \
    TOKEN_KIND(Keyword_static_cast, "static_cast")                             \
    TOKEN_KIND(Keyword_struct, "struct")                                       \
    TOKEN_KIND(Keyword_switch, "switch")                                       \
    TOKEN_KIND(Keyword_template, "template")                                   \
    TOKEN_KIND(Keyword_this, "this")                                           \
    TOKEN_KIND(Keyword_thread_local, "thread_local")                           \
    TOKEN_KIND(Keyword_throw, "throw")                                         \
    TOKEN_KIND(Keyword_true, "true")                                           \
    TOKEN_KIND(Keyword_try, "try")                                             \
    TOKEN_KIND(Keyword_typedef, "typedef")                                     \
    TOKEN_KIND(Keyword_typeid, "typeid")                                       \
    TOKEN_KIND(Keyword_typename, "typename")                                   \
    TOKEN_KIND(Keyword_union, "union")                                         \
    TOKEN_KIND(Keyword_unsigned, "unsigned")                                   \
    TOKEN_KIND(Keyword_using, "using")                                         \
    TOKEN_KIND(Keyword_virtual, "virtual")                                     \
    TOKEN_KIND(Keyword_void, "void")                                           \
    TOKEN_KIND(Keyword_volatile, "volatile")                                   \
    TOKEN_KIND(Keyword_wchar_t, "wchar_t")                                     \
    TOKEN_KIND(Keyword_while, "while")

enum class TokenKind : uint8_t
{
#define TOKEN_KIND(value, name) value,
    TOKEN_KINDS
#undef TOKEN_KIND
};

[[nodiscard]] constexpr std::string_view GetTokenKindName(TokenKind kind)
{
    constexpr auto Names = std::to_array<std::string_view>({
#define TOKEN_KIND(value, name) {name},
        TOKEN_KINDS
#undef TOKEN_KIND
    });

    return Names[static_cast<std::underlying_type_t<TokenKind>>(kind)];
}

struct Token
{
    TokenKind Kind;
    util::Position Position;
    util::SourceView Text;
};

static_assert(sizeof(Token) == 20);

} // namespace tokenizer

template <>
struct std::formatter<tokenizer::TokenKind>
{
    constexpr auto parse(std::format_parse_context& ctx) const
    {
        return ctx.begin();
    }

    auto format(tokenizer::TokenKind kind, std::format_context& ctx) const
    {
        return std::format_to(ctx.out(), "{}", GetTokenKindName(kind));
    }
};
