#ifndef INCLUDE_PJH_PLATFORM_PLATFORM_HPP
#define INCLUDE_PJH_PLATFORM_PLATFORM_HPP

// ── Unified platform detection ──────────────────────────────────────────
// Every compilation unit in pjh_platform should include this header first.
// The macros below are the SINGLE point of truth for platform detection.
// Source files check `#if PJH_PLATFORM_WINDOWS` / `#elif PJH_PLATFORM_LINUX`
// instead of scattering raw `_WIN32` / `__linux__` checks.
// ────────────────────────────────────────────────────────────────────────

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
