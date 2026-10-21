#include "config_help.hpp"

#include "config_help_data.hpp"

#include <ostream>
#include <string_view>

namespace hit_fmt {

namespace {

constexpr std::string_view Reset = "\033[0m";

constexpr std::string_view Heading = "\033[1;36m";

constexpr std::string_view Setting = "\033[1;33m";

constexpr std::string_view Example = "\033[36m";

constexpr std::string_view Comment = "\033[2m";

void PrintHeading(std::ostream& output, std::string_view title, bool color)
{
    if (color) {
        output << Heading;
    }
    output << title;
    if (color) {
        output << Reset;
    }
    output << '\n';
}

void PrintCode(std::ostream& output, std::string_view code, bool color)
{
    size_t start = 0;
    while (start < code.size()) {
        auto end = code.find('\n', start);
        auto line = code.substr(
            start,
            end == std::string_view::npos ? code.size() - start : end - start);
        start = end == std::string_view::npos ? code.size() : end + 1;
        if (line.empty()) {
            output << '\n';
            continue;
        }
        if (color) {
            output
                << (line.starts_with("//") || line.starts_with("#")
                    ? Comment
                    : Example);
        }
        output << "    " << line;
        if (color) {
            output << Reset;
        }
        output << '\n';
    }
}

} // namespace

void PrintConfigHelp(std::ostream& output, bool useColor)
{
    PrintHeading(output, "Configuration options", useColor);
    output
        << "Flat Key = Value syntax; # comments and quoted-string arrays.\n"
            "Defaults preserve source break topology. "
            "Canonical layout is opt-in.\n"
            "Unknown option names and invalid values are errors.\n\n";
    PrintHeading(output, "Configuration inheritance", useColor);
    output
        << "include = \"base.hit-fmt\" loads a base configuration.\n"
            "Relative paths start at the including file's directory; absolute\n"
            "filesystem paths are also accepted. @/ starts at the nearest\n"
            "ancestor containing a .git or .arc file or directory.\n"
            "Local settings override the base regardless of include position.\n"
            "Lists replace inherited lists; [] clears them. Nested includes\n"
            "are supported (64 files max). Cycles, missing files and\n"
            "duplicate includes are errors.\n"
            "Validation uses merged settings.\n\n";
    PrintCode(
        output,
        "include = \"@/configs/base.hit-fmt\"\nColumnLimit = 100\n",
        useColor);
    output << '\n';
    for (const auto& section : HelpSections) {
        PrintHeading(output, section.Title, useColor);
        output << section.Description;
        for (const auto& example : section.Examples) {
            if (useColor) {
                output << Setting;
            }
            output << example.Title;
            if (useColor) {
                output << Reset;
            }
            output << '\n';
            PrintCode(output, example.Code, useColor);
            output << '\n';
        }
    }
}

} // namespace hit_fmt
