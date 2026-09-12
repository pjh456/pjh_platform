#include <chrono>
#include <cstddef>
#include <ctime>
#include <limits>
#include <pjh_platform/clock.hpp>
#include <pjh_platform/platform.hpp>
#include <string>
#include <utility>

namespace pjh::platform
{
    namespace
    {
        // "YYYY-MM-DDTHH:MM:SS.mmmZ" is exactly 24 ASCII characters.
        constexpr std::size_t kIso8601UtcLength = 24;

        // Appends @p value as exactly @p width ASCII digits (most significant
        // first). Callers guarantee 0 <= value < 10^width and width <= 4, so no
        // sign/overflow handling and no locale/stdio is needed.
        auto append_padded(std::string &out, int value, int width) -> void
        {
            char digits[4];
            for (int i = width - 1; i >= 0; --i)
            {
                digits[i] = static_cast<char>('0' + value % 10);
                value /= 10;
            }
            out.append(digits, static_cast<std::size_t>(width));
        }

        // UTC calendar conversion through the reentrant platform converter.
        // Note the opposite argument order: POSIX takes (source, result),
        // MSVC takes (result, source). Never use the non-reentrant C converter
        // (it shares one static buffer across calls).
        auto to_utc_tm(std::time_t t, std::tm &out) -> bool
        {
#if PJH_PLATFORM_WINDOWS
            return ::gmtime_s(&out, &t) == 0;
#else
            return ::gmtime_r(&t, &out) != nullptr;
#endif
        }
    }  // namespace

    auto Clock::now() -> std::chrono::system_clock::time_point
    {
        return std::chrono::system_clock::now();
    }

    auto Clock::monotonic_now() -> std::chrono::steady_clock::time_point
    {
        return std::chrono::steady_clock::now();
    }

    auto Clock::unix_millis() -> std::int64_t
    {
        return std::chrono::floor<std::chrono::milliseconds>(now().time_since_epoch()).count();
    }

    auto Clock::format_iso8601_utc(std::chrono::system_clock::time_point tp)
        -> pjh::result::Result<std::string, ErrorCode>
    {
        // Floor to whole milliseconds first, then split into whole seconds plus
        // a non-negative [0, 999] millisecond part. Floor (not duration_cast)
        // keeps the instant before the millisecond boundary, so autosave-style
        // names never jump ahead across a second.
        const auto total_ms = std::chrono::floor<std::chrono::milliseconds>(tp.time_since_epoch());
        if (total_ms.count() < 0)
            return pjh::result::Failure<ErrorCode>{ErrorCode::InvalidArgument};

        const auto whole_seconds = std::chrono::floor<std::chrono::seconds>(total_ms);
        const auto millis = static_cast<int>((total_ms - whole_seconds).count());

        const auto sec_count = whole_seconds.count();
        if (sec_count > static_cast<std::int64_t>(std::numeric_limits<std::time_t>::max()))
            return pjh::result::Failure<ErrorCode>{ErrorCode::InvalidArgument};

        std::time_t t = static_cast<std::time_t>(sec_count);
        std::tm tm_value{};
        if (!to_utc_tm(t, tm_value))
            return pjh::result::Failure<ErrorCode>{ErrorCode::InvalidArgument};

        const int year = tm_value.tm_year + 1900;
        if (year < 0 || year > 9999)
            return pjh::result::Failure<ErrorCode>{ErrorCode::InvalidArgument};

        std::string out;
        out.reserve(kIso8601UtcLength);
        append_padded(out, year, 4);
        out.push_back('-');
        append_padded(out, tm_value.tm_mon + 1, 2);
        out.push_back('-');
        append_padded(out, tm_value.tm_mday, 2);
        out.push_back('T');
        append_padded(out, tm_value.tm_hour, 2);
        out.push_back(':');
        append_padded(out, tm_value.tm_min, 2);
        out.push_back(':');
        append_padded(out, tm_value.tm_sec, 2);
        out.push_back('.');
        append_padded(out, millis, 3);
        out.push_back('Z');
        return pjh::result::Result<std::string, ErrorCode>::Ok(std::move(out));
    }
}  // namespace pjh::platform
