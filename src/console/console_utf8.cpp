#include <pjh_platform/console.hpp>
#include <pjh_platform/encoding.hpp>
#include <pjh_platform/platform.hpp>

#include "console_internal.hpp"

#if PJH_PLATFORM_WINDOWS
#include <windows.h>

// Must follow <windows.h>: declares CommandLineToArgvW / LocalFree.
#include <shellapi.h>
#endif

#include <string>
#include <vector>

namespace pjh::platform
{
    namespace
    {
        // Verbatim copy of the narrow `main` arguments, shared by the POSIX
        // implementation and the Windows fallback. A null entry becomes an
        // empty string (never a `std::string(nullptr)`); the result is empty
        // when there is no argument vector.
        auto copy_narrow_argv(int argc, char **argv) -> std::vector<std::string>
        {
            std::vector<std::string> result;
            if (argc <= 0 || argv == nullptr)
                return result;
            result.reserve(static_cast<std::size_t>(argc));
            for (int i = 0; i < argc; ++i)
            {
                if (argv[i] != nullptr)
                    result.emplace_back(argv[i]);
                else
                    result.emplace_back();
            }
            return result;
        }
    }  // namespace

#if PJH_PLATFORM_WINDOWS
    auto Console::enable_utf8() -> pjh::result::Result<void, ErrorCode>
    {
        HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD mode = 0;
        if (!GetConsoleMode(out, &mode))
        {
            // No console attached to stdout (redirected to a file or pipe):
            // leave the code page untouched so redirected bytes are never
            // re-encoded.
            return pjh::result::Result<void, ErrorCode>::Ok();
        }

        const UINT old_cp = GetConsoleOutputCP();
        if (!SetConsoleOutputCP(CP_UTF8))
            return pjh::result::Failure<ErrorCode>{detail::classify_console_error(GetLastError())};
        if (!SetConsoleCP(CP_UTF8))
        {
            // Capture the error before the rollback can overwrite it, then
            // best-effort restore the output code page so the process is not
            // left half-converted.
            const DWORD err = GetLastError();
            SetConsoleOutputCP(old_cp);
            return pjh::result::Failure<ErrorCode>{detail::classify_console_error(err)};
        }
        return pjh::result::Result<void, ErrorCode>::Ok();
    }

    auto Console::utf8_arguments([[maybe_unused]] int argc, [[maybe_unused]] char **argv)
        -> std::vector<std::string>
    {
        int wide_argc = 0;
        LPWSTR *wide = CommandLineToArgvW(GetCommandLineW(), &wide_argc);
        if (wide == nullptr)
            return copy_narrow_argv(argc, argv);

        std::vector<std::string> result;
        result.reserve(static_cast<std::size_t>(wide_argc));
        for (int i = 0; i < wide_argc; ++i) result.push_back(Encoding::to_utf8(wide[i]));
        LocalFree(wide);
        return result;
    }
#else
    auto Console::enable_utf8() -> pjh::result::Result<void, ErrorCode>
    {
        return pjh::result::Result<void, ErrorCode>::Ok();
    }

    auto Console::utf8_arguments(int argc, char **argv) -> std::vector<std::string>
    {
        return copy_narrow_argv(argc, argv);
    }
#endif
}  // namespace pjh::platform
