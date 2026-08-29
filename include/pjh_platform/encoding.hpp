#ifndef INCLUDE_PJH_PLATFORM_ENCODING_HPP
#define INCLUDE_PJH_PLATFORM_ENCODING_HPP

#include <cstdint>
#include <pjh_platform/platform.hpp>
#include <string>
#include <string_view>

#if PJH_PLATFORM_WINDOWS
#include <windows.h>
#endif

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

    // ── UTF-8 → wstring ────────────────────────────────────────────────────
    // On Windows:  wchar_t = 2 bytes (UTF-16)
    // On POSIX:    wchar_t = 4 bytes (UTF-32)

    inline auto Encoding::to_wide(std::string_view utf8) -> std::wstring
    {
        if (utf8.empty())
            return {};

#if PJH_PLATFORM_WINDOWS
        int len =
            MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
        if (len <= 0)
            return {};
        std::wstring result(static_cast<std::size_t>(len), L'\0');
        MultiByteToWideChar(
            CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), result.data(), len);
        return result;
#else
        std::wstring result;
        result.reserve(utf8.size());
        auto it = utf8.begin();
        auto end = utf8.end();
        while (it != end)
        {
            auto c = static_cast<unsigned char>(*it);
            char32_t cp;

            if (c < 0x80)
            {
                cp = static_cast<char32_t>(c);
                ++it;
            }
            else if ((c & 0xE0) == 0xC0)
            {
                if (end - it < 2)
                    break;
                cp = static_cast<char32_t>(c & 0x1F) << 6;
                cp |= static_cast<char32_t>(static_cast<unsigned char>(*(it + 1)) & 0x3F);
                it += 2;
            }
            else if ((c & 0xF0) == 0xE0)
            {
                if (end - it < 3)
                    break;
                cp = static_cast<char32_t>(c & 0x0F) << 12;
                cp |= static_cast<char32_t>(static_cast<unsigned char>(*(it + 1)) & 0x3F) << 6;
                cp |= static_cast<char32_t>(static_cast<unsigned char>(*(it + 2)) & 0x3F);
                it += 3;
            }
            else if ((c & 0xF8) == 0xF0)
            {
                if (end - it < 4)
                    break;
                cp = static_cast<char32_t>(c & 0x07) << 18;
                cp |= static_cast<char32_t>(static_cast<unsigned char>(*(it + 1)) & 0x3F) << 12;
                cp |= static_cast<char32_t>(static_cast<unsigned char>(*(it + 2)) & 0x3F) << 6;
                cp |= static_cast<char32_t>(static_cast<unsigned char>(*(it + 3)) & 0x3F);
                it += 4;
            }
            else
            {
                ++it;
                continue;
            }

            result.push_back(static_cast<wchar_t>(cp));
        }
        return result;
#endif
    }

    // ── wstring → UTF-8 ────────────────────────────────────────────────────

    inline auto Encoding::to_utf8(std::wstring_view wsv) -> std::string
    {
        if (wsv.empty())
            return {};

#if PJH_PLATFORM_WINDOWS
        int len = WideCharToMultiByte(
            CP_UTF8, 0, wsv.data(), static_cast<int>(wsv.size()), nullptr, 0, nullptr, nullptr);
        if (len <= 0)
            return {};
        std::string result(static_cast<std::size_t>(len), '\0');
        WideCharToMultiByte(
            CP_UTF8, 0, wsv.data(), static_cast<int>(wsv.size()), result.data(), len, nullptr,
            nullptr);
        return result;
#else
        std::string result;
        result.reserve(wsv.size() * 3);
        for (wchar_t wc : wsv)
        {
            auto cp = static_cast<char32_t>(wc);

            if (cp < 0x80)
            {
                result.push_back(static_cast<char>(cp));
            }
            else if (cp < 0x800)
            {
                result.push_back(static_cast<char>(0xC0 | (cp >> 6)));
                result.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            }
            else if (cp < 0x10000)
            {
                result.push_back(static_cast<char>(0xE0 | (cp >> 12)));
                result.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                result.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            }
            else
            {
                result.push_back(static_cast<char>(0xF0 | (cp >> 18)));
                result.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
                result.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                result.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            }
        }
        return result;
#endif
    }

}  // namespace pjh::platform

#endif  // INCLUDE_PJH_PLATFORM_ENCODING_HPP
