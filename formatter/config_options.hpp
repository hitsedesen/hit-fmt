#pragma once

// Lexical separation, comments and opaque spans override gap preferences.
// These syntax boundaries preserve horizontal spelling independently of layout.
#define FORMATTER_SYNTAX_BOUNDARY_GAP_OPTIONS(OPT) \
    OPT(formatter::GapPolicy, DeclarationSpecifierSpacing, \
        formatter::GapPolicy::GeneratedSpace) \
    OPT(formatter::GapPolicy, DeclarationNameSpacing, \
        formatter::GapPolicy::GeneratedSpace) \
    OPT(formatter::GapPolicy, ReturnValueSpacing, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, CoReturnValueSpacing, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, ThrowOperandSpacing, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, CoYieldValueSpacing, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, CoAwaitOperandSpacing, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, GotoLabelSpacing, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, CaseValueSpacing, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, NewTypeSpacing, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, DeleteOperandSpacing, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, BaseSpecifierSpacing, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, PackExpansionSpaceBeforeEllipsis, \
        formatter::GapPolicy::GeneratedNoSpace) \
    OPT(formatter::GapPolicy, PackExpansionSpaceAfterEllipsis, \
        formatter::GapPolicy::GeneratedSpace) \
    OPT(formatter::GapPolicy, TemplateParameterNameSpacing, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, TemplateHeaderSpacing, \
        formatter::GapPolicy::GeneratedSpace) \
    OPT(formatter::GapPolicy, RecordNameSpacing, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, EnumKeywordSpacing, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, EnumNameSpacing, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, NamespaceNameSpacing, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, UsingKeywordSpacing, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, ImportKeywordSpacing, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, ModuleKeywordSpacing, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, ExportKeywordSpacing, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, LinkageLanguageSpacing, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, LinkageBraceSpacing, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, LinkageBodyInteriorSpacing, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, ExportBodyInteriorSpacing, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, ModulePartitionColonSpaceBefore, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, ModulePartitionColonSpaceAfter, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, ConceptNameSpacing, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, RequiresClauseSpaceBefore, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, RequiresClauseSpaceAfter, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, RequiresExpressionSpaceBeforeBrace, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, CompoundRequirementInteriorSpacing, \
        formatter::GapPolicy::GeneratedSpace) \
    OPT(formatter::GapPolicy, TypeConstraintSpaceBefore, \
        formatter::GapPolicy::GeneratedSpace) \
    OPT(formatter::GapPolicy, TypeConstraintSpaceAfter, \
        formatter::GapPolicy::GeneratedSpace) \
    OPT(formatter::GapPolicy, FunctionTryBlockSpacing, \
        formatter::GapPolicy::GeneratedSpace) \
    OPT(formatter::GapPolicy, RecordFinalSpacing, \
        formatter::GapPolicy::GeneratedSpace) \
    OPT(formatter::GapPolicy, AlignasSpaceAfterSpecifier, \
        formatter::GapPolicy::GeneratedSpace) \
    OPT(formatter::GapPolicy, LeadingScopeResolutionSpacing, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, ControlHeaderSemicolonSpaceBefore, \
        formatter::GapPolicy::GeneratedNoSpace) \
    OPT(formatter::GapPolicy, StatementSequenceSpacing, \
        formatter::GapPolicy::GeneratedSpace) \
    OPT(formatter::GapPolicy, BlockCommentSpacing, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, ControlClauseSpacing, \
        formatter::GapPolicy::GeneratedSpace) \
    OPT(formatter::GapPolicy, ControlBodySpacing, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, NoexceptSpecifierInteriorSpacing, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, OperatorNameInteriorSpacing, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, DeleteArrayInteriorSpacing, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, StructuredBindingSpaceBeforeBrackets, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, MemberPointerSpaceBeforeQualifier, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, NestedDeclaratorSpaceBeforeParentheses, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, PointerQualifierSpacing, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, StringLiteralFragmentSpacing, \
        formatter::GapPolicy::GeneratedSpace) \
    OPT(formatter::GapPolicy, AsmKeywordSpacing, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, AsmSpaceBeforeParentheses, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, AsmInteriorSpacing, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, ControlKeywordModifierSpacing, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, DependentTemplateNameSpacing, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, DependentTypeNameSpacing, \
        formatter::GapPolicy::Preserve)

#define FORMATTER_GAP_OPTIONS(OPT) \
    FORMATTER_SYNTAX_BOUNDARY_GAP_OPTIONS(OPT) \
    OPT(formatter::GapPolicy, TrailingCommentSpacing, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, ArgumentCommentSpacing, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, ArgumentNameCommentSpacing, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, NamedCastSpaceBeforeOperandParentheses, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, ReturnSpaceBeforeParentheses, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, ThrowSpaceBeforeParentheses, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, CoReturnSpaceBeforeParentheses, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, FunctionSuffixSpecifierSpacing, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, RecordBodyInteriorSpacing, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, NamespaceBodyInteriorSpacing, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, EnumBodyInteriorSpacing, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, RequirementBodyInteriorSpacing, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, TemplateKeywordSpacing, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, TemplateTypeArgumentInteriorSpacing, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, TemplateTypeArgumentSpacing, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, EmptyBraceInitializerInteriorSpacing, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, AttributeInteriorSpacing, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, AttributeSpaceAfterSpecifier, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, NamedCastSpaceBeforeTypeArguments, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, PlacementNewSpaceBeforeArguments, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, AttributeSpaceBeforeSpecifier, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, NewInitializerSpaceBeforeParentheses, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, DeleteArraySpaceBeforeBrackets, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, SizeofPackSpaceBeforeParentheses, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, NewInitializerSpaceBeforeBraces, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, NewInitializerParenthesisInteriorSpacing, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, DeleteArraySpaceAfterBrackets, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, PlacementNewSpaceAfterArguments, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, NewArraySpaceBeforeBrackets, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, NewArrayInteriorSpacing, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, EmptyAttributeInteriorSpacing, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, CoAwaitSpaceBeforeParentheses, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, NewInitializerBraceInteriorSpacing, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, PlacementNewArgumentInteriorSpacing, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, EmptyArrayDeclarationInteriorSpacing, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, EmptyNewArrayInteriorSpacing, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, NoexceptSpaceBeforeSpecifier, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, FunctionQualifierSpacing, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, FunctionRefQualifierSpacing, \
        formatter::GapPolicy::Preserve) \
    OPT(std::optional<formatter::GapPolicy>, FunctionCvRefQualifierSpacing, \
        std::nullopt) \
    OPT(formatter::GapPolicy, NestedDeclaratorInteriorSpacing, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, NestedDeclaratorPointerSpacing, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, MemberPointerDeclaratorSpacing, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, NoexceptSpaceBeforeParentheses, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, AlignasSpaceBeforeParentheses, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, AlignofSpaceBeforeParentheses, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, SizeofSpaceBeforeParentheses, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, DecltypeSpaceBeforeParentheses, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, TypeidSpaceBeforeParentheses, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, RequiresSpaceBeforeParentheses, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, StaticAssertSpaceBeforeParentheses, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, StatementSemicolonSpacing, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, MemberAccessOperatorsSpacing, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, ScopeResolutionSpacing, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, InitializerBracesSpacing, \
        formatter::GapPolicy::GeneratedNoSpace) \
    OPT(formatter::GapPolicy, CastArgumentSpacing, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, GroupingInteriorSpacing, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, ControlBlockConditionSpacing, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, AccessModifierColonSpacing, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, RangeBasedForColonSpacing, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, BlockInteriorSpacing, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, EmptyBlockInteriorSpacing, \
        formatter::GapPolicy::GeneratedNoSpace) \
    OPT(formatter::GapPolicy, EmptyLambdaBodySpacing, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, ControlBlockSpaceBeforeBrace, \
        formatter::GapPolicy::GeneratedSpace) \
    OPT(formatter::GapPolicy, DeclarationSemicolonSpacing, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, UnaryOperatorSpacing, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, PostfixOperatorSpacing, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, TernaryOperatorSpacing, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, BinaryOperatorSpacing, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, AssignmentOperatorSpacing, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, CastSpacing, formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, LoopSemicolonSpacing, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, ConditionInitSemicolonSpacing, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, SubscriptOperatorInteriorSpacing, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, SubscriptOperatorSpacing, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, ArrayDeclarationInteriorSpacing, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, ArrayDeclarationSpacing, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, BraceInitializerSpacing, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, EmptyFunctionCallArgumentsSpacing, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, EmptyFunctionParametersSpacing, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, EmptyLambdaParametersSpacing, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, EmptyLambdaCaptureSpacing, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, StructuredBindingBracketSpacing, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, AmbiguousTemplateDeclarationSpacing, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, ControlBlockKeywordSpacing, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, ClassBraceSpacing, \
        formatter::GapPolicy::GeneratedSpace) \
    OPT(formatter::GapPolicy, LambdaBraceSpacing, \
        formatter::GapPolicy::GeneratedSpace) \
    OPT(formatter::GapPolicy, LambdaSpaceBeforeParameters, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, SpaceAroundCaptureList, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, SpaceAroundCaptureAssignment, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, FunctionSpaceBeforeParameters, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, FunctionCallSpaceBeforeParameters, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, LambdaCallSpaceBeforeArguments, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, SpaceInsideFunctionParameters, \
        formatter::GapPolicy::GeneratedNoSpace) \
    OPT(formatter::GapPolicy, SpaceInsideLambdaParameters, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, SpaceInsideCallArguments, \
        formatter::GapPolicy::GeneratedNoSpace) \
    OPT(formatter::GapPolicy, StructBraceSpacing, \
        formatter::GapPolicy::GeneratedSpace) \
    OPT(formatter::GapPolicy, EnumBraceSpacing, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, UnionBraceSpacing, \
        formatter::GapPolicy::GeneratedSpace) \
    OPT(formatter::GapPolicy, FunctionBraceSpacing, \
        formatter::GapPolicy::GeneratedSpace) \
    OPT(formatter::GapPolicy, NamespaceBraceSpacing, \
        formatter::GapPolicy::GeneratedSpace) \
    OPT(formatter::GapPolicy, CommaSpaceBefore, \
        formatter::GapPolicy::GeneratedNoSpace) \
    OPT(formatter::GapPolicy, CommaSpaceAfter, \
        formatter::GapPolicy::GeneratedSpace) \
    OPT(formatter::GapPolicy, BitFieldColonSpaceBefore, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, BitFieldColonSpaceAfter, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, EnumUnderlyingTypeColonSpaceBefore, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, EnumUnderlyingTypeColonSpaceAfter, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, LabelColonSpaceBefore, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, LabelColonSpaceAfter, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, ReturnArrowSpaceBefore, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, ReturnArrowSpaceAfter, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, OperatorOverloadOperatorSpaceBefore, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, OperatorOverloadOperatorSpaceAfter, \
        formatter::GapPolicy::Preserve) \
    OPT(formatter::GapPolicy, BaseListsColonSpaceBefore, \
        formatter::GapPolicy::GeneratedNoSpace) \
    OPT(formatter::GapPolicy, BaseListsColonSpaceAfter, \
        formatter::GapPolicy::GeneratedSpace) \
    OPT(formatter::GapPolicy, ConstructorListsColonSpaceBefore, \
        formatter::GapPolicy::GeneratedNoSpace) \
    OPT(formatter::GapPolicy, ConstructorListsColonSpaceAfter, \
        formatter::GapPolicy::GeneratedSpace)

#define FORMATTER_DELIMITED_LIST_CATEGORIES(X)                               \
    X(Calls) X(Parameters) X(TemplateParameters) X(TemplateArguments) \
    X(Initializers) X(Captures) \
    X(ControlHeaders) \
    X(Enumerators)
#define FORMATTER_HEADER_LIST_CATEGORIES(X)                                  \
    X(BaseLists) X(ConstructorLists)
#define FORMATTER_LIST_CATEGORIES(X)                                         \
    FORMATTER_DELIMITED_LIST_CATEGORIES(X)                                    \
    FORMATTER_HEADER_LIST_CATEGORIES(X)

// Optional category values spell their disengaged state Inherit on disk.
#define FORMATTER_LIST_CATEGORY_OPTIONS(OPT, category)                        \
    OPT(formatter::CategoryLayout, category##Layout, \
        formatter::CategoryLayout::Inherit) \
    OPT(std::optional<formatter::BreakRequirement>, category##Break,         \
        std::nullopt)                                                       \
    OPT(std::optional<formatter::ListPacking>, category##Packing, \
        std::nullopt) \
    OPT(std::optional<formatter::FirstItemPlacement>, category##FirstItem, \
        std::nullopt) \
    OPT(std::optional<formatter::ContinuationAlignment>, category##Alignment, \
        std::nullopt) \
    OPT(std::optional<std::size_t>, category##ContinuationOffset, \
        std::nullopt) \
    OPT(std::optional<std::size_t>, category##AlignmentFallbackOffset, \
        std::nullopt)

#define FORMATTER_DELIMITED_LIST_CATEGORY_OPTIONS(OPT, category)              \
    FORMATTER_LIST_CATEGORY_OPTIONS(OPT, category)                            \
    OPT(std::optional<formatter::CloserPlacement>, category##Closer,          \
        std::nullopt)

// Each entry defines a field, its default, and its config-file binding.
#define FORMATTER_STYLE_OPTIONS(OPT)                                         \
    OPT(formatter::LayoutMode, Layout, formatter::LayoutMode::Preserve)        \
    OPT(std::size_t, IndentWidth, 4)                                          \
    OPT(std::size_t, ColumnLimit, 120)                                        \
    OPT(formatter::WidthDiagnosticPolicy, WidthDiagnostics, \
        formatter::WidthDiagnosticPolicy::Off) \
    OPT(bool, HonorClangFormatDirectives, true)                               \
    OPT(formatter::IndentationKind, IndentationKind, \
        formatter::IndentationKind::Preserve) \
    OPT(formatter::LineEnding, LineEnding, formatter::LineEnding::Preserve) \
    OPT(formatter::TrailingWhitespace, TrailingWhitespace, \
        formatter::TrailingWhitespace::Preserve) \
    OPT(formatter::BreakRequirement, ListBreak,                              \
        formatter::BreakRequirement::IfNeeded)                               \
    OPT(formatter::ListPacking, ListPacking, formatter::ListPacking::Fill)    \
    OPT(formatter::FirstItemPlacement, ListFirstItem, \
        formatter::FirstItemPlacement::InlineIfFits) \
    OPT(formatter::CloserPlacement, ListCloser, \
        formatter::CloserPlacement::SameLine) \
    OPT(formatter::ContinuationAlignment, ListAlignment, \
        formatter::ContinuationAlignment::Block) \
    /* Zero resolves to IndentWidth, including category overrides. */         \
    OPT(std::size_t, ContinuationOffset, 0)                                   \
    OPT(std::size_t, AlignmentFallbackOffset, 0)                              \
    OPT(bool, KeepMultiline, false)                                         \
    OPT(bool, StringLiteralFragmentsAsArguments, false)                     \
    FORMATTER_DELIMITED_LIST_CATEGORY_OPTIONS(OPT, Calls)                     \
    OPT(std::optional<formatter::FirstItemPlacement>,                        \
        CallsSoleArgumentFirstItem, std::nullopt)                            \
    OPT(bool, CallsSoleLambdaArgumentInline, false)                          \
    FORMATTER_DELIMITED_LIST_CATEGORY_OPTIONS(OPT, Parameters)                \
    FORMATTER_DELIMITED_LIST_CATEGORY_OPTIONS(OPT, TemplateParameters)        \
    FORMATTER_DELIMITED_LIST_CATEGORY_OPTIONS(OPT, TemplateArguments)         \
    FORMATTER_DELIMITED_LIST_CATEGORY_OPTIONS(OPT, Initializers)              \
    FORMATTER_DELIMITED_LIST_CATEGORY_OPTIONS(OPT, Captures)                   \
    FORMATTER_DELIMITED_LIST_CATEGORY_OPTIONS(OPT, ControlHeaders)             \
    OPT(formatter::ListPacking, IfConditionsPacking, \
        formatter::ListPacking::Fill) \
    FORMATTER_DELIMITED_LIST_CATEGORY_OPTIONS(OPT, Enumerators)                \
    FORMATTER_LIST_CATEGORY_OPTIONS(OPT, BaseLists)                           \
    FORMATTER_LIST_CATEGORY_OPTIONS(OPT, ConstructorLists)                    \
    OPT(formatter::BreakRequirement, ExpressionsBreak,                       \
        formatter::BreakRequirement::IfNeeded)                               \
    OPT(formatter::ListPacking, ExpressionsPacking, \
        formatter::ListPacking::Fill) \
    OPT(std::optional<formatter::ListPacking>, AssignmentsPacking, \
        std::nullopt) \
    OPT(formatter::ExpressionAlignment, ExpressionsAlignment, \
        formatter::ExpressionAlignment::Block) \
    OPT(formatter::CategoryLayout, ExpressionsLayout, \
        formatter::CategoryLayout::Inherit) \
    OPT(formatter::OperatorSide, BinaryOperatorSide, \
        formatter::OperatorSide::Trailing) \
    OPT(formatter::OperatorSide, AssignmentOperatorSide, \
        formatter::OperatorSide::Trailing) \
    OPT(formatter::OperatorSide, ShiftOperatorSide, \
        formatter::OperatorSide::Trailing) \
    OPT(formatter::OperatorSide, TernaryOperatorSide, \
        formatter::OperatorSide::Leading) \
    OPT(formatter::TernaryLayout, TernaryLayout, \
        formatter::TernaryLayout::Flexible) \
    OPT(formatter::TernaryOperatorIndentation, TernaryOperatorIndentation, \
        formatter::TernaryOperatorIndentation::Align) \
    OPT(formatter::CategoryLayout, ChainsLayout, \
        formatter::CategoryLayout::Inherit) \
    OPT(formatter::BreakRequirement, ChainsBreak,                            \
        formatter::BreakRequirement::IfNeeded)                               \
    OPT(formatter::ListPacking, ChainsPacking, formatter::ListPacking::Fill)  \
    OPT(formatter::ContinuationAlignment, ChainsAlignment, \
        formatter::ContinuationAlignment::Block) \
    OPT(formatter::CategoryLayout, DeclarationHeadersLayout, \
        formatter::CategoryLayout::Inherit) \
    OPT(bool, BreakAfterTemplateHeader, false) \
    OPT(formatter::RequiresClausePosition, RequiresClausePosition, \
        formatter::RequiresClausePosition::Preserve) \
    OPT(bool, IndentRequiresClause, true) \
    OPT(formatter::FirstItemPlacement, AssignmentRhsPlacement, \
        formatter::FirstItemPlacement::InlineIfFits) \
    OPT(formatter::CategoryLayout, BodiesLayout, \
        formatter::CategoryLayout::Inherit) \
    OPT(formatter::BodyShape, BodyShape, formatter::BodyShape::Expanded)       \
    OPT(std::optional<formatter::BodyShape>, FunctionBodyShape, std::nullopt) \
    OPT(std::optional<formatter::BodyShape>, ControlBodyShape, std::nullopt) \
    OPT(formatter::BodyShape, EmptyBodyShape, formatter::BodyShape::Expanded)  \
    OPT(formatter::CategoryLayout, LambdaBodiesLayout, \
        formatter::CategoryLayout::Inherit) \
    OPT(std::optional<formatter::BodyShape>, LambdaBodyShape, std::nullopt)    \
    OPT(std::optional<formatter::BodyShape>, LambdaEmptyBodyShape, \
        std::nullopt) \
    OPT(formatter::BracePlacement, ClassBracePlacement, \
        formatter::BracePlacement::Attached) \
    OPT(formatter::BracePlacement, StructBracePlacement, \
        formatter::BracePlacement::Attached) \
    OPT(formatter::BracePlacement, UnionBracePlacement, \
        formatter::BracePlacement::Attached) \
    OPT(formatter::BracePlacement, FunctionBracePlacement, \
        formatter::BracePlacement::Attached) \
    OPT(std::optional<formatter::BracePlacement>, ConstructorBracePlacement, \
        std::nullopt) \
    OPT(std::optional<formatter::BracePlacement>, DestructorBracePlacement, \
        std::nullopt) \
    OPT(formatter::BracePlacement, LambdaBracePlacement, \
        formatter::BracePlacement::Attached) \
    OPT(formatter::BracePlacement, NamespaceBracePlacement, \
        formatter::BracePlacement::Attached) \
    OPT(formatter::BracePlacement, ControlBracePlacement, \
        formatter::BracePlacement::Attached) \
    OPT(formatter::BracePlacement, EnumBracePlacement,                       \
        formatter::BracePlacement::Attached)                                 \
    OPT(formatter::BracePlacement, CaseBlockBracePlacement, \
        formatter::BracePlacement::Attached) \
    OPT(formatter::OperatorSide, BaseListsColonSide, \
        formatter::OperatorSide::Leading) \
    OPT(formatter::OperatorSide, BaseListsCommaSide, \
        formatter::OperatorSide::Leading) \
    OPT(formatter::OperatorSide, ConstructorListsColonSide, \
        formatter::OperatorSide::Leading) \
    OPT(formatter::OperatorSide, ConstructorListsCommaSide, \
        formatter::OperatorSide::Leading) \
    OPT(formatter::TrailingCommaPolicy, InitializersTrailingComma, \
        formatter::TrailingCommaPolicy::Preserve) \
    OPT(formatter::PointerReferenceDeclaratorLocation, \
        PointerReferenceDeclaratorLocation, \
        formatter::PointerReferenceDeclaratorLocation::Preserve) \
    OPT(formatter::BodyIndentation, IndentAccessModifiers, \
        formatter::BodyIndentation::Preserve) \
    OPT(std::size_t, AccessModifierIndentWidth, \
        0) \
    OPT(formatter::BodyIndentation, IndentMembers, \
        formatter::BodyIndentation::Preserve) \
    OPT(std::size_t, MemberIndentWidth, 0)                                    \
    OPT(formatter::BodyIndentation, IndentCaseLabels, \
        formatter::BodyIndentation::Preserve) \
    OPT(std::size_t, CaseLabelIndentWidth, 0)                                 \
    OPT(std::size_t, BlockBodyIndentWidth, 0)                                 \
    OPT(formatter::BodyIndentation, IndentNamespaces,                         \
        formatter::BodyIndentation::Preserve)                                \
    OPT(std::size_t, NamespaceIndentWidth, 0)                                \
    OPT(formatter::BodyIndentation, IndentLinkageBlocks,                      \
        formatter::BodyIndentation::Preserve)                                \
    OPT(std::size_t, LinkageIndentWidth, 0)                                   \
    OPT(formatter::StandaloneBlockBraceIndentation, \
        StandaloneBlockBraceIndentation, \
        formatter::StandaloneBlockBraceIndentation::Preserve) \
    OPT(formatter::StatementAttributeIndent, StatementAttributeIndent, \
        formatter::StatementAttributeIndent::Preserve) \
    OPT(std::size_t, StatementAttributeIndentWidth, 0)                        \
    OPT(formatter::BlankLineSetting, NamespaceBlankLinesAfterOpen, {})        \
    OPT(formatter::BlankLineSetting, NamespaceBlankLinesBeforeClose, {})       \
    OPT(formatter::BlankLineSetting, LinkageBlankLinesAfterOpen, {})           \
    OPT(formatter::BlankLineSetting, LinkageBlankLinesBeforeClose, {})         \
    OPT(formatter::BlankLineSetting, AccessSectionBlankLinesBefore, {})        \
    OPT(formatter::BlankLineSetting, AccessSectionBlankLinesAfterLabel, \
        {}) \
    OPT(formatter::BlankLineSetting, DeclarationBlankLines, {})               \
    OPT(formatter::BlankLineSetting, DeclarationCategoryBlankLines, {})       \
    OPT(formatter::BlankLineSetting, MemberDeclarationBlankLines,            \
        formatter::BlankLineSetting::Inherited())                            \
    OPT(formatter::BlankLineSetting, DefinitionBlankLines, {})                 \
    OPT(formatter::BlankLineSetting, MemberDefinitionBlankLines,              \
        formatter::BlankLineSetting::Inherited())                            \
    OPT(formatter::BlankLineSetting, CaseBlankLines, {})                      \
    OPT(formatter::BlankLineSetting, StandaloneCommentBlankLinesBefore, {}) \
    OPT(formatter::BlankLineSetting, StandaloneCommentBlankLinesAfter, {}) \
    OPT(std::optional<std::size_t>, MaxEmptyLines, std::nullopt)              \
    OPT(std::optional<std::size_t>, MemberGroupMaxEmptyLines, std::nullopt)   \
    OPT(formatter::BlankLineSetting, BlockBlankLinesAfter, {})              \
    OPT(formatter::BlankLineSetting, ControlStatementBlankLinesBefore, {})   \
    OPT(formatter::BlankLineSetting, MacroBlankLinesAround, {})             \
    OPT(std::vector<std::string>, BlankLineMacroNames, {})                  \
    OPT(std::vector<std::string>, CallLineFirstArgumentNames, {})             \
    OPT(formatter::AttachedStringOverflow, CallLineFirstArgumentOverflow, \
        formatter::AttachedStringOverflow::Wrap) \
    OPT(std::vector<std::string>, CallLineFirstStringNames, {})               \
    OPT(formatter::AttachedStringOverflow, CallLineFirstStringOverflow, \
        formatter::AttachedStringOverflow::Wrap) \
    OPT(std::vector<std::string>, CallLineMessageNames, {})                   \
    OPT(formatter::MessageHeaderOverflow, CallLineMessageOverflow, \
        formatter::MessageHeaderOverflow::Wrap) \
    OPT(std::vector<std::string>, ChainedCallRootNames, {})                   \
    OPT(std::vector<std::string>, AlwaysBreakChainRootNames, {})              \
    OPT(std::vector<std::string>, AlwaysBreakChainCallNames, {})              \
    OPT(std::vector<std::string>, IndentedCallScopePairs, {})                 \
    OPT(bool, GenerateCallScopeIndentation, false) \
    OPT(std::vector<std::string>, IndentedChainScopePairs, {})                \
    OPT(std::vector<std::string>, StickyChainCallPairs, {})                    \
    OPT(std::vector<std::string>, CallLikeMacroNames, {})                     \
    OPT(std::vector<std::string>, FunctionBodyMacroNames, {})                 \
    OPT(std::vector<std::string>, EnumMacroNames, {})                         \
    OPT(std::vector<std::string>, ClassMacroNames, {})                        \
    OPT(std::vector<std::string>, WhitespaceSensitiveMacros, {})              \
    OPT(formatter::BodyIndentation, IndentPreprocessingDirectives, \
        formatter::BodyIndentation::Preserve) \
    OPT(bool, AlignMacroBackslashes, false) \
    /* Zero preserves spelling; length counts slashes, not indentation. */ \
    OPT(std::size_t, RulerLength, 0) \
    OPT(formatter::RulerPlacement, RulerPlacement, \
        formatter::RulerPlacement::Preserve) \
    OPT(formatter::BlankLineSetting, RulerBlankLinesBefore, {}) \
    OPT(formatter::BlankLineSetting, RulerBlankLinesAfter, {}) \
    OPT(formatter::IncludeOrder, IncludeOrder, \
        formatter::IncludeOrder::Preserve) \
    OPT(formatter::IncludeCategoryOrder, IncludeCategoryOrder, \
        formatter::IncludeCategoryOrder::QuotedFirst) \
    OPT(formatter::IncludeDelimiters, IncludeDelimiters, \
        formatter::IncludeDelimiters::Preserve) \
    OPT(formatter::IncludeMatchingHeader, IncludeMatchingHeader, \
        formatter::IncludeMatchingHeader::Off) \
    OPT(std::string, IncludeMatchingHeaderSuffix, {}) \
    OPT(formatter::IncludeGrouping, IncludeGrouping, \
        formatter::IncludeGrouping::Preserve) \
    OPT(std::vector<formatter::IncludeGroupRule>, IncludeGroups, {}) \
    OPT(formatter::IncludeGroupOrder, IncludeGroupOrder, \
        formatter::IncludeGroupOrder::Directory) \
    OPT(formatter::IncludeDirectoryOrder, IncludeDirectoryOrder, \
        formatter::IncludeDirectoryOrder::NearestFirst) \
    OPT(formatter::IncludeInGroupOrder, IncludeInGroupOrder, \
        formatter::IncludeInGroupOrder::Alphabetical) \
    OPT(formatter::IncludePathBase, IncludePathBase, \
        formatter::IncludePathBase::SourceDirectory) \
    OPT(formatter::IncludeStandardHeaders, IncludeStandardHeaders, \
        formatter::IncludeStandardHeaders::Preserve) \
    OPT(bool, AddMissingControlBraces, false)                                \
    OPT(bool, RemoveNullStatements, false)                                  \
    OPT(bool, RemoveNullDeclarations, false)                                \
    OPT(formatter::EmptyControlBody, EmptyControlBody, \
        formatter::EmptyControlBody::Preserve) \
    OPT(formatter::NamespaceComments, NamespaceComments, \
        formatter::NamespaceComments::Preserve) \
    OPT(std::optional<formatter::TrailingCommentSpaceSetting>, \
        TrailingCommentSpaces, std::nullopt) \
    OPT(std::size_t, NamespaceCommentSpaces, 1)                              \
    OPT(std::size_t, ShortNamespaceLines, 0)                                 \
    FORMATTER_GAP_OPTIONS(OPT)

// Each category supplies case, affix and exemption options together.
#define FORMATTER_NAMING_CATEGORIES(X)                                       \
    X(Class) X(Enum) X(Union) X(Struct) X(Function) X(Concept) X(Variable)     \
    X(Namespace) X(LocalVariable) X(Parameter) X(GlobalVariable)             \
    X(StaticLocalVariable) X(MemberVariable) X(StaticMemberVariable)         \
    X(PublicMemberVariable) X(ProtectedMemberVariable) X(PrivateMemberVariable) \
    X(Enumerator)

#define FORMATTER_CONFIGURATION_OPTIONS(X)                                  \
    X(std::vector<std::string>, NamingNamespaceExemptions, {"std"})          \
    X(bool, NamingStandardLibraryExemptions, false)                         \
    X(std::vector<std::string>, NamingExemptions, {})                        \
    X(std::vector<std::string>, NamingAcronyms, {})                          \
    X(formatter::FunctionDeclarationParameterNames, \
        FunctionDeclarationParameterNames, \
        formatter::FunctionDeclarationParameterNames::Unchecked) \
    X(formatter::FunctionDefinitionParameterNames, \
        FunctionDefinitionParameterNames, \
        formatter::FunctionDefinitionParameterNames::Unchecked) \
    X(std::vector<std::regex>, Exclude, {})
