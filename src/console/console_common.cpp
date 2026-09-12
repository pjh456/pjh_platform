#include <pjh_platform/platform.hpp>

#if PJH_PLATFORM_WINDOWS
#include <windows.h>
#endif

#include "../error_mapping.hpp"
#include "console_internal.hpp"

namespace pjh::platform::detail
{
#if PJH_PLATFORM_WINDOWS
    auto classify_console_error(unsigned long err) -> ErrorCode
    {
        if (err == ERROR_INVALID_HANDLE)
            return ErrorCode::NotATerminal;
        return map_windows_error(err);
    }
#endif
}  // namespace pjh::platform::detail
