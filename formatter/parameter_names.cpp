#include "parameter_names.hpp"

#include <tokenizer/token.hpp>

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace formatter {

namespace {

using NK = parser::NodeKind;

using TK = tokenizer::TokenKind;

using Node = parser::Node;

const Node* Child(const Node& node, NK kind)
{
    for (auto* child = node.Child(); child; child = child->NextSibling) {
        if (child->Kind == kind) {
            return child;
        }
    }
    return nullptr;
}

bool UnsafeSyntax(const Node& node)
{
    switch (node.Kind) {
        case NK::AmbiguousStatement:
        case NK::UnknownTypeName:
        case NK::UnparsedSyntax:
        case NK::MissingToken:
        case NK::UnexpectedToken:
        case NK::Error:
            return true;
        default:
            break;
    }
    for (auto* child = node.Child(); child; child = child->NextSibling) {
        if (UnsafeSyntax(*child)) {
            return true;
        }
    }
    return false;
}

// Do not mistake a name in the parameter list of a function-pointer type for
// the name of the enclosing parameter.
bool HasName(const Node& node)
{
    if (node.Kind == NK::DeclaratorName) {
        return true;
    }
    if (node.Kind == NK::FunctionDeclarator) {
        return false;
    }
    for (auto* child = node.Child(); child; child = child->NextSibling) {
        if (HasName(*child)) {
            return true;
        }
    }
    return false;
}

const Node* FunctionName(const Node& node)
{
    if (node.Kind == NK::DeclaratorName || node.Kind == NK::DestructorName ||
        node.Kind == NK::OperatorFunctionName)
    {
        return &node;
    }
    if (node.Kind == NK::FunctionDeclarator) {
        return nullptr;
    }
    for (auto* child = node.Child(); child; child = child->NextSibling) {
        if (auto* name = FunctionName(*child)) {
            return name;
        }
    }
    return nullptr;
}

util::Position AfterToken(
    const parser::SyntaxTree& tree,
    parser::TokenIndex index)
{
    auto position = tree.TokenPosition(index);
    for (unsigned char c : tree.TokenText(index)) {
        if (c == '\n') {
            ++position.Line;
            position.Column = 1;
        } else if ((c & 0xc0) != 0x80) {
            ++position.Column;
        }
    }
    return position;
}

util::Position NamePosition(
    const parser::SyntaxTree& tree,
    const Node& parameter,
    const Node& specifiers)
{
    if (auto* declarators = Child(parameter, NK::InitDeclaratorList)) {
        if (auto* init = Child(*declarators, NK::InitDeclarator)) {
            if (auto* abstract = Child(*init, NK::AbstractDeclarator)) {
                if (auto* nested = Child(*abstract, NK::NestedDeclarator)) {
                    // In a function pointer, the name goes inside (*...).
                    return tree.TokenPosition(nested->Tokens.End - 1);
                }
                for (auto* child = abstract->Child();
                    child;
                    child = child->NextSibling)
                {
                    if (child->Kind == NK::ArrayDeclarator ||
                        child->Kind == NK::FunctionDeclarator)
                    {
                        return tree.TokenPosition(child->Tokens.Begin);
                    }
                }
                if (!abstract->Tokens.Empty()) {
                    return AfterToken(tree, abstract->Tokens.End - 1);
                }
            }
        }
    }
    auto last = specifiers.Tokens.End;
    while (last > specifiers.Tokens.Begin && tree.Kind(last - 1) == TK::Comment)
    {
        --last;
    }
    return AfterToken(tree, last - 1);
}

bool IsCommentedName(std::string_view text)
{
    if (text.starts_with("/*") && text.ends_with("*/")) {
        text.remove_prefix(2);
        text.remove_suffix(2);
    } else if (text.starts_with("//")) {
        text.remove_prefix(2);
    } else {
        return false;
    }
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) {
        text.remove_suffix(1);
    }
    if (text.empty()) {
        return false;
    }
    auto identifier = [] (char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '_';
    };
    if (text.front() >= '0' && text.front() <= '9') {
        return false;
    }
    for (char c : text) {
        if (!identifier(c)) {
            return false;
        }
    }
    return true;
}

void CheckFunction(
    const parser::SyntaxTree& tree,
    const Node& function,
    FunctionDefinitionParameterNames definitionPolicy,
    std::vector<ParameterNameDiagnostic>& diagnostics)
{
    auto* list = Child(function, NK::InitDeclaratorList);
    if (!list) {
        return;
    }
    for (auto* init = list->Child(); init; init = init->NextSibling) {
        if (init->Kind != NK::InitDeclarator) {
            continue;
        }
        auto* declarator = Child(*init, NK::Declarator);
        auto* signature = declarator
            ? Child(*declarator, NK::FunctionDeclarator)
            : nullptr;
        auto* parameters = signature
            ? Child(*signature, NK::ParameterList)
            : nullptr;
        if (!parameters || UnsafeSyntax(*declarator)) {
            continue;
        }
        std::string functionName = function.Kind == NK::FunctionDefinition
            ? "function definition" : "function declaration";
        if (auto* name = FunctionName(*declarator)) {
            functionName +=
                " '" + std::string(tree.SourceText(name->Tokens)) + "'";
        }
        const auto first = tree.Token(function.Tokens.Begin);
        const auto last = tree.Token(signature->Tokens.End - 1);
        const parser::SourceRange signatureRange{
            first.Text.Begin,
            last.Text.End,
        };
        const auto beginLine = tree.TokenPosition(function.Tokens.Begin).Line;
        const auto endLine = tree.TokenPosition(signature->Tokens.End - 1).Line;
        size_t number = 0;
        for (auto* parameter = parameters->Child();
            parameter;
            parameter = parameter->NextSibling)
        {
            if (parameter->Kind != NK::ParameterDeclaration) {
                continue;
            }
            ++number;
            if (parameter->Tokens.Empty() || UnsafeSyntax(*parameter)) {
                continue;
            }
            auto* specifiers = Child(*parameter, NK::DeclarationSpecifiers);
            if (!specifiers) {
                continue;
            }
            auto begin = parameter->Tokens.Begin;
            auto end = parameter->Tokens.End;
            // A lone identifier may denote either a type or a parameter;
            // void denotes an empty C-style parameter list.
            auto significant = begin;
            for (auto i = begin; i < end; ++i) {
                significant += tree.Kind(i) != TK::Comment;
            }
            if (significant == begin + 1 &&
                (tree.Kind(begin) == TK::Identifier ||
                    tree.Kind(begin) == TK::Keyword_void))
            {
                continue;
            }
            bool unsafe = false;
            for (auto i = begin; i < end; ++i) {
                if (tree.Kind(i) == TK::PreprocessingDirective ||
                    tree.Kind(i) == TK::Operator_Ellipsis)
                {
                    unsafe = true;
                    break;
                }
            }
            if (unsafe || HasName(*parameter)) {
                continue;
            }
            const bool allowCommentedName = definitionPolicy ==
                FunctionDefinitionParameterNames::
                    NameOrCommentedNameRequired;
            if (function.Kind == NK::FunctionDefinition && allowCommentedName) {
                // A comment naming an unused parameter must end the type,
                // not be a note inside it or inside a default expression.
                auto last = end;
                while (last > begin && tree.Kind(last - 1) == TK::Comment) {
                    --last;
                }
                bool hasDefault = false;
                for (auto i = begin; i < last; ++i) {
                    hasDefault |= tree.Kind(i) == TK::Operator_Assign;
                }
                if (!hasDefault && last < end &&
                    IsCommentedName(tree.TokenText(end - 1)))
                {
                    continue;
                }
            }
            diagnostics.push_back({
                .Position = NamePosition(tree, *parameter, *specifiers),
                .Signature = signatureRange,
                .SignatureBeginLine = beginLine,
                .SignatureEndLine = endLine,
                .Function = functionName,
                .ParameterNumber = number,
                .AllowCommentedName = function.Kind == NK::FunctionDefinition &&
                    definitionPolicy == FunctionDefinitionParameterNames::
                        NameOrCommentedNameRequired,
            });
        }
    }
}

} // namespace

std::vector<ParameterNameDiagnostic> CheckParameterNames(
    const parser::SyntaxTree& tree,
    const Configuration& config)
{
    std::vector<ParameterNameDiagnostic> diagnostics;
    std::vector<const Node*> stack{&tree.Root()};
    while (!stack.empty()) {
        auto* node = stack.back();
        stack.pop_back();
        if ((node->Kind == NK::FunctionDeclaration &&
            config.FunctionDeclarationParameterNames ==
                FunctionDeclarationParameterNames::Required) ||
            (node->Kind == NK::FunctionDefinition &&
                config.FunctionDefinitionParameterNames !=
                    FunctionDefinitionParameterNames::Unchecked))
        {
            CheckFunction(
                tree,
                *node,
                config.FunctionDefinitionParameterNames,
                diagnostics);
        }
        for (auto* child = node->Child(); child; child = child->NextSibling) {
            stack.push_back(child);
        }
    }
    std::ranges::sort(
        diagnostics,
        [] (const auto& a, const auto& b) {
            return a.Position.Line < b.Position.Line ||
                (a.Position.Line == b.Position.Line &&
                    a.Position.Column < b.Position.Column);
        });
    return diagnostics;
}

} // namespace formatter
