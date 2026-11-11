#pragma once

#include "file_error.hpp"

#include <util/memory.hpp>
#include <util/types.hpp>

#include <fs/fs.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <memory_resource>
#include <optional>
#include <string>

namespace cli {

struct FileResult
{
    std::pmr::string Output;
    FileFailure Error;
    us Mismatches = 0;
    bool Failed = false;
};

struct BatchMemory
{
    static constexpr us Capacity = 16 * 1024;

    explicit BatchMemory(
        std::pmr::memory_resource* storage = std::pmr::get_default_resource(),
        std::pmr::memory_resource* upstream = std::pmr::get_default_resource())
        : Buffer(Capacity, storage)
        , Resource(Buffer.data(), Buffer.size(), upstream)
    { }

    Vector<u8> Buffer;
    Arena Resource;
};

} // namespace cli

template <typename Context>
struct BasicFileBatch
{
    static constexpr us Capacity = 4;

    struct Entry
    {
        fs::Path Path;
        Context ContextValue;
        cli::FileResult Result;
    };

    explicit BasicFileBatch(
        std::pmr::memory_resource* memory = std::pmr::get_default_resource(),
        std::pmr::memory_resource* upstream = std::pmr::get_default_resource())
        : Memory(memory, upstream)
    { }

    cli::BatchMemory Memory;
    std::array<std::optional<Entry>, Capacity> Entries;
    us FirstIndex = 0;
    us Count = 0;

    void Reset()
    {
        for (auto& entry : Entries) {
            entry.reset();
        }

        Count = 0;
        Memory.Resource.release();
    }
};

using FileBatch = BasicFileBatch<std::nullptr_t>;

// Flush before adding a file that would exceed the byte limit. Oversized
// files form single-file batches. The first file is dispatched immediately.
template <typename Context = std::nullptr_t, typename GetBatch, typename Flush>
bool AddFileToBatch(
    GetBatch&& getBatch,
    us& batchBytes,
    us& nextIndex,
    bool& first,
    const fs::Path& path,
    us estimatedBytes,
    us maxFiles,
    us maxBytes,
    Flush&& flush,
    Context context = {})
{
    auto* batch = getBatch();

    if (!batch) {
        return false;
    }

    auto bytes = std::min(estimatedBytes, maxBytes);

    if (batch->Count != 0 && bytes > maxBytes - batchBytes) {
        if (!flush()) {
            return false;
        }

        batchBytes = 0;
        batch = getBatch();

        if (!batch) {
            return false;
        }
    }

    if (batch->Count == 0) {
        batch->FirstIndex = nextIndex;
    }

    auto* memory = &batch->Memory.Resource;
    batch->Entries[batch->Count].emplace(typename BasicFileBatch<Context>::Entry{
        .Path = fs::Path(path, memory),
        .ContextValue = context,
        .Result = {.Output = std::pmr::string(memory)},
    });
    ++batch->Count;
    ++nextIndex;
    batchBytes += bytes;

    if (batch->Count >= std::min(maxFiles, BasicFileBatch<Context>::Capacity) || batchBytes >= maxBytes || first) {
        first = false;

        if (!flush()) {
            return false;
        }

        batchBytes = 0;
    }

    return true;
}
