#include <pjh_platform/console.hpp>
#include <pjh_platform/platform.hpp>

#include "../error_mapping.hpp"
#include "console_internal.hpp"

#if PJH_PLATFORM_WINDOWS
#include <io.h>
#include <windows.h>
#else
#include <sys/ioctl.h>
#include <unistd.h>
#endif

#include <cerrno>
#include <cstdlib>
#include <string_view>

namespace pjh::platform
{
    auto Console::is_tty(int fd) -> bool
    {
#if PJH_PLATFORM_WINDOWS
        // Guard before _isatty: the UCRT validates `fh` against the lowio
        // handle table (`_nhandle`) and, for anything outside it, asserts in
        // Debug builds and invokes the invalid-parameter handler in Release
        // (even _get_osfhandle validates the same range), so a bad descriptor
        // must never reach the call. `_getmaxstdio()` is not a usable bound:
        // it returns the stdio stream limit (`_nstream`, default 512), which
        // is independent of the lowio handle table (`_nhandle` starts at 64
        // and grows in steps of 64), and `_nhandle` has no exported accessor.
        // The documented domain of this query is the standard descriptors
        // 0/1/2 (see console.hpp); those are always inside the table after
        // CRT initialization, so _isatty is still called for every descriptor
        // the contract admits. Anything else is not a standard-stream
        // descriptor and returns false without touching the CRT.
        if (fd < 0 || fd > 2)
            return false;
        return ::_isatty(fd) != 0;
#else
        return ::isatty(fd) == 1;
#endif
    }

    auto Console::size() -> pjh::result::Result<ConsoleSize, ErrorCode>
    {
#if PJH_PLATFORM_WINDOWS
        HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
        CONSOLE_SCREEN_BUFFER_INFO info{};
        if (!GetConsoleScreenBufferInfo(out, &info))
            return pjh::result::Failure<ErrorCode>{detail::classify_console_error(GetLastError())};
        // The visible window rectangle, not the scrollback buffer (dwSize).
        const int columns = info.srWindow.Right - info.srWindow.Left + 1;
        const int rows = info.srWindow.Bottom - info.srWindow.Top + 1;
        if (columns <= 0 || rows <= 0)
            return pjh::result::Failure<ErrorCode>{ErrorCode::NotATerminal};
        return pjh::result::Result<ConsoleSize, ErrorCode>::Ok(
            ConsoleSize{static_cast<std::size_t>(columns), static_cast<std::size_t>(rows)});
#else
        winsize ws{};
        if (::ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1)
            return pjh::result::Failure<ErrorCode>{detail::map_errno_to_error(errno)};
        if (ws.ws_col == 0 || ws.ws_row == 0)
            return pjh::result::Failure<ErrorCode>{ErrorCode::NotATerminal};
        return pjh::result::Result<ConsoleSize, ErrorCode>::Ok(
            ConsoleSize{static_cast<std::size_t>(ws.ws_col), static_cast<std::size_t>(ws.ws_row)});
#endif
    }

    auto Console::supports_ansi() -> bool
    {
#if PJH_PLATFORM_WINDOWS
        DWORD mode = 0;
        if (!GetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), &mode))
            return false;
#ifdef ENABLE_VIRTUAL_TERMINAL_PROCESSING
        return (mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;
#else
        return false;
#endif
#else
        if (!Console::is_tty(STDOUT_FILENO))
            return false;
        const char *term = std::getenv("TERM");
        if (term == nullptr || term[0] == '\0')
            return false;
        return std::string_view(term) != "dumb";
#endif
    }
}  // namespace pjh::platform
