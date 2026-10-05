#pragma once

#include "syntax_tree.hpp"

#include <util/input.hpp>

#include <functional>
#include <span>
#include <string>
#include <string_view>

namespace parser {

struct ParseError
{
    std::string Message;
    util::Position Position;
};

using ErrorHandler = std::function<void(ParseError)>;

struct ParseOptions
{
    // Exact, unqualified macro names. A balanced invocation followed by a
    // balanced brace body introduces function-body grammar, not a signature.
    // Borrowed for Parse only; the resulting tree retains no configuration.
    std::span<const std::string> FunctionBodyMacroNames = {};
    // Exact Begin:End identifier pairs with distinct, unique endpoints.
    // Balanced function-like invocations delimit a class-member sequence;
    // nested classes must close with their configured matching end macro.
    // Arguments stay opaque; no expanded class/type bindings are inferred.
    std::span<const std::string> ClassMacroNames = {};
};

// Build a recovery-oriented, lossless C++ syntax tree. The parser recognizes
// the structure needed by a formatter and does not do type checking, macro
// expansion, or conditional preprocessing.
[[nodiscard]] SyntaxTree Parse(
    util::PaddedSourceView input,
    ErrorHandler errorHandler = {},
    const ParseOptions& options = {});

} // namespace parser
