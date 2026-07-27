#ifndef INCLUDE_PJH_PLATFORM_ERROR_HPP
#define INCLUDE_PJH_PLATFORM_ERROR_HPP

#include <pjh_result/result.hpp>

namespace pjh::platform
{

    enum class ErrorCode
    {
        success = 0,
        not_found,
        permission_denied,
        already_exists,
        invalid_argument,
        not_supported,
        io_error,
        unknown,
    };

}  // namespace pjh::platform

#endif  // INCLUDE_PJH_PLATFORM_ERROR_HPP
