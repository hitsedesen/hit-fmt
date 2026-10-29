#include "document.hpp"

#include "export/job.hpp"

#include <desktop/preview.hpp>
#include <desktop/task.hpp>

#include <exception>

namespace NPaper::NMarkdown {

namespace {

constexpr std::string_view Welcome = R"help(# Review before exporting
Select a PDF block to locate its Markdown counterpart.
Images are copied beside the exported document.
)help";

TDocument SampleDocument()
{
    return {
        .Title = "Field notes",
        .Pages = {
            {
                .Number = 1,
                .Blocks = {
                    {
                        .Id = 10,
                        .Bounds = {36, 28, 480, 32},
                        .Content = TText{"A quieter reading room", ETextRole::Heading, 1},
                    },
                    {
                        .Id = 11,
                        .Bounds = {36, 76, 480, 64},
                        .Content = TText{"Morning light reaches the shared desks " "before the shelves."},
                    },
                    {
                        .Id = 12,
                        .Bounds = {36, 160, 240, 180},
                        .Content = TImage{"room.png", "Desks beside a window", std::nullopt},
                    },
                    {
                        .Id = 13,
                        .Bounds = {36, 360, 480, 90},
                        .Content = TTable{{"Zone", "Seats"}, {{"Window", "8"}, {"Shelves", "12"}}},
                    },
                },
            },
        },
    };
}

} // namespace

TSession::TSession(TDesktop& desktop, TDocument document, TOptions options)
    : Desktop_(desktop)
    , Document_(std::move(document))
    , Options_(std::move(options))
{ }

void TSession::Refresh()
{
    if (!Dirty_) {
        return;
    }
    Preview_ = TConverter{Options_}.Convert(Document_, EExportMode::Selection);
    Desktop_.Preview().SetMarkdown(Preview_.Markdown).SetSourceMap(Preview_.SourceOffsets).PreserveScroll();
    Desktop_.Status().Show("Ready", Preview_.SourceOffsets.size(), Preview_.Skipped);
    Dirty_ = false;
}

void TSession::RevealSource(std::size_t blockId)
{
    if (const auto* block = Document_.Find(blockId); block != nullptr) {
        Desktop_.Pdf().ScrollTo(block->Bounds.Top).Highlight(block->Bounds);
        for (const auto& [id, offset] : Preview_.SourceOffsets) {
            if (id == blockId) {
                Desktop_.Preview().Reveal(offset);
                break;
            }
        }
    }
}

void TSession::ExportTo(std::string_view path, EExportMode mode)
{
    const auto result = TExportJob{Document_, Options_}.Run(mode);
    if (result.Markdown.empty()) {
        Desktop_.Toast("Select at least one visible block before exporting.");
        return;
    }
    Desktop_
        .Files()
        .Transaction(
            path,
            [&] (auto& output) {
                output.Write(result.Markdown);
                if (Options_.KeepImages) {
                    output.CopyReferencedAssets(Document_, Options_.AssetDirectory);
                }
            });
    Desktop_.Status().Show("Exported", path);
}

void TSession::Run()
{
    auto window = Desktop_.Window(Document_.Title, {.Width = 1120, .Height = 760, .Resizable = true});
    window.Toolbar().Toggle("Images", Options_.KeepImages).Toggle("Page markers", Options_.IncludePageBreaks);
    window.Split(0.46).Left(Desktop_.Pdf().Open(Document_)).Right(Desktop_.Preview().Observe(*this));
    window.Help(Welcome);
    Refresh();
    while (auto event = window.NextEvent()) {
        switch (event->Kind()) {
            case EEventKind::Selection: {
                const auto selected = event->BlockIds();
                for (auto& page : Document_.Pages) {
                    for (auto& block : page.Blocks) {
                        block.Selected = selected.contains(block.Id);
                    }
                }
                Dirty_ = true;
                break;
            }
            case EEventKind::Options:
                Options_.KeepImages = window.Toolbar().Checked("Images");
                Options_.IncludePageBreaks = window.Toolbar().Checked("Page markers");
                Dirty_ = true;
                break;
            case EEventKind::ExportFile:
                if (auto path = Desktop_.Dialogs().SaveFile("Export Markdown", "notes.md", {"*.md", "*.markdown"});
                    path)
                {
                    ExportTo(*path, event->ShiftPressed() ? EExportMode::WholeDocument : EExportMode::Selection);
                }
                break;
            case EEventKind::Close:
                return;
            default:
                break;
        }
        Refresh();
    }
}

TTask<void> OpenPdf(TDesktop& desktop)
{
    try {
        auto path = co_await desktop.Dialogs().OpenFile("Open PDF", {"*.pdf"});
        if (!path) {
            co_return;
        }
        auto reader = desktop.Pdf().Reader().ReadingOrder(EReadingOrder::Columns).RecoverDamagedPages(true);
#if defined(PAPER_ENABLE_OCR)
        reader.Ocr({.Language = "eng", .MinimumConfidence = 0.85});
#endif
        auto document = co_await reader.Extract(
            *path,
            [&desktop, label = path->filename()] (unsigned done, unsigned total) {
                desktop.Status().Show(label, total == 0 ? 0 : static_cast<unsigned>(100.0 * done / total));
            });
        TSession{desktop, std::move(document), {.KeepImages = true, .AssetDirectory = path->stem() + "-assets"}}.Run();
    } catch (const std::exception& error) {
        desktop.Toast("Could not read PDF", error.what());
    }
    co_return;
}

} // namespace NPaper::NMarkdown
