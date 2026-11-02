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

namespace paper::markdown {

namespace {

static_assert(PAPER_EXPORT_ABI_VERSION(1, 2) == 0x0102);

// The table mirrors the two columns in the export dialog.
// hit-fmt off
constexpr std::string_view kExportLabels[][2] = {
    {"selection", "Selected blocks"},
    {"document",  "Whole document" },
};
// hit-fmt on

// clang-format off
constexpr unsigned kProgressThresholds[] = { 0,  25,  50,  75,  100 };
// clang-format on

}  // namespace

ExportJob::ExportJob(const Document& document,
                     Options options)
    : document_(document),
      options_(std::move(options)) {}

void ExportJob::SetProgress(ProgressCallback callback, void* context) {
  callback_ = callback;
  context_ = context;
}

Result ExportJob::Run(ExportMode mode) const {
  if (document_.pages.empty()) {
    throw std::invalid_argument("Cannot export a document without pages");
  }
  for (std::size_t page_index = 0; page_index < document_.pages.size();
       ++page_index) {
    if (callback_) {
      callback_({.kind = ProgressKind::kPage,
                 .value = {.page = document_.pages[page_index].number}},
                context_);
    }
  }
  auto result = Converter{options_}.Convert(document_, mode);
  if (callback_) {
    callback_({.kind = ProgressKind::kFraction, .value = {.fraction = 1.0}},
              context_);
  }
  return result;
}

void RevealExportOffset(PreviewObserver& observer, RevealMethod method,
                        const Result& result, std::size_t offset) {
  const auto next =
      std::upper_bound(result.source_offsets.begin(),
                       result.source_offsets.end(), offset,
                       [](std::size_t position, const auto& entry) {
                         return position < entry.second;
                       });
  if (next != result.source_offsets.begin()) {
    std::invoke(method, observer, std::prev(next)->first);
  }
}

std::ostream& operator<<(std::ostream& output, const ProgressEvent& event) {
  return event.kind == ProgressKind::kPage
      ? output << "page=" << event.value.page
      : output << "progress=" << event.value.fraction;
}

std::string SummarizeExport(const Document& document, const Result& result,
                            ExportFlags flags) {
  const auto status = result.markdown.empty() ? "empty"
      : result.skipped != 0 ? "partial" : "complete";
  std::ostringstream output;
  output << "Document: " << document.title << ", pages: " <<
      document.pages.size() << ", exported blocks: " <<
      result.source_offsets.size() << ", status: " << status;
  output << ", source map: " << (flags.write_source_map ? "yes" : "no") <<
      ", atomic: " << (flags.atomic_replace ? "yes" : "no");
  auto preview = [&document, &result, &output, status, exported_blocks = result
                  .source_offsets.size(), total_pages = document.pages.size()](
      std::size_t maximum_characters) {
        const MarkdownView view{result.markdown
                .data(), std::min(maximum_characters, result.markdown.size())};
        auto bytes = std::unique_ptr<char[]>(new char[view.size + 1]);
        for (std::size_t index = 0; index < view.size; ++index) {
          bytes[index] = view[index];
        }
        bytes[view.size] = '\0';
        output << ", preview of " << document.title << ": " << bytes.get() <<
            " (" << status << ", " << exported_blocks << " blocks on " <<
            total_pages << " pages)";
      };
  preview(/*maximum_characters=*/80);
  return output.str();
}

std::string_view NativeLineEnding() noexcept { return PAPER_EXPORT_NEWLINE; }

unsigned ProgressBucket(unsigned percent) noexcept {
  std::size_t index = 0;
  do {
    ++index;
  } while (index < std::size(kProgressThresholds) &&
           kProgressThresholds[index] <= percent);
  return kProgressThresholds[index - 1];
}

std::string_view ExportModeLabel(ExportMode mode) noexcept {
  return kExportLabels[mode == ExportMode::kSelection ? 0 : 1][1];
}

extern "C" {

void paper_export_progress(unsigned page, void* context) {
  auto* progress = static_cast<ProgressContext*>(context);
  if (progress != nullptr) { progress->last_page = page; }
}

}

} // namespace paper::markdown
