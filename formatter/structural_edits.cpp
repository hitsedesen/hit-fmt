#include "structural_edits.hpp"

#include "namespace_comments_internal.hpp"
#include "structural_edits_internal.hpp"
#include "text_policies.hpp"

#include <unicode/utf8.hpp>

#include <algorithm>
#include <filesystem>
#include <limits>
#include <stdexcept>

namespace formatter {

namespace {

using NK = parser::NodeKind;

using TK = tokenizer::TokenKind;

using TI = parser::TokenIndex;

using SR = parser::SourceRange;

SR Range(const parser::SyntaxTree& tree, parser::TokenRange tokens)
{
    return {tree.Token(tokens.Begin).Text.Begin, tree.Token(tokens.End - 1).Text.End};
}

bool Horizontal(std::string_view text)
{
    return text.find_first_not_of(" \t") == std::string_view::npos;
}

bool ReindentedCommentMatches(std::string_view original, std::string_view output)
{
    // Layout may change only leading horizontal whitespace after a newline
    // inside a block comment. Keep its content and newline atoms exact.
    while (!original.empty() && !output.empty()) {
        const auto c = original.front();

        if (c != output.front()) {
            return false;
        }

        original.remove_prefix(1);
        output.remove_prefix(1);

        if (c == '\r' && original.starts_with('\n') != output.starts_with('\n')) {
            return false;
        }

        if (c == '\r' || c == '\n') {
            while (!original.empty() && (original.front() == ' ' || original.front() == '\t')) {
                original.remove_prefix(1);
            }

            while (!output.empty() && (output.front() == ' ' || output.front() == '\t')) {
                output.remove_prefix(1);
            }
        }
    }

    return original.empty() && output.empty();
}

bool Control(NK kind)
{
    return kind == NK::IfStatement || kind == NK::ForStatement || kind == NK::WhileStatement ||
        kind == NK::SwitchStatement || kind == NK::DoWhileStatement;
}

bool Header(NK kind)
{
    return kind == NK::Condition || kind == NK::ForHeader;
}

bool Attribute(NK kind)
{
    return kind == NK::AttributeSpecifier || kind == NK::ExtensionAttributeSpecifier;
}

void Skip(StructuralPlan& plan, StructuralAction action, parser::TokenRange tokens, StructuralSkip reason)
{
    plan.Skipped.push_back({action, tokens, reason});
}

StructuralActionId Operation(StructuralPlan& plan, StructuralAction kind, parser::TokenRange owner)
{
    auto id = static_cast<StructuralActionId>(plan.Operations.size());
    plan.Operations.push_back({kind, owner});
    return id;
}

void Literal(
    StructuralPlan& plan,
    SR source,
    std::string_view text,
    StructuralActionId id,
    uint32_t depth = 0,
    StructuralEdit::Edge edge = StructuralEdit::Edge::Replace)
{
    auto begin = static_cast<uint32_t>(plan.Pieces.size());

    if (!text.empty()) {
        plan.Pieces.push_back({{}, text, false});
    }

    plan.Edits.push_back({source, begin, static_cast<uint32_t>(plan.Pieces.size()), id, depth, edge});
}

void Sort(StructuralPlan& plan)
{
    std::sort(
        plan.Edits.begin(),
        plan.Edits.end(),
        [] (const auto& a, const auto& b) {
            if (a.Source.Begin != b.Source.Begin) {
                return a.Source.Begin < b.Source.Begin;
            }

            if (a.Boundary != b.Boundary) {
                return a.Boundary < b.Boundary;
            }

            return a.Boundary == StructuralEdit::Edge::Close ? a.Depth > b.Depth : a.Depth < b.Depth;
        });
}

void CheckAnalysisSource(const parser::SyntaxTree& tree, const SourceAnalysis& safety)
{
    if (safety.Gaps().size() != static_cast<size_t>(tree.TokenCount()) + 1 ||
        safety.Source().data() != tree.Source().data())
    {
        throw std::invalid_argument("analysis is not from original source");
    }
}

// Walk in SourceAnalysis preorder, with a bounded explicit heap stack. No
// scratch-node pointers are retained in either public plan.
template <class F>
void Walk(
    const parser::SyntaxTree& tree,
    const SourceAnalysis* safety,
    std::pmr::memory_resource& scratch,
    F visit,
    std::span<const uint8_t> trailingLabels = {})
{
    struct Frame
    {
        const parser::Node* Node;
        const parser::Node* Next;
        NK Previous = NK::Error;
        bool DeclarationSequence = false;
        bool PreviousTrailingLabel = false;
    };

    if (safety) {
        CheckAnalysisSource(tree, *safety);
    }

    std::pmr::vector<Frame> stack(&scratch);
    size_t region = 0;
    auto enter = [&] (const parser::Node* node, NK parent, NK previous, bool previousTrailingLabel) {
        RegionStatus status = RegionStatus::Complete;
        bool trailingLabel = false;

        if (safety) {
            if (region >= safety->Regions().size()) {
                throw std::invalid_argument("analysis/tree mismatch");
            }

            trailingLabel = !trailingLabels.empty() && trailingLabels[region];
            const auto& r = safety->Regions()[region++];

            if (r.Kind != node->Kind || r.Tokens.Begin != node->Tokens.Begin || r.Tokens.End != node->Tokens.End) {
                throw std::invalid_argument("analysis/tree mismatch");
            }

            status = r.Status;
        }

        auto grandparent = stack.size() > 1 ? stack[stack.size() - 2].Node->Kind : NK::Error;
        bool parentSequence = !stack.empty() && stack.back().DeclarationSequence;
        visit(
            *node,
            parent,
            previous,
            grandparent,
            parentSequence,
            static_cast<uint32_t>(stack.size()),
            status,
            trailingLabel,
            previousTrailingLabel);
        const auto* child = node->Child();

        if (!child) {
            return;
        }

        bool sequence = node->Kind == NK::TranslationUnit || node->Kind == NK::NamespaceDeclaration;

        if (node->Kind == NK::ExportDeclaration || node->Kind == NK::LinkageSpecification) {
            auto header = node->Tokens.Begin + (node->Kind == NK::ExportDeclaration ? 1 : 2);

            while (header < node->Tokens.End && tree.Kind(header) == TK::Comment) {
                ++header;
            }

            sequence = header < node->Tokens.End && tree.Kind(header) == TK::Operator_OpenBrace &&
                tree.MatchingDelimiter(header) < node->Tokens.End;
        }

        stack.push_back({node, child, NK::Error, sequence});
    };
    enter(&tree.Root(), NK::Error, NK::Error, false);

    while (!stack.empty()) {
        auto& frame = stack.back();

        if (auto* child = frame.Next) {
            auto parent = frame.Node->Kind;
            auto previous = frame.Previous;
            auto previousTrailingLabel = frame.PreviousTrailingLabel;
            frame.Next = child->NextSibling;

            if (child->Kind != NK::Comment) {
                frame.Previous = child->Kind;
                frame.PreviousTrailingLabel = region < trailingLabels.size() && trailingLabels[region];
            }

            enter(child, parent, previous, previousTrailingLabel);
        } else {
            stack.pop_back();
        }
    }

    if (safety && region != safety->Regions().size()) {
        throw std::invalid_argument("analysis/tree mismatch");
    }
}

template <class F>
void Children(const parser::Node& node, std::span<const SourceRegion> /*regions*/, F visit)
{
    for (const auto* child = node.Child(); child; child = child->NextSibling) {
        visit(*child);
    }
}

template <class F>
void Children(const SourceRegion& node, std::span<const SourceRegion> regions, F visit)
{
    for (auto child = node.FirstChild; child != NoRegion; child = regions[child].NextSibling) {
        visit(regions[child]);
    }
}

bool NullSyntax(const parser::SyntaxTree& tree, const SourceRegion& region)
{
    return (region.Kind == NK::EmptyDeclaration || region.Kind == NK::ExpressionStatement) &&
        region.Tokens.Size() == 1 && tree.Kind(region.Tokens.Begin) == TK::Operator_Semicolon;
}

constexpr uint8_t KnownSyntax = 1;

constexpr uint8_t KnownBefore = 2;

constexpr uint8_t KnownAfter = 4;

constexpr uint8_t UnknownScope = 8;

std::pmr::vector<uint8_t> NullRemovalContexts(
    const parser::SyntaxTree& tree,
    std::span<const SourceRegion> regions,
    std::pmr::memory_resource& scratch)
{
    std::pmr::vector<uint8_t> contexts(regions.size(), KnownSyntax, &scratch);
    // Complete only rules out recovery and ambiguity. A function-shaped macro
    // can still have an unknown function kind and require its trailing ';'.
    // Its apparent body is not a proven statement sequence either.
    for (size_t id = 0; id < regions.size(); ++id) {
        const auto& region = regions[id];

        if ((region.Kind == NK::FunctionDefinition && region.Function == parser::FunctionKind::Unknown) ||
            region.Kind == NK::MacroFunctionDefinition || region.Kind == NK::MacroRecordDeclaration ||
            region.Kind == NK::MacroEnumDeclaration ||
            (region.Parent != NoRegion && (contexts[region.Parent] & UnknownScope)))
        {
            contexts[id] |= UnknownScope;
        }

        if (region.Status != RegionStatus::Complete || (contexts[id] & UnknownScope)) {
            contexts[id] &= ~KnownSyntax;
        }
    }

    for (size_t i = regions.size(); i > 0; --i) {
        const auto id = i - 1;
        const auto& region = regions[id];

        if (!(contexts[id] & KnownSyntax) && region.Parent != NoRegion) {
            contexts[region.Parent] &= ~KnownSyntax;
        }

        // Ignore null runs when looking for syntax on either side: otherwise
        // two semicolons could authorize each other's removal beside a macro.
        const auto next = region.NextSibling;

        if (next == NoRegion ||
            ((regions[next].Kind == NK::Comment || NullSyntax(tree, regions[next]))
                ? contexts[next] & KnownAfter
                : contexts[next] & KnownSyntax))
        {
            contexts[id] |= KnownAfter;
        }
    }

    for (const auto& parent : regions) {
        bool known = true; // The beginning of a syntax sequence is a boundary.

        for (auto id = parent.FirstChild; id != NoRegion; id = regions[id].NextSibling) {
            if (known) {
                contexts[id] |= KnownBefore;
            }

            if (regions[id].Kind != NK::Comment && !NullSyntax(tree, regions[id])) {
                // An explicit function-body hint proves the closing boundary,
                // even though its opaque header and body remain conservative
                // null-removal contexts. It cannot require a trailing ';'.
                known = (contexts[id] & KnownSyntax) ||
                    (regions[id].Kind == NK::MacroFunctionDefinition && regions[id].Status == RegionStatus::Complete);
            }
        }
    }

    return contexts;
}

bool AdjacentComment(const parser::SyntaxTree& tree, parser::TokenRange tokens)
{
    if (tokens.Begin && tree.Kind(tokens.Begin - 1) == TK::Comment) {
        return true;
    }

    return tokens.End < tree.TokenCount() && tree.Kind(tokens.End) == TK::Comment &&
        tree.WhitespaceBefore(tokens.End).find('\n') == std::string_view::npos;
}

struct Include
{
    TI Token;
    SR Atom;
    std::string_view Path;
    bool Angled;
};

struct IncludeKey
{
    std::size_t DirectoryOffset = 0;
    std::size_t DirectoryLength = 0;
    std::size_t Distance = 0;
    std::size_t HeaderDistance = 0;
    std::size_t Rule = 0;
    std::size_t Prefix = 0;
    std::int8_t Priority = 0;
    std::int8_t Depth = IncludeGroupRule::MaxDepth;
    bool Matching = false;
    bool Standard = false;
    bool RuleStandard = false;
};

bool ParseInclude(const parser::SyntaxTree& tree, TI token, Include& result)
{
    if (tree.Kind(token) != TK::PreprocessingDirective) {
        return false;
    }

    auto text = tree.TokenText(token);

    if (text.find_first_of("\n\\") != std::string_view::npos) {
        return false;
    }

    if (text.starts_with('#')) {
        text.remove_prefix(1);
    } else if (text.starts_with("%:")) {
        text.remove_prefix(2);
    } else {
        return false;
    }

    auto trim = [&] {
        auto n = text.find_first_not_of(" \t");
        text.remove_prefix(std::min(n, text.size()));
    };
    trim();

    if (!text.starts_with("include")) {
        return false;
    }

    text.remove_prefix(7);
    trim();

    if (text.empty() || (text.front() != '<' && text.front() != '"')) {
        return false;
    }

    bool angled = text.front() == '<';
    auto close = text.find(angled ? '>' : '"', 1);

    if (close == std::string_view::npos || close == 1 ||
        text.substr(close + 1).find_first_not_of(" \t\r") != std::string_view::npos)
    {
        return false;
    }

    auto range = tree.Token(token).Text;

    if (range.End >= tree.Source().size() || tree.Source()[range.End] != '\n') {
        return false; // Only complete line atoms may move, including their LF.
    }

    // Indentation belongs to the exact line atom too. These gap scans visit
    // disjoint source slices; they never search the whole source per include.
    auto before = tree.WhitespaceBefore(token);
    auto lf = before.rfind('\n');

    if (token && lf == std::string_view::npos) {
        return false;
    }

    auto leading = lf == std::string_view::npos ? before : before.substr(lf + 1);

    if (!Horizontal(leading)) {
        return false;
    }

    auto begin = range.Begin - static_cast<parser::SourceOffset>(leading.size());
    result = {token, {begin, range.End + 1}, text.substr(1, close - 1), angled};
    return true;
}

bool DirectiveOnly(const SourceAnalysis& safety, SR range)
{
    return safety.CanRewriteDirective(range);
}

bool IncludeGap(std::string_view gap, bool crlf)
{
    for (std::size_t i = 0; i < gap.size(); ++i) {
        if (gap[i] == ' ' || gap[i] == '\t') {
            continue;
        }

        if (crlf && gap[i] == '\r' && i + 1 < gap.size() && gap[i + 1] == '\n') {
            ++i;
        } else if (crlf || gap[i] != '\n') {
            return false;
        }
    }

    return true;
}

template <class Accept, class Reject>
void ScanIncludeGroups(
    const parser::SyntaxTree& tree,
    const SourceAnalysis& safety,
    std::pmr::memory_resource& scratch,
    IncludeGrouping grouping,
    Accept accept,
    Reject reject)
{
    std::pmr::vector<Include> group(&scratch);

    for (TI i = 0; i < tree.TokenCount();) {
        Include first;

        if (!ParseInclude(tree, i, first)) {
            ++i;
            continue;
        }

        group.clear();
        group.push_back(first);
        TI end = i + 1;
        Include next;

        while (end < tree.TokenCount() && ParseInclude(tree, end, next) &&
            (group.back().Atom.End == next.Atom.Begin ||
                (grouping != IncludeGrouping::Preserve &&
                    IncludeGap(
                        tree.Source().substr(group.back().Atom.End, next.Atom.Begin - group.back().Atom.End),
                        tree.TokenText(i).ends_with('\r')))) &&
            tree.TokenText(i).ends_with('\r') == tree.TokenText(end).ends_with('\r'))
        {
            group.push_back(next);
            ++end;
        }

        if (group.size() > 1) {
            SR range{group.front().Atom.Begin, group.back().Atom.End};

            if (AdjacentComment(tree, {i, end})) {
                reject(parser::TokenRange{i, end}, StructuralSkip::CommentAttached);
            } else if (!DirectiveOnly(safety, range)) {
                reject(parser::TokenRange{i, end}, StructuralSkip::Protected);
            } else {
                accept(IncludeGroup{{i, end}, range}, std::span<Include>(group));
            }
        }

        i = end;
    }
}

std::string_view Directory(std::string_view path)
{
    const auto slash = path.rfind('/');
    return slash == std::string_view::npos ? std::string_view{} : path.substr(0, slash + 1);
}

std::string_view Filename(std::string_view path)
{
    return path.substr(Directory(path).size());
}

std::string_view Stem(std::string_view name)
{
    const auto dot = name.rfind('.');
    return dot == std::string_view::npos ? name : name.substr(0, dot);
}

bool HeaderFilename(std::string_view name)
{
    const auto dot = name.rfind('.');
    const auto extension = dot == std::string_view::npos ? std::string_view{} : name.substr(dot);
    return extension == ".h" || extension == ".hh" || extension == ".hpp" || extension == ".hxx" || extension == ".h++";
}

void Includes(
    const parser::SyntaxTree& tree,
    const SourceAnalysis& safety,
    StructuralPlan& plan,
    std::pmr::memory_resource& scratch,
    StructuralOptions options,
    IncludeSortContext context)
{
    if (options.Grouping == IncludeGrouping::Preserve) {
        context.Groups = nullptr;
    }
    std::pmr::vector<std::string_view> current(&scratch), parts(&scratch);
    std::pmr::vector<std::string_view> grouped(&scratch);
    const auto inputDirectory = Directory(context.InputFilePath);
    const bool inputAbsolute = inputDirectory.starts_with('/');
    const bool nearestHeaders = options.InGroupOrder == IncludeInGroupOrder::NearestFirst;
    const bool needPaths = options.Grouping != IncludeGrouping::Preserve ||
        options.MatchingHeader == IncludeMatchingHeader::First || nearestHeaders;
    auto distance = [&] (std::span<const std::string_view> path, bool absolute) {
        if (absolute != inputAbsolute) {
            return std::numeric_limits<std::size_t>::max();
        }
        std::size_t common = 0;

        while (common < std::min(current.size(), path.size()) && current[common] == path[common]) {
            ++common;
        }

        return current.size() - common + path.size() - common;
    };
    if (needPaths) {
        AppendIncludePathComponents(current, inputDirectory, inputAbsolute);
    }

    IncludeDelimiterResolver delimiters(options.Delimiters, options.PathBase, context.InputFilePath, scratch);
    const auto sourceName = Filename(context.InputFilePath);
    std::pmr::string directories(&scratch);
    std::pmr::vector<StructuralPiece> pieces(&scratch);
    std::pmr::vector<IncludeKey> keys(&scratch);
    ScanIncludeGroups(
        tree,
        safety,
        scratch,
        options.Grouping,
        [&] (IncludeGroup descriptor, std::span<Include> group) {
            directories.clear();
            pieces.clear();

            if (needPaths || options.Grouping == IncludeGrouping::Regroup ||
                options.StandardHeaders == IncludeStandardHeaders::Last)
            {
                keys.clear();
                keys.resize(group.size());
            }

            if (options.Delimiters != IncludeDelimiters::Preserve) {
                for (auto& atom : group) {
                    atom.Angled = delimiters.Angled(atom.Path, atom.Angled);
                }
            }

            for (std::size_t i = 0; i < group.size() && !keys.empty(); ++i) {
                const auto& atom = group[i];
                auto& key = keys[i];

                if (options.Grouping != IncludeGrouping::Preserve && context.Groups) {
                    const auto match = context.Groups->Classify(atom.Path, atom.Angled);
                    key.Rule = match.Group;
                    key.Prefix = match.Prefix;
                    key.Depth = context.Groups->Depth(match.Group);
                    key.Priority = context.Groups->Priority(match.Group);
                    key.RuleStandard = match.StandardHeaders;
                }

                key.Standard = options.StandardHeaders == IncludeStandardHeaders::Last && !key.RuleStandard &&
                    IsStandardInclude(atom.Path, atom.Angled);

                if (!needPaths) {
                    continue;
                }

                parts.clear();
                // Bare quoted names are local even with root-based project
                // includes. Explicit ./ and ../ spell source-relative paths.
                const bool local = (!atom.Angled && atom.Path.find('/') == std::string_view::npos) ||
                    ((!atom.Angled || options.Delimiters != IncludeDelimiters::Preserve) &&
                        (atom.Path.starts_with("./") || atom.Path.starts_with("../")));
                const bool relative =
                    !atom.Path.starts_with('/') && (local || options.PathBase == IncludePathBase::SourceDirectory);
                const bool absolute = relative ? inputAbsolute : atom.Path.starts_with('/');

                if (relative) {
                    parts.assign(current.begin(), current.end());
                }

                AppendIncludePathComponents(parts, Directory(atom.Path), absolute);

                if (nearestHeaders) {
                    // Keep literal resolved directories distinct from alias
                    // and depth-capped group paths. Compute once per header.
                    key.HeaderDistance = distance(parts, absolute);
                }

                // Canonicalize only the grouping key, never the include atom
                // or the full path used for matching-header detection.
                grouped.clear();
                bool groupAbsolute = absolute;
                const auto prefix = context.Groups ? context.Groups->Prefix(key.Prefix) : std::string_view{};
                const auto canonical = context.Groups ? context.Groups->CanonicalPrefix(key.Rule) : std::string_view{};

                if (options.Grouping == IncludeGrouping::Regroup || key.Depth == 0) {
                    groupAbsolute = options.PathBase == IncludePathBase::SourceDirectory && inputAbsolute;

                    if (options.PathBase == IncludePathBase::SourceDirectory) {
                        grouped.assign(current.begin(), current.end());
                    }

                    AppendIncludePathComponents(grouped, canonical, groupAbsolute);
                } else if (!prefix.empty()) {
                    const auto base = relative ? current.size() : 0;
                    const auto suffix = base + context.Groups->PrefixDepth(key.Prefix);
                    const auto count = key.Depth == IncludeGroupRule::MaxDepth
                        ? parts.size() - suffix
                        : std::min(static_cast<std::size_t>(key.Depth), parts.size() - suffix);
                    grouped.insert(grouped.end(), parts.begin(), parts.begin() + base);
                    AppendIncludePathComponents(grouped, canonical, groupAbsolute);
                    grouped.insert(grouped.end(), parts.begin() + suffix, parts.begin() + suffix + count);
                } else {
                    const auto count = key.Depth == IncludeGroupRule::MaxDepth
                        ? parts.size()
                        : std::min(static_cast<std::size_t>(key.Depth), parts.size());
                    grouped.assign(parts.begin(), parts.begin() + count);
                }

                key.Distance = distance(grouped, groupAbsolute);
                key.DirectoryOffset = directories.size();

                if (groupAbsolute) {
                    directories.push_back('/');
                }

                for (std::size_t index = 0; index < grouped.size(); ++index) {
                    if (index) {
                        directories.push_back('/');
                    }

                    directories.append(grouped[index]);
                }

                key.DirectoryLength = directories.size() - key.DirectoryOffset;
                const auto name = Filename(atom.Path);
                key.Matching = options.MatchingHeader == IncludeMatchingHeader::First && !atom.Angled &&
                    !sourceName.empty() && !HeaderFilename(sourceName) && HeaderFilename(name) &&
                    Stem(name) == Stem(sourceName) && absolute == inputAbsolute && parts == current;
            }

            auto key = [&] (const Include& atom) -> const IncludeKey& {
                return keys[atom.Token - descriptor.Tokens.Begin];
            };
            auto matching = [&] (const Include& atom) {
                return needPaths && key(atom).Matching;
            };
            auto standard = [&] (const Include& atom) {
                return options.StandardHeaders == IncludeStandardHeaders::Last && key(atom).Standard;
            };
            auto directory = [&] (const Include& atom) {
                return std::string_view(directories).substr(key(atom).DirectoryOffset, key(atom).DirectoryLength);
            };
            auto sameGroup = [&] (const Include& a, const Include& b) {
                if (matching(a) != matching(b)) {
                    return false;
                }

                if (matching(a) && matching(b)) {
                    return true;
                }

                if (standard(a) != standard(b)) {
                    return false;
                }

                if (standard(a) && standard(b)) {
                    return true;
                }

                return key(a).Rule == key(b).Rule && a.Angled == b.Angled &&
                    (options.Grouping != IncludeGrouping::Directory || directory(a) == directory(b));
            };
            auto compare = [&] (const Include& a, const Include& b) {
                if (matching(a) != matching(b)) {
                    return matching(a);
                }

                if (standard(a) != standard(b)) {
                    return !standard(a);
                }

                if (a.Angled != b.Angled) {
                    return options.CategoryOrder == IncludeCategoryOrder::AngledFirst ? a.Angled : !a.Angled;
                }

                if (options.Grouping != IncludeGrouping::Preserve && !matching(a) && !standard(a)) {
                    if (key(a).Priority != key(b).Priority) {
                        return key(a).Priority < key(b).Priority;
                    }

                    if (context.Groups && options.GroupOrder == IncludeGroupOrder::Configured &&
                        key(a).Rule != key(b).Rule)
                    {
                        return key(a).Rule < key(b).Rule;
                    }

                    if (options.Grouping == IncludeGrouping::Regroup && context.Groups) {
                        const bool unmatchedA = key(a).Rule == context.Groups->GroupCount();
                        const bool unmatchedB = key(b).Rule == context.Groups->GroupCount();

                        if (unmatchedA != unmatchedB) {
                            return !unmatchedA;
                        }
                    }

                    if (options.DirectoryOrder == IncludeDirectoryOrder::NearestFirst &&
                        key(a).Distance != key(b).Distance)
                    {
                        return key(a).Distance < key(b).Distance;
                    }

                    if (directory(a) != directory(b)) {
                        return directory(a) < directory(b);
                    }

                    if (key(a).Rule != key(b).Rule) {
                        // Standard and fallback groups can share an empty root.
                        // Keep distinct rules contiguous even when roots tie.
                        const auto left = context.Groups->CanonicalPrefix(key(a).Rule);
                        const auto right = context.Groups->CanonicalPrefix(key(b).Rule);
                        return left != right ? left < right : key(a).Rule < key(b).Rule;
                    }

                    if (!nearestHeaders && options.Grouping == IncludeGrouping::Directory &&
                        key(a).Depth == IncludeGroupRule::MaxDepth && Filename(a.Path) != Filename(b.Path))
                    {
                        return Filename(a.Path) < Filename(b.Path);
                    }
                }

                if (nearestHeaders && !matching(a) && !standard(a) && !key(a).RuleStandard &&
                    key(a).HeaderDistance != key(b).HeaderDistance)
                {
                    return key(a).HeaderDistance < key(b).HeaderDistance;
                }

                return a.Path != b.Path ? a.Path < b.Path : a.Token < b.Token;
            };
            const bool sorted = std::is_sorted(group.begin(), group.end(), compare);

            if (sorted && options.Grouping == IncludeGrouping::Preserve) {
                return;
            }

            if (!sorted) {
                std::sort(group.begin(), group.end(), compare);
            }

            const auto ending = tree.TokenText(group.front().Token).ends_with('\r')
                ? std::string_view("\r\n")
                : std::string_view("\n");

            for (std::size_t i = 0; i < group.size(); ++i) {
                if (i && options.Grouping != IncludeGrouping::Preserve && !sameGroup(group[i - 1], group[i])) {
                    pieces.push_back({{}, ending, false});
                }

                pieces.push_back({group[i].Atom, {}, true});
            }

            // Sorted blocks with canonical separators are no-ops, preserving
            // operation identities and avoiding reparsing on repeat runs.
            auto original =
                tree.Source().substr(descriptor.Source.Begin, descriptor.Source.End - descriptor.Source.Begin);
            bool unchanged = true;

            for (const auto& piece : pieces) {
                const auto text = piece.FromSource
                    ? tree.Source().substr(piece.Source.Begin, piece.Source.End - piece.Source.Begin)
                    : piece.Literal;

                if (!original.starts_with(text)) {
                    unchanged = false;
                    break;
                }

                original.remove_prefix(text.size());
            }

            if (unchanged && original.empty()) {
                return;
            }

            auto id = Operation(plan, StructuralAction::IncludeOrder, descriptor.Tokens);
            auto start = static_cast<uint32_t>(plan.Pieces.size());
            plan.Pieces.insert(plan.Pieces.end(), pieces.begin(), pieces.end());
            plan.Edits.push_back({descriptor.Source, start, static_cast<uint32_t>(plan.Pieces.size()), id});
        },
        [&] (parser::TokenRange tokens, StructuralSkip reason) {
            Skip(plan, StructuralAction::IncludeOrder, tokens, reason);
        });
}

size_t Validate(std::string_view source, const StructuralPlan& plan)
{
    size_t size = CheckedSourceAnalysisSize(source.size());
    size_t cursor = 0;
    std::pmr::vector<uint8_t> ownership(plan.Operations.size(), 0, plan.Edits.get_allocator().resource());
    const StructuralEdit* previous = nullptr;

    for (const auto& edit : plan.Edits) {
        if (edit.Source.Begin > edit.Source.End || edit.Source.End > source.size() || edit.Source.Begin < cursor ||
            edit.Operation >= plan.Operations.size() || edit.PieceBegin > edit.PieceEnd ||
            edit.PieceEnd > plan.Pieces.size())
        {
            throw std::invalid_argument("invalid structural edit ownership/range");
        }

        if (previous && previous->Source.Begin == edit.Source.Begin && previous->Source.Empty() &&
            edit.Source.Empty() &&
            (previous->Boundary > edit.Boundary ||
                (previous->Boundary == edit.Boundary &&
                    (edit.Boundary == StructuralEdit::Edge::Close
                        ? previous->Depth <= edit.Depth
                        : previous->Depth >= edit.Depth))))
        {
            throw std::invalid_argument("unordered/co-owned structural insertion");
        }

        auto kind = plan.Operations[edit.Operation].Kind;
        auto bit = edit.Boundary == StructuralEdit::Edge::Open
            ? 1u
            : edit.Boundary == StructuralEdit::Edge::Close ? 2u : 4u;

        if ((ownership[edit.Operation] & bit) != 0 ||
            (kind == StructuralAction::AddMissingControlBraces ? !edit.Source.Empty() || bit == 4u : bit != 4u))
        {
            throw std::invalid_argument("non-atomic structural operation");
        }

        ownership[edit.Operation] |= static_cast<uint8_t>(bit);
        size -= edit.Source.End - edit.Source.Begin;

        for (auto p = edit.PieceBegin; p < edit.PieceEnd; ++p) {
            const auto& piece = plan.Pieces[p];

            if (piece.FromSource && (piece.Source.Begin > piece.Source.End || piece.Source.End > source.size())) {
                throw std::invalid_argument("invalid structural source piece");
            }

            auto extra = piece.FromSource ? piece.Source.End - piece.Source.Begin : piece.Literal.size();

            if (extra > util::MaxSourceSize - size) {
                throw std::length_error("structural output too large");
            }

            size += extra;
        }

        cursor = edit.Source.End;
        previous = &edit;
    }

    for (size_t i = 0; i < ownership.size(); ++i) {
        auto required = plan.Operations[i].Kind == StructuralAction::AddMissingControlBraces ? 3u : 4u;

        if (ownership[i] != required) {
            throw std::invalid_argument("incomplete structural operation");
        }
    }

    return size;
}
TI FirstBeginningAt(const parser::SyntaxTree& tree, parser::SourceOffset offset)
{
    TI low = 0, high = tree.TokenCount();

    while (low < high) {
        TI mid = low + (high - low) / 2;

        if (tree.Token(mid).Text.Begin < offset) {
            low = mid + 1;
        } else {
            high = mid;
        }
    }

    return low;
}

bool Intersects(std::span<const SR> ranges, SR query)
{
    auto it = std::lower_bound(
        ranges.begin(),
        ranges.end(),
        query.Begin,
        [] (SR r, auto offset) {
            return r.End <= offset;
        });
    return it != ranges.end() && (query.Empty() ? it->Begin <= query.Begin : it->Begin < query.End);
}

} // namespace

StructuralPlan::StructuralPlan(std::pmr::memory_resource& memory)
    : Operations(&memory)
    , Edits(&memory)
    , Pieces(&memory)
    , Skipped(&memory)
{ }

StructuralMaterialization::StructuralMaterialization(std::pmr::memory_resource& memory)
    : Text(&memory)
    , OriginalTokens(&memory)
    , Provenance(&memory)
    , GeneratedAdjacencies(&memory)
{ }

NamespaceCommentPlan::NamespaceCommentPlan(std::pmr::memory_resource& memory)
    : Namespaces(&memory)
    , Skipped(&memory)
{ }

std::pmr::vector<IncludeGroup> DiscoverIncludeGroups(
    const parser::SyntaxTree& tree,
    const SourceAnalysis& safety,
    std::pmr::memory_resource& fileMemory,
    std::pmr::memory_resource& scratchMemory,
    IncludeGrouping grouping)
{
    CheckAnalysisSource(tree, safety);
    std::pmr::vector<IncludeGroup> result(&fileMemory);
    ScanIncludeGroups(
        tree,
        safety,
        scratchMemory,
        grouping,
        [&] (IncludeGroup group, std::span<Include>) {
            result.push_back(group);
        },
        [] (parser::TokenRange, StructuralSkip) { });
    return result;
}

namespace {

template <bool Certified>
StructuralPlan PlanStructuralEditsImpl(
    const parser::SyntaxTree& tree,
    const SourceAnalysis& safety,
    StructuralOptions options,
    std::pmr::memory_resource& fileMemory,
    std::pmr::memory_resource& scratchMemory,
    IncludeSortContext includes)
{
    StructuralPlan plan(fileMemory);
    const bool controlEdits = options.AddMissingControlBraces || options.EmptyControlBody;
    std::pmr::vector<uint32_t> barriers(&scratchMemory);
    const auto hasBarrier = [&] (parser::TokenRange tokens) {
        // Braced and ineligible bodies need no token summary. Build it only
        // when an otherwise admissible edit must check comment/directive edges.
        if (barriers.empty()) {
            barriers.resize(size_t(tree.TokenCount()) + 1);

            for (TI i = 0; i < tree.TokenCount(); ++i) {
                barriers[i + 1] =
                    barriers[i] + (tree.Kind(i) == TK::Comment || tree.Kind(i) == TK::PreprocessingDirective);
            }
        }

        return barriers[tokens.End] != barriers[tokens.Begin];
    };

    // Labels stop at their colon in this parser. A control containing an
    // unresolved label also ends before its required statement, which can be
    // a sibling of the whole control. Mark only labels and their control
    // ancestors; each marked control stops later walks of that shared path.
    auto regions = safety.Regions();
    std::pmr::vector<uint8_t> trailingLabels(&scratchMemory);
    if (controlEdits || options.RemoveNullStatements) {
        for (RegionId id = 0; id < regions.size(); ++id) {
            if (regions[id].Kind != NK::LabelStatement && regions[id].Kind != NK::CaseLabel) {
                continue;
            }

            if (trailingLabels.empty()) {
                trailingLabels.resize(regions.size());
            }

            auto label = id;

            while (!trailingLabels[label]) {
                trailingLabels[label] = true;
                const auto parent = regions[label].Parent;

                if (parent == NoRegion || !Control(regions[parent].Kind)) {
                    break;
                }

                label = parent;
            }
        }
    }

    TI namespaceSuffixEnd = tree.TokenCount();
    std::pmr::vector<uint8_t> nullContexts(&scratchMemory);
    const auto hasNullContext = [&] (size_t region, uint8_t required) {
        // Most files have no removable null syntax. Build the neighboring
        // syntax summaries only when a complete candidate needs them.
        if (nullContexts.empty()) {
            nullContexts = NullRemovalContexts(tree, regions, scratchMemory);
        }

        return (nullContexts[region] & required) == required;
    };
    auto visit = [&] (
        const auto& node,
        NK parent,
        NK previous,
        NK grandparent,
        bool declarationSequence,
        auto depth,
        RegionStatus status,
        bool trailingLabel,
        bool previousTrailingLabel,
        size_t region) {

            if (controlEdits && Control(node.Kind)) {
                bool attributes = false, labels = false, directives = false;

                Children(
                    node,
                    regions,
                    [&] (const auto& child) {
                        attributes |= Attribute(child.Kind);
                        labels |= child.Kind == NK::CaseLabel || child.Kind == NK::LabelStatement;
                        directives |=
                            child.Kind == NK::PreprocessingDirective || child.Kind == NK::PreprocessingConditional;
                    });

                Children(
                    node,
                    regions,
                    [&] (const auto& body) {
                        if (Header(body.Kind) || Attribute(body.Kind) || body.Kind == NK::CompoundStatement ||
                            body.Kind == NK::Comment || body.Kind == NK::PreprocessingDirective || body.Tokens.Empty())
                        {
                            return;
                        }

                        auto tokens = body.Tokens;
                        bool null = body.Kind == NK::ExpressionStatement && tokens.Size() == 1 &&
                            tree.Kind(tokens.Begin) == TK::Operator_Semicolon;

                        if (!(null ? options.EmptyControlBody : options.AddMissingControlBraces)) {
                            return;
                        }

                        // Preserve the conventional else-if chain; nested
                        // then-controls are wrapped together, with closes
                        // ordered deepest-first.
                        if (body.Kind == NK::IfStatement && tokens.Begin &&
                            tree.Kind(tokens.Begin - 1) == TK::Keyword_else)
                        {
                            return;
                        }

                        auto action = null
                            ? StructuralAction::EmptyControlBody
                            : StructuralAction::AddMissingControlBraces;
                        auto range = Range(tree, tokens);

                        // Replacing a null body's semicolon cannot capture comments
                        // elsewhere in the control; only directly attached comments fence it.
                        if (status != RegionStatus::Complete) {
                            Skip(plan, action, tokens, StructuralSkip::Incomplete);
                        } else if (attributes) {
                            Skip(plan, action, tokens, StructuralSkip::Attributes);
                        } else if (trailingLabel || labels || body.Kind == NK::LabelStatement ||
                            body.Kind == NK::CaseLabel)
                        {
                            Skip(plan, action, tokens, StructuralSkip::LabelBody);
                        } else if (directives) {
                            Skip(plan, action, tokens, StructuralSkip::DirectiveBoundary);
                        } else if ((!null && hasBarrier(node.Tokens)) || AdjacentComment(tree, tokens)) {
                            Skip(plan, action, tokens, StructuralSkip::CommentAttached);
                        } else if (safety.IntersectsImmutable(range) ||
                            // Null bodies replace only the semicolon.
                            // A protected following gap fences inserted
                            // closes, not replacements ending at its boundary.
                            (!null && safety.IntersectsImmutable({range.End, range.End})) ||
                            (safety.Gaps()[tokens.Begin].Preserve && safety.Gaps()[tokens.Begin].Source.Empty()) ||
                            (!null && safety.Gaps()[tokens.End].Preserve && safety.Gaps()[tokens.End].Source.Empty()))
                        {
                            Skip(plan, action, tokens, StructuralSkip::Protected);
                        } else {
                            auto id = Operation(plan, action, tokens);

                            if (null) {
                                Literal(plan, range, "{}", id, depth());
                            } else {
                                Literal(plan, {range.Begin, range.Begin}, "{", id, depth(), StructuralEdit::Edge::Open);
                                Literal(plan, {range.End, range.End}, "}", id, depth(), StructuralEdit::Edge::Close);
                            }
                        }
                    });
            }

            bool nullStatement = options.RemoveNullStatements && parent == NK::CompoundStatement &&
                node.Kind == NK::ExpressionStatement && node.Tokens.Size() == 1;
            bool declarationParent = parent == NK::TranslationUnit || parent == NK::NamespaceDeclaration ||
                parent == NK::LinkageSpecification || parent == NK::ExportDeclaration;
            bool nullDeclaration = options.RemoveNullDeclarations && declarationParent &&
                node.Kind == NK::EmptyDeclaration && node.Tokens.Size() == 1;
            bool functionSuffix = options.RemoveNullDeclarations && node.Kind == NK::FunctionDefinition &&
                !node.Tokens.Empty() && tree.Kind(node.Tokens.End - 1) == TK::Operator_Semicolon;

            if (!(nullStatement || nullDeclaration || functionSuffix)) {
                return;
            }

            TI token = functionSuffix ? node.Tokens.End - 1 : node.Tokens.Begin;

            if (tree.Kind(token) != TK::Operator_Semicolon) {
                return;
            }

            // A namespace's same-line null-declaration suffix is
            // redundant even when a comment follows it. Remove only the
            // semicolon atoms, retaining the authored comment and gaps.
            bool namespaceSuffix = nullDeclaration && token && Horizontal(tree.WhitespaceBefore(token)) &&
                ((previous == NK::NamespaceDeclaration && tree.Kind(token - 1) == TK::Operator_CloseBrace) ||
                    (previous == NK::EmptyDeclaration && namespaceSuffixEnd == token));

            if (namespaceSuffix) {
                namespaceSuffixEnd = token + 1;
            }

            auto action = nullStatement
                ? StructuralAction::RemoveNullStatements
                : StructuralAction::RemoveNullDeclarations;
            parser::TokenRange tokens{token, token + 1};
            const auto requiredContext = functionSuffix
                ? KnownSyntax | KnownAfter
                : KnownSyntax | KnownBefore | KnownAfter;

            if (status != RegionStatus::Complete || !hasNullContext(region, requiredContext)) {
                Skip(plan, action, tokens, StructuralSkip::Incomplete);
            } else if ((nullDeclaration && !declarationSequence) ||
                (nullStatement && (grandparent == NK::Argument || grandparent == NK::StatementExpression)))
            {
                // GNU statement expressions can change from void to a
                // value if their final null statement disappears. Macro
                // braced arguments and non-sequence exported declarations
                // are not redundant bodies.
                Skip(plan, action, tokens, StructuralSkip::RequiredBody);
            } else if (nullStatement &&
                (previousTrailingLabel || previous == NK::CaseLabel || previous == NK::LabelStatement))
            {
                Skip(plan, action, tokens, StructuralSkip::LabelBody);
            } else if (!namespaceSuffix && AdjacentComment(tree, tokens)) {
                Skip(plan, action, tokens, StructuralSkip::CommentAttached);
            } else if (safety.IntersectsImmutable(Range(tree, tokens)) ||
                (safety.Gaps()[tokens.Begin].Preserve && safety.Gaps()[tokens.Begin].Source.Empty()) ||
                (safety.Gaps()[tokens.End].Preserve && safety.Gaps()[tokens.End].Source.Empty()))
            {
                Skip(plan, action, tokens, StructuralSkip::Protected);
            } else if (options.PreserveBareCR && safety.Gaps()[token].HasBareCR &&
                safety.Original(token).ends_with('\r'))
            {
                // Retain the first original separator of a zero-gap
                // deletion chain before admission: no cancellation or ID
                // remapping.
                Skip(plan, action, tokens, StructuralSkip::RequiredBody);
            } else {
                auto id = Operation(plan, action, tokens);
                // Token-only removal cannot take neighboring comments
                // or gaps.
                Literal(plan, Range(tree, tokens), "", id);
            }
        };
    if constexpr (Certified) {
        CheckAnalysisSource(tree, safety);
        std::pmr::vector<uint32_t> depths(&scratchMemory);
        for (RegionId id = 0; id < regions.size(); ++id) {
            const auto& node = regions[id];
            const bool control = controlEdits && Control(node.Kind);
            const bool nullStatement =
                options.RemoveNullStatements && node.Kind == NK::ExpressionStatement && node.Tokens.Size() == 1;
            const bool nullDeclaration =
                options.RemoveNullDeclarations && node.Kind == NK::EmptyDeclaration && node.Tokens.Size() == 1;
            const bool functionSuffix = options.RemoveNullDeclarations && node.Kind == NK::FunctionDefinition &&
                !node.Tokens.Empty() && tree.Kind(node.Tokens.End - 1) == TK::Operator_Semicolon;

            if (!(control || nullStatement || nullDeclaration || functionSuffix)) {
                continue;
            }

            const auto parent = node.Parent;
            const auto parentKind = parent == NoRegion ? NK::Error : regions[parent].Kind;
            const auto grandparent = parent == NoRegion ? NoRegion : regions[parent].Parent;
            auto previous = NoRegion;

            if ((nullStatement || nullDeclaration) && parent != NoRegion && id > parent + 1) {
                previous = id - 1;

                for (;;) {
                    while (previous != NoRegion && regions[previous].Parent != parent) {
                        previous = regions[previous].Parent;
                    }

                    if (previous == NoRegion || regions[previous].Kind != NK::Comment) {
                        break;
                    }

                    previous = previous > parent + 1 ? previous - 1 : NoRegion;
                }
            }

            bool sequence = parentKind == NK::TranslationUnit || parentKind == NK::NamespaceDeclaration;

            if (parentKind == NK::ExportDeclaration || parentKind == NK::LinkageSpecification) {
                auto header = regions[parent].Tokens.Begin + (parentKind == NK::ExportDeclaration ? 1 : 2);

                while (header < regions[parent].Tokens.End && tree.Kind(header) == TK::Comment) {
                    ++header;
                }

                sequence = header < regions[parent].Tokens.End && tree.Kind(header) == TK::Operator_OpenBrace &&
                    tree.MatchingDelimiter(header) < regions[parent].Tokens.End;
            }

            const auto depth = [&] {
                // Only admitted control edits use depth. Compute it once if
                // needed, keeping deeply nested controls linear in node count.
                if (depths.empty()) {
                    depths.resize(regions.size());

                    for (RegionId i = 0; i < regions.size(); ++i) {
                        depths[i] = regions[i].Parent == NoRegion ? 0 : depths[regions[i].Parent] + 1;
                    }
                }

                return depths[id];
            };
            visit(
                node,
                parentKind,
                previous == NoRegion ? NK::Error : regions[previous].Kind,
                grandparent == NoRegion ? NK::Error : regions[grandparent].Kind,
                sequence,
                depth,
                node.Status,
                !trailingLabels.empty() && trailingLabels[id],
                previous != NoRegion && !trailingLabels.empty() && trailingLabels[previous],
                id);
        }
    } else {
        size_t regionIndex = 0;
        Walk(
            tree,
            &safety,
            scratchMemory,
            [&] (
                const parser::Node& node,
                NK parent,
                NK previous,
                NK grandparent,
                bool sequence,
                uint32_t depth,
                RegionStatus status,
                bool trailingLabel,
                bool previousTrailingLabel) {
                    visit(
                        node,
                        parent,
                        previous,
                        grandparent,
                        sequence,
                        [depth] {
                            return depth;
                        },
                        status,
                        trailingLabel,
                        previousTrailingLabel,
                        regionIndex++);
                },
            trailingLabels);
    }
    std::string relativeInput;

    if ((options.SortIncludes || options.Delimiters != IncludeDelimiters::Preserve) &&
        options.PathBase == IncludePathBase::ProjectRoot && !includes.InputFilePath.empty())
    {
        const std::filesystem::path input(includes.InputFilePath);

        if (input.is_absolute()) {
            std::error_code error;
            const auto root = std::filesystem::current_path(error);

            if (!error) {
                const auto relative = input.lexically_normal().lexically_relative(root);

                // Project-root includes and filenames passed relative to the
                // working directory must use the same coordinate system.
                // External absolute roots retain their lexical coordinates.
                if (!relative.empty() && *relative.begin() != "..") {
                    relativeInput = relative.generic_string();
                    includes.InputFilePath = relativeInput;
                }
            }
        }
    }

    if (options.SortIncludes) {
        Includes(tree, safety, plan, scratchMemory, options, includes);
    }

    PlanTextPolicyEdits(tree, safety, options, plan, fileMemory, scratchMemory, includes.InputFilePath);
    PlanRulerEdits(tree, safety, options, plan, fileMemory, scratchMemory);
    Sort(plan);
    (void)Validate(tree.Source(), plan);
    return plan;
}

} // namespace

StructuralPlan PlanStructuralEdits(
    const parser::SyntaxTree& tree,
    const SourceAnalysis& safety,
    StructuralOptions options,
    std::pmr::memory_resource& fileMemory,
    std::pmr::memory_resource& scratchMemory,
    IncludeSortContext includes)
{
    return PlanStructuralEditsImpl<false>(tree, safety, options, fileMemory, scratchMemory, includes);
}

StructuralPlan detail::PlanStructuralEditsCertified(
    const parser::SyntaxTree& tree,
    const SourceAnalysis& safety,
    StructuralOptions options,
    std::pmr::memory_resource& fileMemory,
    std::pmr::memory_resource& scratchMemory,
    IncludeSortContext includes)
{
    return PlanStructuralEditsImpl<true>(tree, safety, options, fileMemory, scratchMemory, includes);
}

namespace {

StructuralMaterialization MaterializeStructuralEditsImpl(
    const parser::SyntaxTree& tree,
    const StructuralPlan& plan,
    std::pmr::memory_resource& fileMemory,
    std::pmr::memory_resource& scratchMemory,
    std::pmr::vector<detail::CopiedTokenRun>* copied)
{
    auto size = Validate(tree.Source(), plan);
    auto boundary = [&] (parser::SourceOffset offset) {
        auto next = FirstBeginningAt(tree, offset);
        return !next || tree.Token(next - 1).Text.End <= offset;
    };

    for (const auto& edit : plan.Edits) {
        if (!boundary(edit.Source.Begin) || !boundary(edit.Source.End)) {
            throw std::invalid_argument("structural edit splits an original token");
        }
    }

    for (const auto& piece : plan.Pieces) {
        if (piece.FromSource && (!boundary(piece.Source.Begin) || !boundary(piece.Source.End))) {
            throw std::invalid_argument("structural piece splits an original token");
        }
    }

    StructuralMaterialization result(fileMemory);
    result.Text.reserve(size);

    if (copied) {
        copied->clear();
        copied->reserve(plan.Pieces.size() + plan.Edits.size() + 1);
    } else {
        result.OriginalTokens.resize(tree.TokenCount());
    }

    result.Provenance.reserve(plan.Pieces.size() + plan.Edits.size() + 1);
    result.GeneratedAdjacencies.reserve(plan.Edits.size());
    auto copy = [&] (SR source, StructuralActionId operation) {
        if (source.Empty()) {
            return;
        }

        auto begin = static_cast<parser::SourceOffset>(result.Text.size());
        result.Text.append(tree.SourceText(source));
        result.Provenance
            .push_back({{begin, static_cast<parser::SourceOffset>(result.Text.size())}, source, operation, false});

        const auto first = FirstBeginningAt(tree, source.Begin);

        if (copied) {
            const auto end = FirstBeginningAt(tree, source.End);

            if (first != end) {
                copied->push_back({{first, end}, source.Begin, begin});
            }

            return;
        }

        for (TI i = first; i < tree.TokenCount(); ++i) {
            auto token = tree.Token(i).Text;

            if (token.Begin >= source.End) {
                break;
            }

            if (token.End > source.End || result.OriginalTokens[i].Present) {
                throw std::invalid_argument("split/duplicated original token");
            }

            result.OriginalTokens[i] = {{begin + token.Begin - source.Begin, begin + token.End - source.Begin}, true};
        }
    };
    parser::SourceOffset cursor = 0;

    for (const auto& edit : plan.Edits) {
        copy({cursor, edit.Source.Begin}, NoStructuralAction);
        auto begin = static_cast<parser::SourceOffset>(result.Text.size());

        for (auto p = edit.PieceBegin; p < edit.PieceEnd; ++p) {
            const auto& piece = plan.Pieces[p];

            if (piece.FromSource) {
                copy(piece.Source, edit.Operation);
            } else {
                auto start = static_cast<parser::SourceOffset>(result.Text.size());
                result.Text.append(piece.Literal);
                result.Provenance.push_back({
                    {start, static_cast<parser::SourceOffset>(result.Text.size())},
                    edit.Source,
                    edit.Operation,
                    true,
                });
            }
        }

        result.GeneratedAdjacencies
            .push_back({{begin, static_cast<parser::SourceOffset>(result.Text.size())}, edit.Operation});
        cursor = edit.Source.End;
    }

    copy({cursor, static_cast<parser::SourceOffset>(tree.Source().size())}, NoStructuralAction);
    if (copied) {
        std::sort(
            copied->begin(),
            copied->end(),
            [] (const auto& a, const auto& b) {
                return a.Original.Begin < b.Original.Begin;
            });

        for (size_t i = 1; i < copied->size(); ++i) {
            if ((*copied)[i - 1].Original.End > (*copied)[i].Original.Begin) {
                throw std::invalid_argument("split/duplicated original token");
            }
        }

        for (size_t i = 0; i < plan.Edits.size(); ++i) {
            const auto& edit = plan.Edits[i];
            const auto leftIndex = FirstBeginningAt(tree, edit.Source.Begin);
            const auto rightIndex = FirstBeginningAt(tree, edit.Source.End);
            auto& range = result.GeneratedAdjacencies[i].Output;
            auto left = std::lower_bound(
                copied->begin(),
                copied->end(),
                leftIndex,
                [] (const auto& run, TI token) {
                    return run.Original.Begin < token;
                });

            if (left == copied->begin()) {
                range.Begin = 0;
            } else {
                --left;
                const auto token = std::min(leftIndex, left->Original.End) - 1;
                range.Begin = std::min(range.Begin, detail::CopiedTokenPosition(tree, *left, token).Output.End);
            }

            const auto right = std::lower_bound(
                copied->begin(),
                copied->end(),
                rightIndex,
                [] (const auto& run, TI token) {
                    return run.Original.End <= token;
                });

            if (right == copied->end()) {
                range.End = static_cast<parser::SourceOffset>(result.Text.size());
            } else {
                const auto token = std::max(rightIndex, right->Original.Begin);
                range.End = std::max(range.End, detail::CopiedTokenPosition(tree, *right, token).Output.Begin);
            }
        }
    } else {
        // Linear nearest-survivor tables bound chains of neighboring deletions.
        std::pmr::vector<TI> before(tree.TokenCount() + 1, tree.TokenCount(), &scratchMemory);
        std::pmr::vector<TI> after(tree.TokenCount() + 1, tree.TokenCount(), &scratchMemory);
        for (TI i = 0; i < tree.TokenCount(); ++i) {
            before[i + 1] = result.OriginalTokens[i].Present ? i : before[i];
        }

        for (TI i = tree.TokenCount(); i > 0; --i) {
            after[i - 1] = result.OriginalTokens[i - 1].Present ? i - 1 : after[i];
        }

        for (size_t i = 0; i < plan.Edits.size(); ++i) {
            const auto& edit = plan.Edits[i];
            auto leftIndex = FirstBeginningAt(tree, edit.Source.Begin);
            auto rightIndex = FirstBeginningAt(tree, edit.Source.End);
            auto left = before[leftIndex];
            auto right = after[rightIndex];
            auto& range = result.GeneratedAdjacencies[i].Output;

            if (left != tree.TokenCount()) {
                range.Begin = std::min(range.Begin, result.OriginalTokens[left].Output.End);
            } else {
                range.Begin = 0;
            }

            if (right != tree.TokenCount()) {
                range.End = std::max(range.End, result.OriginalTokens[right].Output.Begin);
            } else {
                range.End = static_cast<parser::SourceOffset>(result.Text.size());
            }
        }
    }

    return result;
}

} // namespace

StructuralMaterialization MaterializeStructuralEdits(
    const parser::SyntaxTree& tree,
    const StructuralPlan& plan,
    std::pmr::memory_resource& fileMemory,
    std::pmr::memory_resource& scratchMemory)
{
    return MaterializeStructuralEditsImpl(tree, plan, fileMemory, scratchMemory, nullptr);
}

StructuralMaterialization detail::MaterializeStructuralEditsCompact(
    const parser::SyntaxTree& tree,
    const StructuralPlan& plan,
    std::pmr::memory_resource& fileMemory,
    std::pmr::memory_resource& scratchMemory,
    std::pmr::vector<CopiedTokenRun>& copied)
{
    return MaterializeStructuralEditsImpl(tree, plan, fileMemory, scratchMemory, &copied);
}

void detail::MaterializeOriginalTokenOffsets(
    const parser::SyntaxTree& original,
    StructuralMaterialization& materialized,
    std::span<const CopiedTokenRun> copied)
{
    if (!materialized.OriginalTokens.empty()) {
        return;
    }

    materialized.OriginalTokens.resize(original.TokenCount());

    for (const auto& run : copied) {
        for (auto token = run.Original.Begin; token < run.Original.End; ++token) {
            materialized.OriginalTokens[token] = CopiedTokenPosition(original, run, token);
        }
    }
}

namespace {

bool NamespaceClosingComment(const parser::SyntaxTree& tree, parser::TokenRange header, TI comment)
{
    auto text = tree.TokenText(comment);

    if (!text.starts_with("//")) {
        return false;
    }

    text.remove_prefix(2);
    const auto trim = [&] {
        const auto begin = text.find_first_not_of(" \t\r");
        text = begin == std::string_view::npos ? std::string_view{} : text.substr(begin);
        const auto end = text.find_last_not_of(" \t\r");
        text = end == std::string_view::npos ? std::string_view{} : text.substr(0, end + 1);
    };
    trim();

    if (text == "anonymous namespace" || text == "unnamed namespace" || text == "Unnamed namespace") {
        return true;
    }

    if (text == "namespace") {
        return true;
    }

    if (text.starts_with("namespace") && text.size() > 9 && (text[9] == ' ' || text[9] == '\t')) {
        text.remove_prefix(9);
        trim();
        bool start = true;

        while (!text.empty()) {
            if (!start && text.starts_with("::")) {
                text.remove_prefix(2);
                start = true;
                continue;
            }

            const auto cp = unicode::utf8::GetFirstCodePoint(text);

            if (!cp ||
                (cp->Value != '_' && !(start ? unicode::IsXIDStart(cp->Value) : unicode::IsXIDContinue(cp->Value))))
            {
                return false;
            }

            text.remove_prefix(cp->Size);
            start = false;
        }

        return !start;
    }

    // A bare label is unambiguous only when it spells this namespace's name.
    bool named = false;

    for (auto token = header.Begin; token < header.End; ++token) {
        if (tree.Kind(token) != TK::Identifier && tree.Kind(token) != TK::Operator_ScopeResolution) {
            continue;
        }

        const auto name = tree.TokenText(token);

        if (!text.starts_with(name)) {
            return false;
        }

        text.remove_prefix(name.size());
        named = true;
    }

    return named && text.empty();
}

template <bool Certified>
NamespaceCommentPlan PlanNamespaceCommentsImpl(
    const parser::SyntaxTree& original,
    const SourceAnalysis& safety,
    NamespaceCommentOptions options,
    std::pmr::memory_resource& fileMemory,
    std::pmr::memory_resource& scratchMemory)
{
    NamespaceCommentPlan plan(fileMemory);
    plan.Options = options;

    if (options.Spaces > 32) {
        throw std::invalid_argument("namespace comment spacing exceeds 32");
    }

    if (options.Policy == NamespaceCommentPolicy::Preserve) {
        return plan;
    }
    std::pmr::vector<TI> namespaceKeywords(&scratchMemory);
    for (TI i = 0; i < original.TokenCount(); ++i) {
        if (original.Kind(i) == TK::Keyword_namespace) {
            namespaceKeywords.push_back(i);
        }
    }

    auto visit = [&] (const auto& node) {
        if (node.Kind != NK::NamespaceDeclaration) {
            // A keyword in opaque/generic grammar is evidence to SKIP,
            // never permission to invent a namespace identity or
            // inspect its text.
            if (node.Kind == NK::Declaration || node.Kind == NK::UnparsedSyntax) {
                const auto keyword =
                    std::lower_bound(namespaceKeywords.begin(), namespaceKeywords.end(), node.Tokens.Begin);

                if (keyword != namespaceKeywords.end() && *keyword < node.Tokens.End) {
                    plan.Skipped
                        .push_back({StructuralAction::NamespaceComments, node.Tokens, StructuralSkip::Incomplete});
                }
            }

            return;
        }

        if (node.Tokens.Empty()) {
            return;
        }

        auto fail = [&] (StructuralSkip reason) {
            plan.Skipped.push_back({StructuralAction::NamespaceComments, node.Tokens, reason});
        };
        TI open = original.TokenCount();
        bool expectName = true, named = false, valid = true;

        for (TI i = node.Tokens.Begin; i < node.Tokens.End; ++i) {
            auto kind = original.Kind(i);

            if (kind == TK::Operator_OpenBrace) {
                open = i;
                break;
            }

            if ((kind == TK::Keyword_namespace && expectName && !named) || (kind == TK::Keyword_inline && expectName)) {
                continue;
            }

            if (kind == TK::Identifier && expectName) {
                expectName = false;
                named = true;
            } else if (kind == TK::Operator_ScopeResolution && !expectName) {
                expectName = true;
            } else {
                valid = false;
                break;
            }
        }

        if (open == original.TokenCount() || !valid || (expectName && named))
            return; // alias/non-header
        auto close = original.MatchingDelimiter(open);

        // Opaque body syntax does not change a parsed namespace identity.
        // Its matching brace must still end this declaration exactly.
        if (close >= node.Tokens.End || close + 1 != node.Tokens.End || original.Kind(close) != TK::Operator_CloseBrace)
        {
            fail(StructuralSkip::Incomplete);
            return;
        }

        auto begin = original.Token(close).Text.End;
        auto comment = original.TokenCount();
        auto suffix = close + 1;

        while (suffix < original.TokenCount() && original.Kind(suffix) == TK::Operator_Semicolon &&
            Horizontal(original.WhitespaceBefore(suffix)))
        {
            ++suffix;
        }

        if (suffix < original.TokenCount() && original.Kind(suffix) == TK::Comment &&
            Horizontal(original.WhitespaceBefore(suffix)))
        {
            comment = suffix;
        }

        SR window{begin, comment == original.TokenCount() ? begin : original.Token(comment).Text.End};

        if (safety.IntersectsImmutable(Range(original, {node.Tokens.Begin, open + 1})) ||
            safety.IntersectsImmutable(Range(original, {close, close + 1})) || safety.IntersectsImmutable(window))
        {
            fail(StructuralSkip::Protected);
            return;
        }

        const parser::TokenRange header{node.Tokens.Begin, open};
        const bool explanatory =
            comment != original.TokenCount() && !NamespaceClosingComment(original, header, comment);
        plan.Namespaces.push_back({node.Tokens, header, open, close, comment, explanatory});
    };

    if constexpr (Certified) {
        CheckAnalysisSource(original, safety);

        for (const auto& node : safety.Regions()) {
            if (node.Kind == NK::NamespaceDeclaration || node.Kind == NK::Declaration ||
                node.Kind == NK::UnparsedSyntax)
            {
                visit(node);
            }
        }
    } else {
        Walk(
            original,
            &safety,
            scratchMemory,
            [&] (const parser::Node& node, NK, NK, NK, bool, uint32_t, RegionStatus, bool, bool) {
                visit(node);
            });
    }

    return plan;
}

} // namespace

NamespaceCommentPlan PlanNamespaceComments(
    const parser::SyntaxTree& original,
    const SourceAnalysis& safety,
    NamespaceCommentOptions options,
    std::pmr::memory_resource& fileMemory,
    std::pmr::memory_resource& scratchMemory)
{
    return PlanNamespaceCommentsImpl<false>(original, safety, options, fileMemory, scratchMemory);
}

NamespaceCommentPlan detail::PlanNamespaceCommentsCertified(
    const parser::SyntaxTree& original,
    const SourceAnalysis& safety,
    NamespaceCommentOptions options,
    std::pmr::memory_resource& fileMemory,
    std::pmr::memory_resource& scratchMemory)
{
    return PlanNamespaceCommentsImpl<true>(original, safety, options, fileMemory, scratchMemory);
}

namespace {

template <class Mapping>
StructuralPlan ResolveNamespaceCommentsImpl(
    const parser::SyntaxTree& original,
    const NamespaceCommentPlan& plan,
    std::string_view output,
    const Mapping& mapping,
    std::span<const SR> outputProtection,
    std::pmr::memory_resource& fileMemory,
    std::pmr::memory_resource& scratchMemory,
    bool certified)
{
    (void)CheckedSourceAnalysisSize(output.size());
    StructuralPlan resolved(fileMemory);
    resolved.Skipped.assign(plan.Skipped.begin(), plan.Skipped.end());

    if (plan.Options.Policy == NamespaceCommentPolicy::Preserve) {
        return resolved;
    }

    if (mapping.size() != original.TokenCount()) {
        throw std::invalid_argument("original token mapping size");
    }

    parser::SourceOffset previousEnd = 0;

    for (auto r : outputProtection) {
        if (r.Begin > r.End || r.End > output.size() || r.Begin < previousEnd) {
            throw std::invalid_argument("output protection ordering");
        }

        previousEnd = r.End;
    }

    if (plan.Namespaces.empty()) {
        return resolved;
    }

    std::pmr::vector<parser::SourceOffset> lines(&scratchMemory);
    auto line = [&] (parser::SourceOffset offset) {
        if (lines.empty()) {
            lines.push_back(0);

            for (auto i = output.find('\n'); i != std::string_view::npos; i = output.find('\n', i + 1)) {
                lines.push_back(static_cast<parser::SourceOffset>(i + 1));
            }
        }

        return static_cast<size_t>(std::upper_bound(lines.begin(), lines.end(), offset) - lines.begin() - 1);
    };
    const auto count = original.TokenCount();
    auto valid = [&] (TI token) {
        if (token >= count || !mapping[token].Present) {
            return false;
        }

        const auto r = mapping[token].Output;

        if (r.Begin > r.End || r.End > output.size()) {
            return false;
        }

        if (certified) {
            return true;
        }

        const auto sourceText = original.TokenText(token);
        const auto outputText = output.substr(r.Begin, r.End - r.Begin);
        return outputText == sourceText ||
            (original.Kind(token) == TK::Comment && sourceText.starts_with("/*") &&
                ReindentedCommentMatches(sourceText, outputText));
    };
    struct Interior
    {
        parser::SourceOffset Begin = UINT32_MAX;
        parser::SourceOffset End = 0;
        bool Invalid = false;

        void Merge(const Interior& other)
        {
            Begin = std::min(Begin, other.Begin);
            End = std::max(End, other.End);
            Invalid |= other.Invalid;
        }
    };

    std::pmr::vector<Interior> interiors(plan.Namespaces.size(), &scratchMemory);
    std::pmr::vector<size_t> active(&scratchMemory);
    size_t nextNamespace = 0;

    // Namespace intervals are nested/disjoint in original-token preorder.
    // Visit each token once, merging a completed child's envelope into its
    // parent once. Containment checks do not require original interior order:
    // authorized include permutations and deleted tokens remain admissible.
    if (!certified) {
        for (TI token = 0; token < count; ++token) {
            while (!active.empty() && plan.Namespaces[active.back()].Close == token) {
                auto closed = active.back();
                active.pop_back();

                if (!active.empty()) {
                    interiors[active.back()].Merge(interiors[closed]);
                }
            }

            if (!active.empty() && mapping[token].Present) {
                auto r = mapping[token].Output;
                interiors[active.back()].Merge({r.Begin, r.End, !valid(token)});
            }

            while (nextNamespace < plan.Namespaces.size() && plan.Namespaces[nextNamespace].Open == token) {
                const auto& ns = plan.Namespaces[nextNamespace];

                if (ns.Close <= token || ns.Close >= count || ns.Header.Begin >= ns.Header.End ||
                    ns.Header.End != token || (!active.empty() && ns.Close >= plan.Namespaces[active.back()].Close))
                {
                    throw std::invalid_argument("invalid original namespace identity");
                }

                active.push_back(nextNamespace++);
            }
        }

        if (!active.empty() || nextNamespace != plan.Namespaces.size()) {
            throw std::invalid_argument("unordered original namespace identities");
        }
    }

    for (size_t namespaceIndex = 0; namespaceIndex < plan.Namespaces.size(); ++namespaceIndex) {
        const auto& ns = plan.Namespaces[namespaceIndex];
        auto skip = [&] (StructuralSkip reason) {
            Skip(resolved, StructuralAction::NamespaceComments, ns.Owner, reason);
        };
        bool existing = ns.Comment != original.TokenCount();
        bool validHeader = true;
        parser::SourceOffset headerEnd = 0;

        for (TI token = ns.Header.Begin; token < ns.Header.End; ++token) {
            if (!valid(token) || mapping[token].Output.Begin < headerEnd) {
                validHeader = false;
                break;
            }

            headerEnd = mapping[token].Output.End;
        }

        if (!validHeader || !valid(ns.Open) || !valid(ns.Close) || (existing && !valid(ns.Comment)) ||
            headerEnd > mapping[ns.Open].Output.Begin || mapping[ns.Open].Output.End > mapping[ns.Close].Output.Begin)
        {
            skip(StructuralSkip::InvalidMapping);
            continue;
        }

        const auto& interior = interiors[namespaceIndex];
        // Search only the adjacent deleted runs. Valid namespace headers and
        // closers stop neighboring queries, so nested namespaces do not
        // repeatedly scan their interiors.
        auto previous = count;

        for (auto token = ns.Header.Begin; token > 0;) {
            if (mapping[--token].Present) {
                previous = token;
                break;
            }
        }

        auto following = ns.Close + 1;

        while (following < count && !mapping[following].Present) {
            ++following;
        }

        if (interior.Invalid ||
            (interior.Begin != UINT32_MAX &&
                (interior.Begin < mapping[ns.Open].Output.End || interior.End > mapping[ns.Close].Output.Begin)) ||
            (previous != count &&
                (!valid(previous) || mapping[previous].Output.End > mapping[ns.Header.Begin].Output.Begin)) ||
            (following != count &&
                (!valid(following) || mapping[following].Output.Begin < mapping[ns.Close].Output.End)))
        {
            skip(StructuralSkip::InvalidMapping);
            continue;
        }

        auto begin = mapping[ns.Close].Output.End;
        auto end = begin;
        const auto shortNamespace = [&] {
            if (!plan.Options.ShortNamespaceLines) {
                return false;
            }

            const auto openLine = line(mapping[ns.Open].Output.Begin);
            const auto closeLine = line(mapping[ns.Close].Output.Begin);
            const auto interior = closeLine > openLine ? closeLine - openLine - 1 : 0;
            return interior <= plan.Options.ShortNamespaceLines;
        };

        if (ns.ExplanatoryComment) {
            if (plan.Options.Policy != NamespaceCommentPolicy::Remove) {
                if (shortNamespace()) {
                    skip(StructuralSkip::ShortNamespace);
                } else {
                    Skip(
                        resolved,
                        StructuralAction::NamespaceComments,
                        {ns.Close, ns.Close + 1},
                        StructuralSkip::MissingNamespaceComment);
                }
            }

            continue;
        }

        if (existing) {
            // Comments after a null suffix become closing comments only if
            // normalization actually removed every intervening semicolon.
            if (following < ns.Comment) {
                skip(StructuralSkip::FollowingText);
                continue;
            }

            auto r = mapping[ns.Comment].Output;

            if (r.Begin < begin || !Horizontal(output.substr(begin, r.Begin - begin))) {
                skip(StructuralSkip::InvalidMapping);
                continue;
            }

            end = r.End;

            // Keep token-owned CR before the existing LF, not in replacement.
            if (end > begin && output[end - 1] == '\r' && end < output.size() && output[end] == '\n') {
                --end;
            }

            if (plan.Options.Policy == NamespaceCommentPolicy::AddMissing) {
                continue;
            }
        } else {
            if (plan.Options.Policy == NamespaceCommentPolicy::Remove) {
                continue;
            }

            // A surviving null declaration can own the closing comment.
            // Layout may move that suffix to another line, but it must not
            // turn an originally occupied suffix into a missing comment.
            if (following != count && original.Kind(following) == TK::Operator_Semicolon) {
                skip(StructuralSkip::FollowingText);
                continue;
            }

            if (shortNamespace()) {
                skip(StructuralSkip::ShortNamespace);
                continue;
            }

            const auto newline = output.find('\n', begin);
            const auto lineEnd = static_cast<parser::SourceOffset>(
                newline == std::string_view::npos ? output.size() : newline);
            auto suffix = output.substr(begin, lineEnd - begin);

            if (suffix.ends_with('\r') &&
                (!plan.Options.PreserveBareCR || (lineEnd < output.size() && output[lineEnd] == '\n')))
            {
                suffix.remove_suffix(1);
            }

            if (!Horizontal(suffix)) {
                skip(StructuralSkip::FollowingText);
                continue;
            }

            // Replace only horizontal trailing space; do not move any atom.
            end = begin + static_cast<parser::SourceOffset>(suffix.size());
        }

        if (Intersects(outputProtection, {begin, end}) || Intersects(outputProtection, mapping[ns.Close].Output)) {
            skip(StructuralSkip::Protected);
            continue;
        }

        auto id = Operation(resolved, StructuralAction::NamespaceComments, ns.Owner);
        auto pieceBegin = static_cast<uint32_t>(resolved.Pieces.size());

        if (plan.Options.Policy != NamespaceCommentPolicy::Remove) {
            // A single shared static space view avoids per-comment strings.
            static constexpr std::string_view Spaces = "                                ";

            if (plan.Options.Spaces > Spaces.size()) {
                throw std::invalid_argument("namespace comment spacing exceeds 32");
            }

            if (plan.Options.Spaces) {
                resolved.Pieces.push_back({{}, Spaces.substr(0, plan.Options.Spaces), false});
            }

            resolved.Pieces.push_back({{}, "// namespace", false});
            bool firstName = true;

            for (TI i = ns.Header.Begin; i < ns.Header.End; ++i) {
                auto kind = original.Kind(i);

                if (kind != TK::Identifier && kind != TK::Operator_ScopeResolution) {
                    continue;
                }

                if (firstName) {
                    resolved.Pieces.push_back({{}, " ", false});
                    firstName = false;
                }

                resolved.Pieces.push_back({{}, original.TokenText(i), false});
            }
        }

        auto cursor = begin;
        bool identical = true;

        for (size_t p = pieceBegin; p < resolved.Pieces.size(); ++p) {
            auto literal = resolved.Pieces[p].Literal;

            if (literal.size() > end - cursor || output.substr(cursor, literal.size()) != literal) {
                identical = false;
                break;
            }

            cursor += static_cast<parser::SourceOffset>(literal.size());
        }

        if (identical && cursor == end) {
            resolved.Pieces.resize(pieceBegin);
            resolved.Operations.pop_back();
        } else {
            resolved.Edits.push_back({{begin, end}, pieceBegin, static_cast<uint32_t>(resolved.Pieces.size()), id});
        }
    }

    Sort(resolved);
    (void)Validate(output, resolved);
    return resolved;
}

struct SparseNamespaceMapping
{
    size_t Count;
    std::span<const detail::NamespaceTokenPosition> Positions;

    size_t size() const
    {
        return Count;
    }

    TokenOutputOffset operator[](TI token) const
    {
        const auto found = std::lower_bound(
            Positions.begin(),
            Positions.end(),
            token,
            [] (const auto& position, TI value) {
                return position.Original < value;
            });
        return found != Positions.end() && found->Original == token ? found->Position : TokenOutputOffset{};
    }
};

} // namespace

StructuralPlan ResolveNamespaceComments(
    const parser::SyntaxTree& original,
    const NamespaceCommentPlan& plan,
    std::string_view output,
    std::span<const TokenOutputOffset> mapping,
    std::span<const SR> outputProtection,
    std::pmr::memory_resource& fileMemory,
    std::pmr::memory_resource& scratchMemory)
{
    return ResolveNamespaceCommentsImpl(
        original,
        plan,
        output,
        mapping,
        outputProtection,
        fileMemory,
        scratchMemory,
        false);
}

StructuralPlan detail::ResolveNamespaceCommentsCertified(
    const parser::SyntaxTree& original,
    const NamespaceCommentPlan& plan,
    std::string_view output,
    std::span<const NamespaceTokenPosition> positions,
    std::span<const SR> outputProtection,
    std::pmr::memory_resource& fileMemory,
    std::pmr::memory_resource& scratchMemory)
{
    return ResolveNamespaceCommentsImpl(
        original,
        plan,
        output,
        SparseNamespaceMapping{original.TokenCount(), positions},
        outputProtection,
        fileMemory,
        scratchMemory,
        true);
}

std::pmr::string MaterializeNamespaceComments(
    std::string_view output,
    const StructuralPlan& resolved,
    std::pmr::memory_resource& fileMemory)
{
    std::pmr::string result(&fileMemory);
    result.reserve(Validate(output, resolved));
    parser::SourceOffset cursor = 0;

    for (const auto& edit : resolved.Edits) {
        result.append(output.substr(cursor, edit.Source.Begin - cursor));

        for (auto p = edit.PieceBegin; p < edit.PieceEnd; ++p) {
            const auto& piece = resolved.Pieces[p];

            if (piece.FromSource) {
                throw std::invalid_argument("comment plan requires literal/name pieces");
            }

            result.append(piece.Literal);
        }

        cursor = edit.Source.End;
    }

    result.append(output.substr(cursor));
    return result;
}

} // namespace formatter
