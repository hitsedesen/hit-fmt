#pragma once

#include "../document.hpp"

#include <cstddef>
#include <iosfwd>
#include <string_view>
#include <tuple>

namespace NPaper::NMarkdown {

enum class EProgressKind : unsigned
{
    Page,
    Fraction
};

union TProgressValue
{
    unsigned Page;
    double Fraction;
};

struct TProgressEvent
{
    EProgressKind Kind;
    TProgressValue Value;
};

struct TExportFlags
{
    unsigned WriteSourceMap : 1 = 1; // Keep preview links in the exported sidecar.
    unsigned AtomicReplace : 1 = 1; // Publish only after every asset is copied.
};

struct TMarkdownView
{
    const char* Data;
    std::size_t Size;

    const char& operator[](std::size_t index) const noexcept
    {
        return Data[index];
    }

    explicit operator bool() const noexcept
    {
        return Size != 0;
    }
};

struct TProgressContext
{
    unsigned LastPage = 0;
};

using TProgressCallback = void (*)(const TProgressEvent& event, void* context);
using TRevealMethod = void (IPreviewObserver::*)(std::size_t blockId);

class IExportOperation
{
public:
    virtual ~IExportOperation() = default;
    virtual TResult Run(EExportMode mode) const = 0;
};

class IProgressSource
{
public:
    virtual ~IProgressSource() = default;
    virtual void SetProgress(TProgressCallback callback, void* context) = 0;
};

class TExportJob final
    : public IExportOperation
    , public IProgressSource
{
public:
    TExportJob(const TDocument& document, TOptions options);
    TResult Run(EExportMode mode) const override;
    void SetProgress(TProgressCallback callback, void* context) override;
private:
    const TDocument& Document_;
    TOptions Options_;
    TProgressCallback Callback_ = nullptr;
    void* Context_ = nullptr;
};

template <
    class TMarkdownOutputStream,
    class TSourceMappingOutputStream,
    class TAssetReferenceOutputStream,
    class TExportProgressReporter>
void DispatchExport(
    const TResult& result,
    const std::tuple<
        TMarkdownOutputStream&,
        TSourceMappingOutputStream&,
        TAssetReferenceOutputStream&,
        TExportProgressReporter&>& destinations)
{
    auto& [markdownOutput, sourceMappingOutput, assetReferenceOutput, reportProgress] = destinations;
    markdownOutput.Write(result.Markdown);
    sourceMappingOutput.Write(result.SourceOffsets);
    assetReferenceOutput.Finish();
    reportProgress(result.Markdown.size());
}

void RevealExportOffset(IPreviewObserver& observer, TRevealMethod method, const TResult& result, std::size_t offset);
std::ostream& operator<<(std::ostream& output, const TProgressEvent& event);
std::string SummarizeExport(const TDocument& document, const TResult& result, TExportFlags flags);
std::string_view NativeLineEnding() noexcept;

unsigned ProgressBucket(unsigned percent) noexcept;
std::string_view ExportModeLabel(EExportMode mode) noexcept;

extern "C" {

void paper_export_progress(unsigned page, void* context);

}

} // namespace NPaper::NMarkdown
