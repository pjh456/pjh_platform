#include <doctest/doctest.h>

#include <pjh_platform/platform.hpp>

// ── Platform detection compilation tests ────────────────────────────────
// These verify that the preprocessor macros defined in platform.hpp
// are consistent and that exactly one OS family is selected.

TEST_CASE("PJH_PLATFORM_LINUX state")
{
#if defined(PJH_PLATFORM_LINUX)
    CHECK(PJH_PLATFORM_LINUX == 1);
#endif
}

TEST_CASE("PJH_PLATFORM_MACOS state")
{
#if defined(PJH_PLATFORM_MACOS)
    CHECK(PJH_PLATFORM_MACOS == 1);
#endif
}

TEST_CASE("PJH_PLATFORM_WINDOWS state")
{
#if defined(PJH_PLATFORM_WINDOWS)
    CHECK(PJH_PLATFORM_WINDOWS == 1);
#endif
}

TEST_CASE("PJH_PLATFORM_UNIX state")
{
#if defined(PJH_PLATFORM_UNIX)
    CHECK(PJH_PLATFORM_UNIX == 1);
#endif
}

TEST_CASE("exactly one OS family is selected")
{
    int count = 0;
#ifdef PJH_PLATFORM_LINUX
    ++count;
#endif
#ifdef PJH_PLATFORM_MACOS
    ++count;
#endif
#ifdef PJH_PLATFORM_WINDOWS
    ++count;
#endif
    CHECK_EQ(count, 1);
}
