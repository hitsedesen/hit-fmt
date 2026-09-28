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
        auto line = code.substr(start, end == std::string_view::npos
                ? code.size() - start
                : end - start);
        start = end == std::string_view::npos ? code.size() : end + 1;
        if (line.empty()) {
            output << '\n';
            continue;
        }
        if (color) {
            output << (line.starts_with("//") || line.starts_with("#")
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
    output << "Examples show the effect of each setting on C or C++ code. "
              "Other settings named in an example are assumed to be enabled.\n"
              "Preserve leaves source layout unchanged and is not shown. "
              "Naming checks report violations; they never rename code.\n\n";
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
