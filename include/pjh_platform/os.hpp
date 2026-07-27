#ifndef INCLUDE_PJH_PLATFORM_OS_HPP
#define INCLUDE_PJH_PLATFORM_OS_HPP

#include <pjh_platform/platform.hpp>

#include <bit>
#include <string_view>

namespace pjh::platform
{

    class Os
    {
        Os() = delete;

    public:
        // ── Compile-time OS detection ──────────────────────────────────────

#ifdef PJH_PLATFORM_WINDOWS
        static constexpr bool is_windows = true;
        static constexpr bool is_linux   = false;
        static constexpr bool is_macos   = false;
        static constexpr bool is_posix   = false;
#elif defined(PJH_PLATFORM_MACOS)
        static constexpr bool is_windows = false;
        static constexpr bool is_linux   = false;
        static constexpr bool is_macos   = true;
        static constexpr bool is_posix   = true;
#else
        static constexpr bool is_windows = false;
        static constexpr bool is_linux   = true;
        static constexpr bool is_macos   = false;
        static constexpr bool is_posix   = true;
#endif

        static constexpr bool is_64bit = sizeof(void*) == 8;

        // ── Architecture ───────────────────────────────────────────────────

#if defined(__x86_64__) || defined(_M_AMD64)
        static constexpr std::string_view arch_name = "x86_64";
#elif defined(__i386__) || defined(_M_IX86)
        static constexpr std::string_view arch_name = "x86";
#elif defined(__aarch64__) || defined(_M_ARM64)
        static constexpr std::string_view arch_name = "aarch64";
#elif defined(__arm__) || defined(_M_ARM)
        static constexpr std::string_view arch_name = "arm";
#elif defined(__riscv)
        #if __riscv_xlen == 64
        static constexpr std::string_view arch_name = "riscv64";
        #else
        static constexpr std::string_view arch_name = "riscv32";
        #endif
#else
        static constexpr std::string_view arch_name = "unknown";
#endif

        static constexpr auto endianness = std::endian::native;

        // ── OS helpers ─────────────────────────────────────────────────────

        static constexpr auto name() noexcept -> std::string_view
        {
            if constexpr (is_windows)
                return "windows";
            else if constexpr (is_macos)
                return "macos";
            else
                return "linux";
        }

        static constexpr char path_separator = is_windows ? '\\' : '/';

        static constexpr std::string_view path_list_separator = is_windows ? ";" : ":";
    };

} // namespace pjh::platform

#endif // INCLUDE_PJH_PLATFORM_OS_HPP
