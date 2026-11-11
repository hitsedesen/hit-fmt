#include <util/memory.hpp>

#include <cli/error.hpp>
#include <cli/file_error.hpp>
#include <cli/flags.hpp>

#include <gtest/gtest.h>

#include <tests/golden.hpp>

#include <filesystem>
#include <memory_resource>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

TEST(Error, DiagnosticGoldenRetainsTransientDetails)
{
    cli::FileFailure error;
    {
        Arena scratch;
        const fs::Path path("generated/temporary-directory/source.cpp", &scratch);
        try {
            throw std::runtime_error(std::string("failed to process ") + "the generated input");
        } catch (const std::exception&) {
            error = cli::FileError(path, cli::ExceptionError());
        }
    }

    std::ostringstream output;
    output << error << '\n';
    output << cli::FileError("large.cpp", cli::Warning{"file is too large; skipping"}) << '\n';
    output << cli::FileError("changed.cpp", cli::MakeError("missing changed ranges for input file")) << '\n';

    cli::Flags flags;
    const std::string argument = "unexpected-" + std::string(64, 'x');
    const char* argv[] = {"hit-fmt", argument.c_str()};
    const auto unexpected = flags.Parse(argv);
    ASSERT_TRUE(unexpected);
    EXPECT_EQ(unexpected.Code, cli::ErrorCode::UnexpectedArgument);
    EXPECT_EQ(unexpected.Argument.data(), argument.data());
    output << unexpected << '\n';

    test::golden::ExpectGolden(std::filesystem::path(HIT_FMT_TESTDATA_DIR) / "errors.out", output.str());
}

TEST(Error, SystemMessageIsFormattedOnlyWhenReported)
{
    struct Category
        : std::error_category
    {
        mutable int Calls = 0;

        const char* name() const noexcept override
        {
            return "test";
        }

        std::string message(int /*code*/) const override
        {
            ++Calls;
            return "operation failed";
        }
    } category;

    auto error = cli::FileError("source.cpp", cli::SystemFailure{"read file", {1, category}});
    EXPECT_EQ(category.Calls, 0);
    std::ostringstream output;
    output << error;
    EXPECT_EQ(category.Calls, 1);
    test::golden::ExpectGolden(std::filesystem::path(HIT_FMT_TESTDATA_DIR) / "system_error.out", output.str());
}
