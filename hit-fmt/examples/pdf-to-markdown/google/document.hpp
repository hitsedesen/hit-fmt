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
  double left = 0, top = 0, width = 0, height = 0;
  [[nodiscard]] double Bottom() const noexcept { return top + height; }
};

enum class TextRole { kParagraph, kHeading, kCaption, kFooter };
enum class ExportMode { kSelection, kWholeDocument };

struct Text {
  std::string value;
  TextRole role = TextRole::kParagraph;
  unsigned level = 1;
  bool italic = false;
};

struct Image {
  std::string asset, description;
  std::optional<std::string> link;
};

struct Table {
  std::vector<std::string> headings;
  std::vector<std::vector<std::string>> rows;
};

using Content = std::variant<Text, Image, Table>;
struct Block {
  std::size_t id;
  Bounds bounds;
  Content content;
  bool selected = true;
};

struct Page { unsigned number; std::vector<Block> blocks; };

struct Document {
  std::string title;
  std::vector<Page> pages;
  [[nodiscard]] const Block* Find(std::size_t id) const noexcept;
};

struct Options {
  bool keep_images = true;
  bool include_page_breaks = false;
  bool drop_footers = true;
  std::string asset_directory = "assets";
};

struct Result {
  std::string markdown;
  std::vector<std::pair<std::size_t, std::size_t>> source_offsets;
  unsigned skipped = 0;
};

template <class T>
concept TextSink = requires(T& sink,
                            std::string_view text) {
                              { sink.append(text) } -> std::same_as<T&>;
                            };

template <TextSink Sink, class... Parts>
    requires (std::convertible_to<const Parts&, std::string_view> && ...)
void AppendParts(Sink& sink,
                 const Parts&... parts)
noexcept((noexcept(sink.append(parts)) && ...)) { (sink.append(parts), ...); }

class Converter {
 public:
  explicit Converter(Options options = {}) : options_(std::move(options)) {}
  [[nodiscard]] Result Convert(const Document& document, ExportMode mode) const;
  [[nodiscard]] const Options& options() const & noexcept { return options_; }
 private:
  Options options_;
  std::string Render(const Content& content) const;
};

class PreviewObserver {
 public:
  virtual ~PreviewObserver() = default;
  virtual void RevealSource(std::size_t block_id) = 0;
};

// These interfaces belong to the demonstration's imaginary desktop toolkit.
class Desktop;
class Session final
    : public PreviewObserver {
 public:
  Session(Desktop& desktop, Document document, Options options);
  void Run();
  void RevealSource(std::size_t block_id) override;
 private:
  Desktop& desktop_;
  Document document_;
  Options options_;
  Result preview_;
  bool dirty_ = true;
  void Refresh();
  void ExportTo(std::string_view path, ExportMode mode);
};

} // namespace paper::markdown
