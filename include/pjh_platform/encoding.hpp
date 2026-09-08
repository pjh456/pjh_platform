#ifndef INCLUDE_PJH_PLATFORM_ENCODING_HPP
#define INCLUDE_PJH_PLATFORM_ENCODING_HPP

#include <string>
#include <string_view>

namespace pjh::platform
{

    /// @brief UTF-8 and wide-string conversion helpers.
    ///
    /// @details Used internally to bridge the wide-char platform APIs (the
    ///          `Env` module on Windows). The library treats UTF-8 as its
    ///          canonical string encoding.
    ///
    /// @platform Windows, Linux, macOS. The underlying representation differs:
    ///          on Windows `wchar_t` is 16-bit UTF-16; on POSIX it is 32-bit
    ///          UTF-32.
    class Encoding
    {
        Encoding() = delete;

    public:
        /**
         * @brief Converts UTF-8 encoded bytes to a wide string.
         *
         * @details Windows: uses `MultiByteToWideChar(CP_UTF8)`, producing
         *          UTF-16. POSIX: a manual UTF-8 to UTF-32 decoder. Windows
         *          returns an empty string when the input is not valid UTF-8;
         *          POSIX is lenient and skips malformed bytes, stopping early
         *          on truncated sequences.
         *
         * @param utf8 UTF-8 encoded input.
         *
         * @return Wide string, or empty when @p utf8 is empty or (on Windows)
         *         not valid UTF-8.
         *
         * @exception Never throws.
         *
         * @sideeffect None.
         *
         * @platform All supported platforms.
         */
        static auto to_wide(std::string_view utf8) -> std::wstring;

        /**
         * @brief Converts a wide string to UTF-8 encoded bytes.
         *
         * @details Windows: uses `WideCharToMultiByte(CP_UTF8)`, consuming
         *          UTF-16 (including surrogate pairs). POSIX: a manual UTF-32
         *          to UTF-8 encoder.
         *
         * @param wsv Wide input.
         *
         * @return UTF-8 string, or empty when @p wsv is empty or (on Windows)
         *         not valid UTF-16.
         *
         * @exception Never throws.
         *
         * @sideeffect None.
         *
         * @platform All supported platforms.
         */
        static auto to_utf8(std::wstring_view wsv) -> std::string;
    };

}  // namespace pjh::platform

#endif  // INCLUDE_PJH_PLATFORM_ENCODING_HPP
