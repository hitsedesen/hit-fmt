#include "text_policies.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace formatter {

bool IsRuler(std::string_view text)
{
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) {
        text.remove_suffix(1);
    }

    return text.size() >= 3 && text.find_first_not_of('/') == std::string_view::npos;
}

bool CanRewriteTextAtom(const SourceAnalysis& safety, parser::SourceRange source, bool directive)
{
    return directive ? safety.CanRewriteDirective(source) : !safety.IntersectsImmutable(source);
}

namespace {

std::string_view DirectiveName(std::string_view text, size_t& marker, size_t& begin)
{
    marker = text.starts_with('#') ? 1 : text.starts_with("%:") ? 2 : 0;

    if (marker == 0) {
        return {};
    }

    begin = marker;

    while (begin < text.size() && (text[begin] == ' ' || text[begin] == '\t')) {
        ++begin;
    }

    size_t end = begin;

    while (end < text.size() &&
        ((text[end] >= 'a' && text[end] <= 'z') || (text[end] >= 'A' && text[end] <= 'Z') ||
            (text[end] >= '0' && text[end] <= '9') || text[end] == '_'))
    {
        ++end;
    }

    return text.substr(begin, end - begin);
}

struct Continuation
{
    size_t Gap;
    size_t Slash;
    size_t Column;
};

// Adjust padding only where preprocessing whitespace already separates the
// logical tokens, before the splice or at the start of its following line.
// Never introduce whitespace into a spliced identifier/operator or literal,
// nor move a backslash inside a comment. Raw literals are conservative fences.
void AlignContinuations(std::pmr::string& text, std::pmr::memory_resource& scratch)
{
    if (text.find("R\"") != std::string_view::npos) {
        return;
    }

    std::pmr::vector<Continuation> lines(&scratch);
    char quote = 0;
    bool block = false;
    bool comment = false;
    size_t target = 0;

    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        const char next = i + 1 < text.size() ? text[i + 1] : '\0';
        const bool splice = c == '\\' && (next == '\n' || (next == '\r' && i + 2 < text.size() && text[i + 2] == '\n'));

        if (splice) {
            size_t gap = i;

            while (gap && (text[gap - 1] == ' ' || text[gap - 1] == '\t')) {
                --gap;
            }

            const auto following = i + (next == '\r' ? 3 : 2);
            const bool separatedAfter = following < text.size() &&
                (text[following] == ' ' || text[following] == '\t' || text[following] == '\r' ||
                    text[following] == '\n');

            if (quote || block || comment || (gap == i && !separatedAfter)) {
                return;
            }

            // Measure the prefix without its padding, with renderer tab stops.
            size_t line = text.rfind('\n', gap);
            line = line == std::string_view::npos ? 0 : line + 1;
            size_t width = 0;

            for (size_t p = line; p < gap; ++p) {
                width += text[p] == '\t' ? 8 - width % 8 : 1;
            }

            if (width >= 10000) {
                return;
            }

            target = std::max(target, width + 1);
            lines.push_back({gap, i, width});
            i += next == '\r' ? 2 : 1;
            continue;
        }

        if (c == '\n') {
            comment = false;
            continue;
        }

        if (comment) {
            continue;
        }

        if (block) {
            if (c == '*' && next == '/') {
                block = false;
                ++i;
            }
        } else if (quote) {
            if (c == '\\' && next != '\0') {
                ++i;
            } else if (c == quote) {
                quote = 0;
            }
        } else if (c == '/' && next == '*') {
            block = true;
            ++i;
        } else if (c == '/' && next == '/') {
            comment = true;
            ++i;
        } else if (c == '"' || c == '\'') {
            quote = c;
        }
    }

    if (lines.empty()) {
        return;
    }

    std::pmr::string aligned(&scratch);
    aligned.reserve(text.size());
    size_t cursor = 0;

    for (const auto& line : lines) {
        aligned.append(text.data() + cursor, line.Gap - cursor);
        aligned.append(target - line.Column, ' ');
        cursor = line.Slash;
    }

    aligned.append(text.data() + cursor, text.size() - cursor);
    text.assign(aligned);
}

std::string_view StoreText(std::string_view text, std::pmr::memory_resource& file)
{
    auto* bytes = static_cast<char*>(file.allocate(text.size(), alignof(char)));
    std::memcpy(bytes, text.data(), text.size());
    return {bytes, text.size()};
}

bool RewriteIncludeDelimiters(std::pmr::string& text, size_t begin, IncludeDelimiterResolver& delimiters)
{
    if (text.find_first_of("\n\\") != std::string_view::npos) {
        return false;
    }

    const auto open = text.find_first_not_of(" \t", begin);

    if (open == std::string_view::npos || (text[open] != '"' && text[open] != '<')) {
        return false;
    }

    const bool angled = text[open] == '<';
    const auto close = text.find(angled ? '>' : '"', open + 1);

    if (close == std::string_view::npos || close == open + 1) {
        return false;
    }

    auto tail = std::string_view(text).substr(close + 1);

    while (!tail.empty()) {
        const auto next = tail.find_first_not_of(" \t\r");
        tail.remove_prefix(std::min(next, tail.size()));

        if (tail.empty() || tail.starts_with("//")) {
            break;
        }

        if (!tail.starts_with("/*")) {
            return false;
        }

        const auto end = tail.find("*/", 2);

        if (end == std::string_view::npos) {
            return false;
        }

        tail.remove_prefix(end + 2);
    }

    const bool desired = delimiters.Angled(std::string_view(text).substr(open + 1, close - open - 1), angled);
    text[open] = desired ? '<' : '"';
    text[close] = desired ? '>' : '"';
    return desired != angled;
}

void ReplaceAtom(
    StructuralPlan& plan,
    parser::SourceRange source,
    parser::TokenIndex token,
    StructuralAction action,
    std::string_view text,
    std::pmr::memory_resource& file)
{
    const auto stored = StoreText(text, file);
    const auto operation = static_cast<StructuralActionId>(plan.Operations.size());
    plan.Operations.push_back({action, {token, token + 1}});
    const auto piece = static_cast<uint32_t>(plan.Pieces.size());
    plan.Pieces.push_back({{}, stored, false});
    plan.Edits.push_back({source, piece, piece + 1, operation});
}

} // namespace

void PlanTextPolicyEdits(
    const parser::SyntaxTree& tree,
    const SourceAnalysis& safety,
    StructuralOptions options,
    StructuralPlan& plan,
    std::pmr::memory_resource& fileMemory,
    std::pmr::memory_resource& scratchMemory,
    std::string_view inputFilePath)
{
    const bool indentDirectives = options.IndentPreprocessingDirectives == BodyIndentation::Indent;
    const bool rewriteDirectives = options.IndentPreprocessingDirectives != BodyIndentation::Preserve;
    const bool rewriteRulers = options.Rulers == RulerPlacement::Preserve && options.RulerLength != 0;

    if ((indentDirectives && (options.IndentWidth == 0 || options.IndentWidth > 32)) ||
        (options.RulerLength != 0 && (options.RulerLength < 3 || options.RulerLength > 10000)))
    {
        throw std::invalid_argument("invalid directive indent width or ruler length");
    }

    if (!rewriteDirectives && !options.AlignMacroBackslashes && !rewriteRulers &&
        options.Delimiters == IncludeDelimiters::Preserve)
    {
        return;
    }

    IncludeDelimiterResolver delimiters(options.Delimiters, options.PathBase, inputFilePath, scratchMemory);
    struct MovedAtom
    {
        parser::SourceRange Source;
        uint32_t Piece;
    };

    std::pmr::vector<MovedAtom> moved(&scratchMemory);

    for (const auto& edit : plan.Edits) {
        if (plan.Operations[edit.Operation].Kind == StructuralAction::IncludeOrder) {
            for (auto p = edit.PieceBegin; p < edit.PieceEnd; ++p) {
                if (plan.Pieces[p].FromSource) {
                    moved.push_back({plan.Pieces[p].Source, p});
                }
            }
        }
    }

    std::sort(
        moved.begin(),
        moved.end(),
        [] (const auto& a, const auto& b) {
            return a.Source.Begin < b.Source.Begin;
        });
    size_t depth = 0;
    std::pmr::string replacement(&scratchMemory);
    std::pmr::string line(&scratchMemory);

    for (parser::TokenIndex token = 0; token < tree.TokenCount(); ++token) {
        const auto text = tree.TokenText(token);
        const auto atom = tree.Token(token).Text;
        parser::SourceRange source{atom.Begin, atom.End};

        if (tree.Kind(token) == tokenizer::TokenKind::Comment) {
            if (rewriteRulers && IsRuler(text) && (token == 0 || safety.Gaps()[token].HasLineFeed) &&
                (token + 1 == tree.TokenCount() || safety.Gaps()[token + 1].HasLineFeed) &&
                CanRewriteTextAtom(safety, source, false))
            {
                replacement.assign(options.RulerLength, '/');

                if (text.ends_with('\r')) {
                    replacement.push_back('\r');
                }

                if (replacement != text) {
                    ReplaceAtom(plan, source, token, StructuralAction::RulerLength, replacement, fileMemory);
                }
            }

            continue;
        }

        if (tree.Kind(token) != tokenizer::TokenKind::PreprocessingDirective) {
            continue;
        }

        size_t marker = 0, begin = 0;
        const auto name = DirectiveName(text, marker, begin);
        const bool close = name == "endif";
        const bool branch = name == "else" || name == "elif" || name == "elifdef" || name == "elifndef";

        if (close && depth) {
            --depth;
        }

        const auto level = indentDirectives ? (branch && depth ? depth - 1 : depth) : 0;
        bool indent = false;

        if (rewriteDirectives && (!indentDirectives || level <= 10000 / options.IndentWidth)) {
            // Own only the horizontal prefix, never the preceding newline.
            // Replacing it with the token keeps the marker and directive at
            // the same nesting level, including in sorted include atoms.
            const auto before = tree.WhitespaceBefore(token);
            const auto lf = before.rfind('\n');
            const auto leading = lf == std::string_view::npos ? before : before.substr(lf + 1);
            indent = (token == 0 || lf != std::string_view::npos) &&
                leading.find_first_not_of(" \t") == std::string_view::npos;

            if (indent) {
                source.Begin -= static_cast<parser::SourceOffset>(leading.size());
            }
        }

        if (!name.empty() && CanRewriteTextAtom(safety, source, true)) {
            replacement.assign(text);
            const bool includeChanged = options.Delimiters != IncludeDelimiters::Preserve && name == "include" &&
                RewriteIncludeDelimiters(replacement, begin + name.size(), delimiters);

            if (indent) {
                replacement.erase(marker, begin - marker);
                replacement.insert(0, level * options.IndentWidth, ' ');
            }

            if (options.AlignMacroBackslashes && name == "define") {
                AlignContinuations(replacement, scratchMemory);
            }

            if (replacement != tree.SourceText(source)) {
                // A sorted include owns its full line. Rewrite that borrowed
                // piece instead of adding an overlapping token operation.
                const auto found = std::lower_bound(
                    moved.begin(),
                    moved.end(),
                    source.Begin,
                    [] (const auto& atom, auto begin) {
                        return atom.Source.End <= begin;
                    });

                if (found != moved.end() && found->Source.Begin <= source.Begin && source.End <= found->Source.End) {
                    line.assign(tree.SourceText(parser::SourceRange{found->Source.Begin, source.Begin}));
                    line.append(replacement);
                    line.append(tree.SourceText(parser::SourceRange{source.End, found->Source.End}));
                    auto& piece = plan.Pieces[found->Piece];
                    piece.Literal = StoreText(line, fileMemory);
                    piece.FromSource = false;
                } else {
                    ReplaceAtom(
                        plan,
                        source,
                        token,
                        includeChanged ? StructuralAction::IncludeDelimiters : StructuralAction::DirectiveWhitespace,
                        replacement,
                        fileMemory);
                }
            }
        }

        if (name == "if" || name == "ifdef" || name == "ifndef") {
            ++depth;
        }
    }
}

} // namespace formatter
