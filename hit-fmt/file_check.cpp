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

// Emitted widths count bytes and tab stops, not tokenizer code points.
size_t WidthCaretColumn(std::string_view text, size_t target)
{
    size_t width = 1, column = 1;
    for (size_t offset = 0; offset < text.size() && width < target; ++column) {
        const auto c = static_cast<unsigned char>(text[offset]);
        const auto size = c < 0x80
            ? 1
            : unicode::utf8::GetFirstCodePointSize(text.substr(offset))
                .value_or(1);
        const auto nextWidth = width + (c == '\t' ? 8 - (width - 1) % 8 : size);
        if (nextWidth > target) {
            break;
        }
        width = nextWidth;
        offset += size;
    }
    return column;
}

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

    util::Position Position(size_t offset, bool containing = false)
    {
        if (offset < Cursor_ || offset > Text_.size()) {
            throw std::runtime_error("invalid emitted diagnostic offset");
        }
        while (Cursor_ < offset) {
            if (containing && NextOffset() > offset) {
                return Current_;
            }
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
        return Cursor_ +
            (c < 0x80
                ? 1
                : unicode::utf8::GetFirstCodePointSize(Text_.substr(Cursor_))
                    .value_or(1));
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

void MapWidthPositions(const parser::SyntaxTree& tree, CheckedFile& checked)
{
    if (checked.Formatting.Diagnostics.empty()) {
        return;
    }
    std::pmr::monotonic_buffer_resource scratch;
    std::pmr::vector<parser::TokenIndex> tokens(&scratch);
    tokens.reserve(tree.TokenCount());
    const auto& mapped = checked.Formatting.OriginalTokens;
    for (parser::TokenIndex token = 0; token < tree.TokenCount(); ++token) {
        if (mapped[token].Present) {
            tokens.push_back(token);
        }
    }
    std::sort(
        tokens.begin(),
        tokens.end(),
        [&] (auto a, auto b) {
            return mapped[a].Output.Begin < mapped[b].Output.Begin;
        });
    const auto& text = checked.Formatting.Text;
    size_t offset = 0, column = 1;
    uint32_t line = 1;
    struct Point
    {
        size_t Offset;
        size_t Diagnostic;
    };
    std::pmr::vector<Point> points(&scratch);
    points.reserve(checked.Formatting.Diagnostics.size());
    for (const auto& diagnostic : checked.Formatting.Diagnostics) {
        // Match the formatter's byte-width/tab-stop coordinates, including
        // CRLF and standalone CR. Translate the offending byte inside a
        // surviving token; generated text uses an original token anchor.
        while (offset < text.size() &&
            (line < diagnostic.Position.Line ||
                column < diagnostic.Position.Column))
        {
            const auto c = text[offset++];
            if (c == '\r' || c == '\n') {
                if (c == '\r' && offset < text.size() && text[offset] == '\n') {
                    ++offset;
                }
                ++line;
                column = 1;
            } else {
                column += c == '\t' ? 8 - (column - 1) % 8 : 1;
            }
        }
        auto token = std::upper_bound(
            tokens.begin(),
            tokens.end(),
            offset,
            [
                &
            ] (
                size_t point,
                auto id) { return point < mapped[id].Output.Begin; });
        if (token != tokens.begin()) {
            --token;
        }
        // Generated text, such as a namespace comment, uses the preceding
        // original token as its source anchor.
        size_t original = 0;
        if (token != tokens.end()) {
            const auto source = tree.Token(*token).Text;
            const auto output = mapped[*token].Output;
            original = source.Begin;
            if (offset >= output.Begin && offset < output.End &&
                output.End - output.Begin == source.End - source.Begin)
            {
                original += offset - output.Begin;
            }
        }
        points.push_back({original, points.size()});
    }
    std::sort(
        points.begin(),
        points.end(),
        [] (auto a, auto b) {
            return a.Offset < b.Offset;
        });
    checked.OriginalWidthPositions.resize(points.size());
    PositionSweep original(tree.Source());
    for (const auto point : points) {
        checked.OriginalWidthPositions[point.Diagnostic] =
            original.Position(point.Offset, true);
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
    checked.ColumnLimit = configuration.Formatting.Policies.ColumnLimit;
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
    MapWidthPositions(tree, checked);
    return checked;
}

void PrintFileIssues(
    std::ostream& output,
    std::string_view path,
    std::string_view originalSource,
    const CheckedFile& checked,
    bool fix,
    size_t terminalWidth)
{
    for (const auto& diagnostic : checked.Semantic) {
        const auto position = fix ? diagnostic.Emitted : diagnostic.Original;
        output << "fail: " << diagnostic.Message << "\n  --> " << path << ':'
            << position.Line << ':' << position.Column
            << (fix ? " [written output]" : " [original source]") << '\n';
    }
    if (checked.Formatting.Diagnostics.empty()) {
        return;
    }
    const std::string_view source = fix
        ? checked.Formatting.Text
        : originalSource;
    // One scan indexes borrowed line views, regardless of diagnostic order.
    std::vector<std::string_view> lines;
    lines.reserve(std::count(source.begin(), source.end(), '\n') + 1);
    for (size_t begin = 0; begin <= source.size();) {
        const auto newline = fix
            ? source.find_first_of("\r\n", begin)
            : source.find('\n', begin);
        const auto end = newline == source.npos ? source.size() : newline;
        auto text = source.substr(begin, end - begin);
        if (text.ends_with('\r')) {
            text.remove_suffix(1);
        }
        lines.push_back(text);
        if (newline == source.npos) {
            break;
        }
        begin = end + 1;
        if (fix && source[end] == '\r' && begin < source.size() &&
            source[begin] == '\n')
        {
            ++begin;
        }
    }
    for (size_t i = 0; i < checked.Formatting.Diagnostics.size(); ++i) {
        const auto& diagnostic = checked.Formatting.Diagnostics[i];
        const auto position = fix
            ? diagnostic.Position
            : checked.OriginalWidthPositions.at(i);
        output
            << "fail: cannot format this line automatically "
                "within ColumnLimit " << checked.ColumnLimit << "\n  --> "
            << path << ':' << position.Line << ':' << position.Column
            << (fix ? " [written output]" : " [original source]") << '\n';
        const auto number = std::to_string(position.Line);
        const auto gutter = std::string(number.size() + 1, ' ') + "| ";
        const auto prefix = number + " | ";
        const auto text = lines.at(position.Line - 1);
        const auto column = fix
            ? WidthCaretColumn(text, position.Column)
            : position.Column;
        output << gutter.substr(0, gutter.size() - 1) << '\n' << prefix;
        const auto excerpt = PrintSourceExcerpt(
            output,
            text,
            column,
            prefix.size(),
            terminalWidth);
        if (excerpt.CaretVisible) {
            output << gutter << std::string(excerpt.CaretOffset, ' ') << "^\n";
        }
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
        issue("fail: cannot format automatically within column limit");
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
