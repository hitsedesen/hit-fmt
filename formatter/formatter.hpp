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
enum class FunctionParameterSplit { Preserve, Pack, OnePerLine };
enum class FirstFunctionParameterPosition { NameLine, NewLine };
enum class FunctionParameterIndent { IndentWidth, Continuation };

enum class SameLineBraceSpacing
{
    Preserve,
    SingleSpace,
};

enum class LambdaSpaceBeforeParameters { Preserve, Space, NoSpace };

struct Style
{
    size_t IndentWidth = 4;
    size_t ColumnLimit = 120;
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
    SameLineBraceSpacing ClassBraceSpacing = SameLineBraceSpacing::Preserve;
    SameLineBraceSpacing LambdaBraceSpacing = SameLineBraceSpacing::Preserve;
    formatter::LambdaSpaceBeforeParameters LambdaSpaceBeforeParameters =
        formatter::LambdaSpaceBeforeParameters::Preserve;
    SameLineBraceSpacing StructBraceSpacing = SameLineBraceSpacing::Preserve;
    SameLineBraceSpacing EnumBraceSpacing = SameLineBraceSpacing::Preserve;
    SameLineBraceSpacing UnionBraceSpacing = SameLineBraceSpacing::Preserve;
    SameLineBraceSpacing FunctionBraceSpacing = SameLineBraceSpacing::Preserve;
    SameLineBraceSpacing NamespaceBraceSpacing = SameLineBraceSpacing::Preserve;
};

// Preserve source-owned spans except for enabled declaration brace,
// lambda capture-to-parameter spacing, namespace comments, and parameter rules.
[[nodiscard]] std::string Format(const parser::SyntaxTree& tree, const Style& style);
[[nodiscard]] std::string Format(const parser::SyntaxTree& tree);
[[nodiscard]] std::string Format(util::PaddedSourceView source, const Style& style);
[[nodiscard]] std::string Format(util::PaddedSourceView source);

} // namespace formatter
