#include "declaration_kind.hpp"
#include "text_policies.hpp"

#include <algorithm>

namespace formatter {

void PlanRulerEdits(
    const parser::SyntaxTree& tree,
    const SourceAnalysis& safety,
    StructuralOptions options,
    StructuralPlan& plan,
    std::pmr::memory_resource& fileMemory,
    std::pmr::memory_resource& scratchMemory)
{
    if (options.Rulers == RulerPlacement::Preserve) {
        return;
    }

    using NK = parser::NodeKind;
    using TK = tokenizer::TokenKind;
    using TI = parser::TokenIndex;
    enum class Scope : uint8_t
    {
        None,
        Global,
        Namespace,
        Body
    };

    const auto regions = safety.Regions();
    std::pmr::vector<uint8_t> independent(regions.size(), 0, &scratchMemory);
    std::pmr::vector<Scope> scopes(regions.size(), Scope::None, &scratchMemory);
    std::pmr::vector<uint8_t> removed(tree.TokenCount(), 0, &scratchMemory);
    for (size_t i = regions.size(); i-- > 0;) {
        const auto& node = regions[i];
        const bool namespaceBody = node.Kind == NK::NamespaceDeclaration && !node.Tokens.Empty() &&
            tree.Kind(node.Tokens.End - 1) == TK::Operator_CloseBrace;
        independent[i] = node.Kind == NK::RecordDeclaration || node.Kind == NK::MacroRecordDeclaration ||
            node.Kind == NK::EnumDeclaration || node.Kind == NK::MacroEnumDeclaration || namespaceBody ||
            node.Kind == NK::LinkageSpecification;

        if (node.Kind == NK::TemplateDeclaration || node.Kind == NK::ExportDeclaration) {
            for (auto child = node.FirstChild; child != NoRegion; child = regions[child].NextSibling) {
                independent[i] |= independent[child];
            }
        }
    }

    auto standalone = [&] (TI token) {
        return tree.Kind(token) == TK::Comment && IsRuler(tree.TokenText(token)) &&
            (token == 0 || safety.Gaps()[token].HasLineFeed) &&
            (token + 1 == tree.TokenCount() || safety.Gaps()[token + 1].HasLineFeed);
    };

    for (size_t i = 0; i < regions.size(); ++i) {
        const auto& node = regions[i];
        const auto parent = node.Parent == NoRegion ? Scope::Global : scopes[node.Parent];
        const bool declarationScope = (parent == Scope::Global || parent == Scope::Namespace) &&
            (node.Kind == NK::TranslationUnit || node.Kind == NK::NamespaceDeclaration ||
                node.Kind == NK::PreprocessingConditional || node.Kind == NK::PreprocessingBranch ||
                node.Kind == NK::ExportDeclaration || node.Kind == NK::LinkageSpecification);
        const bool body = parent == Scope::Body || node.Kind == NK::RecordDeclaration ||
            node.Kind == NK::MacroRecordDeclaration || node.Kind == NK::EnumDeclaration ||
            node.Kind == NK::MacroEnumDeclaration || node.Kind == NK::FunctionDefinition ||
            node.Kind == NK::MacroFunctionDefinition || node.Kind == NK::CompoundStatement ||
            node.Kind == NK::LambdaExpression || node.Kind == NK::LambdaBody || node.Kind == NK::RequirementBody;
        scopes[i] = body
            ? Scope::Body
            : declarationScope ? (node.Kind == NK::NamespaceDeclaration ? Scope::Namespace : parent) : Scope::None;

        if (node.Kind == NK::Comment && parent == Scope::Body && standalone(node.Tokens.Begin)) {
            removed[node.Tokens.Begin] = true;
        }
    }

    const auto lf = tree.Source().find('\n');
    const bool crlf = options.RulerLineEnding == LineEnding::CRLF ||
        (options.RulerLineEnding == LineEnding::Preserve && lf != std::string_view::npos && lf &&
            tree.Source()[lf - 1] == '\r');
    const size_t length = options.RulerLength ? options.RulerLength : 80;
    const size_t newlineSize = crlf ? 2 : 1;
    auto* bytes = static_cast<char*>(fileMemory.allocate(length + 2 * newlineSize, alignof(char)));
    std::fill_n(bytes, length + 2 * newlineSize, '/');

    if (crlf) {
        bytes[0] = bytes[length + newlineSize] = '\r';
    }

    bytes[newlineSize - 1] = bytes[length + 2 * newlineSize - 1] = '\n';
    const std::string_view insertion(bytes, length + 2 * newlineSize);
    const std::string_view ruler(bytes + newlineSize, length);
    auto* crBytes = static_cast<char*>(fileMemory.allocate(length + 1, alignof(char)));
    std::fill_n(crBytes, length, '/');
    crBytes[length] = '\r';
    const std::string_view crRuler(crBytes, length + 1);
    auto edit = [&] (parser::SourceRange source, parser::TokenRange owner, std::string_view text) {
        const auto operation = static_cast<StructuralActionId>(plan.Operations.size());
        plan.Operations.push_back({StructuralAction::RulerPlacement, owner});
        const auto piece = static_cast<uint32_t>(plan.Pieces.size());

        if (!text.empty()) {
            plan.Pieces.push_back({{}, text, false});
        }

        plan.Edits.push_back({source, piece, static_cast<uint32_t>(plan.Pieces.size()), operation});
    };
    auto afterTrailing = [&] (TI token, TI end) {
        while (token < end && tree.Kind(token) == TK::Comment && !safety.Gaps()[token].HasLineFeed) {
            ++token;
        }

        return token;
    };
    auto boundary = [&] (TI begin, TI end) {
        // Recovery nodes can own trailing comments. A ruler at their edge
        // still separates blocks even when it is not a direct sequence child.
        for (TI token = begin; token && tree.Kind(token - 1) == TK::Comment; --token) {
            if (standalone(token - 1) && !removed[token - 1]) {
                return;
            }
        }

        begin = afterTrailing(begin, end);

        for (TI token = begin; token < end; ++token) {
            if (standalone(token) && !removed[token]) {
                return;
            }

            if (tree.Kind(token) != TK::Comment) {
                return;
            }
        }

        // Leading documentation stays with the following declaration; same-line
        // trailing comments stay with the preceding one. Never cross a directive.
        const auto& gap = safety.Gaps()[begin];
        const auto point = gap.Source.Begin + parser::SourceOffset(gap.MustStartWithLineFeed);

        if (gap.Preserve || !safety.CanReplaceGap(begin, "\n") || safety.IntersectsImmutable({point, point})) {
            return;
        }

        edit({point, point}, {begin, begin + 1}, insertion);
    };

    for (size_t i = 0; i < regions.size(); ++i) {
        const auto& node = regions[i];
        const bool namespaced = node.Kind == NK::NamespaceDeclaration;

        if (scopes[i] != Scope::Namespace || (!namespaced && node.Kind != NK::PreprocessingBranch)) {
            continue;
        }

        TI begin = node.Tokens.Begin;
        TI close = node.Tokens.End;

        if (namespaced) {
            TI open = begin;

            while (open < close && tree.Kind(open) != TK::Operator_OpenBrace) {
                ++open;
            }

            if (open == close) {
                continue; // Namespace alias.
            }

            close = tree.MatchingDelimiter(open);

            if (close >= node.Tokens.End || close + 1 != node.Tokens.End) {
                continue;
            }

            begin = open + 1;
        }

        TI left = begin;
        RegionId previous = NoRegion;
        bool prefix = namespaced;

        for (auto child = node.FirstChild; child != NoRegion; child = regions[child].NextSibling) {
            const auto& part = regions[child];

            if (part.Tokens.Empty() || part.Tokens.Begin < begin || part.Tokens.End > close || part.Kind == NK::Comment)
            {
                continue;
            }

            if (part.Kind == NK::EmptyDeclaration && options.RemoveNullDeclarations) {
                left = part.Tokens.End;
                continue;
            }

            TI keyword = part.Tokens.Begin + 1;

            while (keyword < part.Tokens.End && tree.Kind(keyword) == TK::Comment) {
                ++keyword;
            }

            const bool usingDirective = part.Kind == NK::UsingDeclaration && keyword < part.Tokens.End &&
                tree.Kind(keyword) == TK::Keyword_namespace;

            if (prefix && usingDirective) {
                for (TI token = left; token < part.Tokens.Begin; ++token) {
                    if (standalone(token)) {
                        removed[token] = true;
                    }
                }

                left = part.Tokens.End;
                continue;
            }

            const bool known = DeclarationItem(part.Kind) &&
                (part.Status == RegionStatus::Complete || part.Kind == NK::NamespaceDeclaration);
            // State, aliases and free functions can belong to the preceding
            // type. Only an independently declared type/namespace starts a new
            // inferred block; existing rulers express finer logical groups.
            const bool afterNamespace =
                previous != NoRegion && regions[previous].Kind == NK::NamespaceDeclaration && independent[previous];

            if (prefix || (known && previous != NoRegion && (independent[child] || afterNamespace))) {
                boundary(left, part.Tokens.Begin);
            }

            prefix = false;
            left = part.Tokens.End;
            previous = known ? child : NoRegion;
        }

        if (namespaced) {
            boundary(left, close);
        }
    }

    for (TI token = 0; token < tree.TokenCount(); ++token) {
        const auto atom = tree.Token(token).Text;
        const parser::SourceRange source{atom.Begin, atom.End};

        if (!standalone(token) || safety.IntersectsImmutable(source)) {
            continue;
        }

        const auto text = tree.TokenText(token);

        if (removed[token]) {
            edit(source, {token, token + 1}, {});
        } else if (options.RulerLength && text != ruler &&
            !(text.ends_with('\r') && text.substr(0, text.size() - 1) == ruler))
        {
            // Keep token-owned CR; whitespace policy controls editable endings.
            edit(source, {token, token + 1}, text.ends_with('\r') ? crRuler : ruler);
        }
    }
}

} // namespace formatter
