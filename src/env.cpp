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

    namespace {
        template <typename Func>
        void for_each_env_entry(Func&& func)
        {
#if PJH_PLATFORM_WINDOWS
            auto* block = GetEnvironmentStringsW();
            if (!block)
                return;
            for (auto* env = block; *env; env += std::wcslen(env) + 1)
            {
                std::wstring_view entry(env);
                auto eq = entry.find(L'=');
                if (eq != std::wstring_view::npos)
                    func(Encoding::to_utf8(entry.substr(0, eq)),
                         Encoding::to_utf8(entry.substr(eq + 1)));
            }
            FreeEnvironmentStringsW(block);
#else
            if (!environ)
                return;
            for (auto** env = environ; *env; ++env)
            {
                std::string_view entry(*env);
                auto eq = entry.find('=');
                if (eq != std::string_view::npos)
                    func(std::string(entry.substr(0, eq)),
                         std::string(entry.substr(eq + 1)));
            }
#endif
        }
    }

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
        auto n   = std::string(name);
        const char* val = std::getenv(n.c_str());
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
        for_each_env_entry([&](auto&& k, auto&& v) {
            m.emplace(std::move(k), std::move(v));
        });
        return m;
    }

    auto Env::list()
        -> std::vector<std::pair<std::string, std::string>>
    {
        std::vector<std::pair<std::string, std::string>> result;
        for_each_env_entry([&](auto&& k, auto&& v) {
            result.emplace_back(std::move(k), std::move(v));
        });
        return result;
    }

} // namespace pjh::platform
