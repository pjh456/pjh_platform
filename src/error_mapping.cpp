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
        // A symlink loop is unresolvable, not discoverable: map it to
        // NotFound (task 31 ruling, recorded in .w1mer CHANGES).
        case ELOOP:
            return ErrorCode::NotFound;
        case EACCES:
        case EPERM:
            return ErrorCode::PermissionDenied;
        case EEXIST:
            return ErrorCode::AlreadyExists;
        case EINVAL:
            return ErrorCode::InvalidArgument;
#ifdef EISDIR
        // A directory where a file was expected: a parameter-shape error, not a
        // generic I/O failure (the POSIX counterpart of the Fs directory-target
        // checks). The Windows side has no EISDIR and uses an attribute check.
        case EISDIR:
            return ErrorCode::InvalidArgument;
#endif
#ifdef ENOTDIR
        // A path component is not a directory: the path is unresolvable, the
        // POSIX counterpart of the Windows ERROR_PATH_NOT_FOUND family, so it
        // shares the NotFound family with ENOENT/ELOOP above.
        case ENOTDIR:
            return ErrorCode::NotFound;
#endif
#ifdef EROFS
        case EROFS:
            return ErrorCode::PermissionDenied;
#endif
#ifdef ENAMETOOLONG
        case ENAMETOOLONG:
            return ErrorCode::InvalidArgument;
#endif
#ifdef EMFILE
        case EMFILE:
            return ErrorCode::LimitReached;
#endif
#ifdef ENFILE
        case ENFILE:
            return ErrorCode::LimitReached;
#endif
#ifdef ENOTSUP
        case ENOTSUP:
            return ErrorCode::NotSupported;
#endif
#ifdef ENOTTY
        // A terminal-only request (e.g. ioctl(TIOCGWINSZ)) on a stream that is
        // not a terminal: the dedicated NotATerminal code is more precise than
        // the generic fallback.
        case ENOTTY:
            return ErrorCode::NotATerminal;
#endif
        case EIO:
            return ErrorCode::IoError;
        case ENOSPC:
            return ErrorCode::LimitReached;
        case EINTR:
            return ErrorCode::Interrupted;
        // EBUSY (a mount point or an in-use resource) and ENODEV (device
        // semantics) have no semantically matching code in the closed
        // ErrorCode set; they deliberately fall through to the documented
        // Unknown fallback rather than borrowing an unrelated code. Add a
        // dedicated enumerator only if a future feature needs the
        // distinction.
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
