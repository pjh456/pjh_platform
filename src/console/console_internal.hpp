#ifndef INCLUDE_PJH_PLATFORM_CONSOLE_INTERNAL_HPP
#define INCLUDE_PJH_PLATFORM_CONSOLE_INTERNAL_HPP

#include <pjh_platform/error.hpp>
#include <pjh_platform/platform.hpp>

#if PJH_PLATFORM_WINDOWS
#include <windows.h>
#endif

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
}  // namespace pjh::platform::detail

#endif  // INCLUDE_PJH_PLATFORM_CONSOLE_INTERNAL_HPP
