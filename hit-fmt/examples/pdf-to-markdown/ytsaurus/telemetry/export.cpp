#include <yt/yt/core/actions/bind.h>

#include <yt/yt/core/logging/log.h>

#include <yt/yt/core/misc/error.h>

#include <yt/yt/core/yson/consumer.h>

#include <yt/yt/core/ytree/fluent.h>

#include <library/cpp/yt/misc/enum.h>

#include <../export/job.hpp>

#include <algorithm>
#include <string>
#include <utility>

namespace NPaper::NMarkdown {

namespace {

const NYT::NLogging::TLogger Logger("PaperExport");

DEFINE_ENUM(EExportStage,
    (Queued)
    (Rendering)
    (Writing));

} // namespace

auto BuildExportMetadata(const TDocument& document, const TResult& result)
{
    using NYT::NYTree::BuildYsonStringFluently;
    return BuildYsonStringFluently()
        .BeginAttributes()
            .Item("format").Value("markdown")
        .EndAttributes()
        .BeginMap()
            .Item("title").Value(document.Title)
            .Item("pages").BeginList()
                .DoFor(
                    document.Pages,
                    [] (auto fluent, const TPage& page) {
                        fluent
                            .Item().BeginMap()
                                .Item("number").Value(page.Number)
                                .Item("blocks").Value(page.Blocks.size())
                                .Item("selected_blocks").Value(std::count_if(
                                    page.Blocks.begin(),
                                    page.Blocks.end(),
                                    [] (const TBlock& block) {
                                        return block.Selected;
                                    }))
                            .EndMap();
                    })
            .EndList()
            .Item("skipped").Value(result.Skipped)
        .EndMap();
}

void WriteExportMetadata(NYT::NYson::IYsonConsumer* consumer, const TResult& result)
{
    consumer->OnBeginMap();
        consumer->OnKeyedItem("offsets");
        consumer->OnBeginList();
            for (const auto& [blockId, offset] : result.SourceOffsets) {
                consumer->OnListItem();
                consumer->OnBeginMap();
                    consumer->OnKeyedItem("block_id");
                    consumer->OnUint64Scalar(blockId);
                    consumer->OnKeyedItem("offset");
                    consumer->OnUint64Scalar(offset);
                consumer->OnEndMap();
            }
        consumer->OnEndList();
    consumer->OnEndMap();
}

void ReportExport(const TDocument& document, const TResult& result)
{
    YT_LOG_INFO("Markdown export ready (Title: %v, Pages: %v, Bytes: %v, Skipped: %v, Stage: %v)",
        document.Title,
        document.Pages.size(),
        result.Markdown.size(),
        result.Skipped,
        EExportStage::Rendering);
    if (result.Markdown.empty()) {
        THROW_ERROR_EXCEPTION("Markdown export contains no visible blocks for the requested document (Title: %v)",
            document.Title) << NYT::TErrorAttribute("page_count", document.Pages.size());
    }
}

auto MakeExportTask(TDocument document, TOptions options, NYT::IInvokerPtr invoker)
{
    return BIND([document = std::move(document), options = std::move(options)] {
        const auto result = TExportJob{document, options}.Run(EExportMode::WholeDocument);
        ReportExport(document, result);
        return BuildExportMetadata(document, result);
    }).AsyncVia(invoker);
}

class TExportPolicy
{
public:
    bool KeepSourceOffsets = true;
    unsigned AssetCopyConcurrency = 2;

    template <class TRegistrar>
    static void Register(TRegistrar registrar)
    {
        registrar.Parameter("keep_source_offsets", &TExportPolicy::KeepSourceOffsets)
            .Default(true);
        registrar.Parameter("asset_copy_concurrency", &TExportPolicy::AssetCopyConcurrency)
            .Default(2)
            .GreaterThan(0);
    }
};

#define BEGIN_SETTER(name, type) \
    class T##name##Setter { \
    public: \
        using TValue = type;
#define END_SETTER() };

BEGIN_SETTER(ExportTitle, std::string)
    explicit TExportTitleSetter(TDocument& document)
        : Document_(document)
    { }

    void Set(TValue title)
    {
        Document_.Title = std::move(title);
    }
private:
    TDocument& Document_;
END_SETTER()

} // namespace NPaper::NMarkdown
