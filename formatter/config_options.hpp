#pragma once

// Each entry defines a field, its default, and its config-file binding.
// The last argument is the maximum for positive integers (zero otherwise).
#define FORMATTER_STYLE_OPTIONS(OPT)                                              \
    OPT(std::size_t, IndentWidth, 4, 32)                                         \
    OPT(std::size_t, ColumnLimit, 120, 10000)                                    \
    OPT(formatter::ExpressionLayoutCondition, ExpressionLayoutCondition,        \
        formatter::ExpressionLayoutCondition::Preserve, 0)                     \
    OPT(formatter::ExpressionLayoutAlgorithm, ExpressionLayoutAlgorithm,        \
        formatter::ExpressionLayoutAlgorithm::Greedy, 0)                       \
    OPT(formatter::BinaryOperatorPlacement, BinaryOperatorPlacement,            \
        formatter::BinaryOperatorPlacement::Trailing, 0)                       \
    OPT(formatter::TernaryOperatorPlacement, TernaryOperatorPlacement,          \
        formatter::TernaryOperatorPlacement::Preserve, 0)                      \
    OPT(formatter::TernarySplit, TernarySplit,                                    \
        formatter::TernarySplit::Preserve, 0)                                   \
    OPT(formatter::IncludeOrder, IncludeOrder,                                   \
        formatter::IncludeOrder::Preserve, 0)                                  \
    OPT(formatter::BodyIndentation, IndentAccessModifiers,                       \
        formatter::BodyIndentation::Preserve, 0)                               \
    OPT(formatter::BodyIndentation, IndentMembers,                               \
        formatter::BodyIndentation::Preserve, 0)                               \
    OPT(formatter::InheritanceListMode, InheritanceList,                        \
        formatter::InheritanceListMode::Preserve, 0)                           \
    OPT(formatter::InheritanceListColonPlacement, InheritanceListColonPlacement, \
        formatter::InheritanceListColonPlacement::AttachToBase, 0)            \
    OPT(formatter::InheritanceListSpacing, InheritanceListColonSpaceBefore,     \
        formatter::InheritanceListSpacing::NoSpace, 0)                         \
    OPT(formatter::InheritanceListSpacing, InheritanceListColonSpaceAfter,      \
        formatter::InheritanceListSpacing::Space, 0)                           \
    OPT(formatter::InheritanceListCommaPlacement, InheritanceListCommaPlacement, \
        formatter::InheritanceListCommaPlacement::AttachToNext, 0)            \
    OPT(formatter::InheritanceListSpacing, InheritanceListCommaSpaceBefore,     \
        formatter::InheritanceListSpacing::NoSpace, 0)                         \
    OPT(formatter::InheritanceListSpacing, InheritanceListCommaSpaceAfter,      \
        formatter::InheritanceListSpacing::Space, 0)                           \
    OPT(formatter::InheritanceListStart, InheritanceListStart,                  \
        formatter::InheritanceListStart::NewLine, 0)                           \
    OPT(formatter::InheritanceListSplit, InheritanceListSplit,                  \
        formatter::InheritanceListSplit::OnePerLine, 0)                        \
    /* Zero uses the global IndentWidth. */                                    \
    OPT(std::size_t, InheritanceListIndent, 0, 32)                               \
    OPT(formatter::BracePlacement, ClassBracePlacement,                          \
        formatter::BracePlacement::Preserve, 0)                                \
    OPT(formatter::BracePlacement, StructBracePlacement,                         \
        formatter::BracePlacement::Preserve, 0)                                \
    OPT(formatter::BracePlacement, UnionBracePlacement,                          \
        formatter::BracePlacement::Preserve, 0)                                \
    OPT(formatter::BracePlacement, FunctionBracePlacement,                       \
        formatter::BracePlacement::Preserve, 0)                                \
    OPT(formatter::BracePlacement, LambdaBracePlacement,                         \
        formatter::BracePlacement::Preserve, 0)                                \
    OPT(formatter::BracePlacement, NamespaceBracePlacement,                      \
        formatter::BracePlacement::Preserve, 0)                                \
    OPT(formatter::NamespaceComments, NamespaceComments,                        \
        formatter::NamespaceComments::Preserve, 0)                             \
    OPT(formatter::FunctionParameterSplit, FunctionParameterSplit,             \
        formatter::FunctionParameterSplit::Preserve, 0)                        \
    OPT(formatter::FirstFunctionParameterPosition, FirstFunctionParameterPosition, \
        formatter::FirstFunctionParameterPosition::NameLine, 0)               \
    OPT(bool, EnforceFirstFunctionParameterPosition, false, 0)                  \
    OPT(formatter::FunctionParameterIndent, FunctionParameterIndent,           \
        formatter::FunctionParameterIndent::IndentWidth, 0)                   \
    OPT(bool, MergeFunctionParameters, false, 0)                                 \
    OPT(formatter::CallArgumentSplit, CallArgumentSplit,                         \
        formatter::CallArgumentSplit::Preserve, 0)                             \
    OPT(formatter::FirstCallArgumentPosition, FirstCallArgumentPosition,        \
        formatter::FirstCallArgumentPosition::CallLine, 0)                     \
    OPT(bool, EnforceFirstCallArgumentPosition, false, 0)                        \
    OPT(formatter::CallArgumentIndent, CallArgumentIndent,                       \
        formatter::CallArgumentIndent::IndentWidth, 0)                         \
    OPT(formatter::ControlBlockKeywordSpacing, ControlBlockKeywordSpacing,      \
        formatter::ControlBlockKeywordSpacing::Preserve, 0)                    \
    OPT(formatter::ControlBlockBracePlacement, ControlBlockBracePlacement,      \
        formatter::ControlBlockBracePlacement::Preserve, 0)                    \
    OPT(formatter::SameLineBraceSpacing, ClassBraceSpacing,                     \
        formatter::SameLineBraceSpacing::Preserve, 0)                          \
    OPT(formatter::SameLineBraceSpacing, LambdaBraceSpacing,                    \
        formatter::SameLineBraceSpacing::Preserve, 0)                          \
    OPT(formatter::LambdaSpaceBeforeParameters, LambdaSpaceBeforeParameters,    \
        formatter::LambdaSpaceBeforeParameters::Preserve, 0)                   \
    OPT(formatter::LambdaCaptureSpacing, SpaceAroundCaptureList,                \
        formatter::LambdaCaptureSpacing::Preserve, 0)                          \
    OPT(formatter::LambdaCaptureSpacing, SpaceAroundCaptureAssignment,          \
        formatter::LambdaCaptureSpacing::Preserve, 0)                          \
    OPT(formatter::FunctionSpaceBeforeParameters, FunctionSpaceBeforeParameters, \
        formatter::FunctionSpaceBeforeParameters::Preserve, 0)                 \
    OPT(formatter::FunctionCallSpaceBeforeParameters,                        \
        FunctionCallSpaceBeforeParameters,                                     \
        formatter::FunctionCallSpaceBeforeParameters::Preserve, 0)             \
    OPT(formatter::SpaceInsideFunctionParameters, SpaceInsideFunctionParameters, \
        formatter::SpaceInsideFunctionParameters::Preserve, 0)                 \
    OPT(formatter::SpaceInsideLambdaParameters, SpaceInsideLambdaParameters,    \
        formatter::SpaceInsideLambdaParameters::Preserve, 0)                   \
    OPT(formatter::SpaceInsideCallArguments, SpaceInsideCallArguments,          \
        formatter::SpaceInsideCallArguments::Preserve, 0)                      \
    OPT(formatter::SameLineBraceSpacing, StructBraceSpacing,                    \
        formatter::SameLineBraceSpacing::Preserve, 0)                          \
    OPT(formatter::SameLineBraceSpacing, EnumBraceSpacing,                      \
        formatter::SameLineBraceSpacing::Preserve, 0)                          \
    OPT(formatter::SameLineBraceSpacing, UnionBraceSpacing,                     \
        formatter::SameLineBraceSpacing::Preserve, 0)                          \
    OPT(formatter::SameLineBraceSpacing, FunctionBraceSpacing,                  \
        formatter::SameLineBraceSpacing::Preserve, 0)                          \
    OPT(formatter::SameLineBraceSpacing, NamespaceBraceSpacing,                 \
        formatter::SameLineBraceSpacing::Preserve, 0)

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
