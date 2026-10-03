#pragma once

#include "config_vertical.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <type_traits>

namespace formatter {

template <typename T> struct ConfigEnumNames;

#define CONFIG_ENUM_ITEM(value) value,
#define CONFIG_ENUM_NAME(value) #value,
#define DEFINE_CONFIG_ENUM(type, values)                                      \
    enum class type : std::uint8_t { values(CONFIG_ENUM_ITEM) };               \
    template <> struct ConfigEnumNames<type> {                                 \
        static constexpr auto Values = std::to_array<std::string_view>({       \
            values(CONFIG_ENUM_NAME)                                          \
        });                                                                   \
    }

#define LAYOUT_VALUES(X) X(Preserve) X(Canonical)
DEFINE_CONFIG_ENUM(LayoutMode, LAYOUT_VALUES);
#undef LAYOUT_VALUES
#define CATEGORY_LAYOUT_VALUES(X) X(Inherit) X(Preserve) X(Canonical)
DEFINE_CONFIG_ENUM(CategoryLayout, CATEGORY_LAYOUT_VALUES);
#undef CATEGORY_LAYOUT_VALUES
#define GAP_VALUES(X)                                                         \
    X(Preserve) X(Space) X(NoSpace) X(GeneratedSpace) X(GeneratedNoSpace)
DEFINE_CONFIG_ENUM(GapPolicy, GAP_VALUES);
#undef GAP_VALUES
#define LIST_PACKING_VALUES(X) X(Fill) X(VerticalWhenBroken)
DEFINE_CONFIG_ENUM(ListPacking, LIST_PACKING_VALUES);
#undef LIST_PACKING_VALUES
#define BREAK_REQUIREMENT_VALUES(X) X(IfNeeded) X(Always)
DEFINE_CONFIG_ENUM(BreakRequirement, BREAK_REQUIREMENT_VALUES);
#undef BREAK_REQUIREMENT_VALUES
#define FIRST_ITEM_VALUES(X) X(InlineIfFits) X(NextLine)
DEFINE_CONFIG_ENUM(FirstItemPlacement, FIRST_ITEM_VALUES);
#undef FIRST_ITEM_VALUES
#define CLOSER_VALUES(X) X(SameLine) X(OwnLine)
DEFINE_CONFIG_ENUM(CloserPlacement, CLOSER_VALUES);
#undef CLOSER_VALUES
#define ALIGNMENT_VALUES(X) X(Block) X(Delimiter)
DEFINE_CONFIG_ENUM(ContinuationAlignment, ALIGNMENT_VALUES);
#undef ALIGNMENT_VALUES
#define EXPRESSION_ALIGNMENT_VALUES(X) X(Block) X(Operand)
DEFINE_CONFIG_ENUM(ExpressionAlignment, EXPRESSION_ALIGNMENT_VALUES);
#undef EXPRESSION_ALIGNMENT_VALUES
#define INCLUDE_MATCHING_HEADER_VALUES(X) X(Off) X(First)
DEFINE_CONFIG_ENUM(IncludeMatchingHeader, INCLUDE_MATCHING_HEADER_VALUES);
#undef INCLUDE_MATCHING_HEADER_VALUES
#define INCLUDE_GROUPING_VALUES(X) X(Preserve) X(Regroup)
DEFINE_CONFIG_ENUM(IncludeGrouping, INCLUDE_GROUPING_VALUES);
#undef INCLUDE_GROUPING_VALUES
#define BRACE_VALUES(X) X(Attached) X(NextLine) X(NextLineIfHeaderBroken)
DEFINE_CONFIG_ENUM(BracePlacement, BRACE_VALUES);
#undef BRACE_VALUES
#define TRAILING_COMMA_VALUES(X) X(Preserve) X(Never) X(WhenMultiline)
DEFINE_CONFIG_ENUM(TrailingCommaPolicy, TRAILING_COMMA_VALUES);
#undef TRAILING_COMMA_VALUES
#define OPERATOR_SIDE_VALUES(X) X(Leading) X(Trailing)
DEFINE_CONFIG_ENUM(OperatorSide, OPERATOR_SIDE_VALUES);
#undef OPERATOR_SIDE_VALUES
#define TERNARY_VALUES(X) X(Flexible) X(ThreePart)
DEFINE_CONFIG_ENUM(TernaryLayout, TERNARY_VALUES);
#undef TERNARY_VALUES
#define WIDTH_DIAGNOSTICS_VALUES(X) X(Off) X(Report)
DEFINE_CONFIG_ENUM(WidthDiagnosticPolicy, WIDTH_DIAGNOSTICS_VALUES);
#undef WIDTH_DIAGNOSTICS_VALUES
#define BODY_SHAPE_VALUES(X) X(Expanded) X(CompactIfFits)
DEFINE_CONFIG_ENUM(BodyShape, BODY_SHAPE_VALUES);
#undef BODY_SHAPE_VALUES
#define NAMESPACE_COMMENTS_VALUES(X) X(Preserve) X(Remove) X(AddMissing) X(Fix)
DEFINE_CONFIG_ENUM(NamespaceComments, NAMESPACE_COMMENTS_VALUES);
#undef NAMESPACE_COMMENTS_VALUES
#define INCLUDE_ORDER_VALUES(X) X(Preserve) X(Sort)
DEFINE_CONFIG_ENUM(IncludeOrder, INCLUDE_ORDER_VALUES);
#undef INCLUDE_ORDER_VALUES
#define BODY_INDENTATION_VALUES(X) X(Preserve) X(Indent) X(NoIndent)
DEFINE_CONFIG_ENUM(BodyIndentation, BODY_INDENTATION_VALUES);
#undef BODY_INDENTATION_VALUES
#define STANDALONE_BRACE_INDENT_VALUES(X) X(Preserve) X(Indent)
DEFINE_CONFIG_ENUM(StandaloneBlockBraceIndentation,
    STANDALONE_BRACE_INDENT_VALUES);
#undef STANDALONE_BRACE_INDENT_VALUES
#define STATEMENT_ATTRIBUTE_INDENT_VALUES(X) X(Preserve) X(Block) X(Indent)
DEFINE_CONFIG_ENUM(StatementAttributeIndent, STATEMENT_ATTRIBUTE_INDENT_VALUES);
#undef STATEMENT_ATTRIBUTE_INDENT_VALUES
#define POINTER_LOCATION_VALUES(X) X(Preserve) X(Left) X(Middle) X(Right)
DEFINE_CONFIG_ENUM(PointerReferenceDeclaratorLocation, POINTER_LOCATION_VALUES);
#undef POINTER_LOCATION_VALUES
#define INDENTATION_KIND_VALUES(X) X(Preserve) X(Spaces) X(Tabs)
DEFINE_CONFIG_ENUM(IndentationKind, INDENTATION_KIND_VALUES);
#undef INDENTATION_KIND_VALUES
// Preserve retains the legacy first-original-LF/generated/copied convention.
#define LINE_ENDING_VALUES(X) X(Preserve) X(LF) X(CRLF)
DEFINE_CONFIG_ENUM(LineEnding, LINE_ENDING_VALUES);
#undef LINE_ENDING_VALUES
#define TRAILING_WHITESPACE_VALUES(X) X(Preserve) X(Trim)
DEFINE_CONFIG_ENUM(TrailingWhitespace, TRAILING_WHITESPACE_VALUES);
#undef TRAILING_WHITESPACE_VALUES
#define EMPTY_CONTROL_BODY_VALUES(X) X(Preserve) X(EmptyBlock)
DEFINE_CONFIG_ENUM(EmptyControlBody, EMPTY_CONTROL_BODY_VALUES);
#undef EMPTY_CONTROL_BODY_VALUES
#define DECLARATION_PARAMETER_NAMES_VALUES(X) X(Unchecked) X(Required)
DEFINE_CONFIG_ENUM(FunctionDeclarationParameterNames,
    DECLARATION_PARAMETER_NAMES_VALUES);
#undef DECLARATION_PARAMETER_NAMES_VALUES
#define DEFINITION_PARAMETER_NAMES_VALUES(X)                                  \
    X(Unchecked) X(NameRequired) X(NameOrCommentedNameRequired)
DEFINE_CONFIG_ENUM(FunctionDefinitionParameterNames,
    DEFINITION_PARAMETER_NAMES_VALUES);
#undef DEFINITION_PARAMETER_NAMES_VALUES

// Diagnostic spellings are intentionally independent of C++ enumerator names.
enum class NameCase : std::uint8_t
{
    Unchecked, CamelCase, PascalCase, SnakeCase, ScreamingSnakeCase
};
template <> struct ConfigEnumNames<NameCase>
{
    static constexpr auto Values = std::to_array<std::string_view>({
        "Unchecked",
        "camelCase",
        "PascalCase",
        "snake_case",
        "SCREAMING_SNAKE_CASE",
    });
};

#undef DEFINE_CONFIG_ENUM
#undef CONFIG_ENUM_ITEM
#undef CONFIG_ENUM_NAME

template <typename T>
[[nodiscard]] constexpr std::optional<T> DeserializeConfigEnum(
    std::string_view spelling)
{
    for (std::size_t index = 0;
         index < ConfigEnumNames<T>::Values.size();
        ++index)
    {
        if (ConfigEnumNames<T>::Values[index] == spelling) {
            return static_cast<T>(index);
        }
    }
    return std::nullopt;
}

template <typename T>
[[nodiscard]] constexpr std::string_view SerializeConfigEnum(T value)
{
    return ConfigEnumNames<T>::Values[
        static_cast<std::underlying_type_t<T>>(value)];
}

} // namespace formatter
