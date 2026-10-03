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

bool Upper(char c)
{
    return c >= 'A' && c <= 'Z';
}

bool Lower(char c)
{
    return c >= 'a' && c <= 'z';
}

bool Digit(char c)
{
    return c >= '0' && c <= '9';
}

std::string_view CaseLabel(NameCase casing)
{
    switch (casing) {
        case NameCase::CamelCase:
            return "camelCase";
        case NameCase::PascalCase:
            return "PascalCase";
        case NameCase::SnakeCase:
            return "snake_case";
        case NameCase::ScreamingSnakeCase:
            return "SCREAMING_SNAKE_CASE";
        case NameCase::Unchecked:
            return "";
    }
    return "";
}

std::string ExpectedName(const NameRule& rule, bool prefixOK, bool caseOK)
{
    std::string expected = "expected ";
    if (!prefixOK && rule.Prefixes) {
        expected += "prefix ";
        for (size_t i = 0; i < rule.Prefixes->size(); ++i) {
            if (i) {
                expected += i + 1 == rule.Prefixes->size() ? " or " : ", ";
            }
            expected += "'" + (*rule.Prefixes)[i] + "'";
        }
    }
    if (!caseOK && rule.Case != NameCase::Unchecked) {
        if (!prefixOK) {
            expected += " and ";
        }
        expected += CaseLabel(rule.Case);
        if (rule.Prefixes) {
            expected += " after the prefix";
        }
    }
    return expected;
}

bool ValidCase(std::string_view name, NameCase casing)
{
    if (name.empty()) {
        return false;
    }
    if (casing == NameCase::Unchecked) {
        return true;
    }
    if (casing == NameCase::CamelCase || casing == NameCase::PascalCase) {
        if (
            !(casing == NameCase::CamelCase
                    ? Lower(name.front())
                    : Upper(name.front())))
        {
            return false;
        }
        return std::ranges::all_of(
            name,
            [] (char c) {
                return Lower(c) || Upper(c) || Digit(c);
            });
    }
    if (
        name.front() == '_' || name.back() == '_' ||
        name.find("__") != std::string_view::npos)
    {
        return false;
    }
    return std::ranges::all_of(
        name,
        [casing] (char c) {
            return c == '_' || Digit(c) ||
                (casing == NameCase::SnakeCase ? Lower(c) : Upper(c));
        });
}

const parser::Node* Child(const parser::Node& node, NK kind)
{
    for (auto* child = node.Child(); child; child = child->NextSibling) {
        if (child->Kind == kind) {
            return child;
        }
    }
    return nullptr;
}

class Checker
{
public:
    Checker(const parser::SyntaxTree& tree, const Configuration& config)
        : Tree(tree)
        , Style(config.Naming)
        , ExemptNamespaces(config.NamingNamespaceExemptions)
    { }

    std::vector<NameDiagnostic> Run()
    {
        struct Frame
        {
            const parser::Node* Node;
            size_t ScopeLength;
            bool Exit;
        };
        std::vector<Frame> stack{{&Tree.Root(), 0, false}};
        while (!stack.empty()) {
            auto frame = stack.back();
            stack.pop_back();
            if (frame.Exit) {
                Scope.resize(frame.ScopeLength);
                continue;
            }
            const auto& node = *frame.Node;
            if (
                node.Kind == NK::FunctionDeclaration ||
                (node.Kind == NK::RecordDeclaration && !HasRecordBody(node)) ||
                (node.Kind == NK::EnumDeclaration &&
                        !Child(node, NK::EnumeratorList)) ||
                IsExplicitInstantiation(node))
            {
                continue;
            }
            auto parentLength = Scope.size();
            auto namespaceNames = node.Kind == NK::NamespaceDeclaration
                ? NamespaceNames(node)
                : std::vector<Index>{};
            for (auto name : namespaceNames) {
                if (!Scope.empty()) {
                    Scope += "::";
                }
                Scope += Tree.TokenText(name);
            }
            if (IsExempt()) {
                Scope.resize(parentLength);
                continue;
            }
            Visit(node, namespaceNames);
            stack.push_back({&node, parentLength, true});
            for (auto* child = node.Child(); child; child = child->NextSibling)
            {
                stack.push_back({child, 0, false});
            }
        }
        std::ranges::sort(
            Results,
            [] (const auto& a, const auto& b) {
                return a.Position.Line < b.Position.Line ||
                    (a.Position.Line == b.Position.Line &&
                            a.Position.Column < b.Position.Column);
            });
        return std::move(Results);
    }

    void Check(Index index, const NameRule& rule, std::string_view category)
    {
        if (rule.Case == NameCase::Unchecked && !rule.Prefixes) {
            return;
        }
        if (index >= Tree.TokenCount() || Tree.Kind(index) != TK::Identifier) {
            return;
        }
        auto name = Tree.TokenText(index);
        bool prefixOK = !rule.Prefixes;
        bool caseOK = rule.Case == NameCase::Unchecked;
        if (rule.Prefixes) {
            for (const auto& prefix : *rule.Prefixes) {
                if (!name.starts_with(prefix)) {
                    continue;
                }
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
            auto issue =
                !prefixOK && !caseOK
                    ? "prefix and case"
                    : !prefixOK ? "prefix" : "case";
            message += issue;
            Results
                .push_back(
                    {
                        Tree.TokenPosition(index),
                        std::move(message),
                        issue,
                        ExpectedName(rule, prefixOK, caseOK),
                    });
        }
    }

private:
    Index Next(Index index, Index end) const
    {
        while (index < end && Tree.Kind(index) == TK::Comment) {
            ++index;
        }
        return index;
    }

    // A nested declarator may be a function pointer, not a function.
    void CheckDeclarators(const parser::Node& node, bool function)
    {
        // Constructors, destructors and conversion functions have no return
        // type. Their spelling follows the type/operator rather than the
        // ordinary function naming policy.
        if (function && !Child(node, NK::DeclarationSpecifiers)) {
            return;
        }
        auto* list = Child(node, NK::InitDeclaratorList);
        if (!list) {
            return;
        }
        for (auto* init = list->Child(); init; init = init->NextSibling) {
            if (init->Kind != NK::InitDeclarator) {
                continue;
            }
            auto* declarator = Child(*init, NK::Declarator);
            if (!declarator) {
                continue;
            }
            auto* nested = Child(*declarator, NK::NestedDeclarator);
            // A parenthesized pointer to function is a variable if the
            // enclosing declaration was classified as a variable declaration.
            if (nested) {
                if (!function && Child(*nested, NK::PointerOperator)) {
                    if (auto* name = Child(*nested, NK::DeclaratorName);
                        name && name->Tokens.Size() == 1)
                    {
                        Check(name->Tokens.Begin, Style.Variable, "variable");
                    }
                }
                continue;
            }
            auto* name = Child(*declarator, NK::DeclaratorName);
            if (!name || name->Tokens.Size() != 1) {
                continue;
            }
            bool hasFunction =
                Child(*declarator, NK::FunctionDeclarator) != nullptr;
            if (function && hasFunction) {
                Check(name->Tokens.Begin, Style.Function, "function");
            }
            if (!function && !hasFunction) {
                Check(name->Tokens.Begin, Style.Variable, "variable");
            }
        }
    }

    bool HasRecordBody(const parser::Node& node) const
    {
        for (auto i = node.Tokens.Begin; i < node.Tokens.End; ++i) {
            if (
                Tree.Kind(i) == TK::Operator_OpenBrace &&
                Tree.Role(i) == parser::PunctuationRole::RecordDelimiter)
            {
                return true;
            }
        }
        return false;
    }

    bool IsExplicitInstantiation(const parser::Node& node) const
    {
        auto start = node.Tokens.Begin;
        if (start >= node.Tokens.End) {
            return false;
        }
        if (
            node.Kind == NK::TemplateDeclaration &&
            Tree.Kind(start) == TK::Keyword_template)
        {
            auto next = Next(start + 1, node.Tokens.End);
            return next < node.Tokens.End &&
                Tree.Kind(next) != TK::Operator_Less;
        }
        // Extern explicit instantiations can be recovered as a general
        // declaration rather than a template-declaration.
        if (
            node.Kind == NK::Declaration &&
            Tree.Kind(start) == TK::Keyword_extern)
        {
            auto next = Next(start + 1, node.Tokens.End);
            return next < node.Tokens.End &&
                Tree.Kind(next) == TK::Keyword_template;
        }
        return false;
    }

    std::vector<Index> NamespaceNames(const parser::Node& node) const
    {
        std::vector<Index> names;
        auto end = node.Tokens.End;
        auto name = Next(node.Tokens.Begin + 1, end);
        while (name < end && Tree.Kind(name) == TK::Identifier) {
            names.push_back(name);
            auto after = Next(name + 1, end);
            if (after >= end) {
                break;
            }
            if (Tree.Kind(after) == TK::Operator_OpenBrace) {
                return names;
            }
            if (Tree.TokenText(after) != "::") {
                break;
            }
            name = Next(after + 1, end);
        }
        // Aliases and incomplete declarations have no namespace body.
        return {};
    }

    bool IsExempt() const
    {
        for (const auto& name : ExemptNamespaces) {
            if (
                Scope == name ||
                (Scope.starts_with(name) && Scope.size() > name.size() + 1 &&
                        Scope.compare(name.size(), 2, "::") == 0))
            {
                return true;
            }
        }
        return false;
    }

    void Visit(
        const parser::Node& node,
        const std::vector<Index>& namespaceNames)
    {
        auto begin = node.Tokens.Begin;
        auto end = node.Tokens.End;
        if (begin >= end || end > Tree.TokenCount()) {
            return;
        }
        switch (node.Kind) {
            case NK::RecordDeclaration: {
                const NameRule* rule = nullptr;
                std::string_view category;
                switch (Tree.Kind(begin)) {
                    case TK::Keyword_class:
                        rule = &Style.Class;
                        category = "class";
                        break;
                    case TK::Keyword_struct:
                        rule = &Style.Struct;
                        category = "struct";
                        break;
                    case TK::Keyword_union:
                        rule = &Style.Union;
                        category = "union";
                        break;
                    default:
                        return;
                }
                auto name = Next(begin + 1, end);
                // Skip attributes, elaborated uses and explicit
                // specializations, as well as qualified names, rather than
                // guessing which token is declared.
                if (name < end && Tree.Kind(name) == TK::Identifier) {
                    auto after = Next(name + 1, end);
                    if (
                        after < end &&
                        (Tree.Kind(after) == TK::Operator_OpenBrace ||
                                Tree.Kind(after) == TK::Operator_Semicolon ||
                                Tree.Kind(after) == TK::Operator_Colon ||
                                Tree.TokenText(after) == "final"))
                    {
                        Check(name, *rule, category);
                    }
                }
                break;
            }
            case NK::EnumDeclaration: {
                auto name = Next(begin + 1, end);
                if (
                    name < end &&
                    (Tree.Kind(name) == TK::Keyword_class ||
                            Tree.Kind(name) == TK::Keyword_struct))
                {
                    name = Next(name + 1, end);
                }
                if (name < end && Tree.Kind(name) == TK::Identifier) {
                    auto after = Next(name + 1, end);
                    if (
                        after < end &&
                        (Tree.Kind(after) == TK::Operator_OpenBrace ||
                                Tree.Kind(after) == TK::Operator_Semicolon ||
                                Tree.Kind(after) == TK::Operator_Colon))
                    {
                        Check(name, Style.Enum, "enum");
                    }
                }
                break;
            }
            case NK::NamespaceDeclaration:
                for (auto name : namespaceNames) {
                    Check(name, Style.Namespace, "namespace");
                }
                break;
            case NK::ConceptDeclaration:
                if (auto* name = Child(node, NK::ConceptName)) {
                    Check(name->Tokens.Begin, Style.Concept, "concept");
                }
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
            default:
                break;
        }
    }

    const parser::SyntaxTree& Tree;
    const NamingStyle& Style;
    const std::vector<std::string>& ExemptNamespaces;
    std::vector<NameDiagnostic> Results;
    std::string Scope;
};

} // namespace

std::vector<NameDiagnostic> CheckNames(
    const parser::SyntaxTree& tree,
    const Configuration& config)
{
    const auto& style = config.Naming;
    auto enabled = [] (const NameRule& rule) {
        return rule.Case != NameCase::Unchecked || rule.Prefixes.has_value();
    };
    if (
        !enabled(style.Class) && !enabled(style.Enum) &&
        !enabled(style.Union) && !enabled(style.Struct) &&
        !enabled(style.Function) && !enabled(style.Concept) &&
        !enabled(style.Variable) && !enabled(style.Namespace))
    {
        return {};
    }
    return Checker(tree, config).Run();
}

} // namespace formatter
