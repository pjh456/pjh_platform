#ifndef TESTS_DOCTEST_FILE_TIME_HPP
#define TESTS_DOCTEST_FILE_TIME_HPP

#include <doctest/doctest.h>

#include <chrono>
#include <filesystem>

/**
 * @file doctest_file_time.hpp
 * @brief doctest stringification for `std::filesystem::file_time_type`.
 *
 * @details doctest renders comparison operands through their `operator<<`.
 *          On libc++ the duration behind `file_time_type` uses a 128-bit
 *          `rep` (`__int128`), for which no unique `std::ostream` overload
 *          exists, so the default stringification fails to compile. These
 *          specializations render the raw clock tick count as a 64-bit
 *          integer instead, which is streamable on every standard library.
 *          The text is used only for failure diagnostics; the assertion
 *          itself still compares the untouched values.
 */

namespace doctest
{

    /// @brief Streams a native filesystem timestamp as its raw clock ticks.
    template <>
    struct StringMaker<std::filesystem::file_time_type>
    {
        static auto convert(const std::filesystem::file_time_type &value) -> String
        {
            return toString(static_cast<long long>(value.time_since_epoch().count()));
        }
    };

    /// @brief Streams a filesystem timestamp duration as its raw clock ticks.
    template <>
    struct StringMaker<std::filesystem::file_time_type::duration>
    {
        static auto convert(const std::filesystem::file_time_type::duration &value) -> String
        {
            return toString(static_cast<long long>(value.count()));
        }
    };

}  // namespace doctest

#endif  // TESTS_DOCTEST_FILE_TIME_HPP
