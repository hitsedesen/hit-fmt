#include "hunks.hpp"

#include <charconv>
#include <istream>
#include <stdexcept>

namespace formatter {

namespace {

size_t ParseHunkLine(std::string_view text, size_t record)
{
    size_t result = 0;
    auto [end, error] = std::from_chars(
        text.data(), text.data() + text.size(), result);
    if (text.empty() || error != std::errc{} ||
        end != text.data() + text.size() || result == 0)
    {
        throw std::runtime_error("changed ranges line " +
            std::to_string(record) + ": expected a positive line number");
    }
    return result;
}

} // namespace

ChangedHunkFiles ParseChangedHunks(
    std::istream& input, const std::filesystem::path& directory)
{
    ChangedHunkFiles files;
    std::string record;
    std::string current;
    size_t line = 0;
    size_t groupHunks = 0;
    while (std::getline(input, record)) {
        ++line;
        if (!record.empty() && record.back() == '\r') record.pop_back();
        if (record.empty()) continue;
        if (record.starts_with("file ")) {
            if (!current.empty() && groupHunks == 0) {
                throw std::runtime_error("changed ranges line " +
                    std::to_string(line) + ": file has no hunks");
            }
            auto path = record.substr(5);
            if (path.empty()) {
                throw std::runtime_error("changed ranges line " +
                    std::to_string(line) + ": empty file path");
            }
            auto absolute = std::filesystem::absolute(directory / path)
                .lexically_normal();
            current = absolute.string();
            files.Paths.emplace_back(absolute);
            groupHunks = 0;
        } else if (record.starts_with("hunk ")) {
            if (current.empty()) {
                throw std::runtime_error("changed ranges line " +
                    std::to_string(line) + ": hunk without file");
            }
            auto range = std::string_view(record).substr(5);
            auto colon = range.find(':');
            if (colon == std::string_view::npos) {
                throw std::runtime_error("changed ranges line " +
                    std::to_string(line) + ": expected hunk <first>:<last>");
            }
            Hunk hunk{ParseHunkLine(range.substr(0, colon), line),
                ParseHunkLine(range.substr(colon + 1), line)};
            if (hunk.End < hunk.Begin) {
                throw std::runtime_error("changed ranges line " +
                    std::to_string(line) + ": reversed hunk");
            }
            files.Hunks[current].push_back(hunk);
            ++groupHunks;
        } else {
            throw std::runtime_error("changed ranges line " +
                std::to_string(line) + ": expected file or hunk record");
        }
    }
    if (!input.eof()) {
        throw std::runtime_error("failed to read changed ranges");
    }
    if (!current.empty() && groupHunks == 0) {
        throw std::runtime_error("changed ranges: file has no hunks");
    }
    return files;
}

} // namespace formatter
