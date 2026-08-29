#include "error_mapping.hpp"

#if PJH_PLATFORM_WINDOWS
#include <windows.h>
#endif

#include <cerrno>

namespace pjh::platform::detail
{
    auto map_errno_to_error(int err) -> ErrorCode
    {
        switch (err)
        {
        case ENOENT:
            return ErrorCode::NotFound;
        case EACCES:
        case EPERM:
            return ErrorCode::PermissionDenied;
        case EEXIST:
            return ErrorCode::AlreadyExists;
        case EINVAL:
            return ErrorCode::InvalidArgument;
#ifdef ENOTSUP
        case ENOTSUP:
            return ErrorCode::NotSupported;
#endif
        case EIO:
            return ErrorCode::IoError;
        case ENOSPC:
            return ErrorCode::LimitReached;
        case EINTR:
            return ErrorCode::Interrupted;
        default:
            return ErrorCode::Unknown;
        }
    }

#if PJH_PLATFORM_WINDOWS
    auto map_windows_error(unsigned long err) -> ErrorCode
    {
        switch (err)
        {
        case ERROR_FILE_NOT_FOUND:
        case ERROR_PATH_NOT_FOUND:
        case ERROR_DIR_NOT_FOUND:
        // The watched directory was removed out from under the in-flight read.
        case ERROR_BROKEN_PIPE:
            return ErrorCode::NotFound;
        case ERROR_ACCESS_DENIED:
            return ErrorCode::PermissionDenied;
        case ERROR_ALREADY_EXISTS:
        case ERROR_FILE_EXISTS:
            return ErrorCode::AlreadyExists;
        case ERROR_INVALID_PARAMETER:
        case ERROR_INVALID_HANDLE:
            return ErrorCode::InvalidArgument;
        case ERROR_NOT_SUPPORTED:
            return ErrorCode::NotSupported;
        default:
            return ErrorCode::Unknown;
        }
    }
#endif

    auto map_error_code(const std::error_code &ec) -> ErrorCode
    {
        if (!ec)
            return ErrorCode::Success;
        if (ec.category() == std::generic_category())
            return map_errno_to_error(ec.value());
#if PJH_PLATFORM_WINDOWS
        if (ec.category() == std::system_category())
            return map_windows_error(static_cast<unsigned long>(ec.value()));
#endif
        return ErrorCode::Unknown;
    }
}  // namespace pjh::platform::detail
