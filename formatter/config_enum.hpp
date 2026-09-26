#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <string_view>
#include <utility>

namespace formatter {

// Expand each value list for both the enum and its on-disk spelling.
#define CONFIG_ENUM_ITEM(value) value,
#define CONFIG_ENUM_NAME(value) #value,
#define NAMED_CONFIG_ENUM_ITEM(value, spelling) value,
#define NAMED_CONFIG_ENUM_NAME(value, spelling) spelling,
#define DEFINE_CONFIG_ENUM_WITH(type, values, item, name)                       \
    enum class type { values(item) };                                          \
    template <> struct ConfigEnumNames<type> {                                 \
        static constexpr auto Values = std::to_array<std::string_view>({       \
            values(name)                                                       \
        });                                                                    \
    }
#define DEFINE_CONFIG_ENUM(type, values)                                       \
    DEFINE_CONFIG_ENUM_WITH(type, values, CONFIG_ENUM_ITEM, CONFIG_ENUM_NAME)
#define DEFINE_NAMED_CONFIG_ENUM(type, values)                                 \
    DEFINE_CONFIG_ENUM_WITH(type, values, NAMED_CONFIG_ENUM_ITEM,              \
        NAMED_CONFIG_ENUM_NAME)

template <typename T> struct ConfigEnumNames;

#define BRACE_PLACEMENT_VALUES(X)                                               \
    X(Preserve) X(Attach) X(NewLine)
DEFINE_CONFIG_ENUM(BracePlacement, BRACE_PLACEMENT_VALUES);
#undef BRACE_PLACEMENT_VALUES

#define NAMESPACE_COMMENTS_VALUES(X)                                            \
    X(Preserve) X(Remove) X(Required)
DEFINE_CONFIG_ENUM(NamespaceComments, NAMESPACE_COMMENTS_VALUES);
#undef NAMESPACE_COMMENTS_VALUES

#define INCLUDE_ORDER_VALUES(X) X(Preserve) X(Sort)
DEFINE_CONFIG_ENUM(IncludeOrder, INCLUDE_ORDER_VALUES);
#undef INCLUDE_ORDER_VALUES

#define BODY_INDENTATION_VALUES(X) X(Preserve) X(Indent) X(NoIndent)
DEFINE_CONFIG_ENUM(BodyIndentation, BODY_INDENTATION_VALUES);
#undef BODY_INDENTATION_VALUES

#define INHERITANCE_LIST_MODE_VALUES(X) X(Preserve) X(Format)
DEFINE_CONFIG_ENUM(InheritanceListMode, INHERITANCE_LIST_MODE_VALUES);
#undef INHERITANCE_LIST_MODE_VALUES

#define INHERITANCE_LIST_COLON_PLACEMENT_VALUES(X)                              \
    X(AttachToBase) X(AttachToClass)
DEFINE_CONFIG_ENUM(InheritanceListColonPlacement,
    INHERITANCE_LIST_COLON_PLACEMENT_VALUES);
#undef INHERITANCE_LIST_COLON_PLACEMENT_VALUES

#define INHERITANCE_LIST_COMMA_PLACEMENT_VALUES(X)                              \
    X(AttachToNext) X(AttachToPrevious)
DEFINE_CONFIG_ENUM(InheritanceListCommaPlacement,
    INHERITANCE_LIST_COMMA_PLACEMENT_VALUES);
#undef INHERITANCE_LIST_COMMA_PLACEMENT_VALUES

#define INHERITANCE_LIST_SPACING_VALUES(X) X(NoSpace) X(Space)
DEFINE_CONFIG_ENUM(InheritanceListSpacing, INHERITANCE_LIST_SPACING_VALUES);
#undef INHERITANCE_LIST_SPACING_VALUES

#define INHERITANCE_LIST_START_VALUES(X)                                        \
    X(NewLine) X(ClassLine) X(NewLineOnOverflow)
DEFINE_CONFIG_ENUM(InheritanceListStart, INHERITANCE_LIST_START_VALUES);
#undef INHERITANCE_LIST_START_VALUES

#define INHERITANCE_LIST_SPLIT_VALUES(X)                                        \
    X(OnePerLine) X(Pack) X(OnePerLineOnOverflow)
DEFINE_CONFIG_ENUM(InheritanceListSplit, INHERITANCE_LIST_SPLIT_VALUES);
#undef INHERITANCE_LIST_SPLIT_VALUES

#define FUNCTION_PARAMETER_SPLIT_VALUES(X)                                      \
    X(Preserve) X(Pack) X(OnePerLine)
DEFINE_CONFIG_ENUM(FunctionParameterSplit, FUNCTION_PARAMETER_SPLIT_VALUES);
#undef FUNCTION_PARAMETER_SPLIT_VALUES

#define CALL_ARGUMENT_SPLIT_VALUES(X) X(Preserve) X(Pack) X(OnePerLine)
DEFINE_CONFIG_ENUM(CallArgumentSplit, CALL_ARGUMENT_SPLIT_VALUES);
#undef CALL_ARGUMENT_SPLIT_VALUES

#define FIRST_CALL_ARGUMENT_POSITION_VALUES(X) X(CallLine) X(NewLine)
DEFINE_CONFIG_ENUM(FirstCallArgumentPosition,
    FIRST_CALL_ARGUMENT_POSITION_VALUES);
#undef FIRST_CALL_ARGUMENT_POSITION_VALUES

#define CALL_ARGUMENT_INDENT_VALUES(X) X(IndentWidth) X(Continuation)
DEFINE_CONFIG_ENUM(CallArgumentIndent, CALL_ARGUMENT_INDENT_VALUES);
#undef CALL_ARGUMENT_INDENT_VALUES

#define EXPRESSION_LAYOUT_CONDITION_VALUES(X)                                   \
    X(Preserve) X(Overflow) X(FitOrOverflow) X(Always)
DEFINE_CONFIG_ENUM(ExpressionLayoutCondition,
    EXPRESSION_LAYOUT_CONDITION_VALUES);
#undef EXPRESSION_LAYOUT_CONDITION_VALUES

#define EXPRESSION_LAYOUT_ALGORITHM_VALUES(X) X(Greedy) X(DynamicProgramming)
DEFINE_CONFIG_ENUM(ExpressionLayoutAlgorithm,
    EXPRESSION_LAYOUT_ALGORITHM_VALUES);
#undef EXPRESSION_LAYOUT_ALGORITHM_VALUES

#define BINARY_OPERATOR_PLACEMENT_VALUES(X) X(Leading) X(Trailing)
DEFINE_CONFIG_ENUM(BinaryOperatorPlacement, BINARY_OPERATOR_PLACEMENT_VALUES);
#undef BINARY_OPERATOR_PLACEMENT_VALUES

#define TERNARY_OPERATOR_PLACEMENT_VALUES(X) X(Preserve) X(Leading) X(Trailing)
DEFINE_CONFIG_ENUM(TernaryOperatorPlacement,
    TERNARY_OPERATOR_PLACEMENT_VALUES);
#undef TERNARY_OPERATOR_PLACEMENT_VALUES

#define TERNARY_SPLIT_VALUES(X) X(Preserve) X(AsNeeded) X(ThreeLines)
DEFINE_CONFIG_ENUM(TernarySplit, TERNARY_SPLIT_VALUES);
#undef TERNARY_SPLIT_VALUES

#define FIRST_FUNCTION_PARAMETER_POSITION_VALUES(X)                            \
    X(NameLine) X(NewLine)
DEFINE_CONFIG_ENUM(FirstFunctionParameterPosition,
    FIRST_FUNCTION_PARAMETER_POSITION_VALUES);
#undef FIRST_FUNCTION_PARAMETER_POSITION_VALUES

#define FUNCTION_PARAMETER_INDENT_VALUES(X)                                     \
    X(IndentWidth) X(Continuation)
DEFINE_CONFIG_ENUM(FunctionParameterIndent, FUNCTION_PARAMETER_INDENT_VALUES);
#undef FUNCTION_PARAMETER_INDENT_VALUES

#define SAME_LINE_BRACE_SPACING_VALUES(X)                                       \
    X(Preserve) X(SingleSpace)
DEFINE_CONFIG_ENUM(SameLineBraceSpacing, SAME_LINE_BRACE_SPACING_VALUES);
#undef SAME_LINE_BRACE_SPACING_VALUES

// These spacing policies share the same serialized values.
#define PRESERVE_SPACE_VALUES(X)                                                \
    X(Preserve) X(Space) X(NoSpace)
DEFINE_CONFIG_ENUM(LambdaSpaceBeforeParameters, PRESERVE_SPACE_VALUES);
DEFINE_CONFIG_ENUM(LambdaCaptureSpacing, PRESERVE_SPACE_VALUES);
DEFINE_CONFIG_ENUM(FunctionSpaceBeforeParameters, PRESERVE_SPACE_VALUES);
DEFINE_CONFIG_ENUM(FunctionCallSpaceBeforeParameters, PRESERVE_SPACE_VALUES);
DEFINE_CONFIG_ENUM(SpaceInsideFunctionParameters, PRESERVE_SPACE_VALUES);
DEFINE_CONFIG_ENUM(SpaceInsideLambdaParameters, PRESERVE_SPACE_VALUES);
DEFINE_CONFIG_ENUM(SpaceInsideCallArguments, PRESERVE_SPACE_VALUES);
DEFINE_CONFIG_ENUM(ControlBlockKeywordSpacing, PRESERVE_SPACE_VALUES);
#undef PRESERVE_SPACE_VALUES

#define CONTROL_BLOCK_BRACE_PLACEMENT_VALUES(X)                                 \
    X(Preserve) X(SameLine) X(NewLine) X(AttachUnlessMultiline)
DEFINE_CONFIG_ENUM(ControlBlockBracePlacement,
    CONTROL_BLOCK_BRACE_PLACEMENT_VALUES);
#undef CONTROL_BLOCK_BRACE_PLACEMENT_VALUES

// NameCase is the only enum whose spellings differ from its C++ members.
#define NAME_CASE_VALUES(X)                                                     \
    X(Unchecked, "Unchecked") X(CamelCase, "camelCase")                         \
    X(PascalCase, "PascalCase") X(SnakeCase, "snake_case")                      \
    X(ScreamingSnakeCase, "SCREAMING_SNAKE_CASE")
DEFINE_NAMED_CONFIG_ENUM(NameCase, NAME_CASE_VALUES);
#undef NAME_CASE_VALUES

#define FUNCTION_DECLARATION_PARAMETER_NAMES_VALUES(X)                          \
    X(Unchecked) X(Required)
DEFINE_CONFIG_ENUM(FunctionDeclarationParameterNames,
    FUNCTION_DECLARATION_PARAMETER_NAMES_VALUES);
#undef FUNCTION_DECLARATION_PARAMETER_NAMES_VALUES

#define FUNCTION_DEFINITION_PARAMETER_NAMES_VALUES(X)                           \
    X(Unchecked) X(NameRequired) X(NameOrCommentedNameRequired)
DEFINE_CONFIG_ENUM(FunctionDefinitionParameterNames,
    FUNCTION_DEFINITION_PARAMETER_NAMES_VALUES);
#undef FUNCTION_DEFINITION_PARAMETER_NAMES_VALUES
#undef DEFINE_CONFIG_ENUM
#undef DEFINE_NAMED_CONFIG_ENUM
#undef DEFINE_CONFIG_ENUM_WITH
#undef CONFIG_ENUM_ITEM
#undef CONFIG_ENUM_NAME
#undef NAMED_CONFIG_ENUM_ITEM
#undef NAMED_CONFIG_ENUM_NAME

template <typename T>
[[nodiscard]] constexpr std::optional<T> DeserializeConfigEnum(
    std::string_view spelling)
{
    for (size_t index = 0; index < ConfigEnumNames<T>::Values.size(); ++index) {
        if (ConfigEnumNames<T>::Values[index] == spelling) {
            return static_cast<T>(index);
        }
    }
    return std::nullopt;
}

template <typename T>
[[nodiscard]] constexpr std::string_view SerializeConfigEnum(T value)
{
    return ConfigEnumNames<T>::Values[std::to_underlying(value)];
}

} // namespace formatter
