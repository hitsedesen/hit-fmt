#include "benchmark_inputs.hpp"

#include <benchmark/benchmark.h>
#include <tokenizer/tokenizer.hpp>
#include <util/input.hpp>

#include <array>
#include <cstdint>
#include <span>

namespace {

void RunTokenizer(
    benchmark::State& state,
    std::span<const util::PaddedSourceView> sources)
{
    int64_t bytesPerIteration = 0;
    for (auto source : sources) {
        bytesPerIteration += static_cast<int64_t>(source.Size);
    }

    int64_t processedTokens = 0;
    for (auto _ : state) {
        for (auto source : sources) {
            tokenizer::Tokenizer tokenizer(source);
            for (const auto& token : tokenizer) {
                benchmark::DoNotOptimize(token.Kind);
                ++processedTokens;
            }
        }
    }

    state.SetBytesProcessed(state.iterations() * bytesPerIteration);
    state.SetItemsProcessed(processedTokens);
}

void BM_TokenizeService(benchmark::State& state)
{
    constexpr std::array Sources{benchmarks::Service};
    RunTokenizer(state, Sources);
}

void BM_TokenizeTemplateLibrary(benchmark::State& state)
{
    constexpr std::array Sources{benchmarks::TemplateLibrary};
    RunTokenizer(state, Sources);
}

void BM_TokenizePlatformHeader(benchmark::State& state)
{
    constexpr std::array Sources{benchmarks::Platform};
    RunTokenizer(state, Sources);
}

void BM_TokenizeMixedCorpus(benchmark::State& state)
{
    RunTokenizer(state, benchmarks::Corpus);
}

BENCHMARK(BM_TokenizeService)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_TokenizeTemplateLibrary)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_TokenizePlatformHeader)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_TokenizeMixedCorpus)->Unit(benchmark::kMicrosecond);

} // namespace

BENCHMARK_MAIN();
