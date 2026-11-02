#include "file_check.hpp"

#include "source_excerpt.hpp"

#include <formatter/naming.hpp>
#include <formatter/parameter_names.hpp>

#include <unicode/utf8.hpp>

#include <algorithm>
#include <memory_resource>
#include <stdexcept>
#include <utility>

namespace hit_fmt {

namespace {

bool Before(util::Position a, util::Position b)
{
    return a.Line < b.Line || (a.Line == b.Line && a.Column < b.Column);
}

// Sorted offsets reuse each line prefix; full diagnostics also reuse its index.
class PositionSweep
{
public:
    PositionSweep(std::string_view text, const formatter::SourceLines* lines)
        : Text_(text)
        , Lines_(lines)
    { }

    util::Position Position(size_t offset)
    {
        if (offset < Cursor_ || offset > Text_.size()) {
            throw std::runtime_error("invalid emitted diagnostic offset");
        }

        if (Lines_) {
            const auto line = Lines_->LineAt(static_cast<parser::SourceOffset>(offset));
            const auto begin = Lines_->Range(line).Begin;

            if (Cursor_ < begin) {
                Cursor_ = begin;
                Current_ = {static_cast<uint32_t>(line), 1};
            }
        }

        while (Cursor_ < offset) {
            Advance();
        }

        if (Cursor_ != offset) {
            throw std::runtime_error("diagnostic offset splits a code point");
        }

        return Current_;
    }

private:
    size_t NextOffset() const
    {
        const auto c = static_cast<unsigned char>(Text_[Cursor_]);
        // Match tokenizer coordinates: each invalid UTF8 byte is one column.
        return Cursor_ + (c < 0x80 ? 1 : unicode::utf8::GetFirstCodePointSize(Text_.substr(Cursor_)).value_or(1));
    }

    void Advance()
    {
        const auto c = static_cast<unsigned char>(Text_[Cursor_]);
        Cursor_ = NextOffset();

        if (c == '\n') {
            ++Current_.Line;
            Current_.Column = 1;
        } else {
            ++Current_.Column;
        }
    }

    std::string_view Text_;
    const formatter::SourceLines* Lines_;
    size_t Cursor_ = 0;
    util::Position Current_{1, 1};
};

bool InLines(size_t line, std::span<const formatter::Hunk> hunks)
{
    const auto it = std::lower_bound(
        hunks.begin(),
        hunks.end(),
        line,
        [] (auto hunk, auto target) {
            return hunk.End < target;
        });
    return it != hunks.end() && it->Begin <= line;
}

std::string_view RequiredChangeMessage(formatter::StructuralAction action)
{
    using Action = formatter::StructuralAction;
    switch (action) {
        case Action::AddMissingControlBraces:
            return "control body requires braces";
        case Action::EmptyControlBody:
            return "empty control body must use an empty block";
        case Action::RemoveNullStatements:
            return "redundant null statement must be removed";
        case Action::RemoveNullDeclarations:
            return "redundant null declaration must be removed";
        case Action::IncludeOrder:
        case Action::NamespaceComments:
            break; // These skips are not proof of a required edit.
    }

    throw std::logic_error("unexpected required hunk edit");
}

constexpr std::string_view BlockedHunkFix = "; cannot automatically fix within selected hunks or allowed spillover";

void MapSemanticPositions(
    const parser::SyntaxTree& tree,
    const formatter::FormatResult& formatting,
    std::vector<SemanticDiagnostic>& semantic,
    std::vector<SemanticDiagnostic>& required)
{
    std::pmr::monotonic_buffer_resource scratch;
    struct Point
    {
        size_t Offset;
        util::Position* Position;
    };

    std::pmr::vector<Point> points(&scratch);
    points.reserve(semantic.size() + required.size());

    for (auto* diagnostics : {&semantic, &required}) {
        for (auto& diagnostic : *diagnostics) {
            const auto anchor = diagnostic.Anchor;
            const auto range = tree.Token(anchor.Token).Text;
            const auto& mapped = formatting.OriginalTokens.at(anchor.Token);

            if (anchor.Offset < range.Begin || anchor.Offset > range.End || !mapped.Present ||
                mapped.Output.End - mapped.Output.Begin != range.End - range.Begin)
            {
                throw std::runtime_error("diagnostic token identity did not survive formatting");
            }

            points.push_back({mapped.Output.Begin + anchor.Offset - range.Begin, &diagnostic.Emitted});
        }
    }

    std::sort(
        points.begin(),
        points.end(),
        [] (const auto& a, const auto& b) {
            return a.Offset < b.Offset;
        });
    PositionSweep emitted(formatting.Text, formatting.Changes ? &formatting.Changes->OutputLines : nullptr);

    for (const auto& point : points) {
        *point.Position = emitted.Position(point.Offset);
    }
}

} // namespace

PreparedConfiguration::PreparedConfiguration(formatter::Configuration configuration)
    : Settings(std::move(configuration))
    , Formatting(Settings.FormattingStyle)
{ }

bool CheckedFile::HasFailures(bool fix) const
{
    return (!fix && FormattingMismatch) || !Semantic.empty() || !RequiredChanges.empty() ||
        !Formatting.Diagnostics.empty();
}

CheckedFile FormatAndCheckFile(
    util::PaddedSourceView source,
    const PreparedConfiguration& configuration,
    std::string_view inputFilePath,
    std::optional<ChangedFileScope> changed,
    DiagnosticDetail diagnostics)
{
    CheckedFile checked;
    std::vector<parser::ParseError> incomplete;
    auto tree = parser::Parse(
        source,
        [&] (parser::ParseError error) {
            if (error.CheckingIncomplete) {
                incomplete.push_back(std::move(error));
            }
        },
        configuration.Formatting.ParsingOptions());
    std::sort(
        incomplete.begin(),
        incomplete.end(),
        [] (const auto& a, const auto& b) {
            return Before(a.Position, b.Position);
        });
    parser::TokenIndex token = 0;

    for (auto& error : incomplete) {
        for (; token < tree.TokenCount(); ++token) {
            const auto position = tree.TokenPosition(token);

            if (position.Line == error.Position.Line && position.Column == error.Position.Column) {
                checked.RequiredChanges
                    .push_back({error.Position, {}, std::move(error.Message), {token, tree.Token(token).Text.Begin}});
                break;
            }
        }
    }

    const bool locations = diagnostics != DiagnosticDetail::Summary;
    checked.ColumnLimit = configuration.Formatting.Policies.ColumnLimit;

    for (auto& diagnostic : formatter::CheckNames(tree, configuration.Settings)) {
        checked.Semantic
            .push_back({diagnostic.Position, {}, diagnostic.Message + "; " + diagnostic.Expected, diagnostic.Anchor});
    }

    for (const auto& diagnostic : formatter::CheckParameterNames(tree, configuration.Settings)) {
        auto message = "parameter #" + std::to_string(diagnostic.ParameterNumber) + " in " + diagnostic.Function +
            ": expected a name";

        if (diagnostic.AllowCommentedName) {
            message += " or /*name*/ comment";
        }

        checked.Semantic.push_back({diagnostic.Position, {}, std::move(message), diagnostic.Anchor});
    }

    const auto namespacePolicy = configuration.Formatting.NamespaceComments.Policy;
    const bool namespaceDiagnostics = namespacePolicy == formatter::NamespaceCommentPolicy::Fix ||
        namespacePolicy == formatter::NamespaceCommentPolicy::AddMissing;
    const bool tokenMap = locations &&
        (changed.has_value() || !checked.Semantic.empty() || !checked.RequiredChanges.empty() || namespaceDiagnostics);
    std::vector<formatter::Hunk> selected, permitted;
    const formatter::FormatRequest request{
        .InputFilePath = inputFilePath,
        .TokenMap = tokenMap,
        .Changes = diagnostics == DiagnosticDetail::Full,
        .ChangeSummary = true,
        .DiagnosticLocations = (locations || changed.has_value()) &&
            configuration.Formatting.Policies.ColumnLimit != 0 &&
            configuration.Formatting.Policies.WidthDiagnostics == formatter::WidthDiagnosticPolicy::Report,
    };

    if (changed) {
        auto result =
            formatter::FormatChanged(tree, configuration.Formatting, changed->Hunks, changed->Spillover, request);
        selected = std::move(result.SelectedLines);
        permitted = std::move(result.PermittedLines);
        checked.Incomplete = std::move(result.Incomplete);

        for (const auto& skip : result.RequiredSkips) {
            checked.RequiredChanges.push_back({
                tree.TokenPosition(skip.Tokens.Begin),
                {},
                std::string(RequiredChangeMessage(skip.Action)),
                {skip.Tokens.Begin, tree.Token(skip.Tokens.Begin).Text.Begin},
                true,
            });
        }

        checked.Formatting = std::move(result.Result);
        std::erase_if(
            checked.Semantic,
            [&] (const auto& diagnostic) {
                return !InLines(diagnostic.Original.Line, selected);
            });
    } else {
        checked.Formatting = formatter::Format(tree, configuration.Formatting, request);
    }

    for (const auto& diagnostic : checked.Formatting.StructuralDiagnostics) {
        if (diagnostic.Action != formatter::StructuralAction::NamespaceComments ||
            diagnostic.Reason != formatter::StructuralSkip::MissingNamespaceComment)
        {
            continue;
        }

        const auto token = diagnostic.Tokens.Begin;
        const auto position = tree.TokenPosition(token);

        if (changed && !InLines(position.Line, selected)) {
            continue;
        }

        checked.RequiredChanges.push_back({
            position,
            {},
            "namespace closing comment is missing; existing explanatory comment is preserved",
            {token, tree.Token(token).Text.Begin},
        });
    }

    if (changed) {
        std::erase_if(
            checked.RequiredChanges,
            [&] (const auto& diagnostic) {
                return !InLines(diagnostic.Original.Line, selected);
            });
    }

    const auto& summary = *checked.Formatting.Summary;
    checked.FormattingMismatch = summary.OtherChanges || !summary.IncludeOrderChanges.empty();

    for (auto* items : {&checked.Semantic, &checked.RequiredChanges}) {
        std::sort(
            items->begin(),
            items->end(),
            [] (const auto& a, const auto& b) {
                return Before(a.Original, b.Original);
            });
    }

    if (locations) {
        MapSemanticPositions(tree, checked.Formatting, checked.Semantic, checked.RequiredChanges);
    }

    if (changed) {
        size_t count = 0;

        for (size_t i = 0; i < checked.Formatting.Diagnostics.size(); ++i) {
            if (InLines(checked.Formatting.Diagnostics[i].Original->Line, permitted)) {
                if (count != i) {
                    checked.Formatting.Diagnostics[count] = std::move(checked.Formatting.Diagnostics[i]);
                }

                ++count;
            }
        }

        checked.Formatting.Diagnostics.resize(count);
    }

    return checked;
}

void PrintFileIssues(
    std::ostream& output,
    std::string_view path,
    std::string_view originalSource,
    const CheckedFile& checked,
    bool fix,
    const Terminal& terminal)
{
    if (!fix && !checked.Formatting.Summary->IncludeOrderChanges.empty()) {
        const auto position = checked.Formatting.Summary->IncludeOrderChanges.front().Position;
        output << "fail: includes are not sorted\n  --> " << path << ':' << position.Line << ':' << position.Column
            << " [original source]\n";
    }

    for (const auto& diagnostic : checked.Semantic) {
        const auto position = fix ? diagnostic.Emitted : diagnostic.Original;
        output << "fail: " << diagnostic.Message << "\n  --> " << path << ':' << position.Line << ':' << position.Column
            << (fix ? " [written output]" : " [original source]") << '\n';
    }

    for (const auto& diagnostic : checked.RequiredChanges) {
        const auto position = fix ? diagnostic.Emitted : diagnostic.Original;
        output << "fail: " << diagnostic.Message;

        if (fix && diagnostic.BlockedByHunks) {
            output << BlockedHunkFix;
        }

        output << "\n  --> " << path << ':' << position.Line << ':' << position.Column
            << (fix ? " [written output]" : " [original source]") << '\n';
    }

    if (checked.Formatting.Diagnostics.empty()) {
        return;
    }

    const std::string_view source = fix ? checked.Formatting.Text : originalSource;
    const auto* lines = checked.Formatting.Changes
        ? (fix ? &checked.Formatting.Changes->OutputLines : &checked.Formatting.Changes->OriginalLines)
        : nullptr;
    std::optional<formatter::SourceLines> ownedLines;
    // Width diagnostics treat bare CR as an emitted newline, while source
    // coordinates and hunk authority use physical LF-delimited lines.
    if (!lines || (fix && lines->ContainsBareCR())) {
        ownedLines.emplace(source, fix);
        lines = &*ownedLines;
    }

    for (size_t i = 0; i < checked.Formatting.Diagnostics.size(); ++i) {
        const auto& diagnostic = checked.Formatting.Diagnostics[i];
        const auto position = fix ? diagnostic.Position : *diagnostic.Original;
        output << "fail: "
            << (fix ? "cannot format this line automatically within ColumnLimit " : "line exceeds ColumnLimit ")
            << checked.ColumnLimit << "\n  --> " << path << ':' << position.Line << ':' << position.Column
            << (fix ? " [written output]" : " [original source]") << '\n';
        const auto number = std::to_string(position.Line);
        const auto gutter = std::string(number.size() + 1, ' ') + "| ";
        const auto prefix = number + " | ";
        auto text = lines->Text(source, position.Line);

        if (text.ends_with('\r')) {
            text.remove_suffix(1);
        }

        const auto column = fix ? diagnostic.CodePointColumn : position.Column;
        output << gutter.substr(0, gutter.size() - 1) << '\n' << prefix;
        const auto rendered = RenderLine(text, prefix.size());
        const auto excerpt = PrintSourceExcerpt(output, rendered, column, prefix.size(), terminal.Width);

        if (excerpt.CaretVisible) {
            output << gutter << std::string(excerpt.CaretOffset, ' ') << "^\n";
        }
    }
}

void PrintFileSummaryIssues(std::ostream& output, const CheckedFile& checked, bool fix)
{
    bool separated = false;
    auto issue = [&] (std::string_view message, bool failure = false) {
        if (separated) {
            output << "; ";
        }

        if (failure) {
            output << "fail: ";
        }

        output << message;
        separated = true;
    };

    if (!fix && checked.Formatting.Summary->OtherChanges) {
        issue("formatting mismatch");
    }

    if (!fix && !checked.Formatting.Summary->IncludeOrderChanges.empty()) {
        issue("includes are not sorted", true);
    }

    if (!checked.Semantic.empty()) {
        issue("fail: naming or parameter-name violation");
    }

    for (const auto& diagnostic : checked.RequiredChanges) {
        issue(diagnostic.Message, true);

        if (fix && diagnostic.BlockedByHunks) {
            output << BlockedHunkFix;
        }
    }

    if (!checked.Formatting.Diagnostics.empty()) {
        issue(fix ? "fail: cannot format automatically within column limit" : "fail: column-limit violation");
    }
}

} // namespace hit_fmt
