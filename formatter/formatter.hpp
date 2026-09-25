#pragma once

#include <parser/parser.hpp>
#include <util/input.hpp>

#include <cstddef>
#include <string>

namespace formatter {

enum class BracePlacement
{
    Preserve,
    Attach,
    NewLine,
};

enum class NamespaceComments { Preserve, Remove, Required };
enum class IncludeOrder { Preserve, Sort };
enum class BodyIndentation { Preserve, Indent, NoIndent };
enum class InheritanceListMode { Preserve, Format };
enum class InheritanceListColonPlacement { AttachToBase, AttachToClass };
enum class InheritanceListCommaPlacement { AttachToNext, AttachToPrevious };
enum class InheritanceListSpacing { NoSpace, Space };
enum class InheritanceListStart { NewLine, ClassLine, NewLineOnOverflow };
enum class InheritanceListSplit { OnePerLine, Pack, OnePerLineOnOverflow };
enum class FunctionParameterSplit { Preserve, Pack, OnePerLine };
enum class FirstFunctionParameterPosition { NameLine, NewLine };
enum class FunctionParameterIndent { IndentWidth, Continuation };

enum class SameLineBraceSpacing
{
    Preserve,
    SingleSpace,
};

enum class LambdaSpaceBeforeParameters { Preserve, Space, NoSpace };
enum class LambdaCaptureSpacing { Preserve, Space, NoSpace };
enum class FunctionSpaceBeforeParameters { Preserve, Space, NoSpace };
enum class FunctionCallSpaceBeforeParameters { Preserve, Space, NoSpace };
enum class SpaceInsideFunctionParameters { Preserve, Space, NoSpace };
enum class SpaceInsideLambdaParameters { Preserve, Space, NoSpace };
enum class SpaceInsideCallArguments { Preserve, Space, NoSpace };
enum class ControlBlockKeywordSpacing { Preserve, Space, NoSpace };
enum class ControlBlockBracePlacement
{
    Preserve,
    SameLine,
    NewLine,
    AttachUnlessMultiline,
};

struct Style
{
    size_t IndentWidth = 4;
    size_t ColumnLimit = 120;
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

// Preserve source-owned spans except for enabled declaration brace,
// lambda, function, call, and control keyword spacing, brace placement,
// namespace comments, and parameter rules.
[[nodiscard]] std::string Format(const parser::SyntaxTree& tree, const Style& style);
[[nodiscard]] std::string Format(const parser::SyntaxTree& tree);
[[nodiscard]] std::string Format(util::PaddedSourceView source, const Style& style);
[[nodiscard]] std::string Format(util::PaddedSourceView source);

} // namespace formatter
