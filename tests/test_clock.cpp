#include <doctest/doctest.h>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <pjh_platform/clock.hpp>
#include <pjh_platform/error.hpp>
#include <pjh_platform/platform.hpp>
#include <string>
#include <type_traits>

using pjh::platform::Clock;
using pjh::platform::ErrorCode;

// Compile-time pins: the wall clock must stay a system_clock point, the
// monotonic clock a steady_clock point, and unix_millis a 64-bit signed count.
// Swapping the two clock families or drifting a return type fails the build.
static_assert(std::is_same_v<decltype(Clock::now()), std::chrono::system_clock::time_point>);
static_assert(
    std::is_same_v<decltype(Clock::monotonic_now()), std::chrono::steady_clock::time_point>);
static_assert(std::is_same_v<decltype(Clock::unix_millis()), std::int64_t>);

namespace
{
    // Temporarily overrides the process time zone with a POSIX TZ rule and
    // restores the previous state (value or absence) on scope exit, so a
    // REQUIRE failure's unwind cannot leak a mutated TZ into later cases. The
    // injected zone is UTC+14, which a localtime-based formatter would render
    // as 14:00:00 for the Unix epoch: the T9 assertion is the discriminator.
    class TzRestore
    {
    public:
        explicit TzRestore(const char *value)
        {
            const char *old = std::getenv("TZ");
            m_was_set = old != nullptr;
            if (m_was_set)
                m_old = old;
#if PJH_PLATFORM_WINDOWS
            (void)::_putenv_s("TZ", value);
            ::_tzset();
#else
            (void)::setenv("TZ", value, 1);
            ::tzset();
#endif
        }

        ~TzRestore()
        {
#if PJH_PLATFORM_WINDOWS
            (void)::_putenv_s("TZ", m_was_set ? m_old.c_str() : "");
            ::_tzset();
#else
            if (m_was_set)
                (void)::setenv("TZ", m_old.c_str(), 1);
            else
                (void)::unsetenv("TZ");
            ::tzset();
#endif
        }

    private:
        std::string m_old;
        bool m_was_set = false;
    };
}  // namespace

TEST_CASE("Clock::monotonic_now is non-decreasing across consecutive calls")
{
    const auto a = Clock::monotonic_now();
    const auto b = Clock::monotonic_now();
    CHECK(a <= b);
}

TEST_CASE("Clock::now is close to std::chrono::system_clock::now")
{
    const auto c = Clock::now();
    const auto ref = std::chrono::system_clock::now();
    CHECK(c >= ref - std::chrono::minutes(5));
    CHECK(c <= ref + std::chrono::minutes(5));
}

TEST_CASE("Clock::unix_millis tracks the system clock")
{
    const auto ms = Clock::unix_millis();
    const auto ref = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count();
    CHECK(ms >= ref - 300'000);
    CHECK(ms <= ref + 300'000);
}

TEST_CASE("Clock::format_iso8601_utc formats the Unix epoch exactly")
{
    const auto tp = std::chrono::system_clock::time_point{std::chrono::seconds{0}};
    auto r = Clock::format_iso8601_utc(tp);
    REQUIRE(r.is_ok());
    CHECK_EQ(r.unwrap(), "1970-01-01T00:00:00.000Z");
}

TEST_CASE("Clock::format_iso8601_utc formats a known timestamp")
{
    const auto tp = std::chrono::system_clock::time_point{std::chrono::seconds{1'000'000'000}};
    auto r = Clock::format_iso8601_utc(tp);
    REQUIRE(r.is_ok());
    CHECK_EQ(r.unwrap(), "2001-09-09T01:46:40.000Z");
}

TEST_CASE("Clock::format_iso8601_utc truncates sub-millisecond digits instead of rounding")
{
    const auto base = std::chrono::system_clock::time_point{std::chrono::seconds{1'000'000'000}};

    auto r1 = Clock::format_iso8601_utc(base + std::chrono::microseconds{999'999});
    REQUIRE(r1.is_ok());
    CHECK_EQ(r1.unwrap(), "2001-09-09T01:46:40.999Z");

    auto r2 = Clock::format_iso8601_utc(base + std::chrono::microseconds{999'500});
    REQUIRE(r2.is_ok());
    CHECK_EQ(r2.unwrap(), "2001-09-09T01:46:40.999Z");
}

TEST_CASE("Clock::format_iso8601_utc zero-pads every field")
{
    const auto base = std::chrono::system_clock::time_point{std::chrono::seconds{999'997'323}};
    auto r = Clock::format_iso8601_utc(base + std::chrono::milliseconds{4});
    REQUIRE(r.is_ok());
    const auto text = r.unwrap();
    CHECK_EQ(text, "2001-09-09T01:02:03.004Z");
    CHECK_EQ(text.size(), 24);
    CHECK_EQ(text[10], 'T');
    CHECK_EQ(text.back(), 'Z');
}

TEST_CASE("Clock::format_iso8601_utc rejects instants before the Unix epoch")
{
    const auto before_seconds = std::chrono::system_clock::time_point{std::chrono::seconds{-1}};
    auto r1 = Clock::format_iso8601_utc(before_seconds);
    REQUIRE(r1.is_err());
    CHECK_EQ(r1.unwrap_err(), ErrorCode::InvalidArgument);

    const auto before_millis = std::chrono::system_clock::time_point{std::chrono::milliseconds{-1}};
    auto r2 = Clock::format_iso8601_utc(before_millis);
    REQUIRE(r2.is_err());
    CHECK_EQ(r2.unwrap_err(), ErrorCode::InvalidArgument);

    const auto epoch = std::chrono::system_clock::time_point{std::chrono::seconds{0}};
    CHECK(Clock::format_iso8601_utc(epoch).is_ok());
}

TEST_CASE("Clock::format_iso8601_utc is independent of the local time zone")
{
    // POSIX TZ offset sign is inverted: "ABC-14" means UTC+14. If the
    // implementation consulted localtime, the epoch would render as
    // "1970-01-01T14:00:00.000Z" and this case would fail.
    const TzRestore tz{"ABC-14"};
    const auto epoch = std::chrono::system_clock::time_point{std::chrono::seconds{0}};
    auto r = Clock::format_iso8601_utc(epoch);
    REQUIRE(r.is_ok());
    CHECK_EQ(r.unwrap(), "1970-01-01T00:00:00.000Z");
}
