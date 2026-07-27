#include <pjh_platform/encoding.hpp>
#include <pjh_platform/env.hpp>
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

    auto Env::get(std::string_view name)
        -> pjh::result::Result<std::string, ErrorCode>
    {
#if PJH_PLATFORM_WINDOWS
        auto wname = Encoding::to_wide(name);
        DWORD len = GetEnvironmentVariableW(wname.c_str(), nullptr, 0);
        if (len == 0)
            return pjh::result::Failure<ErrorCode>{ErrorCode::not_found};
        std::wstring wvalue(static_cast<std::size_t>(len), L'\0');
        GetEnvironmentVariableW(wname.c_str(), wvalue.data(), len);
        wvalue.pop_back();
        return pjh::result::Result<std::string, ErrorCode>::Ok(Encoding::to_utf8(wvalue));
#else
        const char* val = std::getenv(name.data());
        if (!val)
            return pjh::result::Failure<ErrorCode>{ErrorCode::not_found};
        return pjh::result::Result<std::string, ErrorCode>::Ok(std::string(val));
#endif
    }

    auto Env::set(std::string_view name, std::string_view value)
        -> pjh::result::Result<void, ErrorCode>
    {
#if PJH_PLATFORM_WINDOWS
        auto wname  = Encoding::to_wide(name);
        auto wvalue = Encoding::to_wide(value);
        if (!SetEnvironmentVariableW(wname.c_str(), wvalue.c_str()))
            return pjh::result::Failure<ErrorCode>{ErrorCode::io_error};
        return pjh::result::Result<void, ErrorCode>::Ok();
#else
        auto n = std::string(name);
        auto v = std::string(value);
        if (::setenv(n.c_str(), v.c_str(), 1) != 0)
            return pjh::result::Failure<ErrorCode>{ErrorCode::io_error};
        return pjh::result::Result<void, ErrorCode>::Ok();
#endif
    }

    auto Env::unset(std::string_view name)
        -> pjh::result::Result<void, ErrorCode>
    {
#if PJH_PLATFORM_WINDOWS
        auto wname = Encoding::to_wide(name);
        if (!SetEnvironmentVariableW(wname.c_str(), nullptr))
            return pjh::result::Failure<ErrorCode>{ErrorCode::io_error};
        return pjh::result::Result<void, ErrorCode>::Ok();
#else
        auto n = std::string(name);
        if (::unsetenv(n.c_str()) != 0)
            return pjh::result::Failure<ErrorCode>{ErrorCode::io_error};
        return pjh::result::Result<void, ErrorCode>::Ok();
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
