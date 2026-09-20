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
    constexpr std::string_view Source = R"cpp(
auto raw = R"delimiter(raw string contents)delimiter";
auto literal = 42_a_user_defined_suffix_longer_than_sso;
auto integer = 42ULL;
auto invalid_escape = "\q";
@
auto broken = R"missing(raw contents;
)cpp";

    size_t errorCount = 0;
    size_t descriptionSize = 0;
    TrackAllocations = true;
    tokenizer::Tokenizer tokenizer(
        Source, [&](tokenizer::TokenizationError error) {
            ++errorCount;
            descriptionSize +=
                tokenizer::GetTokenizationErrorDescription(error.Kind).size();
        });
    size_t tokenCount = 0;
    for (auto token = tokenizer.Get();
         token.Kind != tokenizer::TokenKind::EndOfFile;
         token = tokenizer.Get())
    {
        ++tokenCount;
    }
    TrackAllocations = false;

    if (AllocationCount != 0) {
        std::fprintf(
            stderr, "tokenizer performed %zu allocation(s)\n", AllocationCount);
        return 1;
    }
    if (tokenCount == 0) {
        std::fputs("tokenizer produced no tokens\n", stderr);
        return 1;
    }
    if (errorCount != 3 || descriptionSize == 0) {
        std::fputs("tokenizer did not report expected errors\n", stderr);
        return 1;
    }
    return 0;
}
