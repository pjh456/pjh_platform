#ifndef INCLUDE_PJH_PLATFORM_CONSOLE_HPP
#define INCLUDE_PJH_PLATFORM_CONSOLE_HPP

#include <cstddef>
#include <pjh_platform/error.hpp>
#include <string>
#include <vector>

namespace pjh::platform
{

    /// @brief Terminal size in character cells.
    ///
    /// @platform Windows, Linux, macOS.
    struct ConsoleSize
    {
        /// @brief Number of columns (character cells).
        std::size_t columns;

        /// @brief Number of rows (character cells).
        std::size_t rows;
    };

    /// @brief Cross-platform console/terminal capability queries.
    ///
    /// @details Static-only; every member is a query or a best-effort enable.
    ///          `enable_utf8` is the only mutating call (a documented
    ///          @sideeffect); the rest never change process or terminal state.
    ///          Raw-mode entry/exit is a separate stateful guard (`ConsoleMode`,
    ///          task 61), not part of this class.
    ///
    /// @platform Windows, Linux, macOS. Windows branches use the Win32 console
    ///          APIs; Linux and macOS share the POSIX `isatty` / `ioctl`
    ///          (`TIOCGWINSZ`) implementation.
    class Console
    {
        Console() = delete;

    public:
        /**
         * @brief Enables UTF-8 on the process console (best effort).
         *
         * @details Windows: if standard output is attached to a console, sets
         *          `SetConsoleOutputCP(CP_UTF8)` and `SetConsoleCP(CP_UTF8)`.
         *          The change is process-global and persists for the process;
         *          there is no automatic restore. POSIX: no-op.
         *
         *          When standard output is not a console (redirected to a file
         *          or pipe) the call is a successful no-op and the code page is
         *          left untouched, so redirected bytes are never re-encoded.
         *
         * @return `Ok()` on success, including the POSIX no-op and the
         *         no-console no-op; `Failure(PermissionDenied)` / `Failure(IoError)`
         *         or another mapped code when a console is attached but the code
         *         page could not be set.
         *
         * @exception Never throws.
         *
         * @sideeffect Process-global console output/input code page
         *             (Windows only); persists for the process. This does not
         *             affect the narrow `std::cout` byte stream semantics.
         *
         * @platform Windows mutates the console code page; Linux/macOS no-op.
         */
        [[nodiscard]] static auto enable_utf8() -> pjh::result::Result<void, ErrorCode>;

        /**
         * @brief Whether @p fd refers to an interactive terminal.
         *
         * @details POSIX: `isatty(@p fd)`. Windows: `_isatty(@p fd)`, where
         *          @p fd is a CRT file descriptor; only `0` (stdin), `1`
         *          (stdout), and `2` (stderr) are meaningful. Passing an invalid
         *          descriptor returns `false`; the query never fails and never
         *          changes state.
         *
         * @param fd Standard-stream descriptor: `0` stdin, `1` stdout, `2`
         *           stderr.
         *
         * @return `true` when @p fd is an interactive terminal, `false`
         *         otherwise.
         *
         * @exception Never throws.
         *
         * @sideeffect None.
         *
         * @platform Windows, Linux, macOS.
         */
        [[nodiscard]] static auto is_tty(int fd) -> bool;

        /**
         * @brief Returns the console size in character cells.
         *
         * @details POSIX: `ioctl(STDOUT_FILENO, TIOCGWINSZ)`; Windows:
         *          `GetConsoleScreenBufferInfo` on the standard output handle,
         *          using the visible window rectangle (`srWindow`), not the
         *          scrollback buffer size. No `COLUMNS`/`LINES` environment
         *          fallback is consulted.
         *
         * @return `Ok(ConsoleSize)` with positive `columns`/`rows` when standard
         *         output is a terminal; `Failure(NotATerminal)` when it is not
         *         (redirected or no console), when the platform query reports
         *         `ENOTTY` / `ERROR_INVALID_HANDLE`, or when a terminal reports
         *         zero dimensions; another mapped code for other failures.
         *
         * @exception Never throws.
         *
         * @sideeffect None.
         *
         * @platform Windows, Linux, macOS.
         */
        [[nodiscard]] static auto size() -> pjh::result::Result<ConsoleSize, ErrorCode>;

        /**
         * @brief Probes whether ANSI/VT escape sequences are honored on stdout.
         *
         * @details Heuristic, read-only. POSIX: stdout is a terminal and `TERM`
         *          is set, non-empty, and not `"dumb"`. Windows: stdout is a
         *          console whose mode has `ENABLE_VIRTUAL_TERMINAL_PROCESSING`
         *          (available since Windows 10); on SDKs without that constant
         *          the result is `false`. `NO_COLOR` is deliberately **not**
         *          consulted — this is a capability probe, not a color policy;
         *          callers combine it with their own `NO_COLOR` handling. The
         *          probe never enables VT.
         *
         * @return `true` when ANSI/VT sequences are expected to work, `false`
         *         otherwise.
         *
         * @exception Never throws.
         *
         * @sideeffect None.
         *
         * @platform Windows, Linux, macOS.
         */
        [[nodiscard]] static auto supports_ansi() -> bool;

        /**
         * @brief Converts the process arguments to UTF-8 strings.
         *
         * @details Windows: parses `GetCommandLineW()` with
         *          `CommandLineToArgvW` and converts each wide argument with
         *          `Encoding::to_utf8`, so non-ASCII arguments survive the ANSI
         *          code page. `@p argc` / `@p argv` are ignored on Windows except
         *          as a best-effort fallback if the wide command line cannot be
         *          obtained. POSIX: copies `@p argc` / `@p argv` verbatim as
         *          UTF-8.
         *
         * @param argc Argument count, following the `main` convention.
         * @param argv Argument vector, following the `main` convention; may be
         *             null when @p argc is zero or less.
         *
         * @return One UTF-8 string per argument, program name (`argv[0]`) first;
         *         an empty vector when @p argc is zero or less or @p argv is
         *         null (the program name itself is not fabricated).
         *
         * @exception Never throws (may allocate).
         *
         * @sideeffect None. On Windows the returned strings own their storage;
         *             the temporary `CommandLineToArgvW` buffer is released
         *             before returning.
         *
         * @platform Windows uses the wide command line; Linux/macOS copy
         *           @p argv.
         */
        [[nodiscard]] static auto utf8_arguments(int argc, char **argv) -> std::vector<std::string>;
    };

}  // namespace pjh::platform

#endif  // INCLUDE_PJH_PLATFORM_CONSOLE_HPP
