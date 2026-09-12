#ifndef INCLUDE_PJH_PLATFORM_CONSOLE_INTERNAL_HPP
#define INCLUDE_PJH_PLATFORM_CONSOLE_INTERNAL_HPP

#include <pjh_platform/error.hpp>
#include <pjh_platform/platform.hpp>

#if PJH_PLATFORM_WINDOWS
#include <windows.h>
#else
#include <termios.h>
#include <unistd.h>
#endif

namespace pjh::platform
{
    /// @brief Internal state of a `ConsoleMode` guard: the mode captured on
    ///        entry plus whether raw mode is currently applied. Defined here so
    ///        the platform scalar types stay out of the public header.
    struct ConsoleModeImpl
    {
#if PJH_PLATFORM_WINDOWS
        /// @brief Console input handle for the guarded descriptor.
        HANDLE input = INVALID_HANDLE_VALUE;
        /// @brief Input mode at capture; the exact value restored verbatim.
        DWORD saved_mode = 0;
#else
        /// @brief Terminal descriptor whose line discipline is guarded.
        int fd = -1;
        /// @brief Terminal attributes at capture; restored verbatim.
        termios saved{};
#endif
        /// @brief Whether raw mode is currently applied (guards idempotence and
        ///        prevents restoring a mode that was never changed).
        bool active = false;
    };
}  // namespace pjh::platform

namespace pjh::platform::detail
{
#if PJH_PLATFORM_WINDOWS
    /// @brief Console-boundary overlay on the shared Windows error table.
    ///
    /// @details A console API called on a non-console standard handle reports
    ///          `ERROR_INVALID_HANDLE`; at the console boundary that means
    ///          "not a terminal", so it maps to `NotATerminal` instead of the
    ///          shared table's `InvalidArgument`. Every other code is delegated
    ///          to `map_windows_error`. This mirrors the watch-specific overlay
    ///          `detail::is_path_gone` and does not modify the shared table.
    [[nodiscard]] auto classify_console_error(unsigned long err) -> ErrorCode;
#endif

    /// @brief Captures @p fd's terminal mode into @p impl and applies raw mode.
    /// @return Ok() once raw is active; Failure(NotATerminal) when @p fd is not
    ///         a terminal; otherwise the mapped platform error.
    [[nodiscard]] auto enter_raw(ConsoleModeImpl &impl, int fd)
        -> pjh::result::Result<void, ErrorCode>;

    /// @brief Restores the captured mode and clears `impl.active` on success.
    [[nodiscard]] auto restore_saved(ConsoleModeImpl &impl) -> pjh::result::Result<void, ErrorCode>;

    /// @brief Re-computes raw mode from the captured state and applies it,
    ///        setting `impl.active` on success.
    [[nodiscard]] auto reapply_raw(ConsoleModeImpl &impl) -> pjh::result::Result<void, ErrorCode>;

    /// @brief Best-effort restore used by the destructor/move assignment:
    ///        ignores platform failure, never throws, no-op when not active.
    auto restore_best_effort(ConsoleModeImpl *impl) noexcept -> void;
}  // namespace pjh::platform::detail

#endif  // INCLUDE_PJH_PLATFORM_CONSOLE_INTERNAL_HPP
