#pragma once

#include <functional>
#include <optional>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace hit_fmt {

class UsageError
    : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

class Flags
{
public:
    void Bool(std::string_view name, bool* target, bool value = true);

    template <typename T>
    void Value(
        std::string_view name,
        T* target,
        void (*parse)(T&, std::string_view),
        std::string_view missingValueError)
    {
        const auto apply = [target, parse] (std::optional<std::string_view> value) {
            parse(*target, *value);
        };
        Registered_.push_back({name, ValueMode::Required, missingValueError, apply});
    }

    template <typename T>
    void OptionalValue(std::string_view name, T* target, void (*parse)(T&, std::optional<std::string_view>))
    {
        const auto apply = [target, parse] (std::optional<std::string_view> value) {
            parse(*target, value);
        };
        Registered_.push_back({name, ValueMode::Optional, {}, apply});
    }

    void Positionals(std::function<void(std::string_view)> apply);
    void Parse(std::span<const char* const> args) const;

private:
    enum class ValueMode
    {
        None,
        Required,
        Optional,
    };

    struct Flag
    {
        std::string_view Name;
        ValueMode Mode;
        std::string_view MissingValueError;
        std::function<void(std::optional<std::string_view>)> Apply;

        bool Matches(std::string_view argument) const;
    };

    std::vector<Flag> Registered_;
    std::function<void(std::string_view)> Positionals_;
};

} // namespace hit_fmt
