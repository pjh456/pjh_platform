#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <pjh_platform/env.hpp>
#include <pjh_platform/error.hpp>
#include <pjh_platform/fs.hpp>
#include <pjh_platform/paths.hpp>
#include <pjh_platform/platform.hpp>
#include <string>
#include <string_view>
#include <vector>

#include "error_mapping.hpp"

#if PJH_PLATFORM_WINDOWS
#include <windows.h>

#include <pjh_platform/encoding.hpp>
#elif PJH_PLATFORM_MACOS
#include <mach-o/dyld.h>
#else
#include <unistd.h>

#include <cerrno>
#endif

namespace pjh::platform
{

    namespace
    {
#if !PJH_PLATFORM_WINDOWS
        constexpr std::size_t kInitialExecutablePathBytes = 256;
        constexpr std::size_t kMaxExecutablePathBytes = 1u << 20;  // 1 MiB
#else
        constexpr std::size_t kInitialExecutablePathChars = 260;  // MAX_PATH
        constexpr std::size_t kMaxExecutablePathChars = 32768;
#endif

        // UTF-8 (the public string encoding) -> native std::filesystem::path.
        // std::u8string makes Windows interpret the bytes as UTF-8 and convert
        // them to UTF-16 instead of using the active code page.
        auto path_from_utf8(std::string_view utf8) -> std::filesystem::path
        {
            if (utf8.empty())
                return {};
            const auto *data = reinterpret_cast<const char8_t *>(utf8.data());
            return std::filesystem::path(std::u8string(data, data + utf8.size()));
        }

        // Environment lookup with the XDG "empty means unset" convention: an
        // unset variable and a set-but-empty variable both yield an empty string.
        auto env_nonempty(std::string_view name) -> std::string
        {
            auto value = Env::get(name);
            if (value.is_err())
                return {};
            return value.unwrap();
        }

        // Appends app_name as one path component when it is non-empty.
        auto append_app(const std::filesystem::path &base, std::string_view app_name)
            -> std::filesystem::path
        {
            if (app_name.empty())
                return base;
            return base / path_from_utf8(app_name);
        }

        // Grows @p current by doubling, capped at @p max.
        auto grow_capped(std::size_t current, std::size_t max) -> std::size_t
        {
            std::size_t next = current * 2;
            if (next > max)
                next = max;
            return next;
        }

        auto query_executable_path() -> pjh::result::Result<std::filesystem::path, ErrorCode>
        {
#if PJH_PLATFORM_WINDOWS
            std::vector<wchar_t> buffer(kInitialExecutablePathChars);
            for (;;)
            {
                DWORD length =
                    GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
                if (length == 0)
                {
                    DWORD error = GetLastError();
                    if (error == ERROR_INSUFFICIENT_BUFFER)
                    {
                        if (buffer.size() >= kMaxExecutablePathChars)
                            return pjh::result::Failure<ErrorCode>{ErrorCode::LimitReached};
                        buffer.resize(grow_capped(buffer.size(), kMaxExecutablePathChars));
                        continue;
                    }
                    return pjh::result::Failure<ErrorCode>{detail::map_windows_error(error)};
                }
                if (length < buffer.size())
                {
                    std::wstring_view wide(buffer.data(), static_cast<std::size_t>(length));
                    return pjh::result::Result<std::filesystem::path, ErrorCode>::Ok(
                        path_from_utf8(Encoding::to_utf8(wide)));
                }
                // Truncated: the buffer was too small and holds no NUL.
                if (buffer.size() >= kMaxExecutablePathChars)
                    return pjh::result::Failure<ErrorCode>{ErrorCode::LimitReached};
                buffer.resize(grow_capped(buffer.size(), kMaxExecutablePathChars));
            }
#elif PJH_PLATFORM_MACOS
            std::vector<char> buffer(kInitialExecutablePathBytes);
            std::uint32_t size = static_cast<std::uint32_t>(buffer.size());
            while (_NSGetExecutablePath(buffer.data(), &size) != 0)
            {
                // On failure `size` holds the required length, including the NUL.
                if (buffer.size() >= kMaxExecutablePathBytes)
                    return pjh::result::Failure<ErrorCode>{ErrorCode::LimitReached};
                std::size_t next = size > buffer.size()
                                       ? static_cast<std::size_t>(size)
                                       : grow_capped(buffer.size(), kMaxExecutablePathBytes);
                if (next > kMaxExecutablePathBytes)
                    next = kMaxExecutablePathBytes;
                buffer.resize(next);
                size = static_cast<std::uint32_t>(buffer.size());
            }
            std::filesystem::path reported = path_from_utf8(std::string_view(buffer.data()));
            std::error_code ec;
            auto canonical = std::filesystem::weakly_canonical(reported, ec);
            if (ec)
                return pjh::result::Result<std::filesystem::path, ErrorCode>::Ok(
                    reported.lexically_normal());
            return pjh::result::Result<std::filesystem::path, ErrorCode>::Ok(std::move(canonical));
#else
            std::vector<char> buffer(kInitialExecutablePathBytes);
            for (;;)
            {
                auto length = ::readlink("/proc/self/exe", buffer.data(), buffer.size());
                if (length < 0)
                    return pjh::result::Failure<ErrorCode>{detail::map_errno_to_error(errno)};
                if (length == 0)
                    return pjh::result::Failure<ErrorCode>{ErrorCode::IoError};
                auto count = static_cast<std::size_t>(length);
                // readlink leaves no NUL and reports exactly the buffer size when
                // the link target was truncated.
                if (count < buffer.size())
                    return pjh::result::Result<std::filesystem::path, ErrorCode>::Ok(
                        path_from_utf8(std::string_view(buffer.data(), count)));
                if (buffer.size() >= kMaxExecutablePathBytes)
                    return pjh::result::Failure<ErrorCode>{ErrorCode::LimitReached};
                buffer.resize(grow_capped(buffer.size(), kMaxExecutablePathBytes));
            }
#endif
        }

#if PJH_PLATFORM_WINDOWS
        auto windows_user_dir(std::string_view variable, std::string_view app_name)
            -> pjh::result::Result<std::filesystem::path, ErrorCode>
        {
            auto base = env_nonempty(variable);
            if (base.empty())
                return pjh::result::Failure<ErrorCode>{ErrorCode::NotFound};
            return pjh::result::Result<std::filesystem::path, ErrorCode>::Ok(
                append_app(path_from_utf8(base), app_name));
        }
#elif PJH_PLATFORM_MACOS
        auto macos_user_dir(const std::filesystem::path &library_subdir, std::string_view app_name)
            -> pjh::result::Result<std::filesystem::path, ErrorCode>
        {
            auto home = Fs::home_directory();
            if (home.is_err())
                return pjh::result::Failure<ErrorCode>{ErrorCode::NotFound};
            const auto &home_path = home.unwrap();
            if (home_path.empty())
                return pjh::result::Failure<ErrorCode>{ErrorCode::NotFound};
            return pjh::result::Result<std::filesystem::path, ErrorCode>::Ok(
                append_app(home_path / "Library" / library_subdir, app_name));
        }
#else
        auto posix_user_dir(
            std::string_view xdg_variable,
            const std::filesystem::path &fallback,
            std::string_view app_name) -> pjh::result::Result<std::filesystem::path, ErrorCode>
        {
            auto xdg = env_nonempty(xdg_variable);
            if (!xdg.empty())
                return pjh::result::Result<std::filesystem::path, ErrorCode>::Ok(
                    append_app(path_from_utf8(xdg), app_name));
            auto home = Fs::home_directory();
            if (home.is_err())
                return pjh::result::Failure<ErrorCode>{ErrorCode::NotFound};
            const auto &home_path = home.unwrap();
            if (home_path.empty())
                return pjh::result::Failure<ErrorCode>{ErrorCode::NotFound};
            return pjh::result::Result<std::filesystem::path, ErrorCode>::Ok(
                append_app(home_path / fallback, app_name));
        }
#endif
    }  // namespace

    auto Paths::executable_path() -> pjh::result::Result<std::filesystem::path, ErrorCode>
    {
        return query_executable_path();
    }

    auto Paths::executable_dir() -> pjh::result::Result<std::filesystem::path, ErrorCode>
    {
        auto executable = query_executable_path();
        if (executable.is_err())
            return pjh::result::Failure<ErrorCode>{executable.unwrap_err()};
        return pjh::result::Result<std::filesystem::path, ErrorCode>::Ok(
            executable.unwrap().parent_path());
    }

    auto Paths::config_dir(std::string_view app_name)
        -> pjh::result::Result<std::filesystem::path, ErrorCode>
    {
#if PJH_PLATFORM_WINDOWS
        return windows_user_dir("APPDATA", app_name);
#elif PJH_PLATFORM_MACOS
        return macos_user_dir("Application Support", app_name);
#else
        return posix_user_dir("XDG_CONFIG_HOME", ".config", app_name);
#endif
    }

    auto Paths::data_dir(std::string_view app_name)
        -> pjh::result::Result<std::filesystem::path, ErrorCode>
    {
#if PJH_PLATFORM_WINDOWS
        return windows_user_dir("APPDATA", app_name);
#elif PJH_PLATFORM_MACOS
        return macos_user_dir("Application Support", app_name);
#else
        return posix_user_dir("XDG_DATA_HOME", std::filesystem::path(".local") / "share", app_name);
#endif
    }

    auto Paths::cache_dir(std::string_view app_name)
        -> pjh::result::Result<std::filesystem::path, ErrorCode>
    {
#if PJH_PLATFORM_WINDOWS
        auto base = env_nonempty("LOCALAPPDATA");
        if (base.empty())
            base = env_nonempty("APPDATA");
        if (base.empty())
            return pjh::result::Failure<ErrorCode>{ErrorCode::NotFound};
        auto cache = append_app(path_from_utf8(base), app_name) / "Cache";
        return pjh::result::Result<std::filesystem::path, ErrorCode>::Ok(std::move(cache));
#elif PJH_PLATFORM_MACOS
        return macos_user_dir("Caches", app_name);
#else
        return posix_user_dir("XDG_CACHE_HOME", ".cache", app_name);
#endif
    }

}  // namespace pjh::platform
