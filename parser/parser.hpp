#pragma once

#include "syntax_tree.hpp"

#include <util/input.hpp>

#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace parser {

struct ParseError
{
    std::string Message;
    util::Position Position;
    // A protected continuation prevents certifying formatting conformance.
    bool CheckingIncomplete = false;
};

using ErrorHandler = std::function<void(ParseError)>;

struct ParseOptions
{
    // Exact, unqualified macro names. A balanced invocation and any additional
    // balanced parenthesized groups followed by a balanced brace body introduce
    // function-body grammar. The complete header stays opaque, not a signature.
    // Borrowed for Parse only; the resulting tree retains no configuration.
    std::span<const std::string> FunctionBodyMacroNames = {};
    // Exact Begin:End identifier pairs with distinct, unique endpoints.
    // Balanced function-like invocations delimit a class-member sequence;
    // nested classes must close with their configured matching end macro.
    // Arguments stay opaque; no expanded class/type bindings are inferred.
    std::span<const std::string> ClassMacroNames = {};
    // Invocations whose final argument is a YT-style sequence of (Name) or
    // ((Name)(value)) entries. Other arguments and value expressions stay raw.
    std::span<const std::string> EnumMacroNames = {};
    // Exact, unqualified standalone invocations with C++ expression arguments.
    // The terminating semicolon is optional; no macro expansion is inferred.
    std::span<const std::string> CallLikeMacroNames = {};
};

// Build a recovery-oriented, lossless C++ syntax tree. The parser recognizes
// the structure needed by a formatter and does not do type checking, macro
// expansion, or conditional preprocessing.
[[nodiscard]] SyntaxTree Parse(
    util::PaddedSourceView input,
    ErrorHandler errorHandler = {},
    const ParseOptions& options = {});

// Retokenize and copy grammar when token kinds/order and all non-include
// spellings are unchanged. Only single-line literal #include spellings may
// differ. Positions and trivia are rebuilt; macro hints are inherited from
// original's parsed grammar. Returns nullopt when reuse cannot be proven.
// The result owns its arena and borrows input; original may be released.
[[nodiscard]] std::optional<SyntaxTree> TryReparseIncludes(const SyntaxTree& original, util::PaddedSourceView input);

// Also permit standalone all-slash ruler spelling changes and boundary ruler
// insertion/removal between completed root or namespace declarations. Other
// comment changes and uncertain boundaries return nullopt. All token indices,
// roles, delimiter links, positions and trivia are rebuilt or rebased.
[[nodiscard]] std::optional<SyntaxTree> TryReparseCommentsAndIncludes(
    const SyntaxTree& original,
    util::PaddedSourceView input);

} // namespace parser
