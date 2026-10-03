#pragma once

#include <tokenizer/token.hpp>

namespace parser {
struct BinaryOperatorInfo
{
    // Higher values bind more tightly; zero means unsupported by the table.
    int Precedence = 0;
    bool RightAssociative = false;
};
// Lexical metadata only: callers must establish the token's binary role.
// Unary/declarator uses, template angles, and split tokens need syntax context.
// Conditional expressions are handled separately, not by this binary table.
[[nodiscard]] constexpr BinaryOperatorInfo GetBinaryOperatorInfo(
    tokenizer::TokenKind kind) noexcept
{
    using TK = tokenizer::TokenKind;
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
            return {.Precedence = 2, .RightAssociative = true};
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
} // namespace parser
