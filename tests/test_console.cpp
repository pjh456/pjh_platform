#include <doctest/doctest.h>

#include <pjh_platform/console.hpp>
#include <pjh_platform/error.hpp>
#include <pjh_platform/platform.hpp>
#include <string>
#include <vector>

#if PJH_PLATFORM_WINDOWS
#include <windows.h>
#endif

using pjh::platform::Console;
using pjh::platform::ErrorCode;

// ── enable_utf8 ──────────────────────────────────────────────

#if !PJH_PLATFORM_WINDOWS
TEST_CASE("Console::enable_utf8 is a successful no-op on POSIX")
{
    auto first = Console::enable_utf8();
    auto second = Console::enable_utf8();
    CHECK(first.is_ok());
    CHECK(second.is_ok());
}
#endif

#if PJH_PLATFORM_WINDOWS
TEST_CASE("Console::enable_utf8 reports Ok and sets CP_UTF8 on a Windows console")
{
    DWORD mode = 0;
    const BOOL is_console = GetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), &mode);
    const UINT before = GetConsoleOutputCP();

    auto r = Console::enable_utf8();
    REQUIRE(r.is_ok());

    if (is_console)
        CHECK_EQ(GetConsoleOutputCP(), static_cast<UINT>(CP_UTF8));
    else
        CHECK_EQ(GetConsoleOutputCP(), before);
}
#endif

// ── is_tty ───────────────────────────────────────────────────

TEST_CASE("Console::is_tty returns false for invalid descriptors")
{
    CHECK_FALSE(Console::is_tty(-1));
    CHECK_FALSE(Console::is_tty(9999));

    // The standard descriptors must answer with a bool and never crash.
    (void)Console::is_tty(0);
    (void)Console::is_tty(1);
    (void)Console::is_tty(2);
}

// ── size ─────────────────────────────────────────────────────

TEST_CASE("Console::size fails with NotATerminal when stdout is not a terminal")
{
    if (Console::is_tty(1))
    {
        // Interactive run: the positive path is covered by the sibling case.
        CHECK(Console::size().is_ok());
        return;
    }

    auto r = Console::size();
    REQUIRE(r.is_err());
    // POSIX reaches this through ENOTTY; Windows through the console-only
    // ERROR_INVALID_HANDLE overlay.
    CHECK_EQ(r.unwrap_err(), ErrorCode::NotATerminal);
}

TEST_CASE("Console::size returns positive dimensions on a real terminal")
{
    if (!Console::is_tty(1))
    {
        // ctest captures output, so this is the observed branch there; the
        // redirected path must report NotATerminal rather than 0x0.
        auto r = Console::size();
        REQUIRE(r.is_err());
        CHECK_EQ(r.unwrap_err(), ErrorCode::NotATerminal);
        return;
    }

    auto r = Console::size();
    REQUIRE(r.is_ok());
    CHECK(r.unwrap().columns > 0);
    CHECK(r.unwrap().rows > 0);
}

// ── supports_ansi ────────────────────────────────────────────

TEST_CASE("Console::supports_ansi is false when stdout is not a terminal")
{
    if (!Console::is_tty(1))
        CHECK_FALSE(Console::supports_ansi());
    else
    {
        // A real terminal's answer depends on the host TERM, so only assert
        // that the probe is callable and read-only.
        (void)Console::supports_ansi();
    }
}

// ── utf8_arguments ───────────────────────────────────────────

#if !PJH_PLATFORM_WINDOWS
TEST_CASE("Console::utf8_arguments copies POSIX argv verbatim")
{
    // Byte-exact UTF-8 sample kept as \x escapes so this file stays ASCII.
    char prog[] = "prog";
    char a[] = "a";
    char zhong[] = "\xE4\xB8\xAD";  // 中

    char *argv[] = {prog, a, zhong};
    auto args = Console::utf8_arguments(3, argv);
    REQUIRE(args.size() == 3);
    CHECK_EQ(args[0], std::string("prog"));
    CHECK_EQ(args[1], std::string("a"));
    CHECK_EQ(args[2], std::string("\xE4\xB8\xAD"));
    CHECK_EQ(args[2].size(), std::size_t{3});

    // Null entries become empty strings instead of constructing from nullptr.
    char *with_null[] = {prog, nullptr};
    auto null_args = Console::utf8_arguments(2, with_null);
    REQUIRE(null_args.size() == 2);
    CHECK_EQ(null_args[0], std::string("prog"));
    CHECK(null_args[1].empty());

    // No argument vector / non-positive count yields an empty vector.
    CHECK_EQ(Console::utf8_arguments(0, nullptr).size(), std::size_t{0});
    CHECK_EQ(Console::utf8_arguments(3, nullptr).size(), std::size_t{0});
    CHECK_EQ(Console::utf8_arguments(-1, argv).size(), std::size_t{0});
}
#endif

#if PJH_PLATFORM_WINDOWS
TEST_CASE("Console::utf8_arguments returns the process command line on Windows")
{
    auto args = Console::utf8_arguments(0, nullptr);
    REQUIRE(args.size() >= 1);
    CHECK_FALSE(args.front().empty());
}
#endif

// ── ErrorCode contract ───────────────────────────────────────

TEST_CASE("ErrorCode::NotATerminal is appended without renumbering the closed set")
{
    static_assert(
        static_cast<int>(ErrorCode::NotATerminal) == 11,
        "NotATerminal must be appended after AlreadyWatched");
    static_assert(
        static_cast<int>(ErrorCode::AlreadyWatched) == 10,
        "existing ErrorCode values must not be renumbered");
    CHECK_EQ(static_cast<int>(ErrorCode::NotATerminal), 11);
}

TEST_CASE("Console::enable_utf8 leaves redirected stdout bytes untouched")
{
    auto r = Console::enable_utf8();
    REQUIRE(r.is_ok());

    if (!Console::is_tty(1))
    {
        // The successful no-op must not pretend the stream has terminal
        // capabilities: size() still fails with NotATerminal.
        auto s = Console::size();
        REQUIRE(s.is_err());
        CHECK_EQ(s.unwrap_err(), ErrorCode::NotATerminal);
    }
}
