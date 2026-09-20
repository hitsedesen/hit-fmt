#include "benchmark_inputs.hpp"

#include <benchmark/benchmark.h>
#include <parser/parser.hpp>
#include <util/input.hpp>

#include <array>
#include <cstdint>
#include <span>

namespace {

bool HasParseErrors(std::span<const util::PaddedSourceView> sources)
{
    bool hasErrors = false;
    for (auto source : sources) {
        auto tree = parser::Parse(source, [&](parser::ParseError) {
            hasErrors = true;
        });
        auto rootKind = tree.Root().Kind;
        benchmark::DoNotOptimize(rootKind);
    }
    return hasErrors;
}

void RunParser(
    benchmark::State& state, std::span<const util::PaddedSourceView> sources)
{
    if (HasParseErrors(sources)) {
        state.SkipWithError("benchmark input produced parser errors");
        return;
    }

    int64_t bytesPerIteration = 0;
    for (auto source : sources) {
        bytesPerIteration += static_cast<int64_t>(source.Size);
    }

    int64_t processedTokens = 0;
    for (auto _ : state) {
        for (auto source : sources) {
            auto tree = parser::Parse(source);
            auto* firstChild = tree.Root().FirstChild;
            benchmark::DoNotOptimize(firstChild);
            processedTokens += static_cast<int64_t>(tree.TokenCount());
        }
    }

    state.SetBytesProcessed(state.iterations() * bytesPerIteration);
    state.SetItemsProcessed(processedTokens);
}

void BM_ParseService(benchmark::State& state)
{
    constexpr std::array Sources{benchmarks::Service};
    RunParser(state, Sources);
}

void BM_ParseTemplateLibrary(benchmark::State& state)
{
    constexpr std::array Sources{benchmarks::TemplateLibrary};
    RunParser(state, Sources);
}

void BM_ParsePlatformHeader(benchmark::State& state)
{
    constexpr std::array Sources{benchmarks::Platform};
    RunParser(state, Sources);
}

void BM_ParseMixedCorpus(benchmark::State& state)
{
    RunParser(state, benchmarks::Corpus);
}

BENCHMARK(BM_ParseService)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_ParseTemplateLibrary)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_ParsePlatformHeader)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_ParseMixedCorpus)->Unit(benchmark::kMicrosecond);

} // namespace

BENCHMARK_MAIN();
