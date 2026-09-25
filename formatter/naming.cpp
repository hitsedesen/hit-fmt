#include "naming.hpp"

#include <tokenizer/token.hpp>

#include <algorithm>
#include <string_view>
#include <vector>

namespace formatter {
namespace {

using NK = parser::NodeKind;
using TK = tokenizer::TokenKind;
using Index = parser::TokenIndex;

bool Upper(char c) { return c >= 'A' && c <= 'Z'; }
bool Lower(char c) { return c >= 'a' && c <= 'z'; }
bool Digit(char c) { return c >= '0' && c <= '9'; }

bool ValidCase(std::string_view name, NameCase casing)
{
    if (name.empty()) return false;
    if (casing == NameCase::Unchecked) return true;
    if (casing == NameCase::CamelCase || casing == NameCase::PascalCase) {
        if (!(casing == NameCase::CamelCase ? Lower(name.front())
                                          : Upper(name.front()))) return false;
        return std::ranges::all_of(name, [] (char c) {
            return Lower(c) || Upper(c) || Digit(c);
        });
    }
    if (name.front() == '_' || name.back() == '_' || name.contains("__"))
        return false;
    return std::ranges::all_of(name, [casing] (char c) {
        return c == '_' || Digit(c) ||
            (casing == NameCase::SnakeCase ? Lower(c) : Upper(c));
    });
}

const parser::Node* Child(const parser::Node& node, NK kind)
{
    for (auto* child = node.Child(); child; child = child->NextSibling)
        if (child->Kind == kind) return child;
    return nullptr;
}

class Checker
{
public:
    Checker(const parser::SyntaxTree& tree, const NamingStyle& style)
        : Tree(tree), Style(style) { }

    std::vector<NameDiagnostic> Run()
    {
        std::vector<const parser::Node*> stack{&Tree.Root()};
        while (!stack.empty()) {
            auto* node = stack.back();
            stack.pop_back();
            Visit(*node);
            for (auto* child = node->Child(); child; child = child->NextSibling)
                stack.push_back(child);
        }
        std::ranges::sort(Results, [] (const auto& a, const auto& b) {
            return a.Position.Line < b.Position.Line ||
                (a.Position.Line == b.Position.Line &&
                    a.Position.Column < b.Position.Column);
        });
        return std::move(Results);
    }

    void Check(Index index, const NameRule& rule, std::string_view category)
    {
        if (rule.Case == NameCase::Unchecked && !rule.Prefixes) return;
        if (index >= Tree.TokenCount() || Tree.Kind(index) != TK::Identifier)
            return;
        auto name = Tree.TokenText(index);
        bool prefixOK = !rule.Prefixes;
        bool caseOK = rule.Case == NameCase::Unchecked;
        if (rule.Prefixes) {
            for (const auto& prefix : *rule.Prefixes) {
                if (!name.starts_with(prefix)) continue;
                prefixOK = true;
                caseOK |= ValidCase(name.substr(prefix.size()), rule.Case);
            }
        } else {
            caseOK = ValidCase(name, rule.Case);
        }
        if (!prefixOK || !caseOK) {
            std::string message(category);
            message += " name '";
            message += name;
            message += "' has invalid ";
            message += !prefixOK && !caseOK ? "prefix and case" :
                !prefixOK ? "prefix" : "case";
            Results.push_back({Tree.TokenPosition(index), std::move(message)});
        }
    }

private:
    Index Next(Index index, Index end) const
    {
        while (index < end && Tree.Kind(index) == TK::Comment) ++index;
        return index;
    }

    // A nested declarator may be a function pointer, not a function.
    void CheckDeclarators(const parser::Node& node, bool function)
    {
        // Constructors, destructors and conversion functions have no return
        // type. Their spelling follows the type/operator rather than the
        // ordinary function naming policy.
        if (function && !Child(node, NK::DeclarationSpecifiers)) return;
        auto* list = Child(node, NK::InitDeclaratorList);
        if (!list) return;
        for (auto* init = list->Child(); init; init = init->NextSibling) {
            if (init->Kind != NK::InitDeclarator) continue;
            auto* declarator = Child(*init, NK::Declarator);
            if (!declarator) continue;
            auto* nested = Child(*declarator, NK::NestedDeclarator);
            // A parenthesized pointer to function is a variable if the
            // enclosing declaration was classified as a variable declaration.
            if (nested) {
                if (!function && Child(*nested, NK::PointerOperator)) {
                    if (auto* name = Child(*nested, NK::DeclaratorName);
                        name && name->Tokens.Size() == 1)
                        Check(name->Tokens.Begin, Style.Variable, "variable");
                }
                continue;
            }
            auto* name = Child(*declarator, NK::DeclaratorName);
            if (!name || name->Tokens.Size() != 1) continue;
            bool hasFunction = Child(*declarator, NK::FunctionDeclarator) != nullptr;
            if (function && hasFunction)
                Check(name->Tokens.Begin, Style.Function, "function");
            if (!function && !hasFunction)
                Check(name->Tokens.Begin, Style.Variable, "variable");
        }
    }

    void Visit(const parser::Node& node)
    {
        auto begin = node.Tokens.Begin;
        auto end = node.Tokens.End;
        if (begin >= end || end > Tree.TokenCount()) return;
        switch (node.Kind) {
        case NK::RecordDeclaration: {
            const NameRule* rule = nullptr;
            std::string_view category;
            switch (Tree.Kind(begin)) {
            case TK::Keyword_class: rule = &Style.Class; category = "class"; break;
            case TK::Keyword_struct: rule = &Style.Struct; category = "struct"; break;
            case TK::Keyword_union: rule = &Style.Union; category = "union"; break;
            default: return;
            }
            auto name = Next(begin + 1, end);
            // Skip attributes, elaborated uses, explicit specializations and
            // qualified names rather than guessing which token is declared.
            if (name < end && Tree.Kind(name) == TK::Identifier) {
                auto after = Next(name + 1, end);
                if (after < end && (Tree.Kind(after) == TK::Operator_OpenBrace ||
                    Tree.Kind(after) == TK::Operator_Semicolon ||
                    Tree.Kind(after) == TK::Operator_Colon ||
                    Tree.TokenText(after) == "final"))
                    Check(name, *rule, category);
            }
            break;
        }
        case NK::EnumDeclaration: {
            auto name = Next(begin + 1, end);
            if (name < end && (Tree.Kind(name) == TK::Keyword_class ||
                Tree.Kind(name) == TK::Keyword_struct)) name = Next(name + 1, end);
            if (name < end && Tree.Kind(name) == TK::Identifier) {
                auto after = Next(name + 1, end);
                if (after < end && (Tree.Kind(after) == TK::Operator_OpenBrace ||
                    Tree.Kind(after) == TK::Operator_Semicolon ||
                    Tree.Kind(after) == TK::Operator_Colon))
                    Check(name, Style.Enum, "enum");
            }
            break;
        }
        case NK::NamespaceDeclaration: {
            auto name = Next(begin + 1, end);
            // 'inline namespace' begins at namespace; namespace alias is not
            // a namespace declaration with a body.
            if (name >= end || Tree.Kind(name) != TK::Identifier) break;
            while (name < end && Tree.Kind(name) == TK::Identifier) {
                auto after = Next(name + 1, end);
                if (after >= end) break;
                if (Tree.Kind(after) == TK::Operator_OpenBrace) {
                    Check(name, Style.Namespace, "namespace");
                    break;
                }
                if (Tree.TokenText(after) != "::") break;
                auto following = Next(after + 1, end);
                if (following >= end || Tree.Kind(following) != TK::Identifier)
                    break;
                Check(name, Style.Namespace, "namespace");
                name = following;
            }
            break;
        }
        case NK::ConceptDeclaration:
            if (auto* name = Child(node, NK::ConceptName))
                Check(name->Tokens.Begin, Style.Concept, "concept");
            break;
        case NK::FunctionDefinition:
        case NK::FunctionDeclaration:
            CheckDeclarators(node, true);
            break;
        case NK::Declaration:
        case NK::DeclarationStatement:
        case NK::ParameterDeclaration:
        case NK::RangeForDeclaration:
            CheckDeclarators(node, false);
            break;
        default: break;
        }
    }

    const parser::SyntaxTree& Tree;
    const NamingStyle& Style;
    std::vector<NameDiagnostic> Results;
};

} // namespace

std::vector<NameDiagnostic> CheckNames(
    const parser::SyntaxTree& tree, const NamingStyle& style)
{
    auto enabled = [] (const NameRule& rule) {
        return rule.Case != NameCase::Unchecked || rule.Prefixes.has_value();
    };
    if (!enabled(style.Class) && !enabled(style.Enum) &&
        !enabled(style.Union) && !enabled(style.Struct) &&
        !enabled(style.Function) && !enabled(style.Concept) &&
        !enabled(style.Variable) && !enabled(style.Namespace)) return {};
    return Checker(tree, style).Run();
}

} // namespace formatter
