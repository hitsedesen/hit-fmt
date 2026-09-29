#pragma once

// Each entry defines a field, its default, and its config-file binding.
#define FORMATTER_STYLE_OPTIONS(OPT)                                           \
    OPT(std::size_t, IndentWidth, 4)                                           \
    OPT(std::size_t, ColumnLimit, 120)                                         \
    OPT(formatter::LineLengthCheck, LineLengthCheck,                           \
        formatter::LineLengthCheck::Preserve)                                  \
    OPT(formatter::IndentationKind, IndentationKind,                           \
        formatter::IndentationKind::Preserve)                                  \
    OPT(formatter::PreprocessorIndentation, PreprocessorIndentation,           \
        formatter::PreprocessorIndentation::Preserve)                          \
    OPT(formatter::TokenSpacing, TrailingCommentSpacing,                       \
        formatter::TokenSpacing::Preserve)                                     \
    OPT(formatter::TokenSpacing, NamedCastSpaceBeforeOperandParentheses,       \
        formatter::TokenSpacing::Preserve)                                     \
    OPT(formatter::TokenSpacing, ReturnSpaceBeforeParentheses,                 \
        formatter::TokenSpacing::Preserve)                                     \
    OPT(formatter::TokenSpacing, ThrowSpaceBeforeParentheses,                  \
        formatter::TokenSpacing::Preserve)                                     \
    OPT(formatter::TokenSpacing, CoReturnSpaceBeforeParentheses,               \
        formatter::TokenSpacing::Preserve)                                     \
    OPT(formatter::TokenSpacing, FunctionSuffixSpecifierSpacing,               \
        formatter::TokenSpacing::Preserve)                                     \
    OPT(formatter::TokenSpacing, RecordBodyInteriorSpacing,                    \
        formatter::TokenSpacing::Preserve)                                     \
    OPT(formatter::TokenSpacing, NamespaceBodyInteriorSpacing,                 \
        formatter::TokenSpacing::Preserve)                                     \
    OPT(formatter::TokenSpacing, EnumBodyInteriorSpacing,                      \
        formatter::TokenSpacing::Preserve)                                     \
    OPT(formatter::TokenSpacing, RequirementBodyInteriorSpacing,               \
        formatter::TokenSpacing::Preserve)                                     \
    OPT(formatter::TrailingWhitespace, TrailingWhitespace,                     \
        formatter::TrailingWhitespace::Preserve)                               \
    OPT(formatter::TokenSpacing, TemplateKeywordSpacing,                       \
        formatter::TokenSpacing::Preserve)                                     \
    OPT(formatter::TokenSpacing, TemplateTypeArgumentInteriorSpacing,          \
        formatter::TokenSpacing::Preserve)                                     \
    OPT(formatter::TokenSpacing, TemplateTypeArgumentSpacing,                  \
        formatter::TokenSpacing::Preserve)                                     \
    OPT(formatter::TokenSpacing, EmptyBraceInitializerInteriorSpacing,         \
        formatter::TokenSpacing::Preserve)                                     \
    OPT(formatter::OperatorOverloadOperatorSpacing,                            \
        OperatorOverloadOperatorSpacing,                                       \
        formatter::OperatorOverloadOperatorSpacing::Preserve)                  \
    OPT(formatter::TokenSpacing, AttributeInteriorSpacing,                     \
        formatter::TokenSpacing::Preserve)                                     \
    OPT(formatter::TokenSpacing, AttributeSpaceAfterSpecifier,                 \
        formatter::TokenSpacing::Preserve)                                     \
    OPT(formatter::TokenSpacing, NamedCastSpaceBeforeTypeArguments,            \
        formatter::TokenSpacing::Preserve)                                     \
    OPT(formatter::TokenSpacing, PlacementNewSpaceBeforeArguments,             \
        formatter::TokenSpacing::Preserve)                                     \
    OPT(formatter::TokenSpacing, AttributeSpaceBeforeSpecifier,                \
        formatter::TokenSpacing::Preserve)                                     \
    OPT(formatter::TokenSpacing, NewInitializerSpaceBeforeParentheses,         \
        formatter::TokenSpacing::Preserve)                                     \
    OPT(formatter::TokenSpacing, DeleteArraySpaceBeforeBrackets,               \
        formatter::TokenSpacing::Preserve)                                     \
    OPT(formatter::TokenSpacing, SizeofPackSpaceBeforeParentheses,             \
        formatter::TokenSpacing::Preserve)                                     \
    OPT(formatter::TokenSpacing, NewInitializerSpaceBeforeBraces,              \
        formatter::TokenSpacing::Preserve)                                     \
    OPT(formatter::TokenSpacing, NewInitializerParenthesisInteriorSpacing,     \
        formatter::TokenSpacing::Preserve)                                     \
    OPT(formatter::TokenSpacing, DeleteArraySpaceAfterBrackets,                \
        formatter::TokenSpacing::Preserve)                                     \
    OPT(formatter::TokenSpacing, PlacementNewSpaceAfterArguments,              \
        formatter::TokenSpacing::Preserve)                                     \
    OPT(formatter::TokenSpacing, NewArraySpaceBeforeBrackets,                  \
        formatter::TokenSpacing::Preserve)                                     \
    OPT(formatter::TokenSpacing, NewArrayInteriorSpacing,                      \
        formatter::TokenSpacing::Preserve)                                     \
    OPT(formatter::TokenSpacing, EmptyAttributeInteriorSpacing,                \
        formatter::TokenSpacing::Preserve)                                     \
    OPT(formatter::TokenSpacing, CoAwaitSpaceBeforeParentheses,                \
        formatter::TokenSpacing::Preserve)                                     \
    OPT(formatter::TokenSpacing, NewInitializerBraceInteriorSpacing,           \
        formatter::TokenSpacing::Preserve)                                     \
    OPT(formatter::TokenSpacing, PlacementNewArgumentInteriorSpacing,          \
        formatter::TokenSpacing::Preserve)                                     \
    OPT(formatter::TokenSpacing, EmptyArrayDeclarationInteriorSpacing,         \
        formatter::TokenSpacing::Preserve)                                     \
    OPT(formatter::TokenSpacing, EmptyNewArrayInteriorSpacing,                 \
        formatter::TokenSpacing::Preserve)                                     \
    OPT(formatter::TokenSpacing, NoexceptSpaceBeforeSpecifier,                 \
        formatter::TokenSpacing::Preserve)                                     \
    OPT(formatter::TokenSpacing, FunctionQualifierSpacing,                     \
        formatter::TokenSpacing::Preserve)                                     \
    OPT(formatter::TokenSpacing, FunctionRefQualifierSpacing,                  \
        formatter::TokenSpacing::Preserve)                                     \
    OPT(formatter::TokenSpacing, NestedDeclaratorInteriorSpacing,              \
        formatter::TokenSpacing::Preserve)                                     \
    OPT(formatter::TokenSpacing, NestedDeclaratorPointerSpacing,               \
        formatter::TokenSpacing::Preserve)                                     \
    OPT(formatter::TokenSpacing, MemberPointerDeclaratorSpacing,               \
        formatter::TokenSpacing::Preserve)                                     \
    OPT(formatter::TokenSpacing, NoexceptSpaceBeforeParentheses,               \
        formatter::TokenSpacing::Preserve)                                     \
    OPT(formatter::TokenSpacing, AlignasSpaceBeforeParentheses,                \
        formatter::TokenSpacing::Preserve)                                     \
    OPT(formatter::TokenSpacing, AlignofSpaceBeforeParentheses,                \
        formatter::TokenSpacing::Preserve)                                     \
    OPT(formatter::TokenSpacing, SizeofSpaceBeforeParentheses,                 \
        formatter::TokenSpacing::Preserve)                                     \
    OPT(formatter::TokenSpacing, DecltypeSpaceBeforeParentheses,               \
        formatter::TokenSpacing::Preserve)                                     \
    OPT(formatter::TokenSpacing, TypeidSpaceBeforeParentheses,                 \
        formatter::TokenSpacing::Preserve)                                     \
    OPT(formatter::TokenSpacing, RequiresSpaceBeforeParentheses,               \
        formatter::TokenSpacing::Preserve)                                     \
    OPT(formatter::TokenSpacing, StaticAssertSpaceBeforeParentheses,           \
        formatter::TokenSpacing::Preserve)                                     \
    OPT(formatter::TightOperatorSpacing, StatementSemicolonSpacing,            \
        formatter::TightOperatorSpacing::Preserve)                             \
    OPT(formatter::TightOperatorSpacing, MemberAccessOperatorsSpacing,         \
        formatter::TightOperatorSpacing::Preserve)                             \
    OPT(formatter::TightOperatorSpacing, ScopeResolutionSpacing,               \
        formatter::TightOperatorSpacing::Preserve)                             \
    OPT(formatter::TokenSpacing, InitializerBracesSpacing,                     \
        formatter::TokenSpacing::NoSpace)                                      \
    OPT(bool, EnforceInitializerBracesSpacing, false)                          \
    OPT(formatter::TokenSpacing, CastArgumentSpacing,                          \
        formatter::TokenSpacing::Preserve)                                     \
    OPT(formatter::TokenSpacing, GroupingInteriorSpacing,                      \
        formatter::TokenSpacing::Preserve)                                     \
    OPT(formatter::TokenSpacing, ControlBlockConditionSpacing,                 \
        formatter::TokenSpacing::Preserve)                                     \
    OPT(formatter::TokenSpacing, AccessModifierColonSpacing,                   \
        formatter::TokenSpacing::Preserve)                                     \
    OPT(formatter::TokenSpacing, RangeBasedForColonSpacing,                    \
        formatter::TokenSpacing::Preserve)                                     \
    OPT(formatter::TokenSpacing, BlockInteriorSpacing,                         \
        formatter::TokenSpacing::Preserve)                                     \
    OPT(formatter::TokenSpacing, EmptyBlockInteriorSpacing,                    \
        formatter::TokenSpacing::NoSpace)                                      \
    OPT(bool, EnforceEmptyBlockInteriorSpacing, false)                         \
    OPT(formatter::EmptyLambdaBodySpacing, EmptyLambdaBodySpacing,             \
        formatter::EmptyLambdaBodySpacing::Inherit)                            \
    OPT(formatter::SingleStatementLambdaBodyLayout,                            \
        SingleStatementLambdaBodyLayout,                                       \
        formatter::SingleStatementLambdaBodyLayout::Inherit)                   \
    OPT(formatter::TokenSpacing, ControlBlockSpaceBeforeBrace,                 \
        formatter::TokenSpacing::Space)                                        \
    OPT(bool, EnforceControlBlockSpaceBeforeBrace, false)                      \
    OPT(formatter::TokenSpacing, DeclarationSemicolonSpacing,                  \
        formatter::TokenSpacing::Preserve)                                     \
    OPT(formatter::ColonSpacing, BitFieldColonSpacing,                         \
        formatter::ColonSpacing::Preserve)                                     \
    OPT(formatter::ColonSpacing, EnumUnderlyingTypeColonSpacing,               \
        formatter::ColonSpacing::Preserve)                                     \
    OPT(formatter::ColonSpacing, LabelColonSpacing,                            \
        formatter::ColonSpacing::Preserve)                                     \
    OPT(formatter::InheritanceListMode, ConstructorInitializerList,            \
        formatter::InheritanceListMode::Preserve)                              \
    OPT(formatter::ConstructorInitializerColonPlacement,                       \
        ConstructorInitializerListColonPlacement,                              \
        formatter::ConstructorInitializerColonPlacement::AttachToInitializer)  \
    OPT(formatter::InheritanceListSpacing,                                     \
        ConstructorInitializerListColonSpaceBefore,                            \
        formatter::InheritanceListSpacing::NoSpace)                            \
    OPT(formatter::InheritanceListSpacing,                                     \
        ConstructorInitializerListColonSpaceAfter,                             \
        formatter::InheritanceListSpacing::Space)                              \
    OPT(formatter::InheritanceListCommaPlacement,                              \
        ConstructorInitializerListCommaPlacement,                              \
        formatter::InheritanceListCommaPlacement::AttachToNext)                \
    OPT(formatter::InheritanceListSpacing,                                     \
        ConstructorInitializerListCommaSpaceBefore,                            \
        formatter::InheritanceListSpacing::NoSpace)                            \
    OPT(formatter::InheritanceListSpacing,                                     \
        ConstructorInitializerListCommaSpaceAfter,                             \
        formatter::InheritanceListSpacing::Space)                              \
    OPT(formatter::ConstructorInitializerStart,                                \
        ConstructorInitializerListStart,                                       \
        formatter::ConstructorInitializerStart::NewLine)                       \
    OPT(formatter::InheritanceListSplit, ConstructorInitializerListSplit,      \
        formatter::InheritanceListSplit::OnePerLine)                           \
    /* Zero uses global IndentWidth. */                                        \
    OPT(std::size_t, ConstructorInitializerListIndent, 0)                      \
    OPT(formatter::PointerReferenceDeclaratorLocation,                         \
        PointerReferenceDeclaratorLocation,                                    \
        formatter::PointerReferenceDeclaratorLocation::Preserve)               \
    OPT(formatter::TightOperatorSpacing, UnaryOperatorSpacing,                 \
        formatter::TightOperatorSpacing::Preserve)                             \
    OPT(formatter::TightOperatorSpacing, PostfixOperatorSpacing,               \
        formatter::TightOperatorSpacing::Preserve)                             \
    OPT(formatter::TernaryOperatorSpacing, TernaryOperatorSpacing,             \
        formatter::TernaryOperatorSpacing::Preserve)                           \
    OPT(formatter::TokenSpacing, BinaryOperatorSpacing,                        \
        formatter::TokenSpacing::Preserve)                                     \
    OPT(formatter::TokenSpacing, AssignmentOperatorSpacing,                    \
        formatter::TokenSpacing::Preserve)                                     \
    OPT(formatter::TokenSpacing, CastSpacing,                                  \
        formatter::TokenSpacing::Preserve)                                     \
    OPT(formatter::CommaSpacing, CommaSpacing,                                 \
        formatter::CommaSpacing::Space)                                        \
    OPT(bool, EnforceCommaSpacing, false)                                      \
    OPT(formatter::TokenSpacing, LoopSemicolonSpacing,                         \
        formatter::TokenSpacing::Preserve)                                     \
    OPT(formatter::TokenSpacing, ConditionInitSemicolonSpacing,                \
        formatter::TokenSpacing::Preserve)                                     \
    OPT(formatter::TokenSpacing, SubscriptOperatorInteriorSpacing,             \
        formatter::TokenSpacing::Preserve)                                     \
    OPT(formatter::TokenSpacing, SubscriptOperatorSpacing,                     \
        formatter::TokenSpacing::Preserve)                                     \
    OPT(formatter::TokenSpacing, ArrayDeclarationInteriorSpacing,              \
        formatter::TokenSpacing::Preserve)                                     \
    OPT(formatter::TokenSpacing, ArrayDeclarationSpacing,                      \
        formatter::TokenSpacing::Preserve)                                     \
    OPT(formatter::TokenSpacing, BraceInitializerSpacing,                      \
        formatter::TokenSpacing::Preserve)                                     \
    OPT(formatter::ReturnArrowSpacing, ReturnArrowSpacing,                     \
        formatter::ReturnArrowSpacing::Preserve)                               \
    OPT(formatter::TokenSpacing, EmptyFunctionCallArgumentsSpacing,            \
        formatter::TokenSpacing::Preserve)                                     \
    OPT(formatter::TokenSpacing, EmptyFunctionParametersSpacing,               \
        formatter::TokenSpacing::Preserve)                                     \
    OPT(formatter::TokenSpacing, EmptyLambdaParametersSpacing,                 \
        formatter::TokenSpacing::Preserve)                                     \
    OPT(formatter::TokenSpacing, EmptyLambdaCaptureSpacing,                    \
        formatter::TokenSpacing::Preserve)                                     \
    OPT(formatter::TokenSpacing, StructuredBindingBracketSpacing,              \
        formatter::TokenSpacing::Preserve)                                     \
    OPT(formatter::ExpressionLayoutCondition, ExpressionLayoutCondition,       \
        formatter::ExpressionLayoutCondition::Preserve)                        \
    OPT(formatter::ControlHeaderClauseSplit, ControlHeaderClauseSplit,         \
        formatter::ControlHeaderClauseSplit::Preserve)                         \
    OPT(formatter::CallArgumentIndent, ControlConditionIndent,                 \
        formatter::CallArgumentIndent::IndentWidth)                            \
    OPT(formatter::ForHeaderClauseIndent, ForHeaderClauseIndent,               \
        formatter::ForHeaderClauseIndent::Inherit)                             \
    OPT(formatter::BinaryOperatorPlacement, BinaryOperatorPlacement,           \
        formatter::BinaryOperatorPlacement::Trailing)                          \
    OPT(formatter::BinaryOperatorPlacement, DefaultBinaryOperatorPlacement,    \
        formatter::BinaryOperatorPlacement::Trailing)                          \
    OPT(std::vector<std::string>, TrailingBinaryOperatorList, {})             \
    OPT(std::vector<std::string>, LeadingBinaryOperatorList, {})              \
    OPT(formatter::BinaryOperatorPlacement, AssignmentOperatorPlacement,       \
        formatter::BinaryOperatorPlacement::Leading)                           \
    OPT(formatter::TernaryOperatorPlacement, TernaryOperatorPlacement,         \
        formatter::TernaryOperatorPlacement::Leading)                          \
    OPT(bool, EnforceTernaryOperatorPlacement, false)                          \
    OPT(formatter::TernarySplit, TernarySplit,                                 \
        formatter::TernarySplit::AsNeeded)                                     \
    OPT(bool, EnforceTernarySplit, false)                                      \
    OPT(formatter::IncludeOrder, IncludeOrder,                                 \
        formatter::IncludeOrder::Preserve)                                     \
    OPT(formatter::BodyIndentation, IndentAccessModifiers,                     \
        formatter::BodyIndentation::Preserve)                                  \
    OPT(std::size_t, AccessModifierIndentWidth, 0)                             \
    OPT(formatter::BodyIndentation, IndentMembers,                             \
        formatter::BodyIndentation::Preserve)                                  \
    OPT(std::size_t, MemberIndentWidth, 0)                                     \
    OPT(bool, IndentBlockBodies, false)                                        \
    OPT(bool, IndentLeadingStatementComments, false)                           \
    OPT(std::vector<std::string>, IndentedCallScopePairs, {})                  \
    OPT(formatter::StandaloneBlockBraceIndentation,                            \
        StandaloneBlockBraceIndentation,                                       \
        formatter::StandaloneBlockBraceIndentation::Indent)                    \
    OPT(formatter::AmbiguousTemplateDeclarationSpacing,                        \
        AmbiguousTemplateDeclarationSpacing,                                   \
        formatter::AmbiguousTemplateDeclarationSpacing::Preserve)              \
    OPT(bool, AddMissingControlBraces, false)                                  \
    OPT(bool, RemoveNullStatements, false)                                     \
    OPT(bool, RemoveNullDeclarations, false)                                   \
    OPT(formatter::EmptyControlBody, EmptyControlBody,                         \
        formatter::EmptyControlBody::Preserve)                                 \
    OPT(formatter::NonEmptyBlockBodyLayout, NonEmptyBlockBodyLayout,           \
        formatter::NonEmptyBlockBodyLayout::Preserve)                          \
    OPT(bool, CollapseEmptyFunctionBodies, false)                              \
    OPT(std::size_t, BlockBodyIndentWidth, 0)                                  \
    OPT(formatter::BodyIndentation, IndentCaseLabels,                          \
        formatter::BodyIndentation::Preserve)                                  \
    OPT(std::size_t, CaseLabelIndentWidth, 0)                                  \
    OPT(formatter::StatementAttributeIndent, StatementAttributeIndent,         \
        formatter::StatementAttributeIndent::Preserve)                         \
    OPT(std::size_t, StatementAttributeIndentWidth, 0)                         \
    OPT(formatter::InheritanceListMode, InheritanceList,                       \
        formatter::InheritanceListMode::Preserve)                              \
    OPT(formatter::InheritanceListColonPlacement,                              \
        InheritanceListColonPlacement,                                         \
        formatter::InheritanceListColonPlacement::AttachToBase)                \
    OPT(formatter::InheritanceListSpacing, InheritanceListColonSpaceBefore,    \
        formatter::InheritanceListSpacing::NoSpace)                            \
    OPT(formatter::InheritanceListSpacing, InheritanceListColonSpaceAfter,     \
        formatter::InheritanceListSpacing::Space)                              \
    OPT(formatter::InheritanceListCommaPlacement,                              \
        InheritanceListCommaPlacement,                                         \
        formatter::InheritanceListCommaPlacement::AttachToNext)                \
    OPT(formatter::InheritanceListSpacing, InheritanceListCommaSpaceBefore,    \
        formatter::InheritanceListSpacing::NoSpace)                            \
    OPT(formatter::InheritanceListSpacing, InheritanceListCommaSpaceAfter,     \
        formatter::InheritanceListSpacing::Space)                              \
    OPT(formatter::InheritanceListStart, InheritanceListStart,                 \
        formatter::InheritanceListStart::NewLine)                              \
    OPT(formatter::InheritanceListSplit, InheritanceListSplit,                 \
        formatter::InheritanceListSplit::OnePerLine)                           \
    /* Zero uses the global IndentWidth. */                                    \
    OPT(std::size_t, InheritanceListIndent, 0)                                 \
    OPT(formatter::BracePlacement, ClassBracePlacement,                        \
        formatter::BracePlacement::Attach)                                     \
    OPT(bool, EnforceClassBracePlacement, false)                               \
    OPT(formatter::BracePlacement, StructBracePlacement,                       \
        formatter::BracePlacement::Attach)                                     \
    OPT(bool, EnforceStructBracePlacement, false)                              \
    OPT(formatter::BracePlacement, UnionBracePlacement,                        \
        formatter::BracePlacement::Attach)                                     \
    OPT(bool, EnforceUnionBracePlacement, false)                               \
    OPT(formatter::BracePlacement, FunctionBracePlacement,                     \
        formatter::BracePlacement::Attach)                                     \
    OPT(bool, EnforceFunctionBracePlacement, false)                            \
    OPT(formatter::BracePlacement, LambdaBracePlacement,                       \
        formatter::BracePlacement::Attach)                                     \
    OPT(bool, EnforceLambdaBracePlacement, false)                              \
    OPT(formatter::BracePlacement, NamespaceBracePlacement,                    \
        formatter::BracePlacement::Attach)                                     \
    OPT(bool, EnforceNamespaceBracePlacement, false)                           \
    OPT(formatter::NamespaceComments, NamespaceComments,                       \
        formatter::NamespaceComments::Preserve)                                \
    OPT(std::size_t, NamespaceCommentSpaces, 1)                                \
    OPT(std::size_t, ShortNamespaceLines, 0)                                   \
    /* Zero disables separator normalization. */                               \
    OPT(std::size_t, SeparatorLength, 0)                                       \
    OPT(formatter::FunctionParameterSplit, FunctionParameterSplit,             \
        formatter::FunctionParameterSplit::Pack)                               \
    OPT(bool, EnforceFunctionParameterSplit, false)                            \
    OPT(formatter::FirstFunctionParameterPosition,                             \
        FirstFunctionParameterPosition,                                        \
        formatter::FirstFunctionParameterPosition::NameLine)                   \
    OPT(bool, EnforceFirstFunctionParameterPosition, false)                    \
    OPT(formatter::FunctionParameterIndent, FunctionParameterIndent,           \
        formatter::FunctionParameterIndent::IndentWidth)                       \
    /* Zero keeps the existing FunctionParameterIndent policy. */              \
    OPT(std::size_t, FunctionParameterNewLineIndent, 0)                        \
    OPT(bool, MergeFunctionParameters, false)                                  \
    OPT(formatter::CallArgumentSplit, CallArgumentSplit,                       \
        formatter::CallArgumentSplit::Pack)                                    \
    OPT(bool, EnforceCallArgumentSplit, false)                                 \
    OPT(formatter::FirstCallArgumentPosition, FirstCallArgumentPosition,       \
        formatter::FirstCallArgumentPosition::CallLine)                        \
    OPT(formatter::MultilineSingleCallArgumentPosition,                        \
        MultilineSingleCallArgumentPosition,                                   \
        formatter::MultilineSingleCallArgumentPosition::CallLine)              \
    OPT(bool, EnforceMultilineSingleCallArgumentPosition, false)               \
    OPT(bool, EnforceFirstCallArgumentPosition, false)                         \
    OPT(formatter::CallArgumentIndent, CallArgumentIndent,                     \
        formatter::CallArgumentIndent::IndentWidth)                            \
    OPT(std::vector<std::string>, CallLineFirstStringNames, {})                \
    OPT(std::size_t, CallArgumentSplitColumnLimit, 0)                          \
    OPT(std::vector<std::string>, CallArgumentSplitColumnLimitNames, {})       \
    OPT(std::vector<std::string>, CallLikeMacroNames, {})                      \
    OPT(std::vector<std::string>, WhitespaceSensitiveMacros, {})               \
    OPT(std::vector<std::string>, ChainedCallRootNames, {})                    \
    OPT(formatter::ChainedCallSplitCondition, ChainedCallSplitCondition,       \
        formatter::ChainedCallSplitCondition::Preserve)                        \
    OPT(formatter::ChainedCallLayout, ChainedCallLayout,                       \
        formatter::ChainedCallLayout::OnePerLine)                              \
    OPT(formatter::CallArgumentIndent, ChainedCallIndent,                      \
        formatter::CallArgumentIndent::IndentWidth)                            \
    OPT(formatter::InitializerListSplit, InitializerListSplit,                 \
        formatter::InitializerListSplit::Pack)                                 \
    OPT(bool, EnforceInitializerListSplit, false)                              \
    OPT(formatter::FirstInitializerElementPosition,                            \
        FirstInitializerElementPosition,                                       \
        formatter::FirstInitializerElementPosition::BraceLine)                 \
    OPT(formatter::CallArgumentIndent, InitializerListIndent,                  \
        formatter::CallArgumentIndent::IndentWidth)                            \
    OPT(formatter::InitializerListClosingBrace, InitializerListClosingBrace,   \
        formatter::InitializerListClosingBrace::SameLineUnlessTrailingComma)   \
    OPT(bool, EnforceInitializerListClosingBrace, false)                       \
    OPT(formatter::InitializerListTrailingComma, InitializerListTrailingComma, \
        formatter::InitializerListTrailingComma::Keep)                         \
    OPT(bool, EnforceInitializerListTrailingComma, false)                      \
    OPT(formatter::ClosingDelimiterPlacement,                                  \
        FunctionParameterClosingParenthesis,                                   \
        formatter::ClosingDelimiterPlacement::SameLine)                        \
    OPT(bool, EnforceFunctionParameterClosingParenthesis, false)               \
    OPT(formatter::ClosingDelimiterPlacement,                                  \
        FunctionCallArgumentClosingParenthesis,                                \
        formatter::ClosingDelimiterPlacement::SameLine)                        \
    OPT(bool, EnforceFunctionCallArgumentClosingParenthesis, false)            \
    OPT(formatter::ClosingDelimiterPlacement, BlockClosingBrace,               \
        formatter::ClosingDelimiterPlacement::NewLine)                         \
    OPT(bool, EnforceBlockClosingBrace, false)                                 \
    OPT(bool, MergeInitializerList, false)                                     \
    OPT(formatter::ControlBlockKeywordSpacing, ControlBlockKeywordSpacing,     \
        formatter::ControlBlockKeywordSpacing::Preserve)                       \
    OPT(formatter::ControlBlockBracePlacement, ControlBlockBracePlacement,     \
        formatter::ControlBlockBracePlacement::SameLine)                       \
    OPT(bool, EnforceControlBlockBracePlacement, false)                        \
    OPT(formatter::SameLineBraceSpacing, ClassBraceSpacing,                    \
        formatter::SameLineBraceSpacing::SingleSpace)                          \
    OPT(bool, EnforceClassBraceSpacing, false)                                 \
    OPT(formatter::SameLineBraceSpacing, LambdaBraceSpacing,                   \
        formatter::SameLineBraceSpacing::SingleSpace)                          \
    OPT(bool, EnforceLambdaBraceSpacing, false)                                \
    OPT(formatter::LambdaSpaceBeforeParameters, LambdaSpaceBeforeParameters,   \
        formatter::LambdaSpaceBeforeParameters::Preserve)                      \
    OPT(formatter::LambdaCaptureSpacing, SpaceAroundCaptureList,               \
        formatter::LambdaCaptureSpacing::Preserve)                             \
    OPT(formatter::LambdaCaptureSpacing, SpaceAroundCaptureAssignment,         \
        formatter::LambdaCaptureSpacing::Preserve)                             \
    OPT(formatter::LambdaCaptureSplit, LambdaCaptureSplit,                     \
        formatter::LambdaCaptureSplit::Preserve)                               \
    OPT(formatter::FunctionSpaceBeforeParameters,                              \
        FunctionSpaceBeforeParameters,                                         \
        formatter::FunctionSpaceBeforeParameters::Preserve)                    \
    OPT(formatter::FunctionCallSpaceBeforeParameters,                          \
        FunctionCallSpaceBeforeParameters,                                     \
        formatter::FunctionCallSpaceBeforeParameters::Preserve)                \
    OPT(formatter::LambdaCallSpaceBeforeArguments,                             \
        LambdaCallSpaceBeforeArguments,                                        \
        formatter::LambdaCallSpaceBeforeArguments::Inherit)                    \
    OPT(formatter::SpaceInsideFunctionParameters,                              \
        SpaceInsideFunctionParameters,                                         \
        formatter::SpaceInsideFunctionParameters::NoSpace)                     \
    OPT(bool, EnforceSpaceInsideFunctionParameters, false)                     \
    OPT(formatter::SpaceInsideLambdaParameters, SpaceInsideLambdaParameters,   \
        formatter::SpaceInsideLambdaParameters::Preserve)                      \
    OPT(formatter::SpaceInsideCallArguments, SpaceInsideCallArguments,         \
        formatter::SpaceInsideCallArguments::NoSpace)                          \
    OPT(bool, EnforceSpaceInsideCallArguments, false)                          \
    OPT(formatter::SameLineBraceSpacing, StructBraceSpacing,                   \
        formatter::SameLineBraceSpacing::SingleSpace)                          \
    OPT(bool, EnforceStructBraceSpacing, false)                                \
    OPT(formatter::SameLineBraceSpacing, EnumBraceSpacing,                     \
        formatter::SameLineBraceSpacing::Preserve)                             \
    OPT(formatter::SameLineBraceSpacing, UnionBraceSpacing,                    \
        formatter::SameLineBraceSpacing::SingleSpace)                          \
    OPT(bool, EnforceUnionBraceSpacing, false)                                 \
    OPT(formatter::SameLineBraceSpacing, FunctionBraceSpacing,                 \
        formatter::SameLineBraceSpacing::SingleSpace)                          \
    OPT(bool, EnforceFunctionBraceSpacing, false)                              \
    OPT(formatter::SameLineBraceSpacing, NamespaceBraceSpacing,                \
        formatter::SameLineBraceSpacing::SingleSpace)                          \
    OPT(bool, EnforceNamespaceBraceSpacing, false)

// A category supplies its name-case and prefix options together.
#define FORMATTER_NAMING_CATEGORIES(X)                                         \
    X(Class) X(Enum) X(Union) X(Struct) X(Function) X(Concept) X(Variable)     \
    X(Namespace)

#define FORMATTER_CONFIGURATION_OPTIONS(X)                                     \
    X(std::vector<std::string>, NamingNamespaceExemptions, {"std"})            \
    X(formatter::FunctionDeclarationParameterNames,                            \
        FunctionDeclarationParameterNames,                                     \
        formatter::FunctionDeclarationParameterNames::Unchecked)               \
    X(formatter::FunctionDefinitionParameterNames,                             \
        FunctionDefinitionParameterNames,                                      \
        formatter::FunctionDefinitionParameterNames::Unchecked)                \
    X(std::vector<std::regex>, Exclude, {})
