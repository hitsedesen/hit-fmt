#pragma once

#include <algorithm>
#include <array>
#include <concepts>
#include <coroutine>
#include <cstddef>
#include <exception>
#include <functional>
#include <iterator>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

namespace example::support {

template<class T>
concept BooleanTestable = std::convertible_to<T, bool> && requires(T&& value) {
    { !std::forward<T>(value) } -> std::convertible_to<bool>;
};

template<class Range>
concept ContiguousSizedRange =
    std::ranges::contiguous_range<Range> && std::ranges::sized_range<Range>;

template<class T>
concept Allocator = requires(T allocator, size_t count) {
    typename T::value_type;
    { allocator.allocate(count) } -> std::same_as<typename T::value_type*>;
    allocator.deallocate(allocator.allocate(count), count);
};

template<class... Types>
struct TypeList
{ };

template<class List>
struct TypeListSize;

template<class... Types>
struct TypeListSize<TypeList<Types...>>
    : std::integral_constant<size_t, sizeof...(Types)>
{ };

template<class Head, class List>
struct TypeListPrepend;

template<class Head, class... Tail>
struct TypeListPrepend<Head, TypeList<Tail...>>
{
    using type = TypeList<Head, Tail...>;
};

template<size_t Index, class List>
struct TypeListElement;

template<class Head, class... Tail>
struct TypeListElement<0, TypeList<Head, Tail...>>
{
    using type = Head;
};

template<size_t Index, class Head, class... Tail>
struct TypeListElement<Index, TypeList<Head, Tail...>>
    : TypeListElement<Index - 1, TypeList<Tail...>>
{ };

template<class Function, class Tuple, size_t... Indices>
decltype(auto)
ApplyImpl(Function&& function, Tuple&& tuple, std::index_sequence<Indices...>)
{
    return std::invoke(
        std::forward<Function>(function),
        std::get<Indices>(std::forward<Tuple>(tuple))...);
}

template<class Function, class Tuple>
decltype(auto) Apply(Function&& function, Tuple&& tuple)
{
    constexpr auto Size = std::tuple_size_v<std::remove_reference_t<Tuple>>;
    return ApplyImpl(
        std::forward<Function>(function),
        std::forward<Tuple>(tuple),
        std::make_index_sequence<Size>{});
}

template<class... Functions>
struct Overload : Functions...
{
    using Functions::operator()...;
};

template<class... Functions>
Overload(Functions...) -> Overload<Functions...>;

template<class T, size_t Capacity>
class StaticVector
{
public:
    using value_type = T;
    using iterator = T*;
    using const_iterator = const T*;

    StaticVector() = default;

    StaticVector(const StaticVector& other)
        requires std::copy_constructible<T>
    {
        try {
            for (const auto& value : other) {
                emplace_back(value);
            }
        } catch (...) {
            clear();
            throw;
        }
    }

    StaticVector(StaticVector&& other) noexcept(
        std::is_nothrow_move_constructible_v<T>)
    {
        for (auto& value : other) {
            emplace_back(std::move(value));
        }
        other.clear();
    }

    StaticVector& operator=(StaticVector other) noexcept(
        std::is_nothrow_move_constructible_v<T>)
    {
        swap(other);
        return *this;
    }

    ~StaticVector()
    {
        clear();
    }

    template<class... Arguments>
        requires std::constructible_from<T, Arguments...>
    T& emplace_back(Arguments&&... arguments)
    {
        if (Size_ == Capacity) {
            throw std::bad_alloc();
        }
        auto* value = std::construct_at(
            data() + Size_, std::forward<Arguments>(arguments)...);
        ++Size_;
        return *value;
    }

    void push_back(const T& value)
        requires std::copy_constructible<T>
    {
        emplace_back(value);
    }

    void push_back(T&& value)
        requires std::move_constructible<T>
    {
        emplace_back(std::move(value));
    }

    void pop_back() noexcept
    {
        if (Size_ != 0) {
            --Size_;
            std::destroy_at(data() + Size_);
        }
    }

    void clear() noexcept
    {
        while (Size_ != 0) {
            pop_back();
        }
    }

    void swap(StaticVector& other) noexcept(std::is_nothrow_swappable_v<T>)
    {
        auto common = std::min(Size_, other.Size_);
        for (size_t index = 0; index < common; ++index) {
            using std::swap;
            swap((*this)[index], other[index]);
        }
        while (Size_ > other.Size_) {
            other.emplace_back(std::move(back()));
            pop_back();
        }
        while (other.Size_ > Size_) {
            emplace_back(std::move(other.back()));
            other.pop_back();
        }
    }

    [[nodiscard]] T* data() noexcept
    {
        return reinterpret_cast<T*>(Storage_.data());
    }

    [[nodiscard]] const T* data() const noexcept
    {
        return reinterpret_cast<const T*>(Storage_.data());
    }

    [[nodiscard]] size_t size() const noexcept
    {
        return Size_;
    }

    [[nodiscard]] static consteval size_t capacity() noexcept
    {
        return Capacity;
    }

    [[nodiscard]] bool empty() const noexcept
    {
        return Size_ == 0;
    }

    [[nodiscard]] T& operator[](size_t index) noexcept
    {
        return data()[index];
    }

    [[nodiscard]] const T& operator[](size_t index) const noexcept
    {
        return data()[index];
    }

    [[nodiscard]] T& front() noexcept
    {
        return (*this)[0];
    }

    [[nodiscard]] T& back() noexcept
    {
        return (*this)[Size_ - 1];
    }

    [[nodiscard]] iterator begin() noexcept
    {
        return data();
    }

    [[nodiscard]] iterator end() noexcept
    {
        return data() + Size_;
    }

    [[nodiscard]] const_iterator begin() const noexcept
    {
        return data();
    }

    [[nodiscard]] const_iterator end() const noexcept
    {
        return data() + Size_;
    }

private:
    alignas(T) std::array<std::byte, sizeof(T) * Capacity> Storage_{};
    size_t Size_ = 0;
};

template<class T>
class Generator
{
public:
    struct promise_type
    {
        std::optional<T> Current;
        std::exception_ptr Error;

        Generator get_return_object()
        {
            return Generator{
                std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        std::suspend_always initial_suspend() noexcept
        {
            return {};
        }

        std::suspend_always final_suspend() noexcept
        {
            return {};
        }

        std::suspend_always yield_value(T value)
        {
            Current.emplace(std::move(value));
            return {};
        }

        void return_void() noexcept { }

        void unhandled_exception() noexcept
        {
            Error = std::current_exception();
        }
    };

    class Iterator
    {
    public:
        using iterator_category = std::input_iterator_tag;
        using value_type = T;
        using difference_type = std::ptrdiff_t;

        Iterator() = default;

        explicit Iterator(std::coroutine_handle<promise_type> coroutine)
            : Coroutine_(coroutine)
        { }

        Iterator& operator++()
        {
            Coroutine_.resume();
            if (Coroutine_.done() && Coroutine_.promise().Error) {
                std::rethrow_exception(Coroutine_.promise().Error);
            }
            return *this;
        }

        void operator++(int)
        {
            ++*this;
        }

        [[nodiscard]] const T& operator*() const noexcept
        {
            return *Coroutine_.promise().Current;
        }

        [[nodiscard]] bool operator==(std::default_sentinel_t) const noexcept
        {
            return !Coroutine_ || Coroutine_.done();
        }

    private:
        std::coroutine_handle<promise_type> Coroutine_;
    };

    Generator(const Generator&) = delete;

    Generator(Generator&& other) noexcept
        : Coroutine_(std::exchange(other.Coroutine_, {}))
    { }

    ~Generator()
    {
        if (Coroutine_) {
            Coroutine_.destroy();
        }
    }

    [[nodiscard]] Iterator begin()
    {
        Coroutine_.resume();
        return Iterator(Coroutine_);
    }

    [[nodiscard]] std::default_sentinel_t end() const noexcept
    {
        return {};
    }

private:
    explicit Generator(std::coroutine_handle<promise_type> coroutine)
        : Coroutine_(coroutine)
    { }

    std::coroutine_handle<promise_type> Coroutine_;
};

template<std::integral T>
Generator<T> Iota(T first, T last)
{
    for (auto value = first; value != last; ++value) {
        co_yield value;
    }
}

template<class Range, class Function>
    requires std::ranges::input_range<Range>
auto Transform(Range&& range, Function function)
    -> Generator<std::remove_cvref_t<
        std::invoke_result_t<Function&, std::ranges::range_reference_t<Range>>>>
{
    for (auto&& value : range) {
        co_yield std::invoke(function, std::forward<decltype(value)>(value));
    }
}

template<class T, class... Arguments>
std::unique_ptr<T> MakeUniqueForOverwrite(Arguments&&... arguments)
    requires requires { T(std::forward<Arguments>(arguments)...); }
{
    auto storage = std::allocator<T>{}.allocate(1);
    try {
        std::construct_at(storage, std::forward<Arguments>(arguments)...);
        return std::unique_ptr<T>(storage);
    } catch (...) {
        std::allocator<T>{}.deallocate(storage, 1);
        throw;
    }
}

template<class Variant, class... Visitors>
decltype(auto) Match(Variant&& variant, Visitors&&... visitors)
{
    return std::visit(
        Overload{std::forward<Visitors>(visitors)...},
        std::forward<Variant>(variant));
}

template<class... Values>
constexpr auto HashCombine(const Values&... values) noexcept
{
    size_t seed = 0xcbf29ce484222325ULL;
    ((seed ^= std::hash<Values>{}(values) + 0x9e3779b97f4a7c15ULL + (seed << 6)
          + (seed >> 2)),
     ...);
    return seed;
}

} // namespace example::support
