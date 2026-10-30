#include "structural_edits.hpp"

#include "text_policies.hpp"

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
        bool sequence = node->Kind == NK::TranslationUnit || node->Kind == NK::NamespaceDeclaration;
        if (node->Kind == NK::ExportDeclaration || node->Kind == NK::LinkageSpecification) {
            auto header = node->Tokens.Begin + (node->Kind == NK::ExportDeclaration ? 1 : 2);
            while (header < node->Tokens.End && tree.Kind(header) == TK::Comment) {
                ++header;
            }
            sequence = header < node->Tokens.End && tree.Kind(header) == TK::Operator_OpenBrace &&
                tree.MatchingDelimiter(header) < node->Tokens.End;
        }
        stack.push_back({node, node->Child(), NK::Error, sequence});
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
                known = contexts[id] & KnownSyntax;
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

// Reuse component storage for lexical normalization; no filesystem queries or
// per-include owning paths. Unknown leading parents remain explicit.
void AppendComponents(std::pmr::vector<std::string_view>& parts, std::string_view path, bool absolute)
{
    while (!path.empty()) {
        const auto slash = path.find('/');
        const auto part = path.substr(0, slash);
        if (part == "..") {
            if (!parts.empty() && parts.back() != "..") {
                parts.pop_back();
            } else if (!absolute) {
                parts.push_back(part);
            }
        } else if (!part.empty() && part != ".") {
            parts.push_back(part);
        }
        if (slash == std::string_view::npos) {
            break;
        }
        path.remove_prefix(slash + 1);
    }
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
    std::string relativeInput;
    if (options.PathBase == IncludePathBase::ProjectRoot && !context.InputFilePath.empty()) {
        const std::filesystem::path input(context.InputFilePath);
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
                    context.InputFilePath = relativeInput;
                }
            }
        }
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
        AppendComponents(current, inputDirectory, inputAbsolute);
    }
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
                const bool local = !atom.Angled &&
                    (atom.Path.find('/') == std::string_view::npos || atom.Path.starts_with("./") ||
                        atom.Path.starts_with("../"));
                const bool relative =
                    !atom.Path.starts_with('/') && (local || options.PathBase == IncludePathBase::SourceDirectory);
                const bool absolute = relative ? inputAbsolute : atom.Path.starts_with('/');
                if (relative) {
                    parts.assign(current.begin(), current.end());
                }
                AppendComponents(parts, Directory(atom.Path), absolute);
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
                    AppendComponents(grouped, canonical, groupAbsolute);
                } else if (!prefix.empty()) {
                    const auto base = relative ? current.size() : 0;
                    const auto suffix = base + context.Groups->PrefixDepth(key.Prefix);
                    const auto count = key.Depth == IncludeGroupRule::MaxDepth
                        ? parts.size() - suffix
                        : std::min(static_cast<std::size_t>(key.Depth), parts.size() - suffix);
                    grouped.insert(grouped.end(), parts.begin(), parts.begin() + base);
                    AppendComponents(grouped, canonical, groupAbsolute);
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
    std::pmr::vector<uint8_t> ownership(
        plan.Operations.size(), 0, plan.Edits.get_allocator().resource());
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

StructuralPlan PlanStructuralEdits(
    const parser::SyntaxTree& tree,
    const SourceAnalysis& safety,
    StructuralOptions options,
    std::pmr::memory_resource& fileMemory,
    std::pmr::memory_resource& scratchMemory,
    IncludeSortContext includes)
{
    StructuralPlan plan(fileMemory);
    // Prefix summaries avoid rescanning nested control bodies.
    std::pmr::vector<uint32_t> barriers(&scratchMemory);
    barriers.resize(tree.TokenCount() + 1);
    for (TI i = 0; i < tree.TokenCount(); ++i) {
        barriers[i + 1] = barriers[i] + (tree.Kind(i) == TK::Comment || tree.Kind(i) == TK::PreprocessingDirective);
    }
    // Labels stop at their colon in this parser. A control containing an
    // unresolved label also ends before its required statement, which can be
    // a sibling of the whole control. Propagate this once in region postorder;
    // compound boundaries stop propagation, rather than poisoning the file.
    auto regions = safety.Regions();
    std::pmr::vector<uint8_t> trailingLabels(regions.size(), 0, &scratchMemory);
    for (size_t i = regions.size(); i > 0; --i) {
        const auto& region = regions[i - 1];
        trailingLabels[i - 1] |= region.Kind == NK::LabelStatement || region.Kind == NK::CaseLabel;
        if (trailingLabels[i - 1] && region.Parent != NoRegion && Control(regions[region.Parent].Kind)) {
            trailingLabels[region.Parent] = true;
        }
    }
    TI namespaceSuffixEnd = tree.TokenCount();
    auto nullContexts = options.RemoveNullStatements || options.RemoveNullDeclarations
        ? NullRemovalContexts(tree, regions, scratchMemory)
        : std::pmr::vector<uint8_t>(&scratchMemory);
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
            bool declarationSequence,
            uint32_t depth,
            RegionStatus status,
            bool trailingLabel,
            bool previousTrailingLabel) {
                const auto region = regionIndex++;
                if (Control(node.Kind)) {
                    bool attributes = false, labels = false, directives = false;
                    for (auto* child = node.Child(); child; child = child->NextSibling) {
                        attributes |= Attribute(child->Kind);
                        labels |= child->Kind == NK::CaseLabel || child->Kind == NK::LabelStatement;
                        directives |=
                            child->Kind == NK::PreprocessingDirective || child->Kind == NK::PreprocessingConditional;
                    }
                    for (auto* body = node.Child(); body; body = body->NextSibling) {
                        if (Header(body->Kind) || Attribute(body->Kind) || body->Kind == NK::CompoundStatement ||
                            body->Kind == NK::Comment || body->Kind == NK::PreprocessingDirective ||
                            body->Tokens.Empty())
                        {
                            continue;
                        }
                        auto tokens = body->Tokens;
                        bool null = body->Kind == NK::ExpressionStatement && tokens.Size() == 1 &&
                            tree.Kind(tokens.Begin) == TK::Operator_Semicolon;
                        if (!(null ? options.EmptyControlBody : options.AddMissingControlBraces)) {
                            continue;
                        }
                        // Preserve the conventional else-if chain; nested
                        // then-controls are wrapped together, with closes
                        // ordered deepest-first.
                        if (body->Kind == NK::IfStatement && tokens.Begin &&
                            tree.Kind(tokens.Begin - 1) == TK::Keyword_else)
                        {
                            continue;
                        }
                        auto action = null
                            ? StructuralAction::EmptyControlBody
                            : StructuralAction::AddMissingControlBraces;
                        auto range = Range(tree, tokens);
                        if (status != RegionStatus::Complete) {
                            Skip(plan, action, tokens, StructuralSkip::Incomplete);
                        } else if (attributes) {
                            Skip(plan, action, tokens, StructuralSkip::Attributes);
                        } else if (trailingLabel || labels || body->Kind == NK::LabelStatement ||
                            body->Kind == NK::CaseLabel)
                        {
                            Skip(plan, action, tokens, StructuralSkip::LabelBody);
                        } else if (directives) {
                            Skip(plan, action, tokens, StructuralSkip::DirectiveBoundary);
                        } else if (barriers[node.Tokens.End] != barriers[node.Tokens.Begin] ||
                            AdjacentComment(tree, tokens))
                        {
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
                                Literal(plan, range, "{}", id, depth);
                            } else {
                                Literal(plan, {range.Begin, range.Begin}, "{", id, depth, StructuralEdit::Edge::Open);
                                Literal(plan, {range.End, range.End}, "}", id, depth, StructuralEdit::Edge::Close);
                            }
                        }
                    }
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
                if (status != RegionStatus::Complete || (nullContexts[region] & requiredContext) != requiredContext) {
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
            },
        trailingLabels);
    if (options.SortIncludes) {
        Includes(tree, safety, plan, scratchMemory, options, includes);
    }
    PlanTextPolicyEdits(tree, safety, options, plan, fileMemory, scratchMemory);
    Sort(plan);
    (void)Validate(tree.Source(), plan);
    return plan;
}

StructuralMaterialization MaterializeStructuralEdits(
    const parser::SyntaxTree& tree,
    const StructuralPlan& plan,
    std::pmr::memory_resource& fileMemory,
    std::pmr::memory_resource& scratchMemory)
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
    result.OriginalTokens.resize(tree.TokenCount());
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
        for (TI i = FirstBeginningAt(tree, source.Begin); i < tree.TokenCount(); ++i) {
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
    // Linear nearest-survivor tables bound chains of neighboring deletions.
    std::pmr::vector<TI> before(
        tree.TokenCount() + 1, tree.TokenCount(), &scratchMemory);
    std::pmr::vector<TI> after(
        tree.TokenCount() + 1, tree.TokenCount(), &scratchMemory);
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
    return result;
}

NamespaceCommentPlan PlanNamespaceComments(
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
    std::pmr::vector<uint32_t> namespaceKeywords(
        original.TokenCount() + 1, 0, &scratchMemory);
    for (TI i = 0; i < original.TokenCount(); ++i) {
        namespaceKeywords[i + 1] = namespaceKeywords[i] + (original.Kind(i) == TK::Keyword_namespace);
    }
    Walk(
        original,
        &safety,
        scratchMemory,
        [&] (const parser::Node& node, NK, NK, NK, bool, uint32_t, RegionStatus status, bool, bool) {
            if (node.Kind != NK::NamespaceDeclaration) {
                // A keyword in opaque/generic grammar is evidence to SKIP,
                // never permission to invent a namespace identity or
                // inspect its text.
                if ((node.Kind == NK::Declaration || node.Kind == NK::UnparsedSyntax) &&
                    namespaceKeywords[node.Tokens.End] != namespaceKeywords[node.Tokens.Begin])
                {
                    plan.Skipped
                        .push_back({StructuralAction::NamespaceComments, node.Tokens, StructuralSkip::Incomplete});
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
                if ((kind == TK::Keyword_namespace && expectName && !named) ||
                    (kind == TK::Keyword_inline && expectName))
                {
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
            if (status != RegionStatus::Complete || close >= node.Tokens.End ||
                original.Kind(close) != TK::Operator_CloseBrace)
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
                original.TokenText(suffix).starts_with("//") && Horizontal(original.WhitespaceBefore(suffix)))
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
            plan.Namespaces.push_back({node.Tokens, {node.Tokens.Begin, open}, open, close, comment});
        });
    return plan;
}

StructuralPlan ResolveNamespaceComments(
    const parser::SyntaxTree& original,
    const NamespaceCommentPlan& plan,
    std::string_view output,
    std::span<const TokenOutputOffset> mapping,
    std::span<const SR> outputProtection,
    std::pmr::memory_resource& fileMemory,
    std::pmr::memory_resource& scratchMemory)
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
    std::pmr::vector<parser::SourceOffset> lines(&scratchMemory);
    lines.push_back(0);
    for (size_t i = 0; i < output.size(); ++i) {
        if (output[i] == '\n') {
            lines.push_back(static_cast<parser::SourceOffset>(i + 1));
        }
    }
    auto line = [&] (parser::SourceOffset offset) {
        return static_cast<size_t>(std::upper_bound(lines.begin(), lines.end(), offset) - lines.begin() - 1);
    };
    const auto count = original.TokenCount();
    std::pmr::vector<uint8_t> mappedValid(count, 0, &scratchMemory);
    std::pmr::vector<TI> before(count + 1, count, &scratchMemory);
    std::pmr::vector<TI> after(count + 1, count, &scratchMemory);
    for (TI token = 0; token < count; ++token) {
        auto r = mapping[token].Output;
        if (mapping[token].Present && r.Begin <= r.End && r.End <= output.size()) {
            const auto sourceText = original.TokenText(token);
            const auto outputText = output.substr(r.Begin, r.End - r.Begin);
            mappedValid[token] = outputText == sourceText ||
                (original.Kind(token) == TK::Comment && sourceText.starts_with("/*") &&
                    ReindentedCommentMatches(sourceText, outputText));
        }
        before[token + 1] = mapping[token].Present ? token : before[token];
    }
    for (TI token = count; token > 0; --token) {
        after[token - 1] = mapping[token - 1].Present ? token - 1 : after[token];
    }
    auto valid = [&] (TI token) {
        return token < count && mappedValid[token];
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
            if (ns.Close <= token || ns.Close >= count || ns.Header.Begin >= ns.Header.End || ns.Header.End != token ||
                (!active.empty() && ns.Close >= plan.Namespaces[active.back()].Close))
            {
                throw std::invalid_argument("invalid original namespace identity");
            }
            active.push_back(nextNamespace++);
        }
    }
    if (!active.empty() || nextNamespace != plan.Namespaces.size()) {
        throw std::invalid_argument("unordered original namespace identities");
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
        auto previous = before[ns.Header.Begin];
        auto following = after[ns.Close + 1];
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
            auto openLine = line(mapping[ns.Open].Output.Begin);
            auto closeLine = line(mapping[ns.Close].Output.Begin);
            auto interior = closeLine > openLine ? closeLine - openLine - 1 : 0;
            if (plan.Options.ShortNamespaceLines && interior <= plan.Options.ShortNamespaceLines) {
                skip(StructuralSkip::ShortNamespace);
                continue;
            }
            auto lineEnd = closeLine + 1 < lines.size()
                ? lines[closeLine + 1] - 1
                : static_cast<parser::SourceOffset>(output.size());
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
