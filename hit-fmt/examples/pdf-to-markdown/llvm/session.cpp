#include "document.hpp"

#include "export/job.hpp"

#include <desktop/preview.hpp>
#include <desktop/task.hpp>

#include <exception>

namespace paper::markdown {
namespace {
constexpr std::string_view Welcome = R"help(# Review before exporting
Select a PDF block to locate its Markdown counterpart.
Images are copied beside the exported document.
)help";

Document sampleDocument() {
  return {.Title = "Field notes",
          .Pages = {
                    {.Number = 1,
                     .Blocks = {
                                {.Id = 10, .Geometry = {36, 28, 480, 32},
                                 .Payload =
                                     Text{"A quieter reading room",
                                          TextRole::Heading, 1}},
                                {.Id = 11, .Geometry = {36, 76, 480,
                                                        64}, .Payload = Text{
                                    "Morning light reaches the shared desks "
                                        "before the shelves."}},
                                {.Id = 12, .Geometry = {36, 160, 240, 180},
                                 .Payload =
                                     Image{"room.png", "Desks beside a window",
                                           std::nullopt}},
                                {.Id = 13, .Geometry = {36, 360, 480, 90},
                                 .Payload =
                                     Table{{"Zone", "Seats"},
                                           {{"Window", "8"},
                                             {"Shelves", "12"}}}}}}}};
}
} // namespace

Session::Session(Desktop &ActiveDesktop, Document SourceDocument,
                 Options Settings)
    : ActiveDesktop(ActiveDesktop), SourceDocument(std::move(SourceDocument)),
      Settings(std::move(Settings)) {}

void Session::refresh() {
  if (!Dirty)return;
  PreviewResult =
      Converter{Settings}.convert(SourceDocument, ExportMode::Selection);
  ActiveDesktop.preview().setMarkdown(PreviewResult.Markdown)
      .setSourceMap(PreviewResult.SourceOffsets).preserveScroll();
  ActiveDesktop.status()
      .show("Ready", PreviewResult.SourceOffsets.size(), PreviewResult.Skipped);
  Dirty = false;
}

void Session::revealSource(std::size_t BlockId) {
  if (const auto *SourceBlock = SourceDocument.find(BlockId);
      SourceBlock != nullptr) {
    ActiveDesktop.pdf().scrollTo(SourceBlock->Geometry.Top)
        .highlight(SourceBlock->Geometry);
    for (const auto &[Id, Offset] :
         PreviewResult
             .SourceOffsets)
      if (Id == BlockId) { ActiveDesktop.preview().reveal(Offset); break; }
  }
}

void Session::exportTo(std::string_view Path, ExportMode Mode) {
  const auto ConversionResult = ExportJob{SourceDocument, Settings}.run(Mode);
  if (ConversionResult.Markdown.empty()) {
    ActiveDesktop.toast("Select at least one visible block before exporting.");
    return;
  }
  ActiveDesktop.files().transaction(Path, [&](auto &Output) {
    Output.write(ConversionResult.Markdown);
    if (Settings
        .KeepImages)
      Output.copyReferencedAssets(SourceDocument, Settings.AssetDirectory);
  });
  ActiveDesktop.status().show("Exported", Path);
}

void Session::run() {
  auto Window = ActiveDesktop.window(SourceDocument.Title, {.Width =
          1120, .Height = 760, .Resizable = true});
  Window.toolbar().toggle("Images", Settings.KeepImages)
      .toggle("Page markers", Settings.IncludePageBreaks);
  Window.split(0.46).left(ActiveDesktop.pdf().open(SourceDocument))
      .right(ActiveDesktop.preview().observe(*this));
  Window.help(Welcome);
  refresh();
  while (auto Event = Window.nextEvent()) {
    switch (Event->kind()) {
    case EventKind::Selection:{
        const auto Selected = Event->blockIds();
        for (auto &SourcePage :
             SourceDocument
                 .Pages)
          for (auto &SourceBlock :
               SourcePage
                   .Blocks)
            SourceBlock.Selected = Selected.contains(SourceBlock.Id);
        Dirty = true;
        break;
      }
    case EventKind::Options:
      Settings.KeepImages = Window.toolbar().checked("Images");
      Settings.IncludePageBreaks = Window.toolbar().checked("Page markers");
      Dirty = true;
      break;
    case EventKind::ExportFile:
      if (auto Path = ActiveDesktop.dialogs()
          .saveFile("Export Markdown", "notes.md", {"*.md", "*.markdown"});
          Path)
        exportTo(*Path, Event->shiftPressed() ? ExportMode::WholeDocument
                 : ExportMode::Selection);
      break;
    case EventKind::Close:return;
    default:break;
    }
    refresh();
  }
}

Task<void> openPdf(Desktop &ActiveDesktop) {
  try {
    auto Path =
        co_await ActiveDesktop.dialogs().openFile("Open PDF", {"*.pdf"});
    if (!Path)co_return;
    auto Reader = ActiveDesktop.pdf().reader().readingOrder(
        ReadingOrder::Columns).recoverDamagedPages(true);
#if defined(PAPER_ENABLE_OCR)
    Reader.ocr({.Language = "eng", .MinimumConfidence = 0.85});
#endif
    auto SourceDocument = co_await Reader.extract(*Path, [&ActiveDesktop,
                                                          Label = Path
                                                              ->filename()](
        unsigned Done, unsigned Total) {
      ActiveDesktop.status()
          .show(Label,
                Total == 0 ? 0 : static_cast<unsigned>(100.0 * Done / Total));
    });
    Session{ActiveDesktop, std::move(SourceDocument), {.KeepImages =
                true, .AssetDirectory = Path->stem() + "-assets"}}.run();
  } catch (const std::exception &Error) {
    ActiveDesktop.toast("Could not read PDF", Error.what());
  }
  co_return;
}
} // namespace paper::markdown
