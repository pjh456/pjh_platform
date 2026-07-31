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
