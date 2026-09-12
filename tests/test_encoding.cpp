#include <doctest/doctest.h>

#include <iostream>
#include <pjh_platform/encoding.hpp>
#include <pjh_platform/platform.hpp>
#include <string>
#include <string_view>

using enc = pjh::platform::Encoding;

namespace
{
    // Byte-exact UTF-8 samples kept as \x escapes so this file stays ASCII.
    constexpr std::string_view kZh = "\xE4\xB8\xAD\xE6\x96\x87";  // 中文
    constexpr std::string_view kZhong = "\xE4\xB8\xAD";           // 中
    constexpr std::string_view kZhAbc =
        "\xE4\xB8\xAD\xE6\x96\x87"
        "abc";
    constexpr std::string_view kZhA =
        "\xE4\xB8\xAD\xE6\x96\x87"
        "a";
    constexpr std::string_view kAbZhong = "ab\xE4\xB8\xAD";
    constexpr std::string_view kAbZhongCd =
        "ab\xE4\xB8\xAD"
        "cd";
    constexpr std::string_view kAcute = "\xCC\x81";    // U+0301 combining acute
    constexpr std::string_view kEAcute = "e\xCC\x81";  // e + U+0301
    constexpr std::string_view kEAcuteX =
        "e\xCC\x81"
        "x";
    constexpr std::string_view kZhongAcute = "\xE4\xB8\xAD\xCC\x81";  // 中 + U+0301
    constexpr std::string_view kZhongAcuteX =
        "\xE4\xB8\xAD\xCC\x81"
        "x";
}  // namespace

TEST_CASE("Encoding::to_wide / to_utf8 round-trip: empty string")
{
    auto w = enc::to_wide("");
    CHECK(w.empty());
    auto back = enc::to_utf8(w);
    CHECK_EQ(back, "");
}

TEST_CASE("Encoding::to_wide / to_utf8 round-trip: ASCII")
{
    auto w = enc::to_wide("Hello, world!");
    CHECK(!w.empty());
    auto back = enc::to_utf8(w);
    CHECK_EQ(back, "Hello, world!");
}

TEST_CASE("Encoding::to_wide / to_utf8 round-trip: CJK")
{
    auto w = enc::to_wide("你好世界");
    CHECK(!w.empty());
    auto back = enc::to_utf8(w);
    CHECK_EQ(back, "你好世界");
}

TEST_CASE("Encoding::to_wide / to_utf8 round-trip: emoji (supplementary plane)")
{
    auto w = enc::to_wide("\xF0\x9F\x98\x80");
    CHECK(!w.empty());
    auto back = enc::to_utf8(w);
    CHECK_EQ(back, "\xF0\x9F\x98\x80");
}

TEST_CASE("Encoding::to_wide / to_utf8 round-trip: mixed content")
{
    auto input = "Hello 你好 \xF0\x9F\x98\x80 world!";
    auto w = enc::to_wide(input);
    CHECK(!w.empty());
    auto back = enc::to_utf8(w);
    CHECK_EQ(back, input);
}

TEST_CASE("Encoding::to_wide / to_utf8 round-trip: long text")
{
    std::string input;
    input.reserve(10000);
    for (int i = 0; i < 1000; ++i) input += "ABC\xE4\xB8\xAD\xE6\x96\x87\xF0\x9F\x98\x80";

    auto w = enc::to_wide(input);
    CHECK(!w.empty());
    auto back = enc::to_utf8(w);
    CHECK_EQ(back, input);
}

TEST_CASE("Encoding::to_wide / to_utf8 round-trip: latin-1 supplement")
{
    auto input = "\xC3\xA9\xC3\xA0\xC3\xBC\xC3\xB1";
    auto w = enc::to_wide(input);
    CHECK(!w.empty());
    auto back = enc::to_utf8(w);
    CHECK_EQ(back, input);
}

TEST_CASE("Encoding::to_wide / to_utf8 round-trip: mathematical symbols (BMP)")
{
    auto input = "\xE2\x88\x9E\xE2\x88\x87\xE2\x88\xAB";
    auto w = enc::to_wide(input);
    CHECK(!w.empty());
    auto back = enc::to_utf8(w);
    CHECK_EQ(back, input);
}

// ── Error-path pins ─────────────────────────────────────────────────────
// Strict contract: invalid input yields an empty result on every platform.

TEST_CASE("Encoding::to_wide skips invalid lead bytes")
{
    // Strict contract: invalid lead bytes yield empty on every platform.
    CHECK(enc::to_wide("\x80").empty());
    CHECK(enc::to_wide("\xFF").empty());
    CHECK(enc::to_wide("\x80\xFF\x81").empty());
}

TEST_CASE("Encoding::to_wide rejects an overlong 2-byte sequence")
{
    // Strict contract: invalid input yields empty on every platform.
    CHECK(enc::to_wide("\xC0\x80").empty());
}

TEST_CASE("Encoding::to_wide rejects an overlong NUL embedded mid-string")
{
    // Input bytes 'A', 0xC0, 0x80, 'B'; the split literal keeps the \x80
    // escape from swallowing the trailing 'B' as a hex digit.
    // Strict contract: invalid input yields empty on every platform.
    CHECK(enc::to_wide("A\xC0\x80"
                       "B")
              .empty());
}

TEST_CASE("Encoding::to_wide rejects overlong 3/4-byte sequences")
{
    // Strict contract: invalid input yields empty on every platform.
    CHECK(enc::to_wide("\xE0\x80\x80").empty());
    CHECK(enc::to_wide("\xF0\x80\x80\x80").empty());
}

TEST_CASE("Encoding::to_wide rejects an out-of-range 4-byte code point")
{
    // Strict contract: invalid input yields empty on every platform.
    CHECK(enc::to_wide("\xF5\x80\x80\x80").empty());
}

TEST_CASE("Encoding::to_wide rejects a 3-byte-encoded surrogate")
{
    // Strict contract: invalid input yields empty on every platform.
    CHECK(enc::to_wide("\xED\xB0\x80").empty());
}

TEST_CASE("Encoding::to_wide rejects a truncated sequence after a valid prefix")
{
    // Strict contract: invalid input yields empty on every platform.
    CHECK(enc::to_wide("A\xE4").empty());
}

TEST_CASE("Encoding::to_wide yields empty for a truncated sequence with no valid prefix")
{
    // Strict contract: truncated sequences are rejected, so the result is
    // empty on every platform.
    CHECK(enc::to_wide("\xE4\xB8").empty());
    CHECK(enc::to_wide("\xC0").empty());
    CHECK(enc::to_wide("\xF0\x9F").empty());
}

TEST_CASE("Encoding::to_wide rejects a non-continuation trailing byte")
{
    // Strict contract: invalid input yields empty on every platform.
    CHECK(enc::to_wide("\xC2\x41").empty());
}

TEST_CASE("Encoding::to_utf8 rejects lone surrogates")
{
    // Strict contract: invalid input yields empty on every platform.
    CHECK(enc::to_utf8(std::wstring(1, 0xD800)).empty());
    CHECK(enc::to_utf8(std::wstring(1, 0xDFFF)).empty());
}

#if PJH_PLATFORM_UNIX
TEST_CASE("Encoding::to_utf8 rejects code points above U+10FFFF")
{
    // Strict contract: invalid input yields empty on every platform. The
    // UNIX guard keeps the 32-bit wchar_t literals away from MSVC, where
    // they would truncate.
    CHECK(enc::to_utf8(std::wstring(1, static_cast<wchar_t>(0x110000))).empty());
    CHECK(enc::to_utf8(std::wstring(1, static_cast<wchar_t>(0x200000))).empty());
}
#endif

// ── Display width (UAX #11) ─────────────────────────────────────────────
// Pure logic with byte-exact expectations, so the same assertions hold on
// every lane with no platform branch.

TEST_CASE("Encoding::display_width counts ASCII as one column each")
{
    CHECK_EQ(enc::display_width(""), 0u);
    CHECK_EQ(enc::display_width("abc"), 3u);
    CHECK_EQ(enc::display_width("Hello, world!"), 13u);
    CHECK_EQ(enc::display_width("~"), 1u);
}

TEST_CASE("Encoding::display_width counts East Asian wide and fullwidth as two columns")
{
    CHECK_EQ(enc::display_width(kZh), 4u);
    CHECK_EQ(enc::display_width("\xEF\xBC\xA1"), 2u);      // U+FF21 fullwidth A
    CHECK_EQ(enc::display_width("\xE3\x80\x80"), 2u);      // U+3000 ideographic space
    CHECK_EQ(enc::display_width("\xEA\xB0\x80"), 2u);      // U+AC00 hangul syllable
    CHECK_EQ(enc::display_width("\xE3\x81\x82"), 2u);      // U+3042 hiragana a
    CHECK_EQ(enc::display_width("\xF0\x9F\x98\x80"), 2u);  // U+1F600 wide emoji
}

TEST_CASE("Encoding::display_width counts combining marks and zero-width characters as zero")
{
    CHECK_EQ(enc::display_width(kAcute), 0u);
    CHECK_EQ(enc::display_width(kEAcute), 1u);
    CHECK_EQ(enc::display_width(kEAcuteX), 2u);
    CHECK_EQ(enc::display_width("\xE2\x80\x8D"), 0u);  // U+200D ZWJ
    CHECK_EQ(enc::display_width("\xE2\x80\x8B"), 0u);  // U+200B ZWSP
    CHECK_EQ(enc::display_width("\xEF\xBB\xBF"), 0u);  // U+FEFF BOM
    CHECK_EQ(enc::display_width("\xEF\xB8\x8F"), 0u);  // U+FE0F variation selector-16
}

TEST_CASE("Encoding::display_width counts tab as one column and other controls as zero")
{
    CHECK_EQ(enc::display_width("\t"), 1u);
    CHECK_EQ(enc::display_width("a\tb"), 3u);
    CHECK_EQ(enc::display_width("\x01"), 0u);
    CHECK_EQ(enc::display_width("\x1F"), 0u);
    CHECK_EQ(enc::display_width("\x7F"), 0u);
    CHECK_EQ(enc::display_width("\xC2\x80"), 0u);  // U+0080 C1 control
    CHECK_EQ(enc::display_width("\xC2\x9F"), 0u);  // U+009F C1 control
    CHECK_EQ(
        enc::display_width("a\x01"
                           "b"),
        2u);
}

TEST_CASE("Encoding::display_width treats ambiguous code points as one column")
{
    CHECK_EQ(enc::display_width("\xC2\xA1"), 1u);  // U+00A1 inverted exclamation
    CHECK_EQ(enc::display_width("\xC2\xB7"), 1u);  // U+00B7 middle dot
    CHECK_EQ(enc::display_width("\xCE\xB1"), 1u);  // U+03B1 greek alpha
    CHECK_EQ(enc::display_width("\xC3\xA9"), 1u);  // U+00E9 precomposed e acute
}

TEST_CASE("Encoding::display_width counts malformed bytes as one column each")
{
    CHECK_EQ(enc::display_width("\x80"), 1u);
    CHECK_EQ(enc::display_width("\xFF"), 1u);
    CHECK_EQ(enc::display_width("A\xE4"), 2u);             // truncated 3-byte lead
    CHECK_EQ(enc::display_width("\xED\xB0\x80"), 3u);      // 3-byte surrogate
    CHECK_EQ(enc::display_width("\xF5\x80\x80\x80"), 4u);  // out-of-range lead
    CHECK_EQ(enc::display_width("\xC0\x80"), 2u);          // overlong 2-byte form
    CHECK_EQ(enc::display_width("\xE0\x80\x80"), 3u);      // overlong 3-byte form
}

TEST_CASE("Encoding::display_width handles mixed content and long input")
{
    CHECK_EQ(
        enc::display_width("a\xE4\xB8\xAD"
                           "b"),
        4u);
    CHECK_EQ(
        enc::display_width("\xE4\xB8\xAD"
                           "e\xCC\x81"),
        3u);

    std::string input;
    input.reserve(7000);
    for (int i = 0; i < 1000; ++i)
    {
        input += kZh;
        input += 'a';
    }
    CHECK_EQ(enc::display_width(input), 5000u);
}

TEST_CASE("Encoding::truncate_to_width returns the longest fitting prefix")
{
    CHECK_EQ(enc::truncate_to_width(kZhAbc, 4), kZh);
    CHECK_EQ(enc::truncate_to_width(kZhAbc, 5), kZhA);
    CHECK_EQ(enc::truncate_to_width(kZhAbc, 3), kZhong);
    CHECK_EQ(enc::truncate_to_width(kZhAbc, 2), kZhong);
    CHECK_EQ(enc::truncate_to_width(kZhAbc, 1), std::string_view{});
    CHECK_EQ(enc::truncate_to_width(kZhAbc, 0), std::string_view{});
    CHECK_EQ(enc::truncate_to_width(kZhAbc, 100), kZhAbc);
    CHECK_EQ(enc::truncate_to_width("", 5), std::string_view{});
    CHECK_EQ(enc::truncate_to_width("abc", 2), std::string_view("ab"));
}

TEST_CASE("Encoding::truncate_to_width drops a wide code point whole at the boundary")
{
    CHECK_EQ(enc::truncate_to_width(kAbZhongCd, 4), kAbZhong);
    CHECK_EQ(enc::truncate_to_width(kAbZhongCd, 3), std::string_view("ab"));
    CHECK_EQ(enc::truncate_to_width(kAbZhongCd, 2), std::string_view("ab"));
    CHECK_EQ(enc::truncate_to_width(kAbZhongCd, 1), std::string_view("a"));

    // The prefix boundary always lands on a code point boundary.
    const auto cut = enc::truncate_to_width(kAbZhongCd, 3);
    CHECK_EQ(cut, std::string_view("ab"));
    CHECK_EQ(cut.size(), 2u);
    CHECK(!enc::to_wide(cut).empty());
}

TEST_CASE("Encoding::truncate_to_width keeps combining marks attached to a fitting base")
{
    CHECK_EQ(enc::truncate_to_width(kEAcuteX, 1), kEAcute);
    CHECK_EQ(enc::truncate_to_width(kEAcuteX, 2), kEAcuteX);
    CHECK_EQ(enc::truncate_to_width(kEAcuteX, 0), std::string_view{});

    // A dropped wide code point takes its trailing combining mark with it.
    CHECK_EQ(enc::truncate_to_width(kZhongAcuteX, 2), kZhongAcute);
    CHECK_EQ(enc::truncate_to_width(kZhongAcuteX, 1), std::string_view{});
}

TEST_CASE("Encoding::truncate_to_width handles malformed input byte by byte")
{
    CHECK_EQ(enc::truncate_to_width("A\xFF\xFF", 1), std::string_view("A"));
    CHECK_EQ(enc::truncate_to_width("A\xFF\xFF", 2), std::string_view("A\xFF"));
    CHECK_EQ(enc::truncate_to_width("A\xFF\xFF", 3), std::string_view("A\xFF\xFF"));
    CHECK_EQ(enc::truncate_to_width("\xE4\xB8", 1), std::string_view("\xE4"));
    CHECK_EQ(enc::truncate_to_width("\xE4\xB8", 2), std::string_view("\xE4\xB8"));
    CHECK_EQ(enc::truncate_to_width("\xE4\xB8", 0), std::string_view{});
}

TEST_CASE("Encoding::truncate_to_width returns a zero-copy view into the input")
{
    std::string input(kZhAbc);
    const auto cut = enc::truncate_to_width(input, 4);
    CHECK_EQ(cut, kZh);
    CHECK_EQ(cut.size(), 6u);
    CHECK(cut.data() == input.data());
}
