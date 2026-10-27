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
        auto line = code.substr(start, end == std::string_view::npos ? code.size() - start : end - start);
        start = end == std::string_view::npos ? code.size() : end + 1;
        if (line.empty()) {
            output << '\n';
            continue;
        }
        if (color) {
            output << (line.starts_with("//") || line.starts_with("#") ? Comment : Example);
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
        << "Use Key = Value, # comments and [\"quoted\", \"strings\"] lists.\n"
            "Defaults keep line breaks; Layout = Canonical reflows code.\n"
            "Unknown names and invalid values are errors.\n"
            "Use --generate-config for all options, values and defaults.\n\n";
    PrintHeading(output, "Configuration inheritance", useColor);
    output
        << "Load a base file with include = \"base.hit-fmt\".\n"
            "- Paths start beside this config; absolute paths work too.\n"
            "- @/ starts at the nearest ancestor with a .git or .arc marker.\n"
            "- Local values win, regardless of include position.\n" "- Lists replace inherited lists; [] clears them.\n"
            "- Validate after merging. Nested includes: 64 files max.\n"
            "- Cycles, missing files and duplicate includes are errors.\n\n";
    PrintCode(output, "include = \"@/configs/base.hit-fmt\"\nColumnLimit = 100\n", useColor);
    output << '\n';
    bool firstSection = true;
    for (const auto& section : HelpSections) {
        if (!firstSection) {
            output << '\n';
        }
        firstSection = false;
        PrintHeading(output, section.Title, useColor);
        output << section.Description;
        for (const auto& example : section.Examples) {
            output << '\n';
            if (useColor) {
                output << Setting;
            }
            output << example.Title;
            if (useColor) {
                output << Reset;
            }
            output << '\n';
            PrintCode(output, example.Code, useColor);
        }
    }
}

} // namespace hit_fmt
