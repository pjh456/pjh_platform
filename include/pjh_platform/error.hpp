#ifndef INCLUDE_PJH_PLATFORM_ERROR_HPP
#define INCLUDE_PJH_PLATFORM_ERROR_HPP

#include <pjh_result/result.hpp>

namespace pjh::platform
{

    /// @brief Canonical error codes returned by pjh_platform APIs.
    ///
    /// @details Functions that can fail return
    ///          `pjh::result::Result<T, ErrorCode>` and never throw platform
    ///          errors. The codes are mapped from the underlying OS error by
    ///          the library-internal mapping (`errno` / `GetLastError` /
    ///          `std::error_code`).
    ///
    /// @platform Windows, Linux, macOS.
    enum class ErrorCode
    {
        /// @brief Operation completed successfully.
        Success = 0,

        /// @brief A file, directory, or environment variable was not found.
        NotFound,

        /// @brief The operation was denied by access permissions.
        PermissionDenied,

        /// @brief The target already exists (for example a file being created).
        AlreadyExists,

        /// @brief An argument was invalid (for example paths with no common
        ///        root in `Fs::relative`).
        InvalidArgument,

        /// @brief The operation is not supported on this platform.
        NotSupported,

        /// @brief A generic input/output failure.
        IoError,

        /// @brief An unrecognized error that could not be mapped.
        Unknown,

        /// @brief A system resource limit was reached (for example the
        ///        `inotify` watch count limit).
        LimitReached,

        /// @brief A blocking operation was interrupted by a signal.
        Interrupted,

        /// @brief The target is already being watched by a `FileWatcher`.
        AlreadyWatched,
    };

}  // namespace pjh::platform

#endif  // INCLUDE_PJH_PLATFORM_ERROR_HPP
