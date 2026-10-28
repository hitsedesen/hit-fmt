#include "config_help.hpp"

#include "config_help_data.hpp"

#include <ostream>
#include <string_view>

namespace hit_fmt {

namespace {

void PrintHeading(std::ostream& output, std::string_view title, const TerminalColors& colors)
{
    output << colors.BoldCyan << title << colors.Reset << '\n';
}

void PrintCode(std::ostream& output, std::string_view code, const TerminalColors& colors)
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
        output << (line.starts_with("//") || line.starts_with("#") ? colors.Dim : colors.Cyan) << "    " << line
            << colors.Reset << '\n';
    }
}

} // namespace

void PrintConfigHelp(std::ostream& output, const TerminalColors& colors)
{
    PrintHeading(output, "Configuration options", colors);
    output
        << "Use Key = Value, # comments and [\"quoted\", \"strings\"] lists.\n"
            "Defaults keep line breaks; Layout = Canonical reflows code.\n"
            "Unknown names and invalid values are errors.\n"
            "Use --generate-config for all options, values and defaults.\n\n";
    PrintHeading(output, "Configuration inheritance", colors);
    output
        << "Load a base file with include = \"base.hit-fmt\".\n"
            "- Paths start beside this config; absolute paths work too.\n"
            "- @/ starts at the nearest ancestor with a .git or .arc marker.\n"
            "- Local values win, regardless of include position.\n" "- Lists replace inherited lists; [] clears them.\n"
            "- Validate after merging. Nested includes: 64 files max.\n"
            "- Cycles, missing files and duplicate includes are errors.\n\n";
    PrintCode(output, "include = \"@/configs/base.hit-fmt\"\nColumnLimit = 100\n", colors);
    output << '\n';
    bool firstSection = true;
    for (const auto& section : HelpSections) {
        if (!firstSection) {
            output << '\n';
        }
        firstSection = false;
        PrintHeading(output, section.Title, colors);
        output << section.Description;
        for (const auto& example : section.Examples) {
            output << '\n' << colors.BoldYellow << example.Title << colors.Reset << '\n';
            PrintCode(output, example.Code, colors);
        }
    }
}

} // namespace hit_fmt
