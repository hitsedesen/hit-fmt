#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

#if defined(_WIN32)
#define HIT_PLATFORM_WINDOWS 1
#define HIT_PLATFORM_LINUX 0
#define HIT_PLATFORM_APPLE 0
#elif defined(__APPLE__)
#define HIT_PLATFORM_WINDOWS 0
#define HIT_PLATFORM_LINUX 0
#define HIT_PLATFORM_APPLE 1
#else
#define HIT_PLATFORM_WINDOWS 0
#define HIT_PLATFORM_LINUX 1
#define HIT_PLATFORM_APPLE 0
#endif

#if defined(__clang__)
#define HIT_COMPILER_CLANG 1
#define HIT_COMPILER_GCC 0
#define HIT_COMPILER_MSVC 0
#elif defined(__GNUC__)
#define HIT_COMPILER_CLANG 0
#define HIT_COMPILER_GCC 1
#define HIT_COMPILER_MSVC 0
#elif defined(_MSC_VER)
#define HIT_COMPILER_CLANG 0
#define HIT_COMPILER_GCC 0
#define HIT_COMPILER_MSVC 1
#else
#error Unsupported compiler
#endif

#if HIT_PLATFORM_WINDOWS
#if defined(HIT_BUILD_SHARED)
#define HIT_EXPORT __declspec(dllexport)
#elif defined(HIT_USE_SHARED)
#define HIT_EXPORT __declspec(dllimport)
#else
#define HIT_EXPORT
#endif
#define HIT_LOCAL
#define HIT_CDECL __cdecl
#define HIT_STDCALL __stdcall
#else
#define HIT_EXPORT __attribute__((visibility("default")))
#define HIT_LOCAL __attribute__((visibility("hidden")))
#define HIT_CDECL
#define HIT_STDCALL
#endif

#if HIT_COMPILER_CLANG || HIT_COMPILER_GCC
#define HIT_LIKELY(expression) (__builtin_expect(!!(expression), 1))
#define HIT_UNLIKELY(expression) (__builtin_expect(!!(expression), 0))
#define HIT_FORCE_INLINE inline __attribute__((always_inline))
#define HIT_NO_INLINE __attribute__((noinline))
#define HIT_RESTRICT __restrict__
#define HIT_ASSUME(expression)                                                 \
    do {                                                                       \
        if (!(expression)) {                                                   \
            __builtin_unreachable();                                           \
        }                                                                      \
    }                                                                          \
    while (false)
#define HIT_DEBUG_BREAK() __builtin_trap()
#elif HIT_COMPILER_MSVC
#define HIT_LIKELY(expression) (expression)
#define HIT_UNLIKELY(expression) (expression)
#define HIT_FORCE_INLINE __forceinline
#define HIT_NO_INLINE __declspec(noinline)
#define HIT_RESTRICT __restrict
#define HIT_ASSUME(expression) __assume(expression)
#define HIT_DEBUG_BREAK() __debugbreak()
#endif

#if __has_cpp_attribute(nodiscard)
#define HIT_NODISCARD [[nodiscard]]
#else
#define HIT_NODISCARD
#endif

#if __has_cpp_attribute(likely)
#define HIT_IF_LIKELY(condition) if (condition) [[likely]]
#define HIT_IF_UNLIKELY(condition) if (condition) [[unlikely]]
#else
#define HIT_IF_LIKELY(condition) if (condition)
#define HIT_IF_UNLIKELY(condition) if (condition)
#endif

#define HIT_CONCAT_IMPL(left, right) left##right
#define HIT_CONCAT(left, right) HIT_CONCAT_IMPL(left, right)
#define HIT_STRINGIZE_IMPL(value) #value
#define HIT_STRINGIZE(value) HIT_STRINGIZE_IMPL(value)
#define HIT_UNIQUE_NAME(prefix) HIT_CONCAT(prefix, __COUNTER__)

#define HIT_DELETE_COPY(Type)                                                  \
    Type(const Type&) = delete;                                                \
    Type& operator=(const Type&) = delete

#define HIT_DELETE_MOVE(Type)                                                  \
    Type(Type&&) = delete;                                                     \
    Type& operator=(Type&&) = delete

#define HIT_DEFAULT_MOVE(Type)                                                 \
    Type(Type&&) noexcept = default;                                           \
    Type& operator=(Type&&) noexcept = default

#define HIT_ENUM_BIT_OPERATORS(Type)                                           \
    [[nodiscard]] constexpr Type operator|(Type left, Type right) noexcept     \
    {                                                                          \
        using Underlying = std::underlying_type_t<Type>;                       \
        return static_cast<Type>(                                              \
            static_cast<Underlying>(left) | static_cast<Underlying>(right));   \
    }                                                                          \
    [[nodiscard]] constexpr Type operator&(Type left, Type right) noexcept     \
    {                                                                          \
        using Underlying = std::underlying_type_t<Type>;                       \
        return static_cast<Type>(                                              \
            static_cast<Underlying>(left) & static_cast<Underlying>(right));   \
    }                                                                          \
    constexpr Type& operator|=(Type& left, Type right) noexcept                \
    {                                                                          \
        return left = left | right;                                            \
    }

#define HIT_ASSERT(expression)                                                 \
    do {                                                                       \
        if (HIT_UNLIKELY(!(expression))) {                                     \
            ::example::platform::AssertionFailure(                             \
                #expression, __FILE__, __LINE__, __func__);                    \
        }                                                                      \
    }                                                                          \
    while (false)

#define HIT_ENSURE(expression, message)                                        \
    do {                                                                       \
        if (HIT_UNLIKELY(!(expression))) {                                     \
            ::example::platform::EnsureFailure(                                \
                #expression, message, __FILE__, __LINE__, __func__);           \
        }                                                                      \
    }                                                                          \
    while (false)

#define HIT_SCOPE_EXIT(callback)                                               \
    auto HIT_UNIQUE_NAME(scopeExit_) =                                         \
        ::example::platform::MakeScopeExit(callback)

namespace example::platform {

using AssertionHandler = void (*)(
    const char* expression,
    const char* file,
    int line,
    const char* function) noexcept;

HIT_EXPORT void SetAssertionHandler(AssertionHandler handler) noexcept;

[[noreturn]] HIT_EXPORT void AssertionFailure(
    const char* expression,
    const char* file,
    int line,
    const char* function) noexcept;

[[noreturn]] HIT_EXPORT void EnsureFailure(
    const char* expression,
    const char* message,
    const char* file,
    int line,
    const char* function) noexcept;

template<class Callback>
class [[nodiscard]] ScopeExit
{
public:
    explicit ScopeExit(Callback callback) noexcept(
        std::is_nothrow_move_constructible_v<Callback>)
        : Callback_(std::move(callback))
    { }

    HIT_DELETE_COPY(ScopeExit);

    ScopeExit(ScopeExit&& other) noexcept(
        std::is_nothrow_move_constructible_v<Callback>)
        : Callback_(std::move(other.Callback_))
        , Active_(std::exchange(other.Active_, false))
    { }

    ~ScopeExit() noexcept
    {
        if (Active_) {
            Callback_();
        }
    }

    void Release() noexcept
    {
        Active_ = false;
    }

private:
    [[no_unique_address]] Callback Callback_;
    bool Active_ = true;
};

template<class Callback>
ScopeExit<std::decay_t<Callback>> MakeScopeExit(Callback&& callback)
{
    return ScopeExit<std::decay_t<Callback>>(std::forward<Callback>(callback));
}

struct CpuFeatures
{
    bool Sse2 = false;
    bool Sse42 = false;
    bool Avx = false;
    bool Avx2 = false;
    bool Avx512F = false;
    bool Neon = false;
};

HIT_NODISCARD HIT_EXPORT const CpuFeatures& GetCpuFeatures() noexcept;

HIT_FORCE_INLINE constexpr std::uint32_t
RotateLeft(std::uint32_t value, unsigned int amount) noexcept
{
#if __has_builtin(__builtin_rotateleft32)
    return __builtin_rotateleft32(value, amount);
#else
    amount &= 31;
    return (value << amount) | (value >> ((32 - amount) & 31));
#endif
}

HIT_FORCE_INLINE constexpr std::uint64_t ByteSwap(std::uint64_t value) noexcept
{
#if __has_builtin(__builtin_bswap64)
    return __builtin_bswap64(value);
#else
    value = ((value & 0x00ff00ff00ff00ffULL) << 8)
        | ((value & 0xff00ff00ff00ff00ULL) >> 8);
    value = ((value & 0x0000ffff0000ffffULL) << 16)
        | ((value & 0xffff0000ffff0000ULL) >> 16);
    return (value << 32) | (value >> 32);
#endif
}

#if HIT_PLATFORM_WINDOWS
using NativeFileHandle = void*;
inline constexpr NativeFileHandle InvalidFileHandle =
    reinterpret_cast<NativeFileHandle>(-1);
#elif HIT_PLATFORM_LINUX || HIT_PLATFORM_APPLE
using NativeFileHandle = int;
inline constexpr NativeFileHandle InvalidFileHandle = -1;
#endif

class HIT_EXPORT DynamicLibrary
{
public:
    DynamicLibrary() = default;
    explicit DynamicLibrary(const char* path);
    ~DynamicLibrary();

    HIT_DELETE_COPY(DynamicLibrary);
    HIT_DEFAULT_MOVE(DynamicLibrary);

    [[nodiscard]] explicit operator bool() const noexcept;
    [[nodiscard]] void* FindSymbol(const char* name) const noexcept;

    template<class Function>
    [[nodiscard]] Function* Find(const char* name) const noexcept
    {
        return reinterpret_cast<Function*>(FindSymbol(name));
    }

private:
    void* Handle_ = nullptr;
};

} // namespace example::platform
