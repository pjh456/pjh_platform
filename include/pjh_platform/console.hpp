#ifndef INCLUDE_PJH_PLATFORM_CONSOLE_HPP
#define INCLUDE_PJH_PLATFORM_CONSOLE_HPP

#include <cstddef>
#include <memory>
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

    struct ConsoleModeImpl;  // private, defined in src/console/console_internal.hpp

    /// @brief RAII guard that switches a terminal into raw mode and restores it.
    ///
    /// @details Move-only. `make_raw` captures the terminal's current mode and
    ///          switches it to raw; the destructor restores the captured mode on
    ///          a best-effort basis and never throws. `suspend`/`resume` toggle
    ///          between cooked and raw for a human-in-the-loop window and are
    ///          idempotent (calling either twice, or in either order, is safe).
    ///          POSIX clears `ICANON | ECHO | ISIG` and sets `VMIN = 1`,
    ///          `VTIME = 0`; Windows clears `ENABLE_ECHO_INPUT |
    ///          ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT` on the input
    ///          handle. No other mode bits are changed, so the captured mode is
    ///          restored verbatim.
    ///
    /// @warning Not thread-safe; the terminal/console mode is process-global and
    ///          shared by every descriptor of the same terminal. Guards are not
    ///          reference-counted — each one saves the mode it observes, so
    ///          nested guards must be destroyed in reverse construction order.
    /// @warning An abnormal process exit (SIGKILL/crash) or `std::_Exit` bypasses
    ///          the destructor, leaving the terminal in raw mode; the library
    ///          installs no signal handler. Use `suspend()` around code that may
    ///          terminate the process and call it again on the normal path.
    ///
    /// @platform Windows, Linux, macOS.
    class ConsoleMode
    {
    public:
        /// @brief Deleted: the guard owns a live terminal state.
        ConsoleMode(const ConsoleMode &) = delete;
        /// @brief Deleted: the guard owns a live terminal state.
        auto operator=(const ConsoleMode &) -> ConsoleMode & = delete;

        /// @brief Move constructor; transfers ownership of the saved mode.
        /// @exception Never throws.
        ConsoleMode(ConsoleMode &&other) noexcept;

        /// @brief Move assignment; releases the current saved mode first, then
        ///        takes the other's.
        /// @details The moved-from guard no longer restores anything; its
        ///          `suspend`/`resume` answer `Failure(InvalidArgument)`.
        /// @exception Never throws.
        auto operator=(ConsoleMode &&other) noexcept -> ConsoleMode &;

        /// @brief Restores the captured mode (best effort; never throws).
        /// @details A no-op when the guard was suspended or moved from.
        /// @exception Never throws.
        ~ConsoleMode();

        /**
         * @brief Captures the mode of @p fd and switches it to raw.
         *
         * @details On POSIX @p fd is a file descriptor (`STDIN_FILENO` by
         *          default); on Windows @p fd is a CRT descriptor translated
         *          with `_get_osfhandle` (descriptor `0` by default). @p fd must
         *          refer to an interactive terminal; a redirected or closed
         *          descriptor yields `Failure(NotATerminal)` and changes nothing.
         *
         * @param fd Terminal descriptor; `0` selects standard input.
         *
         * @return `Ok(guard)` after raw mode was applied; `Failure(NotATerminal)`
         *         when @p fd is not a terminal; `Failure(...)` mapped from the
         *         platform error when `tcgetattr`/`tcsetattr` (POSIX) or
         *         `GetConsoleMode`/`SetConsoleMode` (Windows) fails.
         *
         * @exception Never throws (may allocate).
         *
         * @sideeffect Process-global terminal mode of @p fd's terminal.
         *
         * @platform Windows, Linux, macOS.
         */
        [[nodiscard]] static auto make_raw(int fd = 0)
            -> pjh::result::Result<ConsoleMode, ErrorCode>;

        /**
         * @brief Restores cooked mode; idempotent.
         *
         * @return `Ok()` when the guard is already suspended or was successfully
         *         restored; `Failure(InvalidArgument)` on a moved-from guard;
         *         `Failure(...)` mapped from the platform error when the restore
         *         call fails. On failure the raw state is left unchanged.
         *
         * @exception Never throws.
         *
         * @platform Windows, Linux, macOS.
         */
        [[nodiscard]] auto suspend() -> pjh::result::Result<void, ErrorCode>;

        /**
         * @brief Re-applies raw mode; idempotent.
         *
         * @return `Ok()` when raw mode is already active or was successfully
         *         re-applied; `Failure(InvalidArgument)` on a moved-from guard;
         *         `Failure(...)` mapped from the platform error otherwise.
         *
         * @exception Never throws.
         *
         * @platform Windows, Linux, macOS.
         */
        [[nodiscard]] auto resume() -> pjh::result::Result<void, ErrorCode>;

    private:
        ConsoleMode() = default;
        std::unique_ptr<ConsoleModeImpl> impl_;
    };

}  // namespace pjh::platform

#endif  // INCLUDE_PJH_PLATFORM_CONSOLE_HPP
