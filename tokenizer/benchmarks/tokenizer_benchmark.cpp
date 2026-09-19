#include "benchmark_inputs.hpp"

#include <benchmark/benchmark.h>
#include <tokenizer/tokenizer.hpp>

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

namespace {

void RunTokenizer(
    benchmark::State& state, std::span<const std::string_view> sources)
{
    int64_t bytesPerIteration = 0;
    for (auto source : sources) {
        bytesPerIteration += static_cast<int64_t>(source.size());
    }

    int64_t processedTokens = 0;
    for (auto _ : state) {
        for (auto source : sources) {
            tokenizer::Tokenizer tokenizer(source);
            while (true) {
                auto token = tokenizer.Get();
                benchmark::DoNotOptimize(token.Kind);
                if (token.Kind == tokenizer::TokenKind::EndOfFile) {
                    break;
                }
                ++processedTokens;
            }
        }
    }

    state.SetBytesProcessed(state.iterations() * bytesPerIteration);
    state.SetItemsProcessed(processedTokens);
}

void BM_TokenizeService(benchmark::State& state)
{
    constexpr std::array Sources{tokenizer::benchmarks::Service};
    RunTokenizer(state, Sources);
}

void BM_TokenizeTemplateLibrary(benchmark::State& state)
{
    constexpr std::array Sources{tokenizer::benchmarks::TemplateLibrary};
    RunTokenizer(state, Sources);
}

void BM_TokenizePlatformHeader(benchmark::State& state)
{
    constexpr std::array Sources{tokenizer::benchmarks::Platform};
    RunTokenizer(state, Sources);
}

void BM_TokenizeMixedCorpus(benchmark::State& state)
{
    RunTokenizer(state, tokenizer::benchmarks::Corpus);
}

BENCHMARK(BM_TokenizeService)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_TokenizeTemplateLibrary)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_TokenizePlatformHeader)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_TokenizeMixedCorpus)->Unit(benchmark::kMicrosecond);

} // namespace

BENCHMARK_MAIN();
