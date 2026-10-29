#include "document.hpp"
#include "export/job.hpp"
#include <desktop/preview.hpp>
#include <desktop/task.hpp>

#include <exception>

namespace paper::markdown {
namespace {
constexpr std::string_view kWelcome = R"help(# Review before exporting
Select a PDF block to locate its Markdown counterpart.
Images are copied beside the exported document.
)help";

Document SampleDocument() {
  return {.title = "Field notes",
          .pages = {
                    {.number = 1,
                     .blocks = {
                                {.id = 10, .bounds = {36, 28, 480, 32},
                                 .content = Text{"A quieter reading room",
                                                 TextRole::kHeading, 1}},
                                {.id = 11, .bounds = {36, 76, 480, 64},
                                 .content = Text{
                          "Morning light reaches the shared desks "
                              "before the shelves."}},
                                {.id = 12, .bounds = {36, 160, 240,
                                                      180}, .content = Image{
                          "room.png", "Desks beside a window", std::nullopt}},
                                {.id = 13, .bounds = {36, 360, 480, 90},
                                 .content = Table{{"Zone", "Seats"},
                                                  {{"Window", "8"},
                                                   {"Shelves", "12"}}}}}}}};
}
} // namespace

Session::Session(Desktop& desktop, Document document,
                 Options options)
    : desktop_(desktop),
      document_(std::move(document)),
      options_(std::move(options)) {}

void Session::Refresh() {
  if (!dirty_)return;
  preview_ = Converter{options_}.Convert(document_, ExportMode::kSelection);
  desktop_.Preview().SetMarkdown(preview_.markdown)
      .SetSourceMap(preview_.source_offsets).PreserveScroll();
  desktop_.Status().Show("Ready", preview_.source_offsets.size(),
                         preview_.skipped);
  dirty_ = false;
}

void Session::RevealSource(std::size_t block_id) {
  if (const auto* block = document_.Find(block_id);
      block !=
      nullptr) {
    desktop_.Pdf().ScrollTo(block->bounds.top).Highlight(block->bounds);
    for (const auto& [id, offset] :
         preview_
             .source_offsets)
      if (id == block_id) { desktop_.Preview().Reveal(offset); break; }
  }
}

void Session::ExportTo(std::string_view path, ExportMode mode) {
  const auto result = ExportJob{document_, options_}.Run(mode);
  if (result.markdown
      .empty()) {
    desktop_.Toast("Select at least one visible block before exporting.");
    return;
  }
  desktop_.Files().Transaction(path, [&](auto& output) {
    output.Write(result.markdown);
    if (options_
        .keep_images)
      output.CopyReferencedAssets(document_, options_.asset_directory);
  });
  desktop_.Status().Show("Exported", path);
}

void Session::Run() {
  auto window = desktop_
      .Window(document_.title,
              {.width = 1120, .height = 760, .resizable = true});
  window.Toolbar().Toggle("Images", options_.keep_images)
      .Toggle("Page markers", options_.include_page_breaks);
  window.Split(0.46).Left(desktop_.Pdf().Open(document_))
      .Right(desktop_.Preview().Observe(*this));
  window.Help(kWelcome);
  Refresh();
  while (auto event = window
         .NextEvent()) {
    switch (event
            ->Kind()) {
      case EventKind::kSelection: {
        const auto selected = event->BlockIds();
        for (auto& page :
             document_
                 .pages)
          for (auto& block :
               page.blocks)block.selected = selected.contains(block.id);
        dirty_ = true;
        break;
      }
      case EventKind::kOptions:
        options_.keep_images = window.Toolbar().Checked("Images");
        options_.include_page_breaks = window.Toolbar().Checked("Page markers");
        dirty_ = true;
        break;
      case EventKind::kExportFile:
        if (auto path = desktop_.Dialogs().SaveFile("Export Markdown",
                                                    "notes.md",
                                                    {"*.md", "*.markdown"});
            path)
          ExportTo(*path, event->ShiftPressed() ? ExportMode::kWholeDocument
                   : ExportMode::kSelection);
        break;
      case EventKind::kClose: return;
      default: break;
    }
    Refresh();
  }
}

Task<void> OpenPdf(Desktop& desktop) {
  try {
    auto path = co_await desktop.Dialogs().OpenFile("Open PDF", {"*.pdf"});
    if (!path)co_return;
    auto reader = desktop.Pdf().Reader().ReadingOrder(ReadingOrder::kColumns)
        .RecoverDamagedPages(true);
#if defined(PAPER_ENABLE_OCR)
    reader.Ocr({.language = "eng", .minimum_confidence = 0.85});
#endif
    auto document = co_await reader.Extract(*path, [&desktop,
                                                    label = path->filename()](
        unsigned done,
        unsigned total) {
          desktop.Status()
              .Show(label,
                    total == 0 ? 0
                    : static_cast<unsigned>(100.0 * done / total));
        });
    Session{desktop, std::move(document),
            {.keep_images = true, .asset_directory = path->stem() + "-assets"}}
        .Run();
  } catch (
           const std::exception& error) {
    desktop.Toast("Could not read PDF", error.what());
  }
  co_return;
}
} // namespace paper::markdown
