#pragma once

#include "../document.hpp"

#include <cstddef>
#include <iosfwd>
#include <string_view>
#include <tuple>

namespace paper::markdown {

enum class ProgressKind : unsigned { Page, Fraction };

union ProgressValue { unsigned Page; double Fraction; };

struct ProgressEvent { ProgressKind Kind; ProgressValue Value; };

struct ExportFlags {
  unsigned WriteSourceMap : 1 =
      1; // Keep preview links in the exported sidecar.
  unsigned AtomicReplace : 1 = 1; // Publish only after every asset is copied.
};

struct MarkdownView {
  const char *Data;
  std::size_t Size;

  const char &operator[](std::size_t Index) const noexcept {
    return Data[Index];
  }

  explicit operator bool() const noexcept { return Size != 0; }
};

struct ProgressContext { unsigned LastPage = 0; };

using ProgressCallback = void (*)(const ProgressEvent &Event, void *Context);
using RevealMethod = void (PreviewObserver::*)(std::size_t BlockId);

class ExportOperation {
public:
  virtual ~ExportOperation() = default;
  virtual Result run(ExportMode Mode) const = 0;
};

class ProgressSource {
public:
  virtual ~ProgressSource() = default;
  virtual void setProgress(ProgressCallback Callback, void *Context) = 0;
};

class ExportJob final
    : public ExportOperation,
      public ProgressSource {
public:
  ExportJob(const Document &InputDocument, Arguments ExportArguments);
  Result run(ExportMode Mode) const override;
  void setProgress(ProgressCallback Callback, void *Context) override;
private:
  const Document &SourceDocument;
  Arguments Settings;
  ProgressCallback ProgressCallbackValue = nullptr;
  void *ProgressCallbackContext = nullptr;
};

template <class MarkdownStream, class MappingStream, class AssetsStream,
          class ExportProgressReporter>
void dispatchExport(const Result &ExportResult, const std::tuple<
    MarkdownStream&, MappingStream&, AssetsStream&,
    ExportProgressReporter&> &Destinations) {
  auto &[Markdown, Mapping, Assets, Progress] = Destinations;
  Markdown.write(ExportResult.Markdown);
  Mapping.write(ExportResult.SourceOffsets);
  Assets.finish();
  Progress(ExportResult.Markdown.size());
}

void revealExportOffset(PreviewObserver &Observer, RevealMethod Method,
                        const Result &ExportResult, std::size_t Offset);
std::ostream &operator<<(std::ostream &Output, const ProgressEvent &Event);
std::string summarizeExport(const Document &InputDocument,
                            const Result &ExportResult, ExportFlags Flags);
std::string_view nativeLineEnding() noexcept;

unsigned progressBucket(unsigned Percent) noexcept;
std::string_view exportModeLabel(ExportMode Mode) noexcept;

extern "C" {

void paperExportProgress(unsigned Page, void *Context);

}

} // namespace paper::markdown
