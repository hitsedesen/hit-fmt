#pragma once

#include <concepts>
#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace paper::markdown {

struct Bounds {
  double Left = 0, Top = 0, Width = 0, Height = 0;
  [[nodiscard]] double bottom() const noexcept { return Top + Height; }
};

enum class TextRole { Paragraph, Heading, Caption, Footer };
enum class ExportMode { Selection, WholeDocument };

struct Text {
  std::string Value;
  TextRole Role = TextRole::Paragraph;
  unsigned Level = 1;
  bool Italic = false;
};

struct Image {
  std::string Asset, Description;
  std::optional<std::string> Link;
};

struct Table {
  std::vector<std::string> Headings;
  std::vector<std::vector<std::string>> Rows;
};

using Content = std::variant<Text, Image, Table>;
struct Block {
  std::size_t Id;
  Bounds Geometry;
  Content Payload;
  bool Selected = true;
};

struct Page { unsigned Number; std::vector<Block> Blocks; };

struct Document {
  std::string Title;
  std::vector<Page> Pages;
  [[nodiscard]] const Block *find(std::size_t Id) const noexcept;
};

struct Arguments {
  bool KeepImages = true;
  bool IncludePageBreaks = false;
  bool DropFooters = true;
  std::string AssetDirectory = "assets";
};

struct Result {
  std::string Markdown;
  std::vector<std::pair<std::size_t, std::size_t>> SourceOffsets;
  unsigned Skipped = 0;
};

template<class T>
concept TextSink = requires(T &Sink,
                            std::string_view TextValue) {
                              { Sink.append(TextValue) } -> std::same_as<T&>;
                            };

template<TextSink SinkType, class... PartTypes>
    requires (std::convertible_to<const PartTypes&, std::string_view> && ...)
void appendParts(SinkType &Sink,
                 const PartTypes &... Parts) noexcept ((noexcept(Sink.append(
                     Parts)) && ...)) { (Sink.append(Parts), ...); }

class Converter {
public:
  explicit Converter(Arguments Settings = {}) : Settings(std::move(Settings)) {}
  [[nodiscard]] Result convert(const Document &SourceDocument,
                               ExportMode Mode) const;
  [[nodiscard]] const Arguments &options() const & noexcept { return Settings; }
private:
  Arguments Settings;
  std::string render(const Content &Payload) const;
};

class PreviewObserver {
public:
  virtual ~PreviewObserver() = default;
  virtual void revealSource(std::size_t BlockId) = 0;
};

// These interfaces belong to the demonstration's imaginary desktop toolkit.
class Desktop;
class Session final
    : public PreviewObserver {
public:
  Session(Desktop &ActiveDesktop, Document SourceDocument, Arguments Settings);
  void run();
  void revealSource(std::size_t BlockId) override;
private:
  Desktop &ActiveDesktop;
  Document SourceDocument;
  Arguments Settings;
  Result PreviewResult;
  bool Dirty = true;
  void refresh();
  void exportTo(std::string_view Path, ExportMode Mode);
};

} // namespace paper::markdown
