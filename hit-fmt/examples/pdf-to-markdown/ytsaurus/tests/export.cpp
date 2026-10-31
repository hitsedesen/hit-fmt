#include "../export/job.hpp"

#include <gmock/gmock.h>

#include <gtest/gtest.h>

#include <stdexcept>

namespace NPaper::NMarkdown {

////////////////////////////////////////////////////////////////////////////////

class TPreviewObserverMock final
    : public IPreviewObserver
{
public:
    MOCK_METHOD(void, RevealSource, (std::size_t blockId), (override));
};

TEST(TMarkdownExport, RevealsBlockAtExportedOffset)
{
    TPreviewObserverMock observer;
    const TResult result{.Markdown = "# Notes\n\nText\n", .SourceOffsets = {{10, 0}, {11, 9}}};
    EXPECT_CALL(observer, RevealSource(11));
    RevealExportOffset(observer, &IPreviewObserver::RevealSource, result, /*offset*/ 11);
}

TEST(TMarkdownExport, RejectsDocumentWithoutPages)
{
    const TDocument document{.Title = "Empty notebook", .Pages = {}};
    const TExportJob job{document, {}};
    EXPECT_THROW(job.Run(EExportMode::WholeDocument), std::invalid_argument);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NPaper::NMarkdown
