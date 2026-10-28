#include "terminal.hpp"

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <ostream>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>

#include <sys/ioctl.h>
#endif

namespace hit_fmt {

namespace {

bool UseTerminalColors(FILE* stream)
{
    if (std::getenv("NO_COLOR") != nullptr) {
        return false;
    }
    if (auto* term = std::getenv("TERM"); term && std::string_view(term) == "dumb") {
        return false;
    }
#ifdef _WIN32
    return _isatty(_fileno(stream)) != 0;
#else
    return isatty(fileno(stream)) != 0;
#endif
}

} // namespace

TerminalColors GetTerminalColors(FILE* stream)
{
    return TerminalColors(UseTerminalColors(stream));
}

void PrintError(std::ostream& output, std::string_view message, const TerminalColors& colors)
{
    output << colors.BoldRed << message << colors.Reset << '\n';
}

size_t TerminalWidth()
{
    #ifndef _WIN32
    if (winsize size{}; isatty(fileno(stdout)) && ioctl(fileno(stdout), TIOCGWINSZ, &size) == 0 && size.ws_col > 0) {
        return std::max<size_t>(24, size.ws_col);
    }
#endif
    if (const char* value = std::getenv("COLUMNS")) {
        size_t width = 0;
        auto end = value + std::char_traits<char>::length(value);
        auto [next, error] = std::from_chars(value, end, width);
        if (error == std::errc{} && next == end && width >= 24 && width <= 1000) {
            return width;
        }
    }
    return 80;
}

} // namespace hit_fmt
