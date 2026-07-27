#pragma once

#include <string_view>

namespace pjh::platform::os {

// Compile-time OS detection
#if defined(_WIN32) || defined(_WIN64)
    inline constexpr bool is_windows  = true;
    inline constexpr bool is_linux    = false;
    inline constexpr bool is_macos    = false;
    inline constexpr bool is_posix    = false;
#elif defined(__APPLE__)
    inline constexpr bool is_windows  = false;
    inline constexpr bool is_linux    = false;
    inline constexpr bool is_macos    = true;
    inline constexpr bool is_posix    = true;
#else
    inline constexpr bool is_windows  = false;
    inline constexpr bool is_linux    = true;
    inline constexpr bool is_macos    = false;
    inline constexpr bool is_posix    = true;
#endif

inline constexpr std::string_view name() noexcept {
    if constexpr (is_windows) return "windows";
    else if constexpr (is_macos) return "macos";
    else return "linux";
}

inline constexpr char path_separator =
    is_windows ? '\\' : '/';

inline constexpr std::string_view path_list_separator =
    is_windows ? ";" : ":";

} // namespace pjh::platform::os
