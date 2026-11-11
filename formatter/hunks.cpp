#include "hunks.hpp"

#include <util/parse.hpp>
#include <util/types.hpp>

#include <stdexcept>

namespace formatter {

namespace {

us ParseHunkLine(std::string_view text, us record)
{
    auto [result, success] = TryParseInteger<us>(text);

    if (!success || result == 0) {
        throw std::runtime_error("changed ranges line " + std::to_string(record) + ": expected a positive line number");
    }

    return result;
}

} // namespace

ChangedHunkFiles ParseChangedHunks(std::string_view input, const fs::Path& directory)
{
    ChangedHunkFiles files;
    std::string current;
    us line = 0;
    us groupHunks = 0;

    while (!input.empty()) {
        const auto newline = input.find('\n');
        auto record = input.substr(0, newline);
        input = newline == std::string_view::npos ? std::string_view{} : input.substr(newline + 1);
        ++line;

        if (!record.empty() && record.back() == '\r') {
            record.remove_suffix(1);
        }

        if (record.empty()) {
            continue;
        }

        if (record.starts_with("file ")) {
            if (!current.empty() && groupHunks == 0) {
                throw std::runtime_error("changed ranges line " + std::to_string(line) + ": file has no hunks");
            }

            auto path = record.substr(5);

            if (path.empty()) {
                throw std::runtime_error("changed ranges line " + std::to_string(line) + ": empty file path");
            }

            auto resolved = fs::Absolute(directory / fs::Path(path));

            if (resolved.Error.Code) {
                throw std::runtime_error(resolved.Error.Code.message());
            }

            auto absolute = resolved.Value.lexically_normal();
            current = absolute.string();
            files.Paths.emplace_back(absolute);
            groupHunks = 0;
        } else if (record.starts_with("hunk ")) {
            if (current.empty()) {
                throw std::runtime_error("changed ranges line " + std::to_string(line) + ": hunk without file");
            }

            auto range = record.substr(5);
            auto colon = range.find(':');

            if (colon == std::string_view::npos) {
                throw std::runtime_error(
                    "changed ranges line " + std::to_string(line) + ": expected hunk <first>:<last>");
            }

            Hunk hunk{ParseHunkLine(range.substr(0, colon), line), ParseHunkLine(range.substr(colon + 1), line)};

            if (hunk.End < hunk.Begin) {
                throw std::runtime_error("changed ranges line " + std::to_string(line) + ": reversed hunk");
            }

            files.Hunks[current].push_back(hunk);
            ++groupHunks;
        } else {
            throw std::runtime_error("changed ranges line " + std::to_string(line) + ": expected file or hunk record");
        }
    }

    if (!current.empty() && groupHunks == 0) {
        throw std::runtime_error("changed ranges: file has no hunks");
    }

    return files;
}

} // namespace formatter
