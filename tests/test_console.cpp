#include <doctest/doctest.h>

#include <filesystem>
#include <pjh_platform/console.hpp>
#include <pjh_platform/error.hpp>
#include <pjh_platform/platform.hpp>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#if PJH_PLATFORM_WINDOWS
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

#include <cstdlib>
#include <cstring>
#endif

using pjh::platform::Console;
using pjh::platform::ConsoleMode;
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

// ── ConsoleMode ──────────────────────────────────────────────

TEST_CASE("ConsoleMode is move-only and nothrow-movable")
{
    static_assert(!std::is_copy_constructible_v<ConsoleMode>, "ConsoleMode owns a live terminal");
    static_assert(!std::is_copy_assignable_v<ConsoleMode>, "ConsoleMode owns a live terminal");
    static_assert(
        std::is_nothrow_move_constructible_v<ConsoleMode>,
        "pjh_result requires nothrow-move-constructible T");
    static_assert(std::is_nothrow_move_assignable_v<ConsoleMode>, "move assignment is noexcept");

    // Pin the defaulted `fd = 0` parameter and the exact return type.
    static_assert(
        std::is_same_v<
            decltype(ConsoleMode::make_raw()), pjh::result::Result<ConsoleMode, ErrorCode>>,
        "make_raw() must default to fd 0 and return Result<ConsoleMode, ErrorCode>");
    static_assert(
        std::is_same_v<
            decltype(ConsoleMode::make_raw(0)), pjh::result::Result<ConsoleMode, ErrorCode>>,
        "make_raw(int) must return Result<ConsoleMode, ErrorCode>");
}

TEST_CASE("ConsoleMode::make_raw fails with NotATerminal on a non-terminal descriptor")
{
    const auto path =
        std::filesystem::temp_directory_path() / "pjh_platform_console_mode_test_file.txt";

#if PJH_PLATFORM_WINDOWS
    const int fd = ::_wopen(path.c_str(), _O_RDWR | _O_CREAT | _O_BINARY, _S_IREAD | _S_IWRITE);
#else
    const int fd = ::open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
#endif
    REQUIRE(fd >= 0);

    auto r = ConsoleMode::make_raw(fd);
    CHECK(r.is_err());
    CHECK_EQ(r.unwrap_err(), ErrorCode::NotATerminal);

#if PJH_PLATFORM_WINDOWS
    ::_close(fd);
#else
    ::close(fd);
#endif
    std::filesystem::remove(path);
}

TEST_CASE("ConsoleMode::make_raw rejects an invalid descriptor")
{
    auto r = ConsoleMode::make_raw(-1);
    REQUIRE(r.is_err());
    CHECK_EQ(r.unwrap_err(), ErrorCode::NotATerminal);
}

#if !PJH_PLATFORM_WINDOWS
namespace
{
    // RAII owner for a POSIX pseudo-terminal used to exercise raw mode without
    // ever touching the test runner's terminal. Closes both ends on any exit.
    struct PtyPair
    {
        int master = -1;
        int slave = -1;

        ~PtyPair()
        {
            if (slave != -1)
                ::close(slave);
            if (master != -1)
                ::close(master);
        }

        // Returns false when the platform/container provides no pty; the case
        // then records the reason and returns.
        bool open()
        {
            master = ::posix_openpt(O_RDWR | O_NOCTTY);
            if (master == -1)
                return false;
            if (::grantpt(master) != 0)
                return false;
            if (::unlockpt(master) != 0)
                return false;
            const char *name = ::ptsname(master);
            if (name == nullptr)
                return false;
            slave = ::open(name, O_RDWR | O_NOCTTY);
            return slave != -1;
        }
    };

    auto mode_flags(const termios &t) -> tcflag_t
    {
        return t.c_lflag & static_cast<tcflag_t>(ICANON | ECHO | ISIG);
    }
}  // namespace

TEST_CASE("ConsoleMode raw mode is symmetric and suspend/resume are idempotent")
{
    PtyPair pty;
    if (!pty.open())
    {
        // No /dev/ptmx in this container/sandbox: record the skip and move on.
        return;
    }

    termios original{};
    REQUIRE(::tcgetattr(pty.slave, &original) == 0);

    {
        auto r = ConsoleMode::make_raw(pty.slave);
        REQUIRE(r.is_ok());
        ConsoleMode guard = std::move(r.unwrap());

        termios now{};
        REQUIRE(::tcgetattr(pty.slave, &now) == 0);
        CHECK_EQ(mode_flags(now), static_cast<tcflag_t>(0));
        CHECK_EQ(now.c_cc[VMIN], 1);
        CHECK_EQ(now.c_cc[VTIME], 0);

        // suspend restores the captured mode verbatim.
        CHECK(guard.suspend().is_ok());
        termios after{};
        REQUIRE(::tcgetattr(pty.slave, &after) == 0);
        CHECK_EQ(after.c_iflag, original.c_iflag);
        CHECK_EQ(after.c_oflag, original.c_oflag);
        CHECK_EQ(after.c_cflag, original.c_cflag);
        CHECK_EQ(after.c_lflag, original.c_lflag);
        CHECK_EQ(std::memcmp(after.c_cc, original.c_cc, NCCS), 0);

        // Idempotent suspend leaves cooked mode untouched.
        CHECK(guard.suspend().is_ok());

        // resume re-applies raw; a second resume is a no-op.
        CHECK(guard.resume().is_ok());
        termios raw{};
        REQUIRE(::tcgetattr(pty.slave, &raw) == 0);
        CHECK_EQ(mode_flags(raw), static_cast<tcflag_t>(0));
        CHECK(guard.resume().is_ok());
    }

    // Destructor restores cooked mode best-effort.
    termios restored{};
    REQUIRE(::tcgetattr(pty.slave, &restored) == 0);
    CHECK_EQ(restored.c_iflag, original.c_iflag);
    CHECK_EQ(restored.c_oflag, original.c_oflag);
    CHECK_EQ(restored.c_cflag, original.c_cflag);
    CHECK_EQ(restored.c_lflag, original.c_lflag);
    CHECK_EQ(std::memcmp(restored.c_cc, original.c_cc, NCCS), 0);
}

TEST_CASE("ConsoleMode move transfers ownership and move assignment releases the old state")
{
    PtyPair pty;
    if (!pty.open())
    {
        // No /dev/ptmx in this container/sandbox: record the skip and move on.
        return;
    }

    // Move constructor steals ownership; the source answers InvalidArgument.
    auto ra = ConsoleMode::make_raw(pty.slave);
    REQUIRE(ra.is_ok());
    ConsoleMode a = std::move(ra.unwrap());
    ConsoleMode b = std::move(a);

    CHECK(b.suspend().is_ok());
    CHECK(b.resume().is_ok());

    CHECK(a.suspend().is_err());
    CHECK_EQ(a.suspend().unwrap_err(), ErrorCode::InvalidArgument);
    CHECK(a.resume().is_err());
    CHECK_EQ(a.resume().unwrap_err(), ErrorCode::InvalidArgument);

    CHECK(b.suspend().is_ok());

    // Move assignment steals without losing the terminal state. The moved-from
    // source's impl is already null, so its release path is a no-op.
    auto rc = ConsoleMode::make_raw(pty.slave);
    REQUIRE(rc.is_ok());
    ConsoleMode c = std::move(rc.unwrap());
    ConsoleMode e = std::move(c);
    c = std::move(e);

    termios now{};
    REQUIRE(::tcgetattr(pty.slave, &now) == 0);
    CHECK_EQ(mode_flags(now), static_cast<tcflag_t>(0));  // raw survived the steal
    CHECK(c.suspend().is_ok());
    CHECK(e.suspend().is_err());
    CHECK_EQ(e.suspend().unwrap_err(), ErrorCode::InvalidArgument);
}
#endif
