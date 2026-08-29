#ifndef INCLUDE_PJH_PLATFORM_ERROR_MAPPING_HPP
#define INCLUDE_PJH_PLATFORM_ERROR_MAPPING_HPP

#include <pjh_platform/error.hpp>
#include <pjh_platform/platform.hpp>
#include <system_error>

namespace pjh::platform::detail
{
    /// @brief Maps a platform `std::error_code` (generic/errno category on all
    ///        platforms; system/Win32 category on Windows) to `ErrorCode`.
    /// @return `ErrorCode::Success` when @p ec is not set; unrecognized
    ///         categories map to `ErrorCode::Unknown`.
    [[nodiscard]] auto map_error_code(const std::error_code &ec) -> ErrorCode;

    /// @brief Maps a raw `errno` value to the library's `ErrorCode`.
    [[nodiscard]] auto map_errno_to_error(int err) -> ErrorCode;

#if PJH_PLATFORM_WINDOWS
    /// @brief Maps a Win32 error code (e.g. from `GetLastError()`) to the
    ///        library's `ErrorCode`.
    [[nodiscard]] auto map_windows_error(unsigned long err) -> ErrorCode;
#endif
}  // namespace pjh::platform::detail

#endif  // INCLUDE_PJH_PLATFORM_ERROR_MAPPING_HPP
