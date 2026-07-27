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

    class Encoding
    {
        Encoding() = delete;

    public:
        static auto to_wide(std::string_view utf8) -> std::wstring;
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
        int len = MultiByteToWideChar(
            CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
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
                cp |= static_cast<char32_t>(static_cast<unsigned char>(*(it + 1)) & 0x3F)
                      << 6;
                cp |= static_cast<char32_t>(static_cast<unsigned char>(*(it + 2)) & 0x3F);
                it += 3;
            }
            else if ((c & 0xF8) == 0xF0)
            {
                if (end - it < 4)
                    break;
                cp = static_cast<char32_t>(c & 0x07) << 18;
                cp |= static_cast<char32_t>(static_cast<unsigned char>(*(it + 1)) & 0x3F)
                      << 12;
                cp |= static_cast<char32_t>(static_cast<unsigned char>(*(it + 2)) & 0x3F)
                      << 6;
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
            CP_UTF8, 0, wsv.data(), static_cast<int>(wsv.size()), nullptr, 0, nullptr,
            nullptr);
        if (len <= 0)
            return {};
        std::string result(static_cast<std::size_t>(len), '\0');
        WideCharToMultiByte(
            CP_UTF8, 0, wsv.data(), static_cast<int>(wsv.size()), result.data(), len,
            nullptr, nullptr);
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
