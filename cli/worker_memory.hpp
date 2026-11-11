#pragma once

#include <util/memory.hpp>
#include <util/types.hpp>

#include <memory_resource>

namespace cli {

struct WorkerMemory
{
    static constexpr us FileCapacity = 4 * 1024 * 1024;
    static constexpr us ScratchCapacity = 64 * 1024;

    explicit WorkerMemory(std::pmr::memory_resource* upstream = std::pmr::get_default_resource())
        : Storage(FileCapacity + ScratchCapacity, upstream)
        , File(Storage.data(), FileCapacity, upstream)
        , Scratch(Storage.data() + FileCapacity, ScratchCapacity, upstream)
    { }

    Vector<u8> Storage;
    Arena File;
    Arena Scratch;

    void Reset()
    {
        Scratch.release();
        File.release();
    }
};

} // namespace cli
