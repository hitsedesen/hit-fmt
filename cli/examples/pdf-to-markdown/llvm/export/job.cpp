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
constexpr std::string_view ExportLabels[][2] = {
    {"selection", "Selected blocks"},
    {"document",  "Whole document" },
};
// hit-fmt on

// clang-format off
constexpr unsigned ProgressThresholds[] = { 0,  25,  50,  75,  100 };
// clang-format on

} // namespace

ExportJob::ExportJob(const Document &InputDocument,
                     Arguments ExportArguments)
    : SourceDocument(InputDocument), Settings(std::move(ExportArguments)) {}

void ExportJob::setProgress(ProgressCallback Callback, void *Context) {
  ProgressCallbackValue = Callback;
  ProgressCallbackContext = Context;
}

Result ExportJob::run(ExportMode Mode) const {
  if (SourceDocument.Pages.empty()) {
    throw std::invalid_argument("Cannot export a document without pages");
  }
  for (std::size_t PageIndex = 0; PageIndex < SourceDocument.Pages.size();
       ++PageIndex) {
    if (ProgressCallbackValue) {
      ProgressCallbackValue({.Kind = ProgressKind::Page, .Value = {.Page =
                  SourceDocument.Pages[PageIndex].Number}},
                            ProgressCallbackContext);
    }
  }
  auto ExportResult = Converter{Settings}.convert(SourceDocument, Mode);
  if (ProgressCallbackValue) {
    ProgressCallbackValue({.Kind = ProgressKind::Fraction,
                           .Value = {.Fraction = 1.0}},
                          ProgressCallbackContext);
  }
  return ExportResult;
}

void revealExportOffset(PreviewObserver &Observer, RevealMethod Method,
                        const Result &ExportResult, std::size_t Offset) {
  const auto Next = std::upper_bound(ExportResult.SourceOffsets.begin(),
                                     ExportResult.SourceOffsets.end(), Offset,
                                     [](std::size_t Position,
                                         const auto &Entry) {
                                       return Position < Entry.second;
                                     });
  if (Next != ExportResult.SourceOffsets.begin()) {
    std::invoke(Method, Observer, std::prev(Next)->first);
  }
}

std::ostream &operator<<(std::ostream &Output, const ProgressEvent &Event) {
  return Event.Kind == ProgressKind::Page
      ? Output << "page=" << Event.Value.Page
      : Output << "progress=" << Event.Value.Fraction;
}

std::string summarizeExport(const Document &InputDocument,
                            const Result &ExportResult, ExportFlags Flags) {
  const auto Status = ExportResult.Markdown.empty() ? "empty"
      : ExportResult.Skipped != 0 ? "partial" : "complete";
  std::ostringstream Output;
  Output << "Document: " << InputDocument.Title << ", pages: " <<
      InputDocument.Pages.size() << ", exported blocks: " <<
      ExportResult.SourceOffsets.size() << ", status: " << Status;
  Output << ", source map: " << (Flags.WriteSourceMap ? "yes" : "no") <<
      ", atomic: " << (Flags.AtomicReplace ? "yes" : "no");
  auto Preview = [&InputDocument, &ExportResult, &Output, Status,
                  ExportedBlocks = ExportResult.SourceOffsets.size(),
                  TotalPages = InputDocument.Pages.size()](
      std::size_t MaximumCharacters) {
    const MarkdownView View{ExportResult.Markdown
            .data(), std::min(MaximumCharacters, ExportResult.Markdown.size())};
    auto Bytes = std::unique_ptr<char[]>(new char[View.Size + 1]);
    for (std::size_t Index = 0; Index < View.Size; ++Index) {
      Bytes[Index] = View[Index];
    }
    Bytes[View.Size] = '\0';
    Output << ", preview of " << InputDocument.Title << ": " << Bytes.get() <<
        " (" << Status << ", " << ExportedBlocks << " blocks on " <<
        TotalPages << " pages)";
  };
  Preview(/*MaximumCharacters=*/80);
  return Output.str();
}

std::string_view nativeLineEnding() noexcept { return PAPER_EXPORT_NEWLINE; }

unsigned progressBucket(unsigned Percent) noexcept {
  std::size_t Index = 0;
  do {
    ++Index;
  } while (Index < std::size(ProgressThresholds) &&
           ProgressThresholds[Index] <= Percent);
  return ProgressThresholds[Index - 1];
}

std::string_view exportModeLabel(ExportMode Mode) noexcept {
  return ExportLabels[Mode == ExportMode::Selection ? 0 : 1][1];
}

extern "C" {

void paperExportProgress(unsigned Page, void *Context) {
  auto *Progress = static_cast<ProgressContext*>(Context);
  if (Progress != nullptr) { Progress->LastPage = Page; }
}

}

} // namespace paper::markdown
