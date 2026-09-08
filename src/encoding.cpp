#include <pjh_platform/encoding.hpp>
#include <pjh_platform/platform.hpp>

#if PJH_PLATFORM_WINDOWS
#include <windows.h>
#endif

#include <cstdint>

namespace pjh::platform
{

    // ── UTF-8 → wstring ────────────────────────────────────────────────────
    // On Windows:  wchar_t = 2 bytes (UTF-16)
    // On POSIX:    wchar_t = 4 bytes (UTF-32)

    auto Encoding::to_wide(std::string_view utf8) -> std::wstring
    {
        if (utf8.empty())
            return {};

#if PJH_PLATFORM_WINDOWS
        // MB_ERR_INVALID_CHARS: invalid input fails (returns 0) instead of
        // being replaced with U+FFFD, so the contract's empty branch is
        // reachable for it (header @details).
        int len = MultiByteToWideChar(
            CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
        if (len <= 0)
            return {};
        std::wstring result(static_cast<std::size_t>(len), L'\0');
        MultiByteToWideChar(
            CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), static_cast<int>(utf8.size()),
            result.data(), len);
        return result;
#else
        // Strict UTF-8 validation, mirroring the Windows MB_ERR_INVALID_CHARS
        // contract: any malformed sequence fails the whole conversion and
        // yields an empty string instead of being skipped or replaced.
        std::wstring result;
        result.reserve(utf8.size());
        const auto *p = reinterpret_cast<const unsigned char *>(utf8.data());
        const auto *end = p + utf8.size();
        while (p != end)
        {
            const unsigned char lead = *p;
            char32_t cp = 0;
            std::size_t len = 0;

            if (lead < 0x80)
            {
                cp = static_cast<char32_t>(lead);
                len = 1;
            }
            else if (lead >= 0xC2 && lead <= 0xDF)
            {
                cp = static_cast<char32_t>(lead & 0x1F);
                len = 2;
            }
            else if (lead >= 0xE0 && lead <= 0xEF)
            {
                cp = static_cast<char32_t>(lead & 0x0F);
                len = 3;
            }
            else if (lead >= 0xF0 && lead <= 0xF4)
            {
                cp = static_cast<char32_t>(lead & 0x07);
                len = 4;
            }
            else
            {
                return {};
            }

            if (static_cast<std::size_t>(end - p) < len)
                return {};

            for (std::size_t k = 1; k < len; ++k)
            {
                const unsigned char cont = p[k];
                if ((cont & 0xC0) != 0x80)
                    return {};
                cp = static_cast<char32_t>((cp << 6) | (cont & 0x3F));
            }

            if ((len == 2 && cp < 0x80) || (len == 3 && cp < 0x800) || (len == 4 && cp < 0x10000))
                return {};
            if (cp >= 0xD800 && cp <= 0xDFFF)
                return {};
            if (cp > 0x10FFFF)
                return {};

            result.push_back(static_cast<wchar_t>(cp));
            p += len;
        }
        return result;
#endif
    }

    // ── wstring → UTF-8 ────────────────────────────────────────────────────

    auto Encoding::to_utf8(std::wstring_view wsv) -> std::string
    {
        if (wsv.empty())
            return {};

#if PJH_PLATFORM_WINDOWS
        // WC_ERR_INVALID_CHARS: invalid UTF-16 (lone surrogates) fails
        // (returns 0) instead of being encoded as replacement text, so the
        // contract's empty branch is reachable (header @return).
        int len = WideCharToMultiByte(
            CP_UTF8, WC_ERR_INVALID_CHARS, wsv.data(), static_cast<int>(wsv.size()), nullptr, 0,
            nullptr, nullptr);
        if (len <= 0)
            return {};
        std::string result(static_cast<std::size_t>(len), '\0');
        WideCharToMultiByte(
            CP_UTF8, WC_ERR_INVALID_CHARS, wsv.data(), static_cast<int>(wsv.size()), result.data(),
            len, nullptr, nullptr);
        return result;
#else
        // Strict range validation, mirroring the Windows WC_ERR_INVALID_CHARS
        // contract: lone surrogates and code points above U+10FFFF fail the
        // whole conversion and yield an empty string.
        std::string result;
        result.reserve(wsv.size() * 3);
        for (wchar_t wc : wsv)
        {
            auto cp = static_cast<char32_t>(wc);

            if ((cp >= 0xD800 && cp <= 0xDFFF) || cp > 0x10FFFF)
                return {};

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
