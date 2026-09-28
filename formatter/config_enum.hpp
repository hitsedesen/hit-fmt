#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <string_view>
#include <type_traits>
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
    X(Preserve) X(Remove) X(AddMissing) X(Fix)
DEFINE_CONFIG_ENUM(NamespaceComments, NAMESPACE_COMMENTS_VALUES);
#undef NAMESPACE_COMMENTS_VALUES

#define INCLUDE_ORDER_VALUES(X) X(Preserve) X(Sort)
DEFINE_CONFIG_ENUM(IncludeOrder, INCLUDE_ORDER_VALUES);
#undef INCLUDE_ORDER_VALUES

#define BODY_INDENTATION_VALUES(X) X(Preserve) X(Indent) X(NoIndent)
DEFINE_CONFIG_ENUM(BodyIndentation, BODY_INDENTATION_VALUES);
#undef BODY_INDENTATION_VALUES

#define STANDALONE_BLOCK_BRACE_INDENTATION_VALUES(X) X(Preserve) X(Indent)
DEFINE_CONFIG_ENUM(StandaloneBlockBraceIndentation,
    STANDALONE_BLOCK_BRACE_INDENTATION_VALUES);
#undef STANDALONE_BLOCK_BRACE_INDENTATION_VALUES

#define AMBIGUOUS_TEMPLATE_DECLARATION_SPACING_VALUES(X) X(Preserve) X(Space)
DEFINE_CONFIG_ENUM(AmbiguousTemplateDeclarationSpacing,
    AMBIGUOUS_TEMPLATE_DECLARATION_SPACING_VALUES);
#undef AMBIGUOUS_TEMPLATE_DECLARATION_SPACING_VALUES

#define STATEMENT_ATTRIBUTE_INDENT_VALUES(X) X(Preserve) X(Block) X(Indent)
DEFINE_CONFIG_ENUM(StatementAttributeIndent,
    STATEMENT_ATTRIBUTE_INDENT_VALUES);
#undef STATEMENT_ATTRIBUTE_INDENT_VALUES

#define POINTER_REFERENCE_DECLARATOR_LOCATION_VALUES(X)                       \
    X(Preserve) X(Left) X(Middle) X(Right)
DEFINE_CONFIG_ENUM(PointerReferenceDeclaratorLocation,
    POINTER_REFERENCE_DECLARATOR_LOCATION_VALUES);
#undef POINTER_REFERENCE_DECLARATOR_LOCATION_VALUES

#define TOKEN_SPACING_VALUES(X) X(Preserve) X(Space) X(NoSpace)
DEFINE_CONFIG_ENUM(TokenSpacing, TOKEN_SPACING_VALUES);
#undef TOKEN_SPACING_VALUES

#define TIGHT_OPERATOR_SPACING_VALUES(X) X(Preserve) X(NoSpace)
DEFINE_CONFIG_ENUM(TightOperatorSpacing, TIGHT_OPERATOR_SPACING_VALUES);
#undef TIGHT_OPERATOR_SPACING_VALUES

#define TERNARY_OPERATOR_SPACING_VALUES(X) X(Preserve) X(Space)
DEFINE_CONFIG_ENUM(TernaryOperatorSpacing, TERNARY_OPERATOR_SPACING_VALUES);
#undef TERNARY_OPERATOR_SPACING_VALUES

#define COMMA_SPACING_VALUES(X) X(After) X(Space) X(NoSpace)
DEFINE_CONFIG_ENUM(CommaSpacing, COMMA_SPACING_VALUES);
#undef COMMA_SPACING_VALUES

#define COLON_SPACING_VALUES(X)                                                 \
    X(Preserve) X(NoSpace) X(Left) X(Right) X(LeftAndRight)
DEFINE_CONFIG_ENUM(ColonSpacing, COLON_SPACING_VALUES);
#undef COLON_SPACING_VALUES

#define CONSTRUCTOR_COLON_PLACEMENT_VALUES(X)                                   \
    X(AttachToInitializer) X(AttachToConstructor)
DEFINE_CONFIG_ENUM(ConstructorInitializerColonPlacement,
    CONSTRUCTOR_COLON_PLACEMENT_VALUES);
#undef CONSTRUCTOR_COLON_PLACEMENT_VALUES

#define CONSTRUCTOR_LIST_START_VALUES(X)                                        \
    X(NewLine) X(ConstructorLine) X(NewLineOnOverflow)
DEFINE_CONFIG_ENUM(ConstructorInitializerStart,
    CONSTRUCTOR_LIST_START_VALUES);
#undef CONSTRUCTOR_LIST_START_VALUES

#define RETURN_ARROW_SPACING_VALUES(X)                                          \
    X(Preserve) X(NoSpace) X(Left) X(Right) X(LeftAndRight)
DEFINE_CONFIG_ENUM(ReturnArrowSpacing, RETURN_ARROW_SPACING_VALUES);
#undef RETURN_ARROW_SPACING_VALUES

#define OPERATOR_OVERLOAD_SPACING_VALUES(X)                                     \
    X(Preserve) X(NoSpace) X(Left) X(Right) X(LeftAndRight)
DEFINE_CONFIG_ENUM(OperatorOverloadOperatorSpacing,
    OPERATOR_OVERLOAD_SPACING_VALUES);
#undef OPERATOR_OVERLOAD_SPACING_VALUES

#define PREPROCESSOR_INDENTATION_VALUES(X)                                       \
    X(Preserve) X(LineStart) X(PreprocessorIndented)
DEFINE_CONFIG_ENUM(PreprocessorIndentation, PREPROCESSOR_INDENTATION_VALUES);
#undef PREPROCESSOR_INDENTATION_VALUES

#define INDENTATION_KIND_VALUES(X) X(Preserve) X(Spaces) X(Tabs)
DEFINE_CONFIG_ENUM(IndentationKind, INDENTATION_KIND_VALUES);
#undef INDENTATION_KIND_VALUES

#define TRAILING_WHITESPACE_VALUES(X) X(Preserve) X(Trim)
DEFINE_CONFIG_ENUM(TrailingWhitespace, TRAILING_WHITESPACE_VALUES);
#undef TRAILING_WHITESPACE_VALUES

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

#define CHAINED_CALL_SPLIT_CONDITION_VALUES(X) X(Preserve) X(Overflow) X(Always)
DEFINE_CONFIG_ENUM(ChainedCallSplitCondition,
    CHAINED_CALL_SPLIT_CONDITION_VALUES);
#undef CHAINED_CALL_SPLIT_CONDITION_VALUES

#define CHAINED_CALL_LAYOUT_VALUES(X) X(OnePerLine) X(Pack)
DEFINE_CONFIG_ENUM(ChainedCallLayout, CHAINED_CALL_LAYOUT_VALUES);
#undef CHAINED_CALL_LAYOUT_VALUES

#define FIRST_INITIALIZER_ELEMENT_VALUES(X) X(BraceLine) X(NewLine)
DEFINE_CONFIG_ENUM(FirstInitializerElementPosition,
    FIRST_INITIALIZER_ELEMENT_VALUES);
#undef FIRST_INITIALIZER_ELEMENT_VALUES

#define INITIALIZER_CLOSING_BRACE_VALUES(X) X(SameLine) X(NewLine)
DEFINE_CONFIG_ENUM(InitializerClosingBrace, INITIALIZER_CLOSING_BRACE_VALUES);
#undef INITIALIZER_CLOSING_BRACE_VALUES

#define INITIALIZER_TRAILING_COMMA_VALUES(X) X(Keep) X(Remove)
DEFINE_CONFIG_ENUM(InitializerTrailingComma, INITIALIZER_TRAILING_COMMA_VALUES);
#undef INITIALIZER_TRAILING_COMMA_VALUES

#define CLOSING_DELIMITER_VALUES(X) X(SameLine) X(NewLine)
DEFINE_CONFIG_ENUM(ClosingDelimiterPlacement, CLOSING_DELIMITER_VALUES);
#undef CLOSING_DELIMITER_VALUES

#define FIRST_CALL_ARGUMENT_POSITION_VALUES(X) X(CallLine) X(NewLine)
DEFINE_CONFIG_ENUM(FirstCallArgumentPosition,
    FIRST_CALL_ARGUMENT_POSITION_VALUES);
#undef FIRST_CALL_ARGUMENT_POSITION_VALUES

#define LAMBDA_CAPTURE_SPLIT_VALUES(X) X(Preserve) X(OnePerLineOnOverflow)
DEFINE_CONFIG_ENUM(LambdaCaptureSplit, LAMBDA_CAPTURE_SPLIT_VALUES);
#undef LAMBDA_CAPTURE_SPLIT_VALUES

#define MULTILINE_SINGLE_CALL_ARGUMENT_VALUES(X) X(CallLine) X(NewLine)
DEFINE_CONFIG_ENUM(MultilineSingleCallArgumentPosition,
    MULTILINE_SINGLE_CALL_ARGUMENT_VALUES);
#undef MULTILINE_SINGLE_CALL_ARGUMENT_VALUES

#define CALL_ARGUMENT_INDENT_VALUES(X) X(IndentWidth) X(Continuation)
DEFINE_CONFIG_ENUM(CallArgumentIndent, CALL_ARGUMENT_INDENT_VALUES);
#undef CALL_ARGUMENT_INDENT_VALUES

#define LINE_LENGTH_CHECK_VALUES(X) X(Strict) X(Soft)
DEFINE_CONFIG_ENUM(LineLengthCheck, LINE_LENGTH_CHECK_VALUES);
#undef LINE_LENGTH_CHECK_VALUES

#define EXPRESSION_LAYOUT_CONDITION_VALUES(X)                                   \
    X(Preserve) X(Overflow) X(FitOrOverflow) X(Always)
DEFINE_CONFIG_ENUM(ExpressionLayoutCondition,
    EXPRESSION_LAYOUT_CONDITION_VALUES);
#undef EXPRESSION_LAYOUT_CONDITION_VALUES

#define NONEMPTY_BLOCK_BODY_LAYOUT_VALUES(X) X(Preserve) X(Multiline)
DEFINE_CONFIG_ENUM(NonEmptyBlockBodyLayout,
    NONEMPTY_BLOCK_BODY_LAYOUT_VALUES);
#undef NONEMPTY_BLOCK_BODY_LAYOUT_VALUES

#define EMPTY_CONTROL_BODY_VALUES(X) X(Preserve) X(EmptyBlock)
DEFINE_CONFIG_ENUM(EmptyControlBody, EMPTY_CONTROL_BODY_VALUES);
#undef EMPTY_CONTROL_BODY_VALUES

#define CONTROL_HEADER_CLAUSE_SPLIT_VALUES(X) X(Preserve) X(WhenNeeded)
DEFINE_CONFIG_ENUM(ControlHeaderClauseSplit,
    CONTROL_HEADER_CLAUSE_SPLIT_VALUES);
#undef CONTROL_HEADER_CLAUSE_SPLIT_VALUES

#define FOR_HEADER_CLAUSE_INDENT_VALUES(X) X(Inherit) X(IndentWidth) X(Continuation)
DEFINE_CONFIG_ENUM(ForHeaderClauseIndent, FOR_HEADER_CLAUSE_INDENT_VALUES);
#undef FOR_HEADER_CLAUSE_INDENT_VALUES

#define BINARY_OPERATOR_PLACEMENT_VALUES(X) X(Leading) X(Trailing)
DEFINE_CONFIG_ENUM(BinaryOperatorPlacement, BINARY_OPERATOR_PLACEMENT_VALUES);
#undef BINARY_OPERATOR_PLACEMENT_VALUES

#define TERNARY_OPERATOR_PLACEMENT_VALUES(X) X(Leading) X(Trailing)
DEFINE_CONFIG_ENUM(TernaryOperatorPlacement,
    TERNARY_OPERATOR_PLACEMENT_VALUES);
#undef TERNARY_OPERATOR_PLACEMENT_VALUES

#define TERNARY_SPLIT_VALUES(X) X(Preserve) X(AsNeeded) X(ThreeLines)
DEFINE_CONFIG_ENUM(TernarySplit, TERNARY_SPLIT_VALUES);
#undef TERNARY_SPLIT_VALUES

#define FIRST_FUNCTION_PARAMETER_POSITION_VALUES(X)                            \
    X(NameLine) X(NameLineIfFits) X(NewLine)
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
#define LAMBDA_CALL_SPACE_VALUES(X) X(Inherit) X(Preserve) X(Space) X(NoSpace)
DEFINE_CONFIG_ENUM(LambdaCallSpaceBeforeArguments, LAMBDA_CALL_SPACE_VALUES);
#undef LAMBDA_CALL_SPACE_VALUES

#define EMPTY_LAMBDA_BODY_SPACING_VALUES(X)                                    \
    X(Inherit) X(Preserve) X(Space) X(NoSpace)
DEFINE_CONFIG_ENUM(EmptyLambdaBodySpacing, EMPTY_LAMBDA_BODY_SPACING_VALUES);
#undef EMPTY_LAMBDA_BODY_SPACING_VALUES

#define SINGLE_STATEMENT_LAMBDA_BODY_LAYOUT_VALUES(X)                          \
    X(Inherit) X(Preserve) X(OneLineIfFits)
DEFINE_CONFIG_ENUM(SingleStatementLambdaBodyLayout,
    SINGLE_STATEMENT_LAMBDA_BODY_LAYOUT_VALUES);
#undef SINGLE_STATEMENT_LAMBDA_BODY_LAYOUT_VALUES
DEFINE_CONFIG_ENUM(SpaceInsideFunctionParameters, PRESERVE_SPACE_VALUES);
DEFINE_CONFIG_ENUM(SpaceInsideLambdaParameters, PRESERVE_SPACE_VALUES);
DEFINE_CONFIG_ENUM(SpaceInsideCallArguments, PRESERVE_SPACE_VALUES);
DEFINE_CONFIG_ENUM(ControlBlockKeywordSpacing, PRESERVE_SPACE_VALUES);
#undef PRESERVE_SPACE_VALUES

#define CONTROL_BLOCK_BRACE_PLACEMENT_VALUES(X)                                 \
    X(SameLine) X(NewLine) X(AttachUnlessMultiline)
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
    return ConfigEnumNames<T>::Values[
        static_cast<std::underlying_type_t<T>>(value)];
}

} // namespace formatter
