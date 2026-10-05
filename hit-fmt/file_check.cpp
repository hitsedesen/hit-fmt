#include "file_check.hpp"

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

// Sorted queries permit one byte/code-point sweep of each text, including
// multiple parameter-name points on a wide line. No per-diagnostic file scan.
class PositionSweep
{
public:
    explicit PositionSweep(std::string_view text)
        : Text_(text)
    { }

    size_t Offset(util::Position position)
    {
        while (Before(Current_, position) && Cursor_ < Text_.size()) {
            Advance();
        }
        if (Current_.Line != position.Line ||
            Current_.Column != position.Column)
        {
            throw std::runtime_error("invalid original diagnostic position");
        }
        return Cursor_;
    }

    util::Position Position(size_t offset)
    {
        if (offset < Cursor_ || offset > Text_.size()) {
            throw std::runtime_error("invalid emitted diagnostic offset");
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
    void Advance()
    {
        const auto c = static_cast<unsigned char>(Text_[Cursor_]);
        // Match tokenizer coordinates: each invalid UTF8 byte is one column.
        Cursor_ += c < 0x80
            ? 1
            : unicode::utf8::GetFirstCodePointSize(Text_.substr(Cursor_))
                .value_or(1);
        if (c == '\n') {
            ++Current_.Line;
            Current_.Column = 1;
        } else {
            ++Current_.Column;
        }
    }

    std::string_view Text_;
    size_t Cursor_ = 0;
    util::Position Current_{1, 1};
};

void MapSemanticPositions(const parser::SyntaxTree& tree, CheckedFile& checked)
{
    std::sort(
        checked.Semantic.begin(),
        checked.Semantic.end(),
        [] (
            const auto& a,
            const auto& b) { return Before(a.Original, b.Original); });
    std::pmr::monotonic_buffer_resource scratch;
    struct Point
    {
        size_t Offset;
        size_t Diagnostic;
    };
    std::pmr::vector<Point> points(&scratch);
    points.reserve(checked.Semantic.size());
    PositionSweep original(tree.Source());
    for (size_t i = 0; i < checked.Semantic.size(); ++i) {
        const auto offset = original.Offset(checked.Semantic[i].Original);
        // Prefer the token beginning at a point; otherwise the containing or
        // preceding endpoint. Missing-name diagnostics can be insertion points.
        parser::TokenIndex low = 0, high = tree.TokenCount();
        while (low < high) {
            const auto middle = low + (high - low) / 2;
            if (tree.Token(middle).Text.Begin < offset) {
                low = middle + 1;
            } else {
                high = middle;
            }
        }
        auto token = low;
        if (token == tree.TokenCount() ||
            tree.Token(token).Text.Begin != offset)
        {
            if (token == 0) {
                throw std::runtime_error(
                    "diagnostic has no original token anchor");
            }
            --token;
        }
        const auto range = tree.Token(token).Text;
        const auto& mapped = checked.Formatting.OriginalTokens.at(token);
        if (offset < range.Begin || offset > range.End || !mapped.Present ||
            mapped.Output.End - mapped.Output.Begin != range.End - range.Begin)
        {
            throw std::runtime_error(
                "diagnostic token identity did not survive formatting");
        }
        points.push_back({mapped.Output.Begin + offset - range.Begin, i});
    }
    std::sort(
        points.begin(),
        points.end(),
        [] (const auto& a, const auto& b) { return a.Offset < b.Offset; });
    PositionSweep emitted(checked.Formatting.Text);
    for (const auto point : points) {
        checked.Semantic[point.Diagnostic].Emitted =
            emitted.Position(point.Offset);
    }
}

} // namespace

PreparedFileConfiguration::PreparedFileConfiguration(
    formatter::Configuration configuration)
    : Settings(std::move(configuration))
    , Formatting(Settings.FormattingStyle)
{ }

bool CheckedFile::HasFailures(bool fix) const
{
    return (!fix && FormattingMismatch) || !Semantic.empty() ||
        !Formatting.Diagnostics.empty();
}

CheckedFile CheckFile(
    util::PaddedSourceView source,
    const PreparedFileConfiguration& configuration,
    std::string_view inputFilePath)
{
    auto tree = parser::Parse(
        source,
        [] (parser::ParseError) {},
        configuration.Formatting.ParsingOptions());
    CheckedFile checked;
    checked.Formatting = formatter::Format(
        tree,
        configuration.Formatting,
        {.SourceMap = true, .InputFilePath = inputFilePath});
    checked.FormattingMismatch = checked.Formatting.Text != tree.Source();
    for (auto& diagnostic : formatter::CheckNames(tree, configuration.Settings))
    {
        checked.Semantic.push_back({
            diagnostic.Position,
            {},
            diagnostic.Message + "; " + diagnostic.Expected,
        });
    }
    for (const auto& diagnostic :
        formatter::CheckParameterNames(tree, configuration.Settings))
    {
        auto message =
            "parameter #" + std::to_string(diagnostic.ParameterNumber) +
                " in " + diagnostic.Function + ": expected a name";
        if (diagnostic.AllowCommentedName) {
            message += " or /*name*/ comment";
        }
        checked.Semantic
            .push_back({diagnostic.Position, {}, std::move(message)});
    }
    MapSemanticPositions(tree, checked);
    return checked;
}

void PrintFileIssues(
    std::ostream& output,
    std::string_view path,
    const CheckedFile& checked,
    bool fix)
{
    for (const auto& diagnostic : checked.Semantic) {
        const auto position = fix ? diagnostic.Emitted : diagnostic.Original;
        output << "fail: " << diagnostic.Message << "\n  --> " << path << ':'
            << position.Line << ':' << position.Column
            << (fix ? " [written output]" : " [original source]") << '\n';
    }
    for (const auto& diagnostic : checked.Formatting.Diagnostics) {
        output << "fail: " << diagnostic.Message << "\n  --> " << path << ':'
            << diagnostic.Position.Line << ':' << diagnostic.Position.Column
            << (fix
                ? " [written output]"
                : " [formatted output; not original source]") << '\n';
    }
}

void PrintFileSummaryIssues(
    std::ostream& output,
    const CheckedFile& checked,
    bool fix)
{
    bool separated = false;
    auto issue = [&] (std::string_view message) {
        if (separated) {
            output << "; ";
        }
        output << message;
        separated = true;
    };
    if (!fix && checked.FormattingMismatch) {
        issue("formatting mismatch");
    }
    if (!checked.Semantic.empty()) {
        issue("fail: naming or parameter-name violation");
    }
    if (!checked.Formatting.Diagnostics.empty()) {
        issue("fail: emitted width violation");
    }
}

void ValidatePreviewOption(std::string_view argument)
{
    if (argument == "--changed-hunks" ||
        argument.starts_with("--changed-hunks=") ||
        argument == "--changed-ranges" ||
        argument.starts_with("--changed-ranges=") ||
        argument == "--no-spillover")
    {
        throw std::invalid_argument(
            "--changed-hunks/--changed-ranges and --no-spillover are "
            "unsupported in this whole-file preview; constrained-hunk "
            "integration is blocked");
    }
}

} // namespace hit_fmt
