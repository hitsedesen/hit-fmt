#pragma once

#include "token.hpp"

#include <cstdint>
#include <string_view>

namespace tokenizer::detail {

constexpr uint64_t IdentifierKey(std::string_view text)
{
    return text.size() | (static_cast<uint64_t>(text[0]) << 8) |
        (static_cast<uint64_t>(text[1]) << 16) |
        (static_cast<uint64_t>(text.size() > 4 ? text[4] : '\0') << 24) |
        (static_cast<uint64_t>(text.back()) << 32);
}

inline TokenKind ClassifyIdentifier(std::string_view identifier)
{
    if (identifier.size() < 2 || identifier.size() > 16) {
        return TokenKind::Identifier;
    }
    if (identifier.front() < 'a' || identifier.front() > 'z') {
        return TokenKind::Identifier;
    }

#define TOKENIZER_KEYWORD(text, kind)                                          \
    case IdentifierKey(text):                                                  \
        return identifier == text ? TokenKind::kind : TokenKind::Identifier

    switch (IdentifierKey(identifier)) {
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
        default:
            return TokenKind::Identifier;
    }

#undef TOKENIZER_KEYWORD
}

} // namespace tokenizer::detail
