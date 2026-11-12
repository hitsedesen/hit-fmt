#pragma once

#include "arguments.hpp"

#include <functional>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace cli {

class Flags
{
public:
    void Bool(std::string_view name, bool* target, bool value = true);

    template <typename T>
    void Constant(std::string_view name, T* target, T value)
    {
        auto apply = [target, value] (std::optional<std::string_view>) {
            if (target) {
                *target = value;
            }

            return ArgumentError{};
        };
        Registered_.push_back({name, ValueMode::None, {}, apply});
    }

    template <typename T>
    void Required(
        std::string_view name,
        T* target,
        ArgumentError (*parse)(T& target, std::string_view value),
        std::string_view missingValueError)
    {
        auto apply = [target, parse] (std::optional<std::string_view> value) {
            return parse(*target, *value);
        };
        Registered_.push_back({name, ValueMode::Required, missingValueError, apply});
    }

    template <typename T>
    void Optional(
        std::string_view name,
        T* target,
        ArgumentError (*parse)(T& target, std::optional<std::string_view> value))
    {
        auto apply = [target, parse] (std::optional<std::string_view> value) {
            return parse(*target, value);
        };
        Registered_.push_back({name, ValueMode::Optional, {}, apply});
    }

    void Positionals(std::function<void(std::string_view value)> apply);
    ArgumentError Parse(std::span<const char* const> args) const;

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
        std::function<ArgumentError(std::optional<std::string_view>)> Apply;

        bool Matches(std::string_view argument) const;
    };

    std::vector<Flag> Registered_;
    std::function<void(std::string_view value)> Positionals_;
};

} // namespace cli
