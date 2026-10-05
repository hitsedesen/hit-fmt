#include <tokenizer/tokenizer.hpp>

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <string_view>

namespace {

thread_local bool TrackAllocations = false;

thread_local size_t AllocationCount = 0;

} // namespace

void* operator new(size_t size)
{
    if (TrackAllocations) {
        ++AllocationCount;
    }
    if (auto* storage = std::malloc(size == 0 ? 1 : size)) {
        return storage;
    }
    throw std::bad_alloc();
}

void* operator new[](size_t size)
{
    return ::operator new(size);
}

void operator delete(void* storage) noexcept
{
    std::free(storage);
}

void operator delete[](void* storage) noexcept
{
    std::free(storage);
}

void operator delete(void* storage, size_t) noexcept
{
    std::free(storage);
}

void operator delete[](void* storage, size_t) noexcept
{
    std::free(storage);
}

int main()
{
    static constexpr char Source[] = R"cpp(
auto raw = R"delimiter(raw string contents)delimiter";
auto literal = 42_a_user_defined_suffix_longer_than_sso;
auto integer = 42ULL;
auto floating = 0x1'2.a'Bp+3'4_a_suffix_longer_than_sso;
auto separated = 123'456'789_α42'3;
auto invalid_float = 1''2.0;
auto invalid_integer = 0b102;
auto invalid_escape = "\q";
@
auto broken = R"missing(raw contents;
)cpp" "\0\0\0";
    static constexpr util::PaddedSourceView SourceView{
        .Data = Source,
        .Size = sizeof(Source) - util::SourcePaddingSize,
    };

    size_t errorCount = 0;
    size_t descriptionSize = 0;
    TrackAllocations = true;
    tokenizer::Tokenizer tokenizer(
        SourceView, [&](tokenizer::TokenizationError error) {
            ++errorCount;
            descriptionSize +=
                tokenizer::GetTokenizationErrorDescription(error.Kind).size();
        });
    size_t tokenCount = 0;
    for ([[maybe_unused]] const auto& token : tokenizer) {
        ++tokenCount;
    }
    TrackAllocations = false;

    if (AllocationCount != 0) {
        std::fprintf(
            stderr,
            "tokenizer performed %zu allocation(s)\n",
            AllocationCount);
        return 1;
    }
    if (tokenCount == 0) {
        std::fputs("tokenizer produced no tokens\n", stderr);
        return 1;
    }
    if (errorCount != 5 || descriptionSize == 0) {
        std::fputs("tokenizer did not report expected errors\n", stderr);
        return 1;
    }
    return 0;
}
