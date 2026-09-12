#ifndef INCLUDE_PJH_PLATFORM_ENCODING_HPP
#define INCLUDE_PJH_PLATFORM_ENCODING_HPP

#include <cstddef>
#include <string>
#include <string_view>

namespace pjh::platform
{

    /// @brief UTF-8 / wide-string conversion and terminal display-width helpers.
    ///
    /// @details Used internally to bridge the wide-char platform APIs (the
    ///          `Env` module on Windows), and by text-UI consumers to measure
    ///          UTF-8 text in terminal columns. The library treats UTF-8 as its
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
         *          UTF-16. POSIX: a manual UTF-8 to UTF-32 decoder. Both
         *          return an empty string when the input is not valid UTF-8;
         *          the POSIX decoder rejects malformed UTF-8 (invalid lead
         *          byte, non-continuation byte, truncation, overlong form,
         *          surrogate, or code point above U+10FFFF) exactly like the
         *          Windows path.
         *
         * @param utf8 UTF-8 encoded input.
         *
         * @return Wide string, or empty when @p utf8 is empty or not valid
         *         UTF-8.
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
         *          to UTF-8 encoder. Both return an empty string for invalid
         *          wide input: a lone surrogate or a code point above
         *          U+10FFFF.
         *
         * @param wsv Wide input.
         *
         * @return UTF-8 string, or empty when @p wsv is empty or not valid
         *         UTF-16.
         *
         * @exception Never throws.
         *
         * @sideeffect None.
         *
         * @platform All supported platforms.
         */
        static auto to_utf8(std::wstring_view wsv) -> std::string;

        /**
         * @brief Computes the number of terminal columns needed to display @p utf8.
         *
         * @details Pure logic: no locale, font, or terminal is consulted, so the
         *          result is byte-for-byte identical on every platform. Code
         *          points are classified as follows:
         *          - East Asian Wide (W) and Fullwidth (F) code points per
         *            UAX #11 (`EastAsianWidth.txt`; e.g. CJK ideographs,
         *            fullwidth forms, wide emoji): 2 columns.
         *          - Combining marks (Unicode general categories `Mn` / `Me`)
         *            and zero-width format characters (`Cf`, e.g. ZWJ U+200D,
         *            ZWSP U+200B, BOM U+FEFF, variation selectors): 0 columns.
         *          - C0/C1 control characters (`U+0000`-`U+001F`,
         *            `U+007F`-`U+009F`): 0 columns, except tab (`U+0009`) which
         *            counts as 1. Tab stops are not expanded; callers that need
         *            tab-stop alignment must expand tabs before measuring.
         *          - Every malformed UTF-8 byte counts as 1 column, and decoding
         *            resyncs at the next byte (see `@return`).
         *          - Every other code point, including East Asian Ambiguous:
         *            1 column.
         *
         *          This measures display columns, not bytes or code points, and
         *          does not perform grapheme-cluster segmentation, bidirectional
         *          reordering, or tab-stop expansion. Emoji ZWJ sequences are
         *          therefore counted per code point (best effort).
         *
         * @param utf8 UTF-8 encoded input.
         *
         * @return Sum of the per-code-point column widths; `0` for empty input.
         *         A malformed sequence contributes 1 per malformed byte rather
         *         than failing the whole measurement.
         *
         * @exception Never throws.
         *
         * @sideeffect None.
         *
         * @platform All supported platforms. Pure logic, no platform branch.
         */
        [[nodiscard]] static auto display_width(std::string_view utf8) -> std::size_t;

        /**
         * @brief Truncates @p utf8 to the longest prefix that fits in
         *        @p max_width terminal columns.
         *
         * @details The result is the longest leading sequence of whole UTF-8
         *          code points whose display_width() is at most @p max_width. A
         *          double-width code point that would cross the limit is dropped
         *          whole; any combining marks that trail it are dropped with it,
         *          so a combining mark is never left dangling. Multi-byte
         *          sequences are never split, and a malformed byte is treated as
         *          a one-byte, one-column unit. No ellipsis is appended; callers
         *          add their own.
         *
         *          The returned view aliases @p utf8 (zero-copy) and stays valid
         *          only while @p utf8 is alive and unmodified; callers that need
         *          an owning string must copy it explicitly.
         *
         *          Signature note: `detail/50` and `detail/51` drafted a
         *          `std::string` return. The frozen form is this zero-copy
         *          `std::string_view` so a truncation performs no allocation and
         *          no copy.
         *
         * @param utf8 UTF-8 encoded input.
         * @param max_width Maximum number of display columns to keep.
         *
         * @return Longest prefix with display width at most @p max_width; an
         *         empty view when @p max_width is `0` or the first code point
         *         does not fit.
         *
         * @exception Never throws.
         *
         * @sideeffect None.
         *
         * @platform All supported platforms. Pure logic, no platform branch.
         */
        [[nodiscard]] static auto truncate_to_width(std::string_view utf8, std::size_t max_width)
            -> std::string_view;
    };

}  // namespace pjh::platform

#endif  // INCLUDE_PJH_PLATFORM_ENCODING_HPP
