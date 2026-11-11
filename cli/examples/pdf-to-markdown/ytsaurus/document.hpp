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

namespace NPaper::NMarkdown {

////////////////////////////////////////////////////////////////////////////////

struct TBounds
{
    double Left = 0, Top = 0, Width = 0, Height = 0;

    [[nodiscard]] double Bottom() const noexcept
    {
        return Top + Height;
    }
};

////////////////////////////////////////////////////////////////////////////////

enum class ETextRole
{
    Paragraph,
    Heading,
    Caption,
    Footer
};

////////////////////////////////////////////////////////////////////////////////

enum class EExportMode
{
    Selection,
    WholeDocument
};

////////////////////////////////////////////////////////////////////////////////

struct TText
{
    std::string Value;
    ETextRole Role = ETextRole::Paragraph;
    unsigned Level = 1;
    bool Italic = false;
};

////////////////////////////////////////////////////////////////////////////////

struct TImage
{
    std::string Asset, Description;
    std::optional<std::string> Link;
};

////////////////////////////////////////////////////////////////////////////////

struct TTable
{
    std::vector<std::string> Headings;
    std::vector<std::vector<std::string>> Rows;
};

using TContent = std::variant<TText, TImage, TTable>;

////////////////////////////////////////////////////////////////////////////////

struct TBlock
{
    std::size_t Id;
    TBounds Bounds;
    TContent Content;
    bool Selected = true;
};

////////////////////////////////////////////////////////////////////////////////

struct TPage
{
    unsigned Number;
    std::vector<TBlock> Blocks;
};

////////////////////////////////////////////////////////////////////////////////

struct TDocument
{
    std::string Title;
    std::vector<TPage> Pages;
    [[nodiscard]] const TBlock* Find(std::size_t id) const noexcept;
};

////////////////////////////////////////////////////////////////////////////////

struct TArguments
{
    bool KeepImages = true;
    bool IncludePageBreaks = false;
    bool DropFooters = true;
    std::string AssetDirectory = "assets";
};

////////////////////////////////////////////////////////////////////////////////

struct TResult
{
    std::string Markdown;
    std::vector<std::pair<std::size_t, std::size_t>> SourceOffsets;
    unsigned Skipped = 0;
};

template <class TValue>
concept CTextSink = requires(TValue& sink, std::string_view text) {
    { sink.append(text) } -> std::same_as<TValue&>;
};

template <CTextSink TSink, class... TParts>
    requires (std::convertible_to<const TParts&, std::string_view> && ...)
void AppendParts(TSink& sink, const TParts&... parts) noexcept((noexcept(sink.append(parts)) && ...))
{
    (sink.append(parts), ...);
}

////////////////////////////////////////////////////////////////////////////////

class TConverter
{
public:
    explicit TConverter(TArguments options = {})
        : Arguments_(std::move(options))
    { }

    [[nodiscard]] TResult Convert(const TDocument& document, EExportMode mode) const;

    [[nodiscard]] const TArguments& Arguments() const & noexcept
    {
        return Arguments_;
    }
private:
    TArguments Arguments_;
    std::string Render(const TContent& content) const;
};

////////////////////////////////////////////////////////////////////////////////

class IPreviewObserver
{
public:
    virtual ~IPreviewObserver() = default;
    virtual void RevealSource(std::size_t blockId) = 0;
};

////////////////////////////////////////////////////////////////////////////////

// These interfaces belong to the demonstration's imaginary desktop toolkit.
class TDesktop;

////////////////////////////////////////////////////////////////////////////////

class TSession final
    : public IPreviewObserver
{
public:
    TSession(TDesktop& desktop, TDocument document, TArguments options);
    void Run();
    void RevealSource(std::size_t blockId) override;
private:
    TDesktop& Desktop_;
    TDocument Document_;
    TArguments Arguments_;
    TResult Preview_;
    bool Dirty_ = true;
    void Refresh();
    void ExportTo(std::string_view path, EExportMode mode);
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NPaper::NMarkdown
