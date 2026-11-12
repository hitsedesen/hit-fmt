#include "terminal.hpp"

#include <util/parse.hpp>
#include <util/types.hpp>

#include <algorithm>
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

namespace cli {

namespace {

bool UseColors(FILE* stream)
{
    if (std::getenv("NO_COLOR") != nullptr) {
        return false;
    }

    if (auto* term = std::getenv("TERM"); term && std::string_view(term) == "dumb") {
        return false;
    }

    return IsTerminal(stream);
}

us QueryTerminalWidth(FILE* stream)
{
#ifdef _WIN32
    if (auto descriptor = _fileno(stream); descriptor >= 0) {
        auto handle = reinterpret_cast<HANDLE>(_get_osfhandle(descriptor));
        CONSOLE_SCREEN_BUFFER_INFO info{};
        if (GetConsoleScreenBufferInfo(handle, &info)) {
            auto width = info.srWindow.Right - info.srWindow.Left + 1;
            if (width > 0) {
                return std::max<us>(24, width);
            }
        }
    }
#else
    auto descriptor = fileno(stream);
    if (winsize size{}; isatty(descriptor) && ioctl(descriptor, TIOCGWINSZ, &size) == 0 && size.ws_col > 0) {
        return std::max<us>(24, size.ws_col);
    }
#endif
    if (const char* value = std::getenv("COLUMNS")) {
        auto [width, success] = TryParseInteger<us>(value);

        if (success && width >= 24 && width <= 1000) {
            return width;
        }
    }

    return 80;
}

} // namespace

bool IsTerminal(FILE* stream)
{
#ifdef _WIN32
    return _isatty(_fileno(stream)) != 0;
#else
    return isatty(fileno(stream)) != 0;
#endif
}

Terminal GetTerminal(FILE* stream)
{
    return Terminal(UseColors(stream), QueryTerminalWidth(stream));
}

void PrintError(std::ostream& output, const Error& error, const Terminal& terminal)
{
    output << terminal.BoldRed << error << terminal.Reset << '\n';
}

} // namespace cli
