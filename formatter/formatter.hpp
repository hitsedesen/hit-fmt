#pragma once

#include <parser/parser.hpp>
#include <util/input.hpp>

#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace formatter {

inline constexpr std::array<std::string_view, 6>
    DefaultWhitespaceSensitiveMacros{
        "PP_STRINGIZE",
        "DEFINE_ENUM",
        "DEFINE_BIT_ENUM",
        "DEFINE_BIT_ENUM_WITH_UNDERLYING_TYPE",
        "DEFINE_ENUM_WITH_UNDERLYING_TYPE",
        "DEFINE_ERROR_ENUM",
    };

inline constexpr std::array<std::string_view, 7> DefaultForbidBreakAfter{
    "LOG_DEBUG",
    "LOG_ERROR",
    "LOG_FATAL",
    "LOG_INFO",
    "LOG_TRACE",
    "LOG_WARNING",
    "Format",
};

enum class BracketAlignment
{
    Preserve,
    Continuation,
    AlignWithOpening,
};

enum class ClosingBracketAlignment
{
    Preserve,
    AlignWithOpening,
};

enum class BracePlacement
{
    Preserve,
    Attach,
    NewLine,
};

enum class ParenthesisSpacing
{
    Never,
    ControlStatements,
    Always,
};

enum class DeclaratorAlignment
{
    Preserve,
    Left,
    Right,
    Middle,
};

enum class CaseLabelIndentation
{
    Preserve,
    Outdent,
    Indent,
};

struct Style
{
    size_t IndentWidth = 4;
    // Soft target for newly wrapped constructs; existing multiline layouts
    // remain multiline unless Regroup is enabled.
    size_t ColumnLimit = 120;
    // Collapse safe multiline calls and lists when their flat form fits within
    // ColumnLimit.
    bool Regroup = false;
    // Add braces to supported unbraced if/else bodies.
    bool InsertBraces = true;
    // Put braces and following declarators of anonymous records on separate
    // lines.
    bool BreakInlineRecordDeclarations = true;
    // Put a space between class/typename and the ellipsis of a template type
    // parameter pack.
    bool SpaceBeforeTemplateParameterPack = false;
    BracePlacement FunctionBracePlacement = BracePlacement::NewLine;
    BracePlacement RecordBracePlacement = BracePlacement::NewLine;
    BracePlacement ControlBracePlacement = BracePlacement::Preserve;
    BracePlacement LambdaBracePlacement = BracePlacement::Preserve;
    BracePlacement NamespaceBracePlacement = BracePlacement::Attach;
    ParenthesisSpacing SpaceBeforeParentheses =
        ParenthesisSpacing::ControlStatements;
    DeclaratorAlignment PointerAlignment = DeclaratorAlignment::Preserve;
    DeclaratorAlignment ReferenceAlignment = DeclaratorAlignment::Preserve;
    CaseLabelIndentation IndentCaseLabels = CaseLabelIndentation::Preserve;
    bool SpaceInsideParentheses = false;
    bool SpaceInsideSquareBrackets = false;
    bool IndentNamespaces = false;
    bool KeepEmptyBlocksOnOneLine = true;
    bool AllowShortLambdasOnOneLine = true;
    // Indentation of the first item after a multiline opening delimiter.
    BracketAlignment OpenBracketAlignment = BracketAlignment::Continuation;
    // Placement of an existing multiline closing delimiter.
    ClosingBracketAlignment CloseBracketAlignment =
        ClosingBracketAlignment::AlignWithOpening;
    // Function and macro names whose first string argument stays beside the
    // opening parenthesis when the remaining argument list is multiline.
    std::vector<std::string> ForbidBreakAfter{
        DefaultForbidBreakAfter.begin(), DefaultForbidBreakAfter.end()};
    // Token text and trivia inside these macro invocations are emitted
    // verbatim because their internal columns may carry macro-specific style.
    std::vector<std::string> WhitespaceSensitiveMacros{
        DefaultWhitespaceSensitiveMacros.begin(),
        DefaultWhitespaceSensitiveMacros.end()};
};

// Formats an already parsed, lossless syntax tree. Recovery and invalid-token
// regions are emitted verbatim so formatting remains safe during editing. The
// source backing the tree must remain alive for the duration of this call.
[[nodiscard]] std::string
Format(const parser::SyntaxTree& tree, const Style& style);
[[nodiscard]] std::string Format(const parser::SyntaxTree& tree);

// Parse and format an already padded source buffer without copying it.
[[nodiscard]] std::string
Format(util::PaddedSourceView source, const Style& style);
[[nodiscard]] std::string Format(util::PaddedSourceView source);

} // namespace formatter
