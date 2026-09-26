#pragma once

// Each entry defines a field, its default, and its config-file binding.
#define FORMATTER_STYLE_OPTIONS(OPT)                                           \
    OPT(std::size_t, IndentWidth, 4)                                           \
    OPT(std::size_t, ColumnLimit, 120)                                         \
    OPT(formatter::ExpressionLayoutCondition, ExpressionLayoutCondition,       \
        formatter::ExpressionLayoutCondition::Preserve)                        \
    OPT(formatter::ExpressionLayoutAlgorithm, ExpressionLayoutAlgorithm,       \
        formatter::ExpressionLayoutAlgorithm::Greedy)                          \
    OPT(formatter::BinaryOperatorPlacement, BinaryOperatorPlacement,           \
        formatter::BinaryOperatorPlacement::Trailing)                          \
    OPT(formatter::TernaryOperatorPlacement, TernaryOperatorPlacement,         \
        formatter::TernaryOperatorPlacement::Preserve)                         \
    OPT(formatter::TernarySplit, TernarySplit,                                 \
        formatter::TernarySplit::Preserve)                                     \
    OPT(formatter::IncludeOrder, IncludeOrder,                                 \
        formatter::IncludeOrder::Preserve)                                     \
    OPT(formatter::BodyIndentation, IndentAccessModifiers,                     \
        formatter::BodyIndentation::Preserve)                                  \
    OPT(formatter::BodyIndentation, IndentMembers,                             \
        formatter::BodyIndentation::Preserve)                                  \
    OPT(formatter::InheritanceListMode, InheritanceList,                       \
        formatter::InheritanceListMode::Preserve)                              \
    OPT(formatter::InheritanceListColonPlacement, InheritanceListColonPlacement, \
        formatter::InheritanceListColonPlacement::AttachToBase)                \
    OPT(formatter::InheritanceListSpacing, InheritanceListColonSpaceBefore,    \
        formatter::InheritanceListSpacing::NoSpace)                            \
    OPT(formatter::InheritanceListSpacing, InheritanceListColonSpaceAfter,     \
        formatter::InheritanceListSpacing::Space)                              \
    OPT(formatter::InheritanceListCommaPlacement, InheritanceListCommaPlacement, \
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
        formatter::BracePlacement::Preserve)                                   \
    OPT(formatter::BracePlacement, StructBracePlacement,                       \
        formatter::BracePlacement::Preserve)                                   \
    OPT(formatter::BracePlacement, UnionBracePlacement,                        \
        formatter::BracePlacement::Preserve)                                   \
    OPT(formatter::BracePlacement, FunctionBracePlacement,                     \
        formatter::BracePlacement::Preserve)                                   \
    OPT(formatter::BracePlacement, LambdaBracePlacement,                       \
        formatter::BracePlacement::Preserve)                                   \
    OPT(formatter::BracePlacement, NamespaceBracePlacement,                    \
        formatter::BracePlacement::Preserve)                                   \
    OPT(formatter::NamespaceComments, NamespaceComments,                       \
        formatter::NamespaceComments::Preserve)                                \
    OPT(formatter::FunctionParameterSplit, FunctionParameterSplit,             \
        formatter::FunctionParameterSplit::Preserve)                           \
    OPT(formatter::FirstFunctionParameterPosition, FirstFunctionParameterPosition, \
        formatter::FirstFunctionParameterPosition::NameLine)                   \
    OPT(bool, EnforceFirstFunctionParameterPosition, false)                    \
    OPT(formatter::FunctionParameterIndent, FunctionParameterIndent,           \
        formatter::FunctionParameterIndent::IndentWidth)                       \
    OPT(bool, MergeFunctionParameters, false)                                  \
    OPT(formatter::CallArgumentSplit, CallArgumentSplit,                       \
        formatter::CallArgumentSplit::Preserve)                                \
    OPT(formatter::FirstCallArgumentPosition, FirstCallArgumentPosition,       \
        formatter::FirstCallArgumentPosition::CallLine)                        \
    OPT(bool, EnforceFirstCallArgumentPosition, false)                         \
    OPT(formatter::CallArgumentIndent, CallArgumentIndent,                     \
        formatter::CallArgumentIndent::IndentWidth)                            \
    OPT(formatter::ControlBlockKeywordSpacing, ControlBlockKeywordSpacing,     \
        formatter::ControlBlockKeywordSpacing::Preserve)                       \
    OPT(formatter::ControlBlockBracePlacement, ControlBlockBracePlacement,     \
        formatter::ControlBlockBracePlacement::Preserve)                       \
    OPT(formatter::SameLineBraceSpacing, ClassBraceSpacing,                    \
        formatter::SameLineBraceSpacing::Preserve)                             \
    OPT(formatter::SameLineBraceSpacing, LambdaBraceSpacing,                   \
        formatter::SameLineBraceSpacing::Preserve)                             \
    OPT(formatter::LambdaSpaceBeforeParameters, LambdaSpaceBeforeParameters,   \
        formatter::LambdaSpaceBeforeParameters::Preserve)                      \
    OPT(formatter::LambdaCaptureSpacing, SpaceAroundCaptureList,               \
        formatter::LambdaCaptureSpacing::Preserve)                             \
    OPT(formatter::LambdaCaptureSpacing, SpaceAroundCaptureAssignment,         \
        formatter::LambdaCaptureSpacing::Preserve)                             \
    OPT(formatter::FunctionSpaceBeforeParameters, FunctionSpaceBeforeParameters, \
        formatter::FunctionSpaceBeforeParameters::Preserve)                    \
    OPT(formatter::FunctionCallSpaceBeforeParameters,                          \
        FunctionCallSpaceBeforeParameters,                                     \
        formatter::FunctionCallSpaceBeforeParameters::Preserve)                \
    OPT(formatter::SpaceInsideFunctionParameters, SpaceInsideFunctionParameters, \
        formatter::SpaceInsideFunctionParameters::Preserve)                    \
    OPT(formatter::SpaceInsideLambdaParameters, SpaceInsideLambdaParameters,   \
        formatter::SpaceInsideLambdaParameters::Preserve)                      \
    OPT(formatter::SpaceInsideCallArguments, SpaceInsideCallArguments,         \
        formatter::SpaceInsideCallArguments::Preserve)                         \
    OPT(formatter::SameLineBraceSpacing, StructBraceSpacing,                   \
        formatter::SameLineBraceSpacing::Preserve)                             \
    OPT(formatter::SameLineBraceSpacing, EnumBraceSpacing,                     \
        formatter::SameLineBraceSpacing::Preserve)                             \
    OPT(formatter::SameLineBraceSpacing, UnionBraceSpacing,                    \
        formatter::SameLineBraceSpacing::Preserve)                             \
    OPT(formatter::SameLineBraceSpacing, FunctionBraceSpacing,                 \
        formatter::SameLineBraceSpacing::Preserve)                             \
    OPT(formatter::SameLineBraceSpacing, NamespaceBraceSpacing,                \
        formatter::SameLineBraceSpacing::Preserve)

// A category supplies its name-case and prefix options together.
#define FORMATTER_NAMING_CATEGORIES(X)                                          \
    X(Class) X(Enum) X(Union) X(Struct) X(Function) X(Concept) X(Variable)    \
    X(Namespace)

// Explicit on-disk names are needed only when they differ from the member.
#define FORMATTER_NAMING_OPTIONS(X)                                             \
    X(std::vector<std::string>, ExemptNamespaces, {"std"},                     \
        NamingNamespaceExemptions)

#define FORMATTER_CONFIGURATION_OPTIONS(X)                                      \
    X(formatter::FunctionDeclarationParameterNames, DeclarationParameterNames, \
        formatter::FunctionDeclarationParameterNames::Unchecked,              \
        FunctionDeclarationParameterNames)                                     \
    X(formatter::FunctionDefinitionParameterNames, DefinitionParameterNames,  \
        formatter::FunctionDefinitionParameterNames::Unchecked,               \
        FunctionDefinitionParameterNames)                                      \
    X(std::vector<std::regex>, Exclusions, {}, Exclude)
