#include "terminal.hpp"

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <ostream>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <io.h>
#include <windows.h>
#else
#include <unistd.h>

#include <sys/ioctl.h>
#endif

namespace hit_fmt {

namespace {

bool UseColors(FILE* stream)
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

size_t QueryTerminalWidth(FILE* stream)
{
#ifdef _WIN32
    if (const auto descriptor = _fileno(stream); descriptor >= 0) {
        const auto handle = reinterpret_cast<HANDLE>(_get_osfhandle(descriptor));
        CONSOLE_SCREEN_BUFFER_INFO info{};
        if (GetConsoleScreenBufferInfo(handle, &info)) {
            const auto width = info.srWindow.Right - info.srWindow.Left + 1;
            if (width > 0) {
                return std::max<size_t>(24, width);
            }
        }
    }
#else
    const auto descriptor = fileno(stream);
    if (winsize size{}; isatty(descriptor) && ioctl(descriptor, TIOCGWINSZ, &size) == 0 && size.ws_col > 0) {
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

} // namespace

Terminal GetTerminal(FILE* stream)
{
    return Terminal(UseColors(stream), QueryTerminalWidth(stream));
}

void PrintError(std::ostream& output, std::string_view message, const Terminal& terminal)
{
    output << terminal.BoldRed << message << terminal.Reset << '\n';
}

} // namespace hit_fmt
