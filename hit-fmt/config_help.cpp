#include "config_help.hpp"

#include "config_help_data.hpp"

#include <array>
#include <ostream>
#include <string_view>

namespace hit_fmt {

namespace {

enum class HelpBlockKind
{
    Text,
    Heading,
    Example,
    Code
};

struct HelpBlock
{
    HelpBlockKind Kind;
    std::string_view Text;
};

template <typename Emit>
constexpr void VisitHelpBlocks(std::string_view text, Emit emit)
{
    bool code = false;
    size_t blockStart = 0;
    const auto size = text.size();
    const auto* data = text.data();
    const auto flush = [&] (size_t end) {
        if (blockStart < end) {
            emit(
                HelpBlock{code ? HelpBlockKind::Code : HelpBlockKind::Text, text.substr(blockStart, end - blockStart)});
        }
    };
    // Keep scans within compiler constexpr step limits without library calls per byte.
    for (size_t start = 0; start < size;) {
        auto end = start;
        while (end < size && data[end] != '\n') {
            ++end;
        }
        const auto next = end < size ? end + 1 : end;
        const auto first = start < end ? data[start] : '\0';
        if (first == '`' && text.substr(start, end - start).starts_with("```")) {
            const auto line = text.substr(start, end - start);
            flush(start);
            if ((!code && line != "```config") || (code && line != "```")) {
                throw "invalid config help code fence";
            }
            code = !code;
            blockStart = next;
        } else if (!code && first == '#') {
            const auto line = text.substr(start, end - start);
            const bool heading = line.starts_with("# ");
            if (heading || line.starts_with("## ")) {
                flush(start);
                const auto title = line.substr(heading ? 2 : 3);
                if (title.empty()) {
                    throw "empty config help heading";
                }
                emit(HelpBlock{heading ? HelpBlockKind::Heading : HelpBlockKind::Example, title});
                blockStart = next;
            }
        }
        start = next;
    }
    if (code) {
        throw "unclosed config help code fence";
    }
    flush(text.size());
}

constexpr auto HelpBlockCount = [] {
    size_t result = 0;
    VisitHelpBlocks(ConfigHelpText, [&] (HelpBlock) { ++result; });
    return result;
}();

consteval auto ParseConfigHelp()
{
    std::array<HelpBlock, HelpBlockCount> blocks{};
    size_t index = 0;
    VisitHelpBlocks(ConfigHelpText, [&] (HelpBlock block) { blocks[index++] = block; });
    return blocks;
}

constexpr auto HelpBlocks = ParseConfigHelp();

void PrintCode(std::ostream& output, std::string_view code, const Terminal& terminal)
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
        output << (line.starts_with("//") || line.starts_with("#") ? terminal.Dim : terminal.Cyan) << "    " << line
            << terminal.Reset << '\n';
    }
}

} // namespace

void PrintConfigHelp(std::ostream& output, const Terminal& terminal)
{
    for (const auto& block : HelpBlocks) {
        switch (block.Kind) {
            case HelpBlockKind::Text:
                output << block.Text;
                break;
            case HelpBlockKind::Heading:
                output << terminal.BoldCyan << block.Text << terminal.Reset << '\n';
                break;
            case HelpBlockKind::Example:
                output << terminal.BoldYellow << block.Text << terminal.Reset << '\n';
                break;
            case HelpBlockKind::Code:
                PrintCode(output, block.Text, terminal);
                break;
        }
    }
}

} // namespace hit_fmt
