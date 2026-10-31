#include "document.hpp"

#include <algorithm>
#include <type_traits>

namespace NPaper::NMarkdown {

namespace {

std::string Escape(std::string_view text)
{
    std::string output;
    output.reserve(text.size());

    for (char ch : text) {
        if (ch == '*' || ch == '_' || ch == '[' || ch == ']' || ch == '|') {
            output += '\\';
        }

        output += ch == '\n' ? ' ' : ch;
    }

    return output;
}

std::string TableRow(const std::vector<std::string>& cells)
{
    std::string row = "|";

    for (const auto& cell : cells) {
        AppendParts(row, " ", Escape(cell), " |");
    }

    return row + '\n';
}

} // namespace

const TBlock* TDocument::Find(std::size_t id) const noexcept
{
    for (const auto& page : Pages) {
        for (const auto& block : page.Blocks) {
            if (block.Id == id) {
                return &block;
            }
        }
    }

    return nullptr;
}

std::string TConverter::Render(const TContent& content) const
{
    return std::visit(
        [this] <class TValue> (const TValue& item) -> std::string {
            if constexpr (std::same_as<TValue, TText>) {
                if (item.Role == ETextRole::Footer && Options_.DropFooters) {
                    return {};
                }

                auto body = Escape(item.Value);

                if (body.empty()) {
                    return {};
                }

                if (item.Italic) {
                    body = "*" + body + "*";
                }

                switch (item.Role) {
                    case ETextRole::Heading:
                        return std::string(std::clamp(item.Level, 1u, 6u), '#') + " " + body + "\n\n";

                    case ETextRole::Caption:
                        return "> " + body + "\n\n";

                    default:
                        return body + "\n\n";
                }
            } else if constexpr (std::same_as<TValue, TImage>) {
                if (!Options_.KeepImages) {
                    return {};
                }

                auto image = "![" + Escape(item.Description) + "](" + Options_.AssetDirectory + "/" + item.Asset + ")";
                return (item.Link ? "[" + image + "](" + *item.Link + ")" : image) + "\n\n";
            } else {
                if (item.Headings.empty()) {
                    return {};
                }

                auto result = TableRow(item.Headings);
                result += TableRow(std::vector<std::string>(item.Headings.size(), "---"));
                for (const auto& row : item.Rows) {
                    result += TableRow(row);
                }

                return result + '\n';
            }
        },
        content);
}

TResult TConverter::Convert(const TDocument& document, EExportMode mode) const
{
    TResult result{.Markdown = {}, .SourceOffsets = {}, .Skipped = 0};

    for (const TPage& page : document.Pages) {
        const auto pageStart = result.Markdown.size();

        for (const auto& block : page.Blocks) {
            if (mode == EExportMode::Selection && !block.Selected) {
                continue;
            }

            if (auto text = Render(block.Content); !text.empty()) {
                result.SourceOffsets.emplace_back(block.Id, result.Markdown.size());
                result.Markdown += text;
            } else {
                ++result.Skipped;
            }
        }

        // Empty pages must not introduce separators into a selected excerpt.
        if (Options_.IncludePageBreaks && result.Markdown.size() != pageStart) {
            result.Markdown += "<!-- page " + std::to_string(page.Number) + " -->\n\n";
        }
    }

    return result;
}

} // namespace NPaper::NMarkdown
