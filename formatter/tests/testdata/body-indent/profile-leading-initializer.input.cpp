#include <string_view>

using Name = std::string_view;

    constexpr std::string_view Colors[2][3]{
    // Primary colors.
    {"red", "green", "blue"},
    // Alternate colors.
    {"cyan", "magenta", "yellow"},
};

        static const Name Extra[2]{
    "white",
    "black",
};

namespace Palette {
            constexpr Name Names[2]{
    "light",
    "dark",
};
}

struct Theme
{
            static constexpr Name Names[2]{
    "warm",
    "cool",
};
};

void UseColors()
{
            constexpr std::string_view Local[2][2]{
    // First row.
    {"light", "dark"},
    // Second row.
    {"warm", "cool"},
};
    Use(Local);
}
