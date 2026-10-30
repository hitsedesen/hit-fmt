#include "document.hpp"

#include <algorithm>
#include <type_traits>

namespace paper::markdown {
namespace {
std::string Escape(std::string_view text) {
  std::string output;
  output.reserve(text.size());
  for (char ch : text) {
    if (ch == '*' || ch == '_' || ch == '[' || ch == ']' || ch ==
        '|')output += '\\';
    output += ch == '\n' ? ' ' : ch;
  }
  return output;
}

std::string TableRow(const std::vector<std::string>& cells) {
  std::string row = "|";
  for (const auto& cell : cells)AppendParts(row, " ", Escape(cell), " |");
  return row + '\n';
}
} // namespace

const Block* Document::Find(std::size_t id) const noexcept {
  for (const auto& page :
       pages)
    for (const auto& block : page.blocks)if (block.id == id)return &block;
  return nullptr;
}

std::string Converter::Render(const Content& content) const {
  return std::visit([this] <class T>(const T& item) -> std::string {
    if constexpr (std::same_as<T, Text>) {
      if (item.role == TextRole::kFooter && options_.drop_footers)return {};
      auto body = Escape(item.value);
      if (body.empty())return {};
      if (item.italic)body = "*" + body + "*";
      switch (item.role) {
        case TextRole::kHeading:
          return std::string(std::clamp(item.level, 1u, 6u), '#') + " " + body +
              "\n\n";
        case TextRole::kCaption: return "> " + body + "\n\n";
        default: return body + "\n\n";
      }
    } else if constexpr (std::same_as<T, Image>) {
      if (!options_.keep_images)return {};
      auto image = "![" + Escape(item.description) + "](" +
          options_.asset_directory + "/" + item.asset + ")";
      return (item.link ? "[" + image + "](" + *item.link + ")" : image) +
          "\n\n";
    } else {
      if (item.headings.empty())return {};
      auto result = TableRow(item.headings);
      result += TableRow(std::vector<std::string>(item.headings.size(), "---"));
      for (const auto& row : item.rows)result += TableRow(row);
      return result + '\n';
    }
  }, content);
}

Result Converter::Convert(const Document& document, ExportMode mode) const {
  Result result{.markdown = {}, .source_offsets = {}, .skipped = 0};
  for (const Page& page : document.pages) {
    const auto page_start = result.markdown.size();
    for (const auto& block : page.blocks) {
      if (mode == ExportMode::kSelection && !block.selected)continue;
      if (auto text = Render(block.content); !text.empty()) {
        result.source_offsets.emplace_back(block.id, result.markdown.size());
        result.markdown += text;
      } else { ++result.skipped; }
    }
    // Empty pages must not introduce separators into a selected excerpt.
    if (options_.include_page_breaks && result.markdown.size() !=
        page_start)
      result.markdown +=
          "<!-- page " + std::to_string(page.number) + " -->\n\n";
  }
  return result;
}
} // namespace paper::markdown
