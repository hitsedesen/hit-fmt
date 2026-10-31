#pragma once

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <vector>

struct FileBatch
{
    size_t FirstIndex = 0;
    std::vector<std::filesystem::path> Paths;
};

// Flush before adding a file that would exceed the byte limit. Oversized
// files form single-file batches. The first file is dispatched immediately.
template <typename Flush>
bool AddFileToBatch(
    FileBatch& batch,
    size_t& batchBytes,
    size_t& nextIndex,
    bool& first,
    const std::filesystem::path& path,
    size_t estimatedBytes,
    size_t maxFiles,
    size_t maxBytes,
    Flush&& flush)
{
    auto bytes = std::min(estimatedBytes, maxBytes);

    if (!batch.Paths.empty() && bytes > maxBytes - batchBytes) {
        if (!flush()) {
            return false;
        }

        batchBytes = 0;
    }

    if (batch.Paths.empty()) {
        batch.FirstIndex = nextIndex;
    }

    batch.Paths.push_back(path);
    ++nextIndex;
    batchBytes += bytes;

    if (batch.Paths.size() >= maxFiles || batchBytes >= maxBytes || first) {
        first = false;

        if (!flush()) {
            return false;
        }

        batchBytes = 0;
    }

    return true;
}
