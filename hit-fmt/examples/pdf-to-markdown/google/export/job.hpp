#pragma once

#include "../document.hpp"

#include <cstddef>
#include <iosfwd>
#include <string_view>
#include <tuple>

namespace paper::markdown {

enum class ProgressKind : unsigned { kPage, kFraction };

union ProgressValue { unsigned page; double fraction; };

struct ProgressEvent { ProgressKind kind; ProgressValue value; };

struct ExportFlags {
  unsigned write_source_map : 1 =
      1;  // Keep preview links in the exported sidecar.
  unsigned atomic_replace : 1 = 1;  // Publish only after every asset is copied.
};

struct MarkdownView {
  const char* data;
  std::size_t size;

  const char& operator[](std::size_t index) const noexcept {
    return data[index];
  }

  explicit operator bool() const noexcept { return size != 0; }
};

struct ProgressContext { unsigned last_page = 0; };

using ProgressCallback = void (*)(const ProgressEvent& event, void* context);
using RevealMethod = void (PreviewObserver::*)(std::size_t block_id);

class ExportOperation {
 public:
  virtual ~ExportOperation() = default;
  virtual Result Run(ExportMode mode) const = 0;
};

class ProgressSource {
 public:
  virtual ~ProgressSource() = default;
  virtual void SetProgress(ProgressCallback callback, void* context) = 0;
};

class ExportJob final
    : public ExportOperation,
      public ProgressSource {
 public:
  ExportJob(const Document& document, Options options);
  Result Run(ExportMode mode) const override;
  void SetProgress(ProgressCallback callback, void* context) override;
 private:
  const Document& document_;
  Options options_;
  ProgressCallback callback_ = nullptr;
  void* context_ = nullptr;
};

template <class MarkdownOutputStream, class SourceMappingOutputStream,
          class AssetReferenceOutputStream, class ExportProgressReporter>
void DispatchExport(const Result& result, const std::tuple<
    MarkdownOutputStream&, SourceMappingOutputStream&,
    AssetReferenceOutputStream&, ExportProgressReporter&>& destinations) {
  auto& [markdown, mapping, assets, progress] = destinations;
  markdown.Write(result.markdown);
  mapping.Write(result.source_offsets);
  assets.Finish();
  progress(result.markdown.size());
}

void RevealExportOffset(PreviewObserver& observer, RevealMethod method,
                        const Result& result, std::size_t offset);
std::ostream& operator<<(std::ostream& output, const ProgressEvent& event);
std::string SummarizeExport(const Document& document, const Result& result,
                            ExportFlags flags);
std::string_view NativeLineEnding() noexcept;

unsigned ProgressBucket(unsigned percent) noexcept;
std::string_view ExportModeLabel(ExportMode mode) noexcept;

extern "C" {

void paper_export_progress(unsigned page, void* context);

}

} // namespace paper::markdown
