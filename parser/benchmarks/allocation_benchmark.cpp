#include <benchmark/benchmark.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <memory_resource>
#include <new>

namespace {

constexpr size_t InitialBlockSize = 64 * 1024;
constexpr size_t MaximumBlockSize = 1024 * 1024;
constexpr size_t TokenChunkCount = 5;
constexpr size_t NodeBlockCount = 9;
constexpr size_t BindingPairCount = 70;
constexpr size_t TriviaCount = 45;
constexpr size_t RequestCount = TokenChunkCount * 2 + 2 + NodeBlockCount
    + BindingPairCount * 2 + 1 + TriviaCount + 1;
constexpr size_t RequestedBytes = TokenChunkCount * (7184 + 2064) + 40 + 40
    + NodeBlockCount * 10240 + BindingPairCount * (16 + 32) + 3088
    + TriviaCount * 32 + 8;

static_assert(BindingPairCount == (NodeBlockCount - 1) * 8 + 6);
static_assert(RequestCount == 208);
static_assert(RequestedBytes == 146376);

// This reproduces the allocation sizes, counts, resource lifetimes, and block
// rollover of the service parser fixture. Persistent allocations belong to the
// returned syntax tree; scratch allocations belong to the parse context.
template <class AllocatePersistent, class AllocateScratch>
void ReplayParserAllocationTrace(
    AllocatePersistent&& allocatePersistent,
    AllocateScratch&& allocateScratch)
{
    // Five token chunks and the corresponding delimiter-map chunks.
    for (size_t i = 0; i < TokenChunkCount; ++i) {
        allocatePersistent(7184, 8);
        allocateScratch(2064, 8);
    }

    // Token and delimiter chunk indexes.
    allocatePersistent(40, 8);
    allocateScratch(40, 8);

    // Nine node blocks interleaved with 70 scopes and name bindings.
    size_t bindingPairs = 0;
    for (size_t nodeBlock = 0; nodeBlock < NodeBlockCount; ++nodeBlock) {
        allocatePersistent(10240, 8);
        auto pairs = nodeBlock + 1 == NodeBlockCount ? size_t{6} : size_t{8};
        for (size_t i = 0; i < pairs; ++i) {
            allocateScratch(16, 8);
            allocateScratch(32, 8);
            ++bindingPairs;
        }
    }
    benchmark::DoNotOptimize(bindingPairs);

    // Sparse trivia attachments and their chunk index.
    allocatePersistent(3088, 8);
    for (size_t i = 0; i < TriviaCount; ++i) {
        allocatePersistent(32, 8);
    }
    allocatePersistent(8, 8);
}

class ParserArena
{
public:
    ParserArena() = default;

    ~ParserArena()
    {
        while (Head_) {
            auto* next = Head_->Next;
            ::operator delete(Head_);
            Head_ = next;
        }
    }

    ParserArena(const ParserArena&) = delete;
    ParserArena& operator=(const ParserArena&) = delete;

#if defined(__GNUC__) || defined(__clang__)
    __attribute__((noinline))
#endif
    [[nodiscard]] void* Allocate(size_t size, size_t alignment)
    {
        if (!Head_ || !Fits(*Head_, size, alignment)) {
            AddBlock(size + alignment);
        }

        auto address = reinterpret_cast<uintptr_t>(Head_->Data()) + Head_->Used;
        auto aligned = (address + alignment - 1) & ~(alignment - 1);
        Head_->Used =
            aligned - reinterpret_cast<uintptr_t>(Head_->Data()) + size;
        return reinterpret_cast<void*>(aligned);
    }

private:
    struct Block
    {
        Block* Next;
        size_t Used;
        size_t Capacity;

        [[nodiscard]] std::byte* Data()
        {
            return reinterpret_cast<std::byte*>(this + 1);
        }
    };

    [[nodiscard]] static bool
    Fits(const Block& block, size_t size, size_t alignment)
    {
        auto address =
            reinterpret_cast<uintptr_t>(const_cast<Block&>(block).Data())
            + block.Used;
        auto padding = (-address) & (alignment - 1);
        return padding <= block.Capacity - block.Used &&
            size <= block.Capacity - block.Used - padding;
    }

    void AddBlock(size_t minimumCapacity)
    {
        auto capacity = std::max(NextBlockSize_, minimumCapacity);
        auto* memory = ::operator new(sizeof(Block) + capacity);
        Head_ = new (memory) Block{
            .Next = Head_,
            .Used = 0,
            .Capacity = capacity,
        };
        NextBlockSize_ = std::min(capacity * 2, MaximumBlockSize);
    }

    Block* Head_ = nullptr;
    size_t NextBlockSize_ = InitialBlockSize;
};

#if defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#endif
[[nodiscard]] void*
AllocatePmr(std::pmr::memory_resource& memory, size_t size, size_t alignment)
{
    return memory.allocate(size, alignment);
}

void SetCounters(benchmark::State& state)
{
    state.SetItemsProcessed(
        state.iterations() * static_cast<int64_t>(RequestCount));
    state.SetBytesProcessed(
        state.iterations() * static_cast<int64_t>(RequestedBytes));
}

void BM_ParserAllocationTraceArena(benchmark::State& state)
{
    for (auto _ : state) {
        ParserArena persistent;
        ParserArena scratch;
        ReplayParserAllocationTrace(
            [&] (size_t size, size_t alignment) {
                benchmark::DoNotOptimize(persistent.Allocate(size, alignment));
            },
            [&] (size_t size, size_t alignment) {
                benchmark::DoNotOptimize(scratch.Allocate(size, alignment));
            });
    }
    SetCounters(state);
}

void BM_ParserAllocationTracePmr(benchmark::State& state)
{
    for (auto _ : state) {
        auto persistent = std::make_unique<std::pmr::monotonic_buffer_resource>(
            static_cast<size_t>(state.range(0)));
        std::pmr::monotonic_buffer_resource scratch(InitialBlockSize);
        std::pmr::memory_resource* persistentMemory = persistent.get();
        std::pmr::memory_resource* scratchMemory = &scratch;
        ReplayParserAllocationTrace(
            [&] (size_t size, size_t alignment) {
                benchmark::DoNotOptimize(
                    AllocatePmr(*persistentMemory, size, alignment));
            },
            [&] (size_t size, size_t alignment) {
                benchmark::DoNotOptimize(
                    AllocatePmr(*scratchMemory, size, alignment));
            });
    }
    SetCounters(state);
}

BENCHMARK(BM_ParserAllocationTraceArena);
BENCHMARK(BM_ParserAllocationTracePmr)->Arg(InitialBlockSize)->Arg(192 * 1024);

} // namespace

BENCHMARK_MAIN();
