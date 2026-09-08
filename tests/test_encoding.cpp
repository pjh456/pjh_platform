#include <doctest/doctest.h>

#include <iostream>
#include <pjh_platform/encoding.hpp>
#include <pjh_platform/platform.hpp>

using enc = pjh::platform::Encoding;

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
    CHECK(
        enc::to_wide(
            "A\xC0\x80"
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
