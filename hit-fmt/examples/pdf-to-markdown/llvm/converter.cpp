#include "document.hpp"

#include <algorithm>
#include <type_traits>

namespace paper::markdown {
namespace {
std::string escape(std::string_view TextValue) {
  std::string Output;
  Output.reserve(TextValue.size());
  for (char Ch :
       TextValue) {
    if (Ch == '*' || Ch == '_' || Ch == '[' || Ch == ']' || Ch ==
        '|')Output += '\\';
    Output += Ch == '\n' ? ' ' : Ch;
  }
  return Output;
}

std::string tableRow(const std::vector<std::string> &Cells) {
  std::string Row = "|";
  for (const auto &Cell : Cells)appendParts(Row, " ", escape(Cell), " |");
  return Row + '\n';
}
} // namespace

const Block *Document::find(std::size_t Id) const noexcept {
  for (const auto &SourcePage :
       Pages)
    for (const auto &SourceBlock :
         SourcePage.Blocks)if (SourceBlock.Id == Id)return &SourceBlock;
  return nullptr;
}

std::string Converter::render(const Content &Payload) const {
  return std::visit([this]<class T>(const T &Item) -> std::string {
    if constexpr (
                  std::same_as<T, Text>) {
      if (Item.Role == TextRole::Footer && Settings.DropFooters)return {};
      auto Body = escape(Item.Value);
      if (Body.empty())return {};
      if (Item.Italic)Body = "*" + Body + "*";
      switch (Item
              .Role) {
      case TextRole::Heading:
        return std::string(std::clamp(Item.Level, 1u, 6u), '#') + " " + Body +
            "\n\n";
      case TextRole::Caption:return "> " + Body + "\n\n";
      default:return Body + "\n\n";
      }
    } else if constexpr (
                         std::same_as<T, Image>) {
      if (!Settings.KeepImages)return {};
      auto RenderedImage = "![" + escape(Item.Description) + "](" +
          Settings.AssetDirectory + "/" + Item.Asset + ")";
      return (Item.Link ? "[" + RenderedImage + "](" + *Item.Link + ")"
          : RenderedImage) + "\n\n";
    } else {
      if (Item.Headings.empty())return {};
      auto ConversionResult = tableRow(Item.Headings);
      ConversionResult +=
          tableRow(std::vector<std::string>(Item.Headings.size(), "---"));
      for (const auto &Row : Item.Rows)ConversionResult += tableRow(Row);
      return ConversionResult + '\n';
    }
  }, Payload);
}

Result Converter::convert(const Document &SourceDocument,
                          ExportMode Mode) const {
  Result ConversionResult{.Markdown = {}, .SourceOffsets = {}, .Skipped = 0};
  for (const Page &SourcePage : SourceDocument.Pages) {
    const auto PageStart = ConversionResult.Markdown.size();
    for (const auto &SourceBlock :
         SourcePage
             .Blocks) {
      if (Mode == ExportMode::Selection && !SourceBlock.Selected)continue;
      if (auto TextValue = render(SourceBlock.Payload);
          !TextValue
              .empty()) {
        ConversionResult.SourceOffsets
            .emplace_back(SourceBlock.Id, ConversionResult.Markdown.size());
        ConversionResult.Markdown += TextValue;
      } else { ++ConversionResult.Skipped; }
    }
    // Empty pages must not introduce separators into a selected excerpt.
    if (Settings.IncludePageBreaks &&
        ConversionResult.Markdown.size() !=
            PageStart)
      ConversionResult.Markdown +=
          "<!-- page " + std::to_string(SourcePage.Number) + " -->\n\n";
  }
  return ConversionResult;
}
} // namespace paper::markdown
