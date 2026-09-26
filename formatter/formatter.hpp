#pragma once

#include "config_enum.hpp"
#include "config_options.hpp"

#include <parser/parser.hpp>
#include <util/input.hpp>

#include <cstddef>
#include <string>
#include <vector>

namespace formatter {

struct Style
{
#define FORMATTER_STYLE_FIELD(type, name, initial) type name = initial;
    FORMATTER_STYLE_OPTIONS(FORMATTER_STYLE_FIELD)
#undef FORMATTER_STYLE_FIELD
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
