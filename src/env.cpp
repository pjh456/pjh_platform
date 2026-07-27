#include <pjh_platform/env.hpp>
#include <pjh_platform/detail/encoding.hpp>
#include <pjh_platform/error.hpp>
#include <pjh_platform/platform.hpp>

#include <cstdlib>
#include <cstring>

#if PJH_PLATFORM_WINDOWS
#include <windows.h>
#else
#include <cerrno>
extern "C" char** environ;
#endif

namespace pjh::platform
{

    // ── Env ──────────────────────────────────────────────────────────────

    auto Env::get(std::string_view name) -> std::optional<std::string>
    {
#if PJH_PLATFORM_WINDOWS
        auto wname = Encoding::to_wide(name);
        DWORD len = GetEnvironmentVariableW(wname.c_str(), nullptr, 0);
        if (len == 0)
            return std::nullopt;
        std::wstring wvalue(static_cast<std::size_t>(len), L'\0');
        GetEnvironmentVariableW(wname.c_str(), wvalue.data(), len);
        wvalue.pop_back();
        return Encoding::to_utf8(wvalue);
#else
        const char* val = std::getenv(name.data());
        if (!val)
            return std::nullopt;
        return std::string(val);
#endif
    }

    auto Env::set(std::string_view name, std::string_view value)
        -> std::error_code
    {
#if PJH_PLATFORM_WINDOWS
        auto wname  = Encoding::to_wide(name);
        auto wvalue = Encoding::to_wide(value);
        if (!SetEnvironmentVariableW(wname.c_str(), wvalue.c_str()))
            return errc::io_error;
        return {};
#else
        auto n = std::string(name);
        auto v = std::string(value);
        if (::setenv(n.c_str(), v.c_str(), 1) != 0)
            return errc::io_error;
        return {};
#endif
    }

    auto Env::unset(std::string_view name) -> std::error_code
    {
#if PJH_PLATFORM_WINDOWS
        auto wname = Encoding::to_wide(name);
        if (!SetEnvironmentVariableW(wname.c_str(), nullptr))
            return errc::io_error;
        return {};
#else
        auto n = std::string(name);
        if (::unsetenv(n.c_str()) != 0)
            return errc::io_error;
        return {};
#endif
    }

    auto Env::snapshot()
        -> std::unordered_map<std::string, std::string>
    {
        std::unordered_map<std::string, std::string> m;

#if PJH_PLATFORM_WINDOWS
        auto* block = GetEnvironmentStringsW();
        if (!block)
            return m;
        for (auto* env = block; *env; env += std::wcslen(env) + 1)
        {
            std::wstring_view entry(env);
            auto eq = entry.find(L'=');
            if (eq != std::wstring_view::npos)
                m.emplace(
                    Encoding::to_utf8(entry.substr(0, eq)),
                    Encoding::to_utf8(entry.substr(eq + 1)));
        }
        FreeEnvironmentStringsW(block);
#else
        if (!environ)
            return m;
        for (auto** env = environ; *env; ++env)
        {
            std::string_view entry(*env);
            auto eq = entry.find('=');
            if (eq != std::string_view::npos)
                m.emplace(entry.substr(0, eq), entry.substr(eq + 1));
        }
#endif

        return m;
    }

    auto Env::list()
        -> std::vector<std::pair<std::string, std::string>>
    {
        std::vector<std::pair<std::string, std::string>> result;

#if PJH_PLATFORM_WINDOWS
        auto* block = GetEnvironmentStringsW();
        if (!block)
            return result;
        for (auto* env = block; *env; env += std::wcslen(env) + 1)
        {
            std::wstring_view entry(env);
            auto eq = entry.find(L'=');
            if (eq != std::wstring_view::npos)
                result.emplace_back(
                    Encoding::to_utf8(entry.substr(0, eq)),
                    Encoding::to_utf8(entry.substr(eq + 1)));
        }
        FreeEnvironmentStringsW(block);
#else
        if (!environ)
            return result;
        for (auto** env = environ; *env; ++env)
        {
            std::string_view entry(*env);
            auto eq = entry.find('=');
            if (eq != std::string_view::npos)
                result.emplace_back(entry.substr(0, eq), entry.substr(eq + 1));
        }
#endif

        return result;
    }

} // namespace pjh::platform
