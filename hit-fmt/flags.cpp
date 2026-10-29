#include "flags.hpp"

#include <algorithm>
#include <string>
#include <utility>

namespace hit_fmt {

namespace {

bool IsFlag(std::string_view argument)
{
    return argument.starts_with('-') && argument != "-";
}

} // namespace

bool Flags::Flag::Matches(std::string_view argument) const
{
    if (argument == Name) {
        return true;
    }
    if (Mode == ValueMode::None || !argument.starts_with(Name)) {
        return false;
    }
    return !Name.starts_with("--") || argument.substr(Name.size()).starts_with('=');
}

void Flags::Bool(std::string_view name, bool* target, bool value)
{
    const auto apply = [target, value] (std::optional<std::string_view>) {
        if (target) {
            *target = value;
        }
    };
    Registered_.push_back({name, ValueMode::None, {}, apply});
}

void Flags::Positionals(std::function<void(std::string_view)> apply)
{
    Positionals_ = std::move(apply);
}

void Flags::Parse(std::span<const char* const> args) const
{
    if (!args.empty()) {
        args = args.subspan(1);
    }
    bool positionalOnly = false;
    while (!args.empty()) {
        const std::string_view argument(args.front());
        args = args.subspan(1);
        if (positionalOnly || !IsFlag(argument)) {
            if (!Positionals_) {
                throw UsageError("unexpected argument: " + std::string(argument));
            }
            Positionals_(argument);
            continue;
        } else if (argument == "--") {
            positionalOnly = true;
            continue;
        }
        const auto flag = std::ranges::find_if(
            Registered_,
            [&] (const Flag& candidate) {
                return candidate.Matches(argument);
            });
        if (flag == Registered_.end()) {
            throw UsageError("unknown option: " + std::string(argument));
        }
        std::optional<std::string_view> value;
        if (argument != flag->Name) {
            value = argument.substr(flag->Name.size() + (flag->Name.starts_with("--") ? 1 : 0));
        } else if (flag->Mode != ValueMode::None && !args.empty() &&
            (flag->Mode == ValueMode::Required || !IsFlag(args.front())))
        {
            value = args.front();
            args = args.subspan(1);
        } else if (flag->Mode == ValueMode::Required) {
            throw UsageError(std::string(flag->MissingValueError));
        }
        flag->Apply(value);
    }
}

} // namespace hit_fmt
