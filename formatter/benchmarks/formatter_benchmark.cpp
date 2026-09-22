#include "benchmark_inputs.hpp"

#include <benchmark/benchmark.h>
#include <formatter/formatter.hpp>
#include <parser/parser.hpp>

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace {

int64_t InputBytes(std::span<const util::PaddedSourceView> sources)
{
    int64_t result = 0;
    for (auto source : sources) {
        result += static_cast<int64_t>(source.Size);
    }
    return result;
}

void SetCounters(
    benchmark::State& state,
    std::span<const util::PaddedSourceView> sources)
{
    state.SetBytesProcessed(state.iterations() * InputBytes(sources));
    state.SetItemsProcessed(
        state.iterations() * static_cast<int64_t>(sources.size()));
}

void RunFormatter(
    benchmark::State& state,
    std::span<const util::PaddedSourceView> sources)
{
    for (auto _ : state) {
        for (auto source : sources) {
            auto output = formatter::Format(source);
            benchmark::DoNotOptimize(output.data());
            benchmark::DoNotOptimize(output.size());
        }
    }
    SetCounters(state, sources);
}

void RunFormatterOnParsedTrees(
    benchmark::State& state,
    std::span<const util::PaddedSourceView> sources)
{
    std::vector<parser::SyntaxTree> trees;
    trees.reserve(sources.size());
    bool hasErrors = false;
    for (auto source : sources) {
        trees.push_back(parser::Parse(source, [&] (parser::ParseError) {
            hasErrors = true;
        }));
    }
    if (hasErrors) {
        state.SkipWithError("benchmark input produced parser errors");
        return;
    }

    for (auto _ : state) {
        for (const auto& tree : trees) {
            auto output = formatter::Format(tree);
            benchmark::DoNotOptimize(output.data());
            benchmark::DoNotOptimize(output.size());
        }
    }
    SetCounters(state, sources);
}

void BM_FormatService(benchmark::State& state)
{
    constexpr std::array Sources{benchmarks::Service};
    RunFormatter(state, Sources);
}

void BM_FormatTemplateLibrary(benchmark::State& state)
{
    constexpr std::array Sources{benchmarks::TemplateLibrary};
    RunFormatter(state, Sources);
}

void BM_FormatPlatformHeader(benchmark::State& state)
{
    constexpr std::array Sources{benchmarks::Platform};
    RunFormatter(state, Sources);
}

void BM_FormatMixedCorpus(benchmark::State& state)
{
    RunFormatter(state, benchmarks::Corpus);
}

void BM_FormatParsedService(benchmark::State& state)
{
    constexpr std::array Sources{benchmarks::Service};
    RunFormatterOnParsedTrees(state, Sources);
}

void BM_FormatParsedTemplateLibrary(benchmark::State& state)
{
    constexpr std::array Sources{benchmarks::TemplateLibrary};
    RunFormatterOnParsedTrees(state, Sources);
}

void BM_FormatParsedPlatformHeader(benchmark::State& state)
{
    constexpr std::array Sources{benchmarks::Platform};
    RunFormatterOnParsedTrees(state, Sources);
}

void BM_FormatParsedMixedCorpus(benchmark::State& state)
{
    RunFormatterOnParsedTrees(state, benchmarks::Corpus);
}

BENCHMARK(BM_FormatService)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_FormatTemplateLibrary)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_FormatPlatformHeader)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_FormatMixedCorpus)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_FormatParsedService)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_FormatParsedTemplateLibrary)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_FormatParsedPlatformHeader)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_FormatParsedMixedCorpus)->Unit(benchmark::kMicrosecond);

} // namespace

BENCHMARK_MAIN();
