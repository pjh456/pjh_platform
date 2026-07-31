#ifndef INCLUDE_PJH_PLATFORM_PLATFORM_HPP
#define INCLUDE_PJH_PLATFORM_PLATFORM_HPP

/// @file pjh_platform/platform.hpp
/// @brief Single point of truth for platform detection macros.
///
/// @details Every compilation unit in pjh_platform should include this header
///          first. Source files check `#if PJH_PLATFORM_WINDOWS` /
///          `#elif PJH_PLATFORM_LINUX` instead of scattering raw `_WIN32` /
///          `__linux__` checks.
///
/// @platform Windows, Linux, macOS, and other POSIX (`__unix__`) systems.

/**
 * @def PJH_PLATFORM_WINDOWS
 * @brief Defined as `1` when compiling for Microsoft Windows.
 *
 * @details Also defines `WIN32_LEAN_AND_MEAN` and `NOMINMAX` to keep the
 *          Windows SDK minimal.
 *
 * @platform Windows only.
 */

/**
 * @def PJH_PLATFORM_MACOS
 * @brief Defined as `1` when compiling for Apple macOS.
 * @platform macOS only.
 */

/**
 * @def PJH_PLATFORM_LINUX
 * @brief Defined as `1` when compiling for Linux.
 * @platform Linux only.
 */

/**
 * @def PJH_PLATFORM_UNIX
 * @brief Defined as `1` on POSIX platforms (macOS, Linux, other `__unix__`).
 * @platform POSIX platforms.
 */

#if defined(_WIN32)
    #define PJH_PLATFORM_WINDOWS 1
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
#elif defined(__APPLE__)
    #define PJH_PLATFORM_MACOS 1
    #define PJH_PLATFORM_UNIX 1
#elif defined(__linux__)
    #define PJH_PLATFORM_LINUX 1
    #define PJH_PLATFORM_UNIX 1
#elif defined(__unix__)
    #define PJH_PLATFORM_UNIX 1
#else
    #error "pjh_platform: unsupported platform"
#endif

#endif // INCLUDE_PJH_PLATFORM_PLATFORM_HPP
