#ifndef INCLUDE_PJH_PLATFORM_OS_HPP
#define INCLUDE_PJH_PLATFORM_OS_HPP

#include <pjh_platform/platform.hpp>

#include <bit>
#include <string_view>

namespace pjh::platform
{

    /// @brief Compile-time and runtime operating system, architecture, and
    ///        platform details.
    ///
    /// @details Static-only utility class. All members are `constexpr`, so they
    ///          can be used in `static_assert` and template metaprogramming,
    ///          and every value is derived from the single point of truth in
    ///          `pjh_platform/platform.hpp`.
    ///
    /// @platform Windows, Linux, macOS.
    class Os
    {
        Os() = delete;

    public:
        // ── Compile-time OS detection ──────────────────────────────────────

#ifdef PJH_PLATFORM_WINDOWS
        /// @brief True when the target is Windows.
        static constexpr bool is_windows = true;
        /// @brief True when the target is Linux.
        static constexpr bool is_linux   = false;
        /// @brief True when the target is macOS.
        static constexpr bool is_macos   = false;
        /// @brief True when the target is a POSIX platform.
        static constexpr bool is_posix   = false;
#elif defined(PJH_PLATFORM_MACOS)
        /// @brief True when the target is Windows.
        static constexpr bool is_windows = false;
        /// @brief True when the target is Linux.
        static constexpr bool is_linux   = false;
        /// @brief True when the target is macOS.
        static constexpr bool is_macos   = true;
        /// @brief True when the target is a POSIX platform.
        static constexpr bool is_posix   = true;
#else
        /// @brief True when the target is Windows.
        static constexpr bool is_windows = false;
        /// @brief True when the target is Linux.
        static constexpr bool is_linux   = true;
        /// @brief True when the target is macOS.
        static constexpr bool is_macos   = false;
        /// @brief True when the target is a POSIX platform.
        static constexpr bool is_posix   = true;
#endif

        /**
         * @brief True when the pointer width is 64 bits.
         *
         * @details Derived from `sizeof(void*) == 8`.
         *
         * @platform All supported platforms.
         */
        static constexpr bool is_64bit = sizeof(void*) == 8;

        // ── Architecture ───────────────────────────────────────────────────

        /**
         * @brief Canonical name of the target CPU architecture.
         *
         * @details One of `"x86_64"`, `"x86"`, `"aarch64"`, `"arm"`,
         *          `"riscv64"`, `"riscv32"`, or `"unknown"` for an
         *          unrecognized architecture.
         *
         * @platform All supported platforms.
         */
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

        /**
         * @brief Native byte order of the target platform.
         *
         * @details Equal to `std::endian::native` (`little` or `big`).
         *
         * @platform All supported platforms.
         */
        static constexpr auto endianness = std::endian::native;

        // ── OS helpers ─────────────────────────────────────────────────────

        /**
         * @brief Returns the lowercase name of the operating system.
         *
         * @details Returns `"windows"`, `"macos"`, or `"linux"`. Note that any
         *          POSIX target that is not macOS resolves to `"linux"` (the
         *          `platform.hpp` fallback classifies other `__unix__`
         *          systems as Linux).
         *
         * @return `std::string_view` naming the OS.
         *
         * @exception Never throws; `constexpr` and `noexcept`.
         *
         * @sideeffect None.
         *
         * @platform All supported platforms.
         */
        static constexpr auto name() noexcept -> std::string_view
        {
            if constexpr (is_windows)
                return "windows";
            else if constexpr (is_macos)
                return "macos";
            else
                return "linux";
        }

        /**
         * @brief Preferred path component separator for the platform.
         *
         * @details `'\\'` on Windows, `'/'` elsewhere.
         *
         * @platform All supported platforms.
         */
        static constexpr char path_separator = is_windows ? '\\' : '/';

        /**
         * @brief Path list separator for the platform.
         *
         * @details `";"` on Windows, `":"` elsewhere. Used to split/join
         *          search-path variables such as `PATH`.
         *
         * @platform All supported platforms.
         */
        static constexpr std::string_view path_list_separator = is_windows ? ";" : ":";
    };

} // namespace pjh::platform

#endif // INCLUDE_PJH_PLATFORM_OS_HPP
