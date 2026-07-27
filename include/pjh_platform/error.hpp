#ifndef INCLUDE_PJH_PLATFORM_ERROR_HPP
#define INCLUDE_PJH_PLATFORM_ERROR_HPP

#include <pjh_result/result.hpp>

namespace pjh::platform
{

    enum class ErrorCode
    {
        Success = 0,
        NotFound,
        PermissionDenied,
        AlreadyExists,
        InvalidArgument,
        NotSupported,
        IoError,
        Unknown,
    };

}  // namespace pjh::platform

#endif  // INCLUDE_PJH_PLATFORM_ERROR_HPP
