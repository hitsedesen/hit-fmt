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

bool Configured(const NameRule& rule)
{
    return rule.Case.has_value() || rule.Prefixes || rule.Suffixes || !rule.Exemptions.empty();
}

std::string ExpectedName(const NameRule& rule, bool prefixOK, bool suffixOK, bool caseOK, bool acronymOK)
{
    std::string expected = "expected ";
    bool separator = false;
    const auto appendAffix = [&] (std::string_view label, const auto& affixes) {
        if (separator) {
            expected += " and ";
        }

        expected += label;
        expected += " ";

        for (size_t i = 0; i < affixes.size(); ++i) {
            if (i) {
                expected += i + 1 == affixes.size() ? " or " : ", ";
            }

            expected += "'" + affixes[i] + "'";
        }

        separator = true;
    };

    if (!prefixOK) {
        appendAffix("prefix", *rule.Prefixes);
    }

    if (!suffixOK) {
        appendAffix("suffix", *rule.Suffixes);
    }

    if (!caseOK) {
        if (separator) {
            expected += " and ";
        }

        expected += CaseLabel(rule.Case.value_or(NameCase::Unchecked));

        if (rule.Prefixes || rule.Suffixes) {
            expected += " between naming affixes";
        }

        separator = true;
    }

    if (!acronymOK) {
        if (separator) {
            expected += " and ";
        }

        expected += "configured acronym capitalization (two letters uppercase, longer words capitalized; "
            "initial camelCase words lowercase)";
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
        if (!(casing == NameCase::CamelCase ? Lower(name.front()) : Upper(name.front()))) {
            return false;
        }

        return std::ranges::all_of(
            name,
            [] (char c) {
                return Lower(c) || Upper(c) || Digit(c);
            });
    }

    if (name.front() == '_' || name.back() == '_' || name.find("__") != std::string_view::npos) {
        return false;
    }

    return std::ranges::all_of(
        name,
        [casing] (char c) {
            return c == '_' || Digit(c) || (casing == NameCase::SnakeCase ? Lower(c) : Upper(c));
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

enum class VariableScope
{
    Global,
    Local,
    Record
};

struct Context
{
    VariableScope Scope = VariableScope::Global;
    TK Access = TK::Keyword_public;
};

class Checker
{
public:
    Checker(const parser::SyntaxTree& tree, const Configuration& config)
        : Tree(tree)
        , Style(config.Naming)
        , ExemptNamespaces(config.NamingNamespaceExemptions)
        , Acronyms(config.NamingAcronyms)
    { }

    std::vector<NameDiagnostic> Run()
    {
        struct Frame
        {
            const parser::Node* Node;
            Context NamingContext;
            const parser::Node* NextChild = nullptr;
            size_t ScopeLength = 0;
            bool Entered = false;
            std::optional<TK> BranchAccess;
            bool HasElse = false;
        };

        std::vector<Frame> stack{{&Tree.Root(), {}}};

        while (!stack.empty()) {
            auto& frame = stack.back();
            const auto& node = *frame.Node;

            if (!frame.Entered) {
                if ((node.Kind == NK::FunctionDeclaration && !Configured(Style.Parameter)) ||
                    (node.Kind == NK::RecordDeclaration && !HasRecordBody(node)) ||
                    (node.Kind == NK::EnumDeclaration && !Child(node, NK::EnumeratorList)) ||
                    IsExplicitInstantiation(node))
                {
                    stack.pop_back();
                    continue;
                }

                frame.Entered = true;
                frame.ScopeLength = Scope.size();
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
                    Scope.resize(frame.ScopeLength);
                    stack.pop_back();
                    continue;
                }

                Visit(node, namespaceNames, frame.NamingContext);
                auto& context = frame.NamingContext;

                if (node.Kind == NK::RecordDeclaration) {
                    context = {
                        VariableScope::Record,
                        Tree.Kind(node.Tokens.Begin) == TK::Keyword_class ? TK::Keyword_private : TK::Keyword_public,
                    };
                } else if (node.Kind == NK::MacroRecordDeclaration) {
                    // A macro's default access is unknown until an explicit access label.
                    context = {VariableScope::Record, TK::Identifier};
                } else if (node.Kind == NK::FunctionDefinition || node.Kind == NK::MacroFunctionDefinition ||
                    node.Kind == NK::LambdaExpression || node.Kind == NK::RequiresExpression)
                {
                    context = {VariableScope::Local, TK::Keyword_public};
                } else if (node.Kind == NK::AccessSpecifier) {
                    context.Access = Tree.Kind(Next(node.Tokens.Begin, node.Tokens.End));
                }

                frame.NextChild = node.Child();
            }

            if (frame.NextChild) {
                const auto* child = frame.NextChild;
                frame.NextChild = child->NextSibling;
                stack.push_back({child, frame.NamingContext});
                continue;
            }

            auto access = frame.NamingContext.Access;

            if (node.Kind == NK::PreprocessingConditional && frame.BranchAccess) {
                // The absent else branch preserves incoming access. Divergent
                // branches cannot establish an access-specific naming rule.
                access = frame.HasElse || *frame.BranchAccess == access ? *frame.BranchAccess : TK::Identifier;
            }

            Scope.resize(frame.ScopeLength);
            stack.pop_back();

            if (!stack.empty()) {
                auto& parent = stack.back();

                if (node.Kind == NK::PreprocessingBranch && parent.Node->Kind == NK::PreprocessingConditional) {
                    parent.BranchAccess =
                        !parent.BranchAccess || *parent.BranchAccess == access ? access : TK::Identifier;
                    auto directive = Tree.TokenText(node.Tokens.Begin);
                    directive.remove_prefix(directive.starts_with("%:") ? 2 : 1);
                    const auto word = directive.find_first_not_of(" \t");
                    parent.HasElse |= word != std::string_view::npos && directive.substr(word).starts_with("else");
                } else if (node.Kind == NK::AccessSpecifier || node.Kind == NK::PreprocessingConditional) {
                    parent.NamingContext.Access = access;
                }
            }
        }

        std::ranges::sort(
            Results,
            [] (const auto& a, const auto& b) {
                return a.Position.Line < b.Position.Line ||
                    (a.Position.Line == b.Position.Line && a.Position.Column < b.Position.Column);
            });
        return std::move(Results);
    }

    void Check(Index index, const NameRule& rule, std::string_view category)
    {
        const auto casing = rule.Case.value_or(NameCase::Unchecked);

        if (casing == NameCase::Unchecked && !rule.Prefixes && !rule.Suffixes) {
            return;
        }

        if (index >= Tree.TokenCount() || Tree.Kind(index) != TK::Identifier) {
            return;
        }

        const auto name = Tree.TokenText(index);

        if (std::ranges::find(rule.Exemptions, name) != rule.Exemptions.end()) {
            return;
        }

        bool prefixOK = !rule.Prefixes;
        bool suffixOK = !rule.Suffixes;
        bool caseOK = false;
        bool acronymOK = false;
        bool accepted = false;
        const auto checkCore = [&] (std::string_view core) {
            const bool validCase = casing == NameCase::Unchecked || ValidCase(core, casing);
            const bool validAcronyms = ValidAcronyms(core, casing);

            if (validCase || !caseOK) {
                caseOK = validCase;
                acronymOK = validAcronyms;
            }

            accepted |= validCase && validAcronyms;
        };
        const auto checkSuffix = [&] (std::string_view remainder) {
            if (!rule.Suffixes) {
                checkCore(remainder);
                return;
            }

            for (const auto& suffix : *rule.Suffixes) {
                if (remainder.ends_with(suffix)) {
                    suffixOK = true;
                    checkCore(remainder.substr(0, remainder.size() - suffix.size()));
                }
            }
        };

        if (rule.Prefixes) {
            for (const auto& prefix : *rule.Prefixes) {
                if (name.starts_with(prefix)) {
                    prefixOK = true;
                    checkSuffix(name.substr(prefix.size()));
                }
            }
        } else {
            checkSuffix(name);
        }

        // Report affixes independently when no complete affix combination matched.
        if (!prefixOK || !suffixOK) {
            if (!prefixOK) {
                suffixOK = !rule.Suffixes || std::ranges::any_of(
                    *rule.Suffixes,
                    [&] (const auto& suffix) {
                        return name.ends_with(suffix);
                    });
            }

            caseOK = casing == NameCase::Unchecked;
            acronymOK = true;
        }

        if (accepted) {
            return;
        }

        std::string issue;
        const auto addIssue = [&] (std::string_view part) {
            if (!issue.empty()) {
                issue += " and ";
            }

            issue += part;
        };

        if (!prefixOK) {
            addIssue("prefix");
        }

        if (!suffixOK) {
            addIssue("suffix");
        }

        if (!caseOK) {
            addIssue("case");
        }

        if (!acronymOK) {
            addIssue("acronym capitalization");
        }

        std::string message(category);
        message += " name '";
        message += name;
        message += "' has invalid ";
        message += issue;
        Results.push_back({
            Tree.TokenPosition(index),
            std::move(message),
            issue,
            ExpectedName(rule, prefixOK, suffixOK, caseOK, acronymOK),
            {index, Tree.Token(index).Text.Begin},
        });
    }

private:
    bool ValidAcronyms(std::string_view name, NameCase casing) const
    {
        if (Acronyms.empty() || (casing != NameCase::CamelCase && casing != NameCase::PascalCase)) {
            return true;
        }

        size_t begin = 0;

        while (begin < name.size()) {
            if (!Upper(name[begin]) && !Lower(name[begin])) {
                ++begin;
                continue;
            }

            auto end = begin + 1;

            while (end < name.size() && (Upper(name[end]) || Lower(name[end]))) {
                if (Upper(name[end]) && (Lower(name[end - 1]) || (end + 1 < name.size() && Lower(name[end + 1])))) {
                    break;
                }

                ++end;
            }

            const auto word = name.substr(begin, end - begin);

            for (const auto& acronym : Acronyms) {
                if (word.size() != acronym.size()) {
                    continue;
                }

                const auto upper = [] (char c) {
                    return Lower(c) ? static_cast<char>(c - 'a' + 'A') : c;
                };

                if (!std::equal(
                    word.begin(),
                    word.end(),
                    acronym.begin(),
                    [&] (char a, char b) {
                        return upper(a) == upper(b);
                    }))
                {
                    continue;
                }

                const bool initialLowercase = begin == 0 && casing == NameCase::CamelCase;

                for (size_t i = 0; i < word.size(); ++i) {
                    const bool wantUpper = !initialLowercase && (word.size() == 2 || i == 0);

                    if (!(wantUpper ? Upper(word[i]) : Lower(word[i]))) {
                        return false;
                    }
                }
            }

            begin = end;
        }

        return true;
    }

    const NameRule& VariableRule(const parser::Node& node, Context context) const
    {
        if (node.Kind == NK::ParameterDeclaration) {
            return Configured(Style.Parameter) ? Style.Parameter : Style.Variable;
        }

        bool isStatic = false;

        if (const auto* specifiers = Child(node, NK::DeclarationSpecifiers)) {
            for (auto i = specifiers->Tokens.Begin; i < specifiers->Tokens.End; ++i) {
                isStatic |= Tree.Kind(i) == TK::Keyword_static;
            }
        }

        if (context.Scope == VariableScope::Record) {
            if (isStatic && Configured(Style.StaticMemberVariable)) {
                return Style.StaticMemberVariable;
            }

            const auto& accessRule = context.Access == TK::Keyword_private
                ? Style.PrivateMemberVariable
                : context.Access == TK::Keyword_protected
                    ? Style.ProtectedMemberVariable
                    : context.Access == TK::Keyword_public ? Style.PublicMemberVariable : Style.MemberVariable;

            if (Configured(accessRule)) {
                return accessRule;
            }

            if (Configured(Style.MemberVariable)) {
                return Style.MemberVariable;
            }
        } else if (context.Scope == VariableScope::Local) {
            if (isStatic && Configured(Style.StaticLocalVariable)) {
                return Style.StaticLocalVariable;
            }

            if (Configured(Style.LocalVariable)) {
                return Style.LocalVariable;
            }
        } else if (Configured(Style.GlobalVariable)) {
            return Style.GlobalVariable;
        }

        return Style.Variable;
    }

    Index Next(Index index, Index end) const
    {
        while (index < end && Tree.Kind(index) == TK::Comment) {
            ++index;
        }

        return index;
    }

    // A nested declarator may be a function pointer, not a function.
    void CheckDeclarators(const parser::Node& node, bool function, Context context)
    {
        // Constructors can have declaration specifiers such as explicit or
        // constexpr, but their name still belongs to the type.
        if (function &&
            (node.GetFunctionKind() == parser::FunctionKind::Constructor ||
                node.GetFunctionKind() == parser::FunctionKind::Destructor || !Child(node, NK::DeclarationSpecifiers)))
        {
            return;
        }

        const auto& variableRule = VariableRule(node, context);

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

            if (!function) {
                if (const auto* binding = Child(*declarator, NK::StructuredBinding)) {
                    for (auto* name = binding->Child(); name; name = name->NextSibling) {
                        if (name->Kind == NK::DeclaratorName) {
                            Check(name->Tokens.Begin, variableRule, "variable");
                        }
                    }

                    continue;
                }
            }

            auto* nested = Child(*declarator, NK::NestedDeclarator);

            // A parenthesized pointer to function is a variable if the
            // enclosing declaration was classified as a variable declaration.
            if (nested) {
                if (!function && Child(*nested, NK::PointerOperator)) {
                    if (auto* name = Child(*nested, NK::DeclaratorName); name && name->Tokens.Size() == 1) {
                        Check(name->Tokens.Begin, variableRule, "variable");
                    }
                }

                continue;
            }

            auto* name = Child(*declarator, NK::DeclaratorName);

            if (!name || name->Tokens.Size() != 1) {
                continue;
            }

            bool hasFunction = Child(*declarator, NK::FunctionDeclarator) != nullptr;

            if (function && hasFunction) {
                Check(name->Tokens.Begin, Style.Function, "function");
            }

            if (!function && !hasFunction) {
                Check(name->Tokens.Begin, variableRule, "variable");
            }
        }
    }

    bool HasRecordBody(const parser::Node& node) const
    {
        for (auto i = node.Tokens.Begin; i < node.Tokens.End; ++i) {
            if (Tree.Kind(i) == TK::Operator_OpenBrace && Tree.Role(i) == parser::PunctuationRole::RecordDelimiter) {
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

        if (node.Kind == NK::TemplateDeclaration && Tree.Kind(start) == TK::Keyword_template) {
            auto next = Next(start + 1, node.Tokens.End);
            return next < node.Tokens.End && Tree.Kind(next) != TK::Operator_Less;
        }

        // Extern explicit instantiations can be recovered as a general
        // declaration rather than a template-declaration.
        if (node.Kind == NK::Declaration && Tree.Kind(start) == TK::Keyword_extern) {
            auto next = Next(start + 1, node.Tokens.End);
            return next < node.Tokens.End && Tree.Kind(next) == TK::Keyword_template;
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
            if (Scope == name ||
                (Scope.starts_with(name) && Scope.size() > name.size() + 1 && Scope.compare(name.size(), 2, "::") == 0))
            {
                return true;
            }
        }

        return false;
    }

    void Visit(const parser::Node& node, const std::vector<Index>& namespaceNames, Context context)
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

                    if (after < end &&
                        (Tree.Kind(after) == TK::Operator_OpenBrace || Tree.Kind(after) == TK::Operator_Semicolon ||
                            Tree.Kind(after) == TK::Operator_Colon || Tree.TokenText(after) == "final"))
                    {
                        Check(name, *rule, category);
                    }
                }

                break;
            }
            case NK::EnumDeclaration: {
                auto name = Next(begin + 1, end);

                if (name < end && (Tree.Kind(name) == TK::Keyword_class || Tree.Kind(name) == TK::Keyword_struct)) {
                    name = Next(name + 1, end);
                }

                if (name < end && Tree.Kind(name) == TK::Identifier) {
                    auto after = Next(name + 1, end);

                    if (after < end &&
                        (Tree.Kind(after) == TK::Operator_OpenBrace || Tree.Kind(after) == TK::Operator_Semicolon ||
                            Tree.Kind(after) == TK::Operator_Colon))
                    {
                        Check(name, Style.Enum, "enum");
                    }
                }

                break;
            }
            case NK::EnumeratorName:
                Check(begin, Style.Enumerator, "enumerator");
                break;
            case NK::MacroEnumHeader: {
                auto opening = Next(begin + 1, end);

                if (opening < end && Tree.Kind(opening) == TK::Operator_OpenParenthesis) {
                    Check(Next(opening + 1, end), Style.Enum, "enum");
                }

                break;
            }
            case NK::MacroEnumEntry: {
                auto name = Next(begin + 1, end);

                if (name < end && Tree.Kind(name) == TK::Operator_OpenParenthesis) {
                    name = Next(name + 1, end);
                }

                Check(name, Style.Enumerator, "enumerator");
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
                CheckDeclarators(node, true, context);
                break;
            case NK::Declaration:
            case NK::DeclarationStatement:
            case NK::ParameterDeclaration:
            case NK::RangeForDeclaration:
                CheckDeclarators(node, false, context);
                break;
            default:
                break;
        }
    }

    const parser::SyntaxTree& Tree;
    const NamingStyle& Style;
    const std::vector<std::string>& ExemptNamespaces;
    const std::vector<std::string>& Acronyms;
    std::vector<NameDiagnostic> Results;
    std::string Scope;
};

} // namespace

std::vector<NameDiagnostic> CheckNames(const parser::SyntaxTree& tree, const Configuration& config)
{
    bool enabled = false;
#define NAMING_ENABLED(category) \
    enabled |= config.Naming.category.Case.value_or(NameCase::Unchecked) != NameCase::Unchecked || \
        config.Naming.category.Prefixes.has_value() || config.Naming.category.Suffixes.has_value();
    FORMATTER_NAMING_CATEGORIES(NAMING_ENABLED)
#undef NAMING_ENABLED
    if (!enabled) {
        return {};
    }
    return Checker(tree, config).Run();
}

} // namespace formatter
