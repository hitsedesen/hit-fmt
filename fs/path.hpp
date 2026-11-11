#pragma once

#include <util/types.hpp>

#include <compare>
#include <functional>
#include <memory_resource>
#include <string>
#include <string_view>
#include <type_traits>

namespace fs {

#ifdef _WIN32
using Char = wchar_t;
inline constexpr Char Separator = L'\\';
#else
using Char = char;
inline constexpr Char Separator = '/';
#endif
using PathView = std::basic_string_view<Char>;
using NativeString = std::pmr::basic_string<Char>;

NativeString FromUtf8(std::string_view text, std::pmr::memory_resource* memory);
us Utf8Size(PathView text);
void EncodeUtf8(PathView text, char* output, us size);

class Path
{
public:
    using value_type = Char;
    using string_type = NativeString;
    static constexpr Char preferred_separator = Separator;
    Path() = default;

    Path(std::pmr::memory_resource* memory)
        : Text_(memory)
    { }

    Path(PathView text, std::pmr::memory_resource* memory = std::pmr::get_default_resource())
        : Text_(text, memory)
    { }

    Path(const Char* text, std::pmr::memory_resource* memory = std::pmr::get_default_resource())
        : Text_(text, memory)
    { }

    Path(const NativeString& text, std::pmr::memory_resource* memory = std::pmr::get_default_resource())
        : Text_(text, memory)
    { }

    Path(const Path& path, std::pmr::memory_resource* memory)
        : Text_(path.Text_, memory)
    { }

    Path(const Path& other) = default;
    Path(Path&& other) noexcept = default;
    Path& operator=(const Path& other) = default;
    Path& operator=(Path&& other) = default;
#ifdef _WIN32
    Path(std::string_view text, std::pmr::memory_resource* memory = std::pmr::get_default_resource())
        : Text_(FromUtf8(text, memory))
    { }

    Path(const char* text, std::pmr::memory_resource* memory = std::pmr::get_default_resource())
        : Path(std::string_view(text), memory)
    { }
#endif
    template <typename T>
        requires requires(const T& value) {
            value.native();
        }
    Path(const T& path)
        : Path(PathView(path.native()))
    { }

    template <typename Traits, typename Allocator>
    Path(const std::basic_string<Char, Traits, Allocator>& text)
        : Path(PathView(text))
    { }

    operator PathView() const
    {
        return Text_;
    }

    const NativeString& native() const
    {
        return Text_;
    }

    const Char* c_str() const
    {
        return Text_.c_str();
    }

    Char* data()
    {
        return Text_.data();
    }

    const Char* data() const
    {
        return Text_.data();
    }

    bool empty() const
    {
        return Text_.empty();
    }

    us size() const
    {
        return Text_.size();
    }

    Char back() const
    {
        return Text_.back();
    }

    Char operator[](us i) const
    {
        return Text_[i];
    }

    void push_back(Char ch)
    {
        Text_.push_back(ch);
    }

    void resize(us size)
    {
        Text_.resize(size);
    }

    void append(PathView text)
    {
        Text_.append(text);
    }

    Path& operator+=(PathView text)
    {
        append(text);
        return *this;
    }

    template <typename C = char, typename Traits = std::char_traits<C>, typename Allocator = std::allocator<C>>
    std::basic_string<C, Traits, Allocator> string(const Allocator& allocator = {}) const
    {
        static_assert(std::is_same_v<C, char>);
        std::basic_string<C, Traits, Allocator> result(allocator);
        result.resize(Utf8Size(Text_));
        EncodeUtf8(Text_, result.data(), result.size());
        return result;
    }
    template <typename C = char, typename Traits = std::char_traits<C>, typename Allocator = std::allocator<C>>
    std::basic_string<C, Traits, Allocator> generic_string(const Allocator& allocator = {}) const
    {
        auto result = string<C, Traits, Allocator>(allocator);
#ifdef _WIN32
        for (auto& ch : result) {
            if (ch == '\\') {
                ch = '/';
            }
        }
#endif
        return result;
    }
    Path parent_path(std::pmr::memory_resource* memory = std::pmr::get_default_resource()) const;
    Path filename(std::pmr::memory_resource* memory = std::pmr::get_default_resource()) const;
    Path stem() const;
    Path extension() const;
    Path& replace_extension(const Path& extension = {});
    bool is_absolute() const;
    bool has_root_path() const;
    Path lexically_normal(std::pmr::memory_resource* memory = std::pmr::get_default_resource()) const;
    Path lexically_relative(
        const Path& base,
        std::pmr::memory_resource* memory = std::pmr::get_default_resource()) const;
    Path& make_preferred();
    friend Path operator/(const Path& base, const Path& suffix);

    friend bool operator==(const Path& a, const Path& b)
    {
        return a.Text_ == b.Text_;
    }

    friend auto operator<=>(const Path& a, const Path& b)
    {
        return a.Text_ <=> b.Text_;
    }
private:
    NativeString Text_;
};

template <typename T>
    requires requires(const T& value) {
        value.native();
    }
bool operator==(const Path& a, const T& b)
{
    return a == Path(b);
}

} // namespace fs

template <>
struct std::hash<fs::Path>
{
    us operator()(const fs::Path& path) const noexcept
    {
        return std::hash<fs::PathView>{}(path.native());
    }
};
