#include "job.hpp"

#include "../document.hpp"

#include <algorithm>
#include <functional>
#include <iterator>
#include <memory>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <utility>

#define PAPER_EXPORT_ABI_VERSION(major, minor) \
    (((major) << 8) | (minor))

#if defined(_WIN32)
    #if defined(PAPER_EXPORT_UNIX_NEWLINES)
        #define PAPER_EXPORT_NEWLINE "\n"
    #else
        #define PAPER_EXPORT_NEWLINE "\r\n"
    #endif
#elif defined(__unix__) || defined(__APPLE__)
    #define PAPER_EXPORT_NEWLINE "\n"
#else
    #define PAPER_EXPORT_NEWLINE "\n"
#endif

namespace NPaper::NMarkdown {

namespace {

static_assert(PAPER_EXPORT_ABI_VERSION(1, 2) == 0x0102);

// The table mirrors the two columns in the export dialog.
// hit-fmt off
constexpr std::string_view ExportLabels[][2] = {
    {"selection", "Selected blocks"},
    {"document",  "Whole document" },
};
// hit-fmt on

// clang-format off
constexpr unsigned ProgressThresholds[] = { 0,  25,  50,  75,  100 };
// clang-format on

} // namespace

TExportJob::TExportJob(const TDocument& document, TOptions options)
    : Document_(document)
    , Options_(std::move(options))
{ }

void TExportJob::SetProgress(TProgressCallback callback, void* context)
{
    Callback_ = callback;
    Context_ = context;
}

TResult TExportJob::Run(EExportMode mode) const
{
    if (Document_.Pages.empty()) {
        throw std::invalid_argument("Cannot export a document without pages");
    }
    for (std::size_t pageIndex = 0; pageIndex < Document_.Pages.size(); ++pageIndex) {
        if (Callback_) {
            Callback_({.Kind = EProgressKind::Page, .Value = {.Page = Document_.Pages[pageIndex].Number}}, Context_);
        }
    }
    auto result = TConverter{Options_}.Convert(Document_, mode);
    if (Callback_) {
        Callback_({.Kind = EProgressKind::Fraction, .Value = {.Fraction = 1.0}}, Context_);
    }
    return result;
}

void RevealExportOffset(IPreviewObserver& observer, TRevealMethod method, const TResult& result, std::size_t offset)
{
    const auto next = std::upper_bound(
        result.SourceOffsets.begin(),
        result.SourceOffsets.end(),
        offset,
        [] (std::size_t position, const auto& entry) {
            return position < entry.second;
        });
    if (next != result.SourceOffsets.begin()) {
        std::invoke(method, observer, std::prev(next)->first);
    }
}

std::ostream& operator<<(std::ostream& output, const TProgressEvent& event)
{
    return event.Kind == EProgressKind::Page
        ? output << "page=" << event.Value.Page
        : output << "progress=" << event.Value.Fraction;
}

std::string SummarizeExport(const TDocument& document, const TResult& result, TExportFlags flags)
{
    const auto status = result.Markdown.empty() ? "empty" : result.Skipped != 0 ? "partial" : "complete";
    std::ostringstream output;
    output << "Document: " << document.Title << ", pages: " << document.Pages.size() << ", exported blocks: "
        << result.SourceOffsets.size() << ", status: " << status;
    output << ", source map: " << (flags.WriteSourceMap ? "yes" : "no") << ", atomic: "
        << (flags.AtomicReplace ? "yes" : "no");
    auto preview = [
        &document,
        &result,
        &output,
        status,
        exportedBlocks = result.SourceOffsets.size(),
        totalPages = document.Pages.size()
    ] (std::size_t maximumCharacters) {
        const TMarkdownView view{result.Markdown.data(), std::min(maximumCharacters, result.Markdown.size())};
        auto bytes = std::unique_ptr<char[]>(new char[view.Size + 1]);
        for (std::size_t index = 0; index < view.Size; ++index) {
            bytes[index] = view[index];
        }
        bytes[view.Size] = '\0';
        output << ", preview of " << document.Title << ": " << bytes.get() << " (" << status << ", " << exportedBlocks
            << " blocks on " << totalPages << " pages)";
    };
    preview(/*maximumCharacters=*/80);
    return output.str();
}

std::string_view NativeLineEnding() noexcept
{
    return PAPER_EXPORT_NEWLINE;
}

unsigned ProgressBucket(unsigned percent) noexcept
{
    std::size_t index = 0;
    do {
        ++index;
    } while (index < std::size(ProgressThresholds) && ProgressThresholds[index] <= percent);
    return ProgressThresholds[index - 1];
}

std::string_view ExportModeLabel(EExportMode mode) noexcept
{
    return ExportLabels[mode == EExportMode::Selection ? 0 : 1][1];
}

extern "C" {

void paper_export_progress(unsigned page, void* context)
{
    auto* progress = static_cast<TProgressContext*>(context);
    if (progress != nullptr) {
        progress->LastPage = page;
    }
}

}

} // namespace NPaper::NMarkdown
