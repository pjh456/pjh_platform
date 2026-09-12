#ifndef INCLUDE_PJH_PLATFORM_CLOCK_HPP
#define INCLUDE_PJH_PLATFORM_CLOCK_HPP

#include <chrono>
#include <cstdint>
#include <pjh_platform/error.hpp>
#include <string>

namespace pjh::platform
{

    /// @brief Wall-clock and monotonic time, plus UTC ISO 8601 formatting.
    ///
    /// @details Static-only and stateless. `now()` reads the system-wide wall
    ///          clock (`std::chrono::system_clock`); `monotonic_now()` reads
    ///          the monotonic clock (`std::chrono::steady_clock`) and is meant
    ///          for measuring durations — its epoch is unspecified, so only
    ///          differences between two readings are meaningful.
    ///
    ///          `system_clock` is treated as Unix/POSIX time: the epoch is
    ///          1970-01-01T00:00:00Z and leap seconds are not represented (a UTC
    ///          day counts as exactly 86400 seconds), matching the C++20
    ///          `[time.clock.system]` contract and the platform calendar
    ///          converters.
    ///
    ///          Formatting uses the reentrant platform calendar conversion
    ///          (`gmtime_r` on POSIX, `gmtime_s` on Windows) into a
    ///          caller-local `std::tm`; no shared static buffer is touched, so
    ///          every member is safe to call from multiple threads. Local-time
    ///          formatting is deliberately not provided yet; adding it later is
    ///          source-compatible.
    ///
    /// @platform Windows, Linux, macOS.
    class Clock
    {
        Clock() = delete;

    public:
        /**
         * @brief Current wall-clock time.
         *
         * @return The current `std::chrono::system_clock` time point (Unix
         *         time; see the class @details).
         *
         * @exception Never throws.
         *
         * @sideeffect None.
         *
         * @platform Windows, Linux, macOS.
         */
        [[nodiscard]] static auto now() -> std::chrono::system_clock::time_point;

        /**
         * @brief Current monotonic time, for measuring elapsed durations.
         *
         * @details The clock only guarantees non-decreasing readings; its epoch
         *          is unspecified and its value has no absolute meaning.
         *          Subtract two readings to obtain a duration.
         *
         * @return The current `std::chrono::steady_clock` time point.
         *
         * @exception Never throws.
         *
         * @sideeffect None.
         *
         * @platform Windows, Linux, macOS.
         */
        [[nodiscard]] static auto monotonic_now() -> std::chrono::steady_clock::time_point;

        /**
         * @brief Milliseconds since the Unix epoch of the current wall clock.
         *
         * @details Convenience for sortable timestamps (for example autosave
         *          file names). The sub-millisecond part is truncated toward
         *          negative infinity (`std::chrono::floor`), so the value never
         *          rounds ahead across a second boundary.
         *
         * @return Milliseconds since 1970-01-01T00:00:00Z, a 64-bit signed count.
         *
         * @exception Never throws.
         *
         * @sideeffect None.
         *
         * @platform Windows, Linux, macOS.
         */
        [[nodiscard]] static auto unix_millis() -> std::int64_t;

        /**
         * @brief Formats @p tp as an ISO 8601 UTC timestamp.
         *
         * @details The result is always `YYYY-MM-DDTHH:MM:SS.mmmZ` (24 ASCII
         *          characters, zero-padded, no locale). The sub-millisecond
         *          part is truncated toward negative infinity, never rounded:
         *          an instant at 23:59:59.999999Z formats as
         *          `...T23:59:59.999Z`. Formatting is timezone-independent: it
         *          never consults the local time zone, the `TZ` environment
         *          variable, or the C locale.
         *
         * @param tp Wall-clock time to format (Unix time; see the class
         *           @details).
         *
         * @return `Ok(text)` with the 24-character UTC timestamp;
         *         `Failure(InvalidArgument)` when @p tp is before the Unix epoch
         *         (rejected uniformly on every platform, mirroring the MSVC
         *         calendar lower bound), when it does not fit the platform
         *         `time_t` range, when the platform calendar converter rejects
         *         it, or when the resulting year is outside `0000`–`9999`.
         *
         * @exception Never throws (may allocate).
         *
         * @sideeffect None; read-only. Reentrant: a caller-local `std::tm` is
         *             used, never a shared static buffer.
         *
         * @platform Windows, Linux, macOS.
         */
        [[nodiscard]] static auto format_iso8601_utc(std::chrono::system_clock::time_point tp)
            -> pjh::result::Result<std::string, ErrorCode>;
    };

}  // namespace pjh::platform

#endif  // INCLUDE_PJH_PLATFORM_CLOCK_HPP
