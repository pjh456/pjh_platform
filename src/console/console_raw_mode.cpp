#include <pjh_platform/console.hpp>
#include <pjh_platform/platform.hpp>

#include "../error_mapping.hpp"
#include "console_internal.hpp"

#if PJH_PLATFORM_WINDOWS
#include <io.h>
#include <windows.h>
#else
#include <cerrno>
#endif

#include <memory>
#include <utility>

namespace pjh::platform::detail
{
    auto enter_raw(ConsoleModeImpl &impl, int fd) -> pjh::result::Result<void, ErrorCode>
    {
        // A state-changing call, so prove the descriptor is a terminal before
        // touching the line discipline; the check also covers bad descriptors.
        if (!Console::is_tty(fd))
            return pjh::result::Failure<ErrorCode>{ErrorCode::NotATerminal};

#if PJH_PLATFORM_WINDOWS
        HANDLE input = reinterpret_cast<HANDLE>(::_get_osfhandle(fd));
        if (input == INVALID_HANDLE_VALUE)
            return pjh::result::Failure<ErrorCode>{ErrorCode::NotATerminal};

        DWORD saved = 0;
        if (!::GetConsoleMode(input, &saved))
            return pjh::result::Failure<ErrorCode>{classify_console_error(::GetLastError())};

        const DWORD raw = saved & ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT);
        if (!::SetConsoleMode(input, raw))
            return pjh::result::Failure<ErrorCode>{classify_console_error(::GetLastError())};

        impl.input = input;
        impl.saved_mode = saved;
#else
        termios saved{};
        if (::tcgetattr(fd, &saved) != 0)
            return pjh::result::Failure<ErrorCode>{map_errno_to_error(errno)};

        termios raw = saved;
        raw.c_lflag &= static_cast<tcflag_t>(~(ICANON | ECHO | ISIG));
        raw.c_cc[VMIN] = 1;
        raw.c_cc[VTIME] = 0;
        if (::tcsetattr(fd, TCSANOW, &raw) != 0)
            return pjh::result::Failure<ErrorCode>{map_errno_to_error(errno)};

        impl.fd = fd;
        impl.saved = saved;
#endif

        impl.active = true;
        return pjh::result::Result<void, ErrorCode>::Ok();
    }

    auto restore_saved(ConsoleModeImpl &impl) -> pjh::result::Result<void, ErrorCode>
    {
#if PJH_PLATFORM_WINDOWS
        if (!::SetConsoleMode(impl.input, impl.saved_mode))
            return pjh::result::Failure<ErrorCode>{classify_console_error(::GetLastError())};
#else
        if (::tcsetattr(impl.fd, TCSANOW, &impl.saved) != 0)
            return pjh::result::Failure<ErrorCode>{map_errno_to_error(errno)};
#endif

        impl.active = false;
        return pjh::result::Result<void, ErrorCode>::Ok();
    }

    auto reapply_raw(ConsoleModeImpl &impl) -> pjh::result::Result<void, ErrorCode>
    {
#if PJH_PLATFORM_WINDOWS
        const DWORD raw =
            impl.saved_mode & ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT);
        if (!::SetConsoleMode(impl.input, raw))
            return pjh::result::Failure<ErrorCode>{classify_console_error(::GetLastError())};
#else
        termios raw = impl.saved;
        raw.c_lflag &= static_cast<tcflag_t>(~(ICANON | ECHO | ISIG));
        raw.c_cc[VMIN] = 1;
        raw.c_cc[VTIME] = 0;
        if (::tcsetattr(impl.fd, TCSANOW, &raw) != 0)
            return pjh::result::Failure<ErrorCode>{map_errno_to_error(errno)};
#endif

        impl.active = true;
        return pjh::result::Result<void, ErrorCode>::Ok();
    }

    auto restore_best_effort(ConsoleModeImpl *impl) noexcept -> void
    {
        if (impl == nullptr || !impl->active)
            return;
#if PJH_PLATFORM_WINDOWS
        (void)::SetConsoleMode(impl->input, impl->saved_mode);
#else
        (void)::tcsetattr(impl->fd, TCSANOW, &impl->saved);
#endif
        impl->active = false;
    }
}  // namespace pjh::platform::detail

namespace pjh::platform
{
    auto ConsoleMode::make_raw(int fd) -> pjh::result::Result<ConsoleMode, ErrorCode>
    {
        ConsoleMode guard;
        guard.impl_ = std::make_unique<ConsoleModeImpl>();
        if (auto r = detail::enter_raw(*guard.impl_, fd); r.is_err())
            return pjh::result::Failure<ErrorCode>{r.unwrap_err()};
        return pjh::result::Result<ConsoleMode, ErrorCode>::Ok(std::move(guard));
    }

    auto ConsoleMode::suspend() -> pjh::result::Result<void, ErrorCode>
    {
        if (!impl_)
            return pjh::result::Failure<ErrorCode>{ErrorCode::InvalidArgument};
        if (!impl_->active)
            return pjh::result::Result<void, ErrorCode>::Ok();
        return detail::restore_saved(*impl_);
    }

    auto ConsoleMode::resume() -> pjh::result::Result<void, ErrorCode>
    {
        if (!impl_)
            return pjh::result::Failure<ErrorCode>{ErrorCode::InvalidArgument};
        if (impl_->active)
            return pjh::result::Result<void, ErrorCode>::Ok();
        return detail::reapply_raw(*impl_);
    }
}  // namespace pjh::platform
