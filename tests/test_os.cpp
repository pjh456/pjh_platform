#include <doctest/doctest.h>

#include <bit>
#include <iostream>

#include <pjh_platform/os.hpp>

using pjh::platform::Os;

TEST_CASE("Os::is_* constants — exactly one OS is detected")
{
    int count = 0;
    if constexpr (Os::is_windows) ++count;
    if constexpr (Os::is_linux)   ++count;
    if constexpr (Os::is_macos)   ++count;
    CHECK_EQ(count, 1);

    if constexpr (Os::is_windows)
        CHECK(!Os::is_posix);
    else
        CHECK(Os::is_posix);
}

TEST_CASE("Os::is_64bit matches pointer width")
{
    CHECK_EQ(Os::is_64bit, sizeof(void*) == 8);
}

TEST_CASE("Os::arch_name is a known value")
{
    CHECK_NE(Os::arch_name, "unknown");
    CHECK(!Os::arch_name.empty());
}

TEST_CASE("Os::endianness matches std::endian::native")
{
    CHECK_EQ(Os::endianness, std::endian::native);
}

TEST_CASE("Os::name() is consistent with is_* constants")
{
    if constexpr (Os::is_windows)
        CHECK_EQ(Os::name(), "windows");
    else if constexpr (Os::is_macos)
        CHECK_EQ(Os::name(), "macos");
    else
        CHECK_EQ(Os::name(), "linux");
}

TEST_CASE("Os::path_separator")
{
    if constexpr (Os::is_windows)
        CHECK_EQ(Os::path_separator, '\\');
    else
        CHECK_EQ(Os::path_separator, '/');
}

TEST_CASE("Os::path_list_separator")
{
    if constexpr (Os::is_windows)
        CHECK_EQ(Os::path_list_separator, ";");
    else
        CHECK_EQ(Os::path_list_separator, ":");
}

TEST_CASE("Os constants are usable in constexpr contexts")
{
    constexpr auto sep   = Os::path_separator;
    constexpr auto lsep  = Os::path_list_separator;
    constexpr auto os    = Os::name();
    constexpr auto arch  = Os::arch_name;

    CHECK(!os.empty());
    CHECK(!arch.empty());
    CHECK_NE(sep, '\0');
    CHECK(!lsep.empty());
}

TEST_CASE("Os::is_posix matches non-Windows platforms")
{
    if constexpr (Os::is_windows)
        CHECK(!Os::is_posix);
    else
        CHECK(Os::is_posix);
}
