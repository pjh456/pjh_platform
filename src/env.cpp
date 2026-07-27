#include "pjh_platform/env.hpp"
#include "pjh_platform/error.hpp"
#include "pjh_platform/os.hpp"

#include <cstdlib>
#include <cstring>

#if defined(_WIN32) || defined(_WIN64)
    #include <windows.h>
#endif

namespace pjh::platform::env {

auto get(std::string_view name) -> std::optional<std::string> {
#if defined(_WIN32) || defined(_WIN64)
    std::string n(name);
    DWORD len = GetEnvironmentVariableA(n.c_str(), nullptr, 0);
    if (len == 0) return std::nullopt;
    std::string value(len, '\0');
    GetEnvironmentVariableA(n.c_str(), value.data(), static_cast<DWORD>(value.size()));
    value.pop_back();
    return value;
#else
    const char* val = std::getenv(name.data());
    if (!val) return std::nullopt;
    return std::string(val);
#endif
}

auto set(std::string_view name, std::string_view value) -> std::error_code {
#if defined(_WIN32) || defined(_WIN64)
    std::string n(name), v(value);
    if (!SetEnvironmentVariableA(n.c_str(), v.c_str()))
        return errc::io_error;
    return {};
#else
    std::string n(name), v(value);
    if (::setenv(n.c_str(), v.c_str(), 1) != 0)
        return errc::io_error;
    return {};
#endif
}

auto unset(std::string_view name) -> std::error_code {
#if defined(_WIN32) || defined(_WIN64)
    std::string n(name);
    if (!SetEnvironmentVariableA(n.c_str(), nullptr))
        return errc::io_error;
    return {};
#else
    std::string n(name);
    if (::unsetenv(n.c_str()) != 0)
        return errc::io_error;
    return {};
#endif
}

} // namespace pjh::platform::env
