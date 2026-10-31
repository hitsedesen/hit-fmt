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
    const auto regions = safety.Regions();
    std::pmr::vector<uint8_t> declarationScopes(regions.size(), 0, &scratchMemory);

    auto standalone = [&] (TI token) {
        return tree.Kind(token) == TK::Comment && IsRuler(tree.TokenText(token)) &&
            (token == 0 || safety.Gaps()[token].HasLineFeed) &&
            (token + 1 == tree.TokenCount() || safety.Gaps()[token + 1].HasLineFeed);
    };

    for (size_t i = 0; i < regions.size(); ++i) {
        const auto& node = regions[i];
        declarationScopes[i] = (node.Parent == NoRegion || declarationScopes[node.Parent]) &&
            (node.Kind == NK::TranslationUnit || node.Kind == NK::NamespaceDeclaration ||
                node.Kind == NK::PreprocessingConditional || node.Kind == NK::PreprocessingBranch ||
                node.Kind == NK::ExportDeclaration || node.Kind == NK::LinkageSpecification);
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
    auto insert = [&] (parser::SourceOffset point, parser::TokenRange owner) {
        const auto operation = static_cast<StructuralActionId>(plan.Operations.size());
        plan.Operations.push_back({StructuralAction::RulerPlacement, owner});
        const auto piece = static_cast<uint32_t>(plan.Pieces.size());
        plan.Pieces.push_back({{}, insertion, false});
        plan.Edits.push_back({{point, point}, piece, static_cast<uint32_t>(plan.Pieces.size()), operation});
    };
    auto afterTrailing = [&] (TI token, TI end) {
        while (token < end && tree.Kind(token) == TK::Comment && !safety.Gaps()[token].HasLineFeed) {
            ++token;
        }

        return token;
    };
    auto boundary = [&] (TI begin, TI end) {
        // Recovery nodes can own trailing comments. A ruler at their edge
        // still marks the boundary even when it is not a direct sequence child.
        for (TI token = begin; token && tree.Kind(token - 1) == TK::Comment; --token) {
            if (standalone(token - 1)) {
                return;
            }
        }

        begin = afterTrailing(begin, end);

        for (TI token = begin; token < end; ++token) {
            if (standalone(token)) {
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

        insert(point, {begin, begin + 1});
    };

    for (size_t i = 0; i < regions.size(); ++i) {
        const auto& node = regions[i];

        if (!declarationScopes[i] || node.Kind != NK::NamespaceDeclaration) {
            continue;
        }

        TI begin = node.Tokens.Begin;
        TI close = node.Tokens.End;

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
        TI left = begin;
        bool prefix = true;

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
                left = part.Tokens.End;
                continue;
            }

            if (prefix) {
                boundary(left, part.Tokens.Begin);
            }

            prefix = false;
            left = part.Tokens.End;
        }

        boundary(left, close);
    }
}

} // namespace formatter
