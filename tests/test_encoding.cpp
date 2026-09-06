#include <doctest/doctest.h>

#include <iostream>
#include <pjh_platform/encoding.hpp>

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
// Pins on malformed / out-of-range input. Pins marked "reality anchor"
// assert undocumented non-conformant behavior (gap ids G1-G4); they may
// flip only through a deliberate fix commit that updates them.

TEST_CASE("Encoding::to_wide skips invalid lead bytes")
{
    // Documented contract (header @details): "skips malformed bytes".
    CHECK(enc::to_wide("\x80").empty());
    CHECK(enc::to_wide("\xFF").empty());
    CHECK(enc::to_wide("\x80\xFF\x81").empty());
}

TEST_CASE("Encoding::to_wide decodes an overlong 2-byte sequence to NUL")
{
#if PJH_PLATFORM_WINDOWS
    // Contract pin: header @details — empty when input is not valid UTF-8.
    CHECK(enc::to_wide("\xC0\x80").empty());
#else
    // Reality anchor (gap G1): no overlong check; 0xC0 0x80 decodes to U+0000.
    auto w = enc::to_wide("\xC0\x80");
    REQUIRE_EQ(w.size(), 1u);
    CHECK_EQ(static_cast<char32_t>(w[0]), 0u);
#endif
}

TEST_CASE("Encoding::to_wide embeds an overlong NUL mid-string and continues")
{
    // Input bytes 'A', 0xC0, 0x80, 'B'; the split literal keeps the \x80
    // escape from swallowing the trailing 'B' as a hex digit.
#if PJH_PLATFORM_WINDOWS
    CHECK(
        enc::to_wide(
            "A\xC0\x80"
            "B")
            .empty());
#else
    // Reality anchor (gap G1): the NUL is embedded and decoding continues
    // with 'B' — the string carries an interior NUL (c_str() truncation).
    auto w = enc::to_wide(
        "A\xC0\x80"
        "B");
    REQUIRE_EQ(w.size(), 3u);
    CHECK_EQ(static_cast<char32_t>(w[0]), static_cast<char32_t>('A'));
    CHECK_EQ(static_cast<char32_t>(w[1]), 0u);
    CHECK_EQ(static_cast<char32_t>(w[2]), static_cast<char32_t>('B'));
#endif
}

TEST_CASE("Encoding::to_wide decodes overlong 3/4-byte sequences to NUL")
{
#if PJH_PLATFORM_WINDOWS
    CHECK(enc::to_wide("\xE0\x80\x80").empty());
    CHECK(enc::to_wide("\xF0\x80\x80\x80").empty());
#else
    // Reality anchor (gap G1): 0xE0 0x80 0x80 and 0xF0 0x80 0x80 0x80 both
    // decode to U+0000 (no overlong check in the 3/4-byte branches).
    auto w3 = enc::to_wide("\xE0\x80\x80");
    REQUIRE_EQ(w3.size(), 1u);
    CHECK_EQ(static_cast<char32_t>(w3[0]), 0u);
    auto w4 = enc::to_wide("\xF0\x80\x80\x80");
    REQUIRE_EQ(w4.size(), 1u);
    CHECK_EQ(static_cast<char32_t>(w4[0]), 0u);
#endif
}

TEST_CASE("Encoding::to_wide accepts an out-of-range 4-byte code point")
{
#if PJH_PLATFORM_WINDOWS
    CHECK(enc::to_wide("\xF5\x80\x80\x80").empty());
#else
    // Reality anchor (gap G2a): 0xF5 lead decodes to cp 0x140000 (> U+10FFFF)
    // and is pushed as-is; the 4-byte branch has no cp upper bound.
    auto w = enc::to_wide("\xF5\x80\x80\x80");
    REQUIRE_EQ(w.size(), 1u);
    CHECK_EQ(static_cast<char32_t>(w[0]), 0x140000u);
#endif
}

TEST_CASE("Encoding::to_wide accepts a 3-byte-encoded surrogate")
{
#if PJH_PLATFORM_WINDOWS
    CHECK(enc::to_wide("\xED\xB0\x80").empty());
#else
    // Reality anchor (gap G2b): U+DC00, a surrogate, in its canonical
    // 3-byte form ED B0 80 is not rejected; the decoder has no surrogate
    // exclusion.
    auto w = enc::to_wide("\xED\xB0\x80");
    REQUIRE_EQ(w.size(), 1u);
    CHECK_EQ(static_cast<char32_t>(w[0]), 0xDC00u);
#endif
}

TEST_CASE("Encoding::to_wide stops early on a truncated sequence after a valid prefix")
{
#if PJH_PLATFORM_WINDOWS
    // Contract pin: header @details — empty when input is not valid UTF-8.
    CHECK(enc::to_wide("A\xE4").empty());
#else
    // Documented contract (header @details): "stopping early on truncated
    // sequences" — the decoded prefix is kept.
    auto w = enc::to_wide("A\xE4");
    REQUIRE_EQ(w.size(), 1u);
    CHECK_EQ(static_cast<char32_t>(w[0]), static_cast<char32_t>('A'));
#endif
}

TEST_CASE("Encoding::to_wide yields empty for a truncated sequence with no valid prefix")
{
    // Documented contract: truncated sequences stop early; nothing was
    // decoded before the truncation, so the result is empty on every lane.
    CHECK(enc::to_wide("\xE4\xB8").empty());
    CHECK(enc::to_wide("\xC0").empty());
    CHECK(enc::to_wide("\xF0\x9F").empty());
}

TEST_CASE("Encoding::to_wide does not validate trailing bytes as continuations")
{
#if PJH_PLATFORM_WINDOWS
    CHECK(enc::to_wide("\xC2\x41").empty());
#else
    // Reality anchor (gap G3): 0x41 is not a continuation byte, but the
    // decoder masks it with 0x3F and accepts it: cp = (0x02 << 6) | 0x01
    // = 0x81.
    auto w = enc::to_wide("\xC2\x41");
    REQUIRE_EQ(w.size(), 1u);
    CHECK_EQ(static_cast<char32_t>(w[0]), 0x81u);
#endif
}

TEST_CASE("Encoding::to_utf8 emits 3-byte sequences for lone surrogates")
{
#if PJH_PLATFORM_WINDOWS
    // Contract pin: header @return — empty on Windows for invalid UTF-16.
    CHECK(enc::to_utf8(std::wstring(1, 0xD800)).empty());
    CHECK(enc::to_utf8(std::wstring(1, 0xDFFF)).empty());
#else
    // Reality anchor (gap G4a): no surrogate range check; U+D800/U+DFFF get
    // their well-formed 3-byte encodings, which are invalid UTF-8 (surrogates
    // must never be encoded).
    auto lo = enc::to_utf8(std::wstring(1, 0xD800));
    CHECK_EQ(lo, std::string("\xED\xA0\x80", 3));
    auto hi = enc::to_utf8(std::wstring(1, 0xDFFF));
    CHECK_EQ(hi, std::string("\xED\xBF\xBF", 3));
#endif
}

#if PJH_PLATFORM_UNIX
TEST_CASE("Encoding::to_utf8 emits malformed UTF-8 for code points at or above 0x110000")
{
    // Reality anchor (gap G4b/G4c): the 4-byte branch has no cp <= U+10FFFF
    // check. 0x110000 -> well-formed 4-byte shape of an out-of-range cp;
    // 0x200000 -> cp >> 18 == 8, lead byte 0xF8 (a 5-byte start, invalid).
    auto shifted = enc::to_utf8(std::wstring(1, static_cast<wchar_t>(0x110000)));
    CHECK_EQ(shifted, std::string("\xF4\x90\x80\x80", 4));
    auto lead = enc::to_utf8(std::wstring(1, static_cast<wchar_t>(0x200000)));
    CHECK_EQ(lead, std::string("\xF8\x80\x80\x80", 4));
}
#endif
