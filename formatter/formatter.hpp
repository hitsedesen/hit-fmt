#pragma once

#include "config_enum.hpp"

#include <parser/parser.hpp>
#include <util/input.hpp>

#include <cstddef>
#include <string>
#include <vector>

namespace formatter {

struct Style
{
    size_t IndentWidth = 4;
    size_t ColumnLimit = 120;
    formatter::ExpressionLayoutCondition ExpressionLayoutCondition =
        formatter::ExpressionLayoutCondition::Preserve;
    formatter::ExpressionLayoutAlgorithm ExpressionLayoutAlgorithm =
        formatter::ExpressionLayoutAlgorithm::Greedy;
    formatter::BinaryOperatorPlacement BinaryOperatorPlacement =
        formatter::BinaryOperatorPlacement::Trailing;
    formatter::TernaryOperatorPlacement TernaryOperatorPlacement =
        formatter::TernaryOperatorPlacement::Preserve;
    formatter::TernarySplit TernarySplit = formatter::TernarySplit::Preserve;
    formatter::IncludeOrder IncludeOrder = IncludeOrder::Preserve;
    BodyIndentation IndentAccessModifiers = BodyIndentation::Preserve;
    BodyIndentation IndentMembers = BodyIndentation::Preserve;
    InheritanceListMode InheritanceList = InheritanceListMode::Preserve;
    formatter::InheritanceListColonPlacement InheritanceListColonPlacement =
        formatter::InheritanceListColonPlacement::AttachToBase;
    InheritanceListSpacing InheritanceListColonSpaceBefore =
        InheritanceListSpacing::NoSpace;
    InheritanceListSpacing InheritanceListColonSpaceAfter =
        InheritanceListSpacing::Space;
    formatter::InheritanceListCommaPlacement InheritanceListCommaPlacement =
        formatter::InheritanceListCommaPlacement::AttachToNext;
    InheritanceListSpacing InheritanceListCommaSpaceBefore =
        InheritanceListSpacing::NoSpace;
    InheritanceListSpacing InheritanceListCommaSpaceAfter =
        InheritanceListSpacing::Space;
    formatter::InheritanceListStart InheritanceListStart =
        formatter::InheritanceListStart::NewLine;
    formatter::InheritanceListSplit InheritanceListSplit =
        formatter::InheritanceListSplit::OnePerLine;
    // Zero uses the global IndentWidth.
    size_t InheritanceListIndent = 0;
    BracePlacement ClassBracePlacement = BracePlacement::Preserve;
    BracePlacement StructBracePlacement = BracePlacement::Preserve;
    BracePlacement UnionBracePlacement = BracePlacement::Preserve;
    BracePlacement FunctionBracePlacement = BracePlacement::Preserve;
    BracePlacement LambdaBracePlacement = BracePlacement::Preserve;
    BracePlacement NamespaceBracePlacement = BracePlacement::Preserve;
    formatter::NamespaceComments NamespaceComments =
        formatter::NamespaceComments::Preserve;
    formatter::FunctionParameterSplit FunctionParameterSplit =
        formatter::FunctionParameterSplit::Preserve;
    formatter::FirstFunctionParameterPosition FirstFunctionParameterPosition =
        formatter::FirstFunctionParameterPosition::NameLine;
    bool EnforceFirstFunctionParameterPosition = false;
    formatter::FunctionParameterIndent FunctionParameterIndent =
        formatter::FunctionParameterIndent::IndentWidth;
    bool MergeFunctionParameters = false;
    formatter::CallArgumentSplit CallArgumentSplit =
        formatter::CallArgumentSplit::Preserve;
    formatter::FirstCallArgumentPosition FirstCallArgumentPosition =
        formatter::FirstCallArgumentPosition::CallLine;
    bool EnforceFirstCallArgumentPosition = false;
    formatter::CallArgumentIndent CallArgumentIndent =
        formatter::CallArgumentIndent::IndentWidth;
    formatter::ControlBlockKeywordSpacing ControlBlockKeywordSpacing =
        formatter::ControlBlockKeywordSpacing::Preserve;
    formatter::ControlBlockBracePlacement ControlBlockBracePlacement =
        formatter::ControlBlockBracePlacement::Preserve;
    SameLineBraceSpacing ClassBraceSpacing = SameLineBraceSpacing::Preserve;
    SameLineBraceSpacing LambdaBraceSpacing = SameLineBraceSpacing::Preserve;
    formatter::LambdaSpaceBeforeParameters LambdaSpaceBeforeParameters =
        formatter::LambdaSpaceBeforeParameters::Preserve;
    LambdaCaptureSpacing SpaceAroundCaptureList =
        LambdaCaptureSpacing::Preserve;
    LambdaCaptureSpacing SpaceAroundCaptureAssignment =
        LambdaCaptureSpacing::Preserve;
    formatter::FunctionSpaceBeforeParameters FunctionSpaceBeforeParameters =
        formatter::FunctionSpaceBeforeParameters::Preserve;
    formatter::FunctionCallSpaceBeforeParameters FunctionCallSpaceBeforeParameters =
        formatter::FunctionCallSpaceBeforeParameters::Preserve;
    formatter::SpaceInsideFunctionParameters SpaceInsideFunctionParameters =
        formatter::SpaceInsideFunctionParameters::Preserve;
    formatter::SpaceInsideLambdaParameters SpaceInsideLambdaParameters =
        formatter::SpaceInsideLambdaParameters::Preserve;
    formatter::SpaceInsideCallArguments SpaceInsideCallArguments =
        formatter::SpaceInsideCallArguments::Preserve;
    SameLineBraceSpacing StructBraceSpacing = SameLineBraceSpacing::Preserve;
    SameLineBraceSpacing EnumBraceSpacing = SameLineBraceSpacing::Preserve;
    SameLineBraceSpacing UnionBraceSpacing = SameLineBraceSpacing::Preserve;
    SameLineBraceSpacing FunctionBraceSpacing = SameLineBraceSpacing::Preserve;
    SameLineBraceSpacing NamespaceBraceSpacing = SameLineBraceSpacing::Preserve;
};

enum class LayoutDiagnosticKind { LineWidth, OperatorPlacement, TernarySplit };

struct LayoutDiagnostic
{
    util::Position Position;
    std::string Message;
    LayoutDiagnosticKind Kind;
};

// Check the source layout directly without computing a reflow.
[[nodiscard]] std::vector<LayoutDiagnostic> CheckExpressionLayout(
    const parser::SyntaxTree& tree, const Style& style);

// Preserve source-owned spans except for enabled declaration brace,
// lambda, function, call, and control keyword spacing, brace placement,
// namespace comments, parameter and call argument rules, and expression
// wrapping.
[[nodiscard]] std::string Format(const parser::SyntaxTree& tree, const Style& style);
[[nodiscard]] std::string Format(const parser::SyntaxTree& tree);
[[nodiscard]] std::string Format(util::PaddedSourceView source, const Style& style);
[[nodiscard]] std::string Format(util::PaddedSourceView source);

} // namespace formatter
