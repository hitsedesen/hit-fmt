#include "file_check.hpp"

#include "formatting_diagnostics.hpp"
#include "source_excerpt.hpp"

#include <util/memory.hpp>
#include <util/types.hpp>

#include <formatter/naming.hpp>
#include <formatter/parameter_names.hpp>

#include <unicode/utf8.hpp>

#include <algorithm>
#include <memory_resource>
#include <stdexcept>
#include <utility>

namespace cli {

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

    Result<util::Position> Position(us offset)
    {
        if (offset < Cursor_ || offset > Text_.size()) {
            return {{}, MakeError("invalid emitted diagnostic offset")};
        }

        if (Lines_) {
            auto line = Lines_->LineAt(static_cast<parser::SourceOffset>(offset));
            auto begin = Lines_->Range(line).Begin;

            if (Cursor_ < begin) {
                Cursor_ = begin;
                Current_ = {static_cast<u32>(line), 1};
            }
        }

        while (Cursor_ < offset) {
            Advance();
        }

        if (Cursor_ != offset) {
            return {{}, MakeError("diagnostic offset splits a code point")};
        }

        return {Current_, {}};
    }

private:
    us NextOffset() const
    {
        auto c = static_cast<u8>(Text_[Cursor_]);
        // Match tokenizer coordinates: each invalid UTF8 byte is one column.
        return Cursor_ + (c < 0x80 ? 1 : unicode::utf8::GetFirstCodePointSize(Text_.substr(Cursor_)).value_or(1));
    }

    void Advance()
    {
        auto c = static_cast<u8>(Text_[Cursor_]);
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
    us Cursor_ = 0;
    util::Position Current_{1, 1};
};

bool CheckCommentCharacters(const parser::SyntaxTree& tree, bool enabled, std::vector<SemanticDiagnostic>* diagnostics)
{
    if (!enabled) {
        return false;
    }

    bool violation = false;

    for (parser::TokenIndex i = 0; i < tree.TokenCount(); ++i) {
        if (tree.Kind(i) != tokenizer::TokenKind::Comment) {
            continue;
        }

        const auto text = tree.TokenText(i);
        auto position = tree.TokenPosition(i);

        for (us offset = 0; offset < text.size(); ++offset) {
            const auto byte = static_cast<u8>(text[offset]);

            if (byte >= 0x80) {
                if (!diagnostics) {
                    return true;
                }

                diagnostics->push_back({
                    position,
                    {},
                    "comment contains non-ASCII characters (CommentsASCIIOnly)",
                    {i, tree.Token(i).Text.Begin + static_cast<parser::SourceOffset>(offset)},
                });
                violation = true;
                break;
            }

            if (byte == '\n') {
                ++position.Line;
                position.Column = 1;
            } else {
                ++position.Column;
            }
        }
    }

    return violation;
}

bool InLines(us line, std::span<const formatter::Hunk> hunks)
{
    auto it = std::lower_bound(
        hunks.begin(),
        hunks.end(),
        line,
        [] (auto hunk, auto target) {
            return hunk.End < target;
        });
    return it != hunks.end() && it->Begin <= line;
}

std::string ProtectionMessage(formatter::ProtectionReason reasons)
{
    using Reason = formatter::ProtectionReason;
    std::string message = "protected range: ";
    bool separated = false;
    auto append = [&] (Reason reason, std::string_view text) {
        if ((static_cast<u16>(reasons) & static_cast<u16>(reason)) == 0) {
            return;
        }

        if (separated) {
            message += ", ";
        }

        message += text;
        separated = true;
    };
    append(Reason::Disabled, "formatting disabled by off/on directive");
    append(Reason::SensitiveMacro, "whitespace-sensitive or opaque macro");
    append(Reason::Recovery, "parser recovery or invalid token");
    append(Reason::Ambiguous, "ambiguous or unsupported syntax");
    append(Reason::Directive, "preprocessing directive");
    append(Reason::LineSplice, "line splice");
    append(Reason::Explicit, "explicit immutable range");
    append(Reason::HeaderName, "header name spelling");
    append(Reason::OutsideEditScope, "outside selected hunks or permitted edit scope");
    return message;
}

Result<std::string_view> ActionName(formatter::StructuralAction action)
{
    using Action = formatter::StructuralAction;
    switch (action) {
        case Action::AddMissingControlBraces:
            return {"adding control braces", {}};
        case Action::EmptyControlBody:
            return {"rewriting empty control body", {}};
        case Action::RemoveNullStatements:
            return {"removing null statement", {}};
        case Action::RemoveNullDeclarations:
            return {"removing null declaration", {}};
        case Action::IncludeOrder:
            return {"sorting includes", {}};
        case Action::NamespaceComments:
            return {"editing namespace comment", {}};
        case Action::DirectiveWhitespace:
            return {"formatting directive whitespace", {}};
        case Action::IncludeDelimiters:
            return {"rewriting include delimiters", {}};
        case Action::RulerLength:
            return {"resizing ruler", {}};
        case Action::RulerPlacement:
            return {"placing ruler", {}};
    }

    return {{}, MakeError("unexpected structural action")};
}

Result<std::string_view> SkipReason(formatter::StructuralSkip reason)
{
    using Reason = formatter::StructuralSkip;
    switch (reason) {
        case Reason::Incomplete:
            return {"incomplete syntax", {}};
        case Reason::Protected:
            return {"protected source or edit boundary", {}};
        case Reason::CommentAttached:
            return {"attached comment", {}};
        case Reason::Attributes:
            return {"attributes", {}};
        case Reason::LabelBody:
            return {"labeled body", {}};
        case Reason::DirectiveBoundary:
            return {"preprocessing directive boundary", {}};
        case Reason::InvalidMapping:
            return {"source mapping could not be validated", {}};
        case Reason::FollowingText:
            return {"following text prevents a safe edit", {}};
        case Reason::ShortNamespace:
            return {"short namespace exempted by configuration", {}};
        case Reason::RequiredBody:
            return {"body is required", {}};
        case Reason::MissingNamespaceComment:
            return {"existing explanatory comment is preserved", {}};
    }

    return {{}, MakeError("unexpected structural skip reason")};
}

Error CollectFormattingDebug(const parser::SyntaxTree& tree, CheckedFile& checked)
{
    const auto& formatting = checked.Formatting;
    checked.Debug
        .reserve(checked.Debug.size() + formatting.InputProtection.size() + formatting.StructuralDiagnostics.size());
    Arena scratch;
    struct Point
    {
        parser::SourceOffset Offset;
        util::Position* Position;
    };

    Vector<Point> points(&scratch);
    points.reserve(2 * (formatting.InputProtection.size() + formatting.StructuralDiagnostics.size()));
    auto add = [&] (parser::SourceRange source, std::string message) {
        auto& diagnostic = checked.Debug.emplace_back(DebugDiagnostic{{}, {}, std::move(message)});
        points.push_back({source.Begin, &diagnostic.Begin});
        points.push_back({source.End, &diagnostic.End});
    };

    for (const auto& protection : formatting.InputProtection) {
        add(protection.Source, ProtectionMessage(protection.Reasons));
    }

    for (const auto& diagnostic : formatting.StructuralDiagnostics) {
        if (!diagnostic.Tokens.Empty()) {
            auto action = ActionName(diagnostic.Action);

            if (action.Error) {
                return std::move(action.Error);
            }

            auto reason = SkipReason(diagnostic.Reason);

            if (reason.Error) {
                return std::move(reason.Error);
            }

            add(
                {tree.Token(diagnostic.Tokens.Begin).Text.Begin, tree.Token(diagnostic.Tokens.End - 1).Text.End},
                "skipped " + std::string(action.Value) + ": " + std::string(reason.Value));
        }
    }

    std::sort(
        points.begin(),
        points.end(),
        [] (const auto& a, const auto& b) {
            return a.Offset < b.Offset;
        });
    PositionSweep original(tree.Source(), formatting.Changes ? &formatting.Changes->OriginalLines : nullptr);

    for (const auto& point : points) {
        auto position = original.Position(point.Offset);

        if (position.Error) {
            return std::move(position.Error);
        }

        *point.Position = position.Value;
    }

    return {};
}

Result<std::string_view> RequiredChangeMessage(formatter::StructuralAction action)
{
    using Action = formatter::StructuralAction;
    switch (action) {
        case Action::AddMissingControlBraces:
            return {"control body requires braces", {}};
        case Action::EmptyControlBody:
            return {"empty control body must use an empty block", {}};
        case Action::RemoveNullStatements:
            return {"redundant null statement must be removed", {}};
        case Action::RemoveNullDeclarations:
            return {"redundant null declaration must be removed", {}};
        case Action::IncludeOrder:
        case Action::NamespaceComments:
            break; // These skips are not proof of a required edit.
    }

    return {{}, MakeError("unexpected required hunk edit")};
}

constexpr std::string_view BlockedHunkFix = "; cannot automatically fix within selected hunks or allowed spillover";

Error MapSemanticPositions(
    const parser::SyntaxTree& tree,
    const formatter::FormatResult& formatting,
    std::vector<SemanticDiagnostic>& semantic,
    std::vector<SemanticDiagnostic>& required)
{
    Arena scratch;
    struct Point
    {
        us Offset;
        util::Position* Position;
    };

    Vector<Point> points(&scratch);
    points.reserve(semantic.size() + required.size());

    for (auto* diagnostics : {&semantic, &required}) {
        for (auto& diagnostic : *diagnostics) {
            auto anchor = diagnostic.Anchor;

            if (anchor.Token >= tree.TokenCount() || anchor.Token >= formatting.OriginalTokens.size()) {
                return MakeError("diagnostic token identity did not survive formatting");
            }

            auto range = tree.Token(anchor.Token).Text;
            const auto& mapped = formatting.OriginalTokens[anchor.Token];

            if (anchor.Offset < range.Begin || anchor.Offset > range.End || !mapped.Present ||
                mapped.Output.End - mapped.Output.Begin != range.End - range.Begin)
            {
                return MakeError("diagnostic token identity did not survive formatting");
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
        auto position = emitted.Position(point.Offset);

        if (position.Error) {
            return std::move(position.Error);
        }

        *point.Position = position.Value;
    }

    return {};
}

} // namespace

Result<PreparedConfiguration> PrepareConfiguration(formatter::Configuration configuration)
{
    Arena scratch;
    auto exclusions = CompilePathRules(configuration.Exclude, &scratch);

    if (exclusions.Error) {
        return {{}, std::move(exclusions.Error)};
    }

    try {
        formatter::PreparedFormat formatting(configuration.FormattingStyle);
        return {{std::move(configuration), std::move(formatting), std::move(exclusions.Value)}, {}};
    } catch (const std::invalid_argument&) {
        return {{}, ExceptionError()};
    }
}

bool CheckedFile::HasFailures(bool fix) const
{
    return QuickFailure || (!fix && FormattingMismatch) || !Semantic.empty() || !RequiredChanges.empty() ||
        !Formatting.Diagnostics.empty();
}

Result<CheckedFile> FormatAndCheckFile(
    util::PaddedSourceView source,
    const PreparedConfiguration& configuration,
    std::string_view inputFilePath,
    std::optional<ChangedFileScope> changed,
    DiagnosticDetail diagnostics,
    bool debug,
    std::pmr::memory_resource* memory,
    bool fix)
{
    try {
        CheckedFile checked;
        auto parseOptions = configuration.Formatting.ParsingOptions();
        parseOptions.Memory = memory;
        auto tree = parser::Parse(
            source,
            [&] (parser::ParseError error) {
                if (debug) {
                    checked.Debug.push_back({error.Position, error.Position, "parser: " + error.Message});
                }
            },
            parseOptions);
        const bool quick = diagnostics == DiagnosticDetail::Quick && !fix && !changed && !debug;
        bool locations = diagnostics == DiagnosticDetail::Summary || diagnostics == DiagnosticDetail::Detailed;
        checked.ColumnLimit = configuration.Formatting.Policies.ColumnLimit;

        if (quick) {
            if (formatter::HasNamingViolation(tree, configuration.Settings) ||
                formatter::HasParameterNameViolation(tree, configuration.Settings) ||
                CheckCommentCharacters(tree, configuration.Settings.CommentsASCIIOnly, nullptr))
            {
                checked.QuickFailure = true;
                return {std::move(checked), {}};
            }
        } else {
            CheckCommentCharacters(tree, configuration.Settings.CommentsASCIIOnly, &checked.Semantic);

            for (auto& diagnostic : formatter::CheckNames(tree, configuration.Settings)) {
                checked.Semantic.push_back({
                    diagnostic.Position,
                    {},
                    diagnostic.Message + "; " + diagnostic.Expected,
                    diagnostic.Anchor,
                });

            }

            for (const auto& diagnostic : formatter::CheckParameterNames(tree, configuration.Settings)) {
                auto message = "parameter #" + std::to_string(diagnostic.ParameterNumber) + " in " +
                    diagnostic.Function + ": expected a name";

                if (diagnostic.AllowCommentedName) {
                    message += " or /*name*/ comment";
                }

                checked.Semantic.push_back({diagnostic.Position, {}, std::move(message), diagnostic.Anchor});

            }

        }

        auto namespacePolicy = configuration.Formatting.NamespaceComments.Policy;
        bool namespaceDiagnostics = namespacePolicy == formatter::NamespaceCommentPolicy::Fix ||
            namespacePolicy == formatter::NamespaceCommentPolicy::AddMissing;
        bool tokenMap = locations && fix && (!checked.Semantic.empty() || namespaceDiagnostics || changed.has_value());
        std::vector<formatter::Hunk> selected, permitted;
        const formatter::FormatRequest request{
            .InputFilePath = inputFilePath,
            .TokenMap = tokenMap,
            .Changes = diagnostics == DiagnosticDetail::Detailed || (locations && !fix),
            .OutputLineIndex = diagnostics == DiagnosticDetail::Detailed && fix,
            .ChangeSummary = false,
            .DiagnosticLocations = ((locations && (!fix || diagnostics == DiagnosticDetail::Detailed)) ||
                changed.has_value()) && configuration.Formatting.Policies.ColumnLimit != 0 &&
                configuration.Formatting.Policies.WidthDiagnostics == formatter::WidthDiagnosticPolicy::Report,
            .EmittedDiagnosticLocations = locations && fix,
            .FirstDiagnosticOnly = quick,
            .InputProtection = debug,
        };

        if (changed) {
            auto result = formatter::FormatChanged(
                tree,
                configuration.Formatting,
                changed->Hunks,
                changed->Spillover,
                request);
            selected = std::move(result.SelectedLines);
            permitted = std::move(result.PermittedLines);
            checked.Incomplete = std::move(result.Incomplete);

            for (const auto& skip : result.RequiredSkips) {
                auto message = RequiredChangeMessage(skip.Action);

                if (message.Error) {
                    return {{}, std::move(message.Error)};
                }

                checked.RequiredChanges.push_back({
                    tree.TokenPosition(skip.Tokens.Begin),
                    {},
                    std::string(message.Value),
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

        checked.FormattingMismatch = checked.Formatting.Text != source.View();

        if (quick) {

            if (checked.FormattingMismatch || !checked.Formatting.Diagnostics.empty()) {
                return {std::move(checked), {}};
            }
        }

        if (debug) {
            if (auto error = CollectFormattingDebug(tree, checked); error) {
                return {{}, std::move(error)};
            }
        }

        for (const auto& diagnostic : checked.Formatting.StructuralDiagnostics) {
            if (diagnostic.Action != formatter::StructuralAction::NamespaceComments ||
                diagnostic.Reason != formatter::StructuralSkip::MissingNamespaceComment)
            {
                continue;
            }

            auto token = diagnostic.Tokens.Begin;
            auto position = tree.TokenPosition(token);

            if (changed && !InLines(position.Line, selected)) {
                continue;
            }

            checked.RequiredChanges.push_back({
                position,
                {},
                "namespace closing comment is missing; existing explanatory comment is preserved",
                {token, tree.Token(token).Text.Begin},
            });

            if (quick) {
                return {std::move(checked), {}};
            }
        }

        if (changed) {
            std::erase_if(
                checked.RequiredChanges,
                [&] (const auto& diagnostic) {
                    return !InLines(diagnostic.Original.Line, selected);
                });
        }

        for (auto* items : {&checked.Semantic, &checked.RequiredChanges}) {
            std::sort(
                items->begin(),
                items->end(),
                [] (const auto& a, const auto& b) {
                    return Before(a.Original, b.Original);
                });
        }

        if (locations && fix) {
            if (auto error = MapSemanticPositions(tree, checked.Formatting, checked.Semantic, checked.RequiredChanges);
                error)
            {
                return {{}, std::move(error)};
            }
        }

        if (changed) {
            us count = 0;

            for (us i = 0; i < checked.Formatting.Diagnostics.size(); ++i) {
                if (InLines(checked.Formatting.Diagnostics[i].Original->Line, permitted)) {
                    if (count != i) {
                        checked.Formatting.Diagnostics[count] = std::move(checked.Formatting.Diagnostics[i]);
                    }

                    ++count;
                }
            }

            checked.Formatting.Diagnostics.resize(count);
        }

        return {std::move(checked), {}};
    } catch (const std::exception&) {
        return {{}, ExceptionError()};
    }
}

void PrintFileDebug(std::ostream& output, std::string_view path, const CheckedFile& checked)
{
    output << "debug: formatting analysis for " << path << " [original source; range ends exclusive]\n";

    if (checked.Debug.empty()) {
        output << "debug: no parser errors, protected ranges or skipped structural edits\n";
    }

    for (const auto& diagnostic : checked.Debug) {
        output << "debug: " << diagnostic.Message << "\n  --> " << path << ':' << diagnostic.Begin.Line << ':'
            << diagnostic.Begin.Column;

        if (Before(diagnostic.Begin, diagnostic.End)) {
            output << '-' << diagnostic.End.Line << ':' << diagnostic.End.Column;
        }

        output << " [original source]\n";
    }
}

void PrintFileIssues(
    std::ostream& output,
    std::string_view path,
    std::string_view originalSource,
    const CheckedFile& checked,
    bool fix,
    const Terminal& terminal)
{
    if (!fix && checked.Formatting.Summary && !checked.Formatting.Summary->IncludeOrderChanges.empty()) {
        auto position = checked.Formatting.Summary->IncludeOrderChanges.front().Position;
        output << "fail: includes are not sorted\n  --> " << path << ':' << position.Line << ':' << position.Column
            << " [original source]\n";
    }

    for (const auto& diagnostic : checked.Semantic) {
        auto position = fix ? diagnostic.Emitted : diagnostic.Original;
        output << "fail: " << diagnostic.Message << "\n  --> " << path << ':' << position.Line << ':' << position.Column
            << (fix ? " [written output]" : " [original source]") << '\n';
    }

    for (const auto& diagnostic : checked.RequiredChanges) {
        auto position = fix ? diagnostic.Emitted : diagnostic.Original;
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

    std::string_view source = fix ? checked.Formatting.Text : originalSource;
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

    for (us i = 0; i < checked.Formatting.Diagnostics.size(); ++i) {
        const auto& diagnostic = checked.Formatting.Diagnostics[i];
        auto position = fix ? diagnostic.Position : *diagnostic.Original;
        output << "fail: "
            << (fix ? "cannot format this line automatically within ColumnLimit " : "line exceeds ColumnLimit ")
            << checked.ColumnLimit << "\n  --> " << path << ':' << position.Line << ':' << position.Column
            << (fix ? " [written output]" : " [original source]") << '\n';
        auto number = std::to_string(position.Line);
        auto gutter = std::string(number.size() + 1, ' ') + "| ";
        auto prefix = number + " | ";
        auto text = lines->Text(source, position.Line);

        if (text.ends_with('\r')) {
            text.remove_suffix(1);
        }

        auto column = fix ? diagnostic.CodePointColumn : position.Column;
        output << gutter.substr(0, gutter.size() - 1) << '\n' << prefix;
        Arena scratch;
        auto rendered = RenderLine(text, prefix.size(), &scratch);
        auto excerpt = PrintSourceExcerpt(output, rendered, column, prefix.size(), terminal.Width);

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

    if (!fix && checked.FormattingMismatch &&
        (!checked.Formatting.Summary || checked.Formatting.Summary->OtherChanges))
    {
        issue("formatting mismatch");
    }

    if (!fix && checked.Formatting.Summary && !checked.Formatting.Summary->IncludeOrderChanges.empty()) {
        issue("includes are not sorted", true);
    }

    if (!checked.Semantic.empty()) {
        issue("fail: naming, parameter-name or comment violation");
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

Error PrintCheckResult(
    std::ostream& output,
    std::string_view path,
    std::string_view source,
    const CheckedFile& checked,
    DiagnosticDetail diagnostics,
    bool fix,
    const Terminal& terminal,
    bool debug)
{
    if (debug) {
        PrintFileDebug(output, path, checked);
    }

    if (diagnostics == DiagnosticDetail::Quick) {
        if (fix && checked.FormattingMismatch) {
            output << path << ": reformatted\n";
        }

        if (checked.HasFailures(fix)) {
            output << path << ": violations found\n";
        }

        return {};
    }

    if (diagnostics == DiagnosticDetail::Summary) {
        if (fix && checked.FormattingMismatch) {
            output << path << ": reformatted\n";
        }

        auto issue = [&] (util::Position position, std::string_view message) {
            output << path << ':' << position.Line << ':' << position.Column << ": error: " << message << '\n';
        };

        if (!fix && checked.FormattingMismatch) {
            if (auto error = PrintFormattingLocations(output, path, source, checked.Formatting); error) {
                return error;
            }

            for (const auto& change : checked.Formatting.Summary->IncludeOrderChanges) {
                issue(change.Position, "includes are not sorted");
            }
        }

        for (const auto& diagnostic : checked.Semantic) {
            issue(fix ? diagnostic.Emitted : diagnostic.Original, diagnostic.Message);
        }

        for (const auto& diagnostic : checked.RequiredChanges) {
            auto position = fix ? diagnostic.Emitted : diagnostic.Original;
            output << path << ':' << position.Line << ':' << position.Column << ": error: " << diagnostic.Message;

            if (fix && diagnostic.BlockedByHunks) {
                output << BlockedHunkFix;
            }

            output << '\n';
        }

        for (const auto& diagnostic : checked.Formatting.Diagnostics) {
            output << path << ':' << (fix ? diagnostic.Position.Line : diagnostic.Original->Line) << ':'
                << (fix ? diagnostic.CodePointColumn : diagnostic.Original->Column) << ": error: "
                << (fix ? "cannot format this line automatically within ColumnLimit " : "line exceeds ColumnLimit ")
                << checked.ColumnLimit << '\n';
        }

        return {};
    }

    if (checked.FormattingMismatch) {
        if (auto error = PrintFormattingDiagnostics(output, path, source, checked.Formatting, terminal, fix); error) {
            return error;
        }
    }

    PrintFileIssues(output, path, source, checked, fix, terminal);
    return {};
}

void PrintQuickHint(std::ostream& output, bool fix)
{
    output << "hint: rerun with ";

    if (!fix) {
        output << "--fix to apply automatic fixes, ";
    }

    output << "--summary for locations and reasons, or --details for source excerpts and carets.\n";
}

} // namespace cli
