#include <doctest/doctest.h>

#include <pjh_platform/env.hpp>
#include <pjh_platform/platform.hpp>

using pjh::platform::Env;
using pjh::platform::ErrorCode;

TEST_CASE("Env::get returns not_found for non-existent variable")
{
    auto val = Env::get("__NONEXISTENT_VAR_12345__");
    CHECK(val.is_err());
    // task 33 pin: doc env.hpp:32-33 promises Failure(NotFound)
    CHECK_EQ(val.unwrap_err(), ErrorCode::NotFound);
}

TEST_CASE("Env::set and Env::get round-trip")
{
    auto r = Env::set("__TEST_PJH_VAR__", "hello_world");
    CHECK(r.is_ok());

    auto val = Env::get("__TEST_PJH_VAR__");
    REQUIRE(val.is_ok());
    CHECK_EQ(val.unwrap(), "hello_world");
}

TEST_CASE("Env::unset removes variable")
{
    (void)Env::set("__TEST_PJH_VAR__", "temp");
    auto val = Env::get("__TEST_PJH_VAR__");
    REQUIRE(val.is_ok());

    auto r = Env::unset("__TEST_PJH_VAR__");
    CHECK(r.is_ok());

    val = Env::get("__TEST_PJH_VAR__");
    CHECK(val.is_err());
    // task 33 pin: doc env.hpp:32-33 promises Failure(NotFound)
    CHECK_EQ(val.unwrap_err(), ErrorCode::NotFound);
}

TEST_CASE("Env::snapshot contains expected variables")
{
    (void)Env::set("__TEST_PJH_SNAP__", "snap_value");

    auto snap = Env::snapshot();
    CHECK_GE(snap.size(), 1);

    auto it = snap.find("__TEST_PJH_SNAP__");
    REQUIRE(it != snap.end());
    CHECK_EQ(it->second, "snap_value");
}

TEST_CASE("Env::list returns all environment variables")
{
    (void)Env::set("__TEST_PJH_LIST__", "list_value");

    auto entries = Env::list();
    CHECK_GE(entries.size(), 1);

    bool found = false;
    for (const auto &[key, val] : entries)
    {
        if (key == "__TEST_PJH_LIST__")
        {
            CHECK_EQ(val, "list_value");
            found = true;
        }
    }
    CHECK(found);
}

// ── Task 33 pins ─────────────────────────────────────────────────────────
// Deepen shallow is_err() assertions to exact codes and pin documented
// Env behaviors (env.hpp clauses cited per case). Pure logic: process
// environment only, no files, no timing. Keys are unique per case and
// defensively unset before and after.

TEST_CASE("Env::set with an empty value round-trips as an empty string")
{
    // Contract pin (task 33): env.hpp:55-56 — "setenv with overwrite
    // enabled, so an existing variable is replaced". Sharp half: an empty
    // value is stored, not conflated with absent — the Windows get query
    // returns len==1 (the terminator) for an empty value, never len==0
    // (env.cpp:56-62); POSIX getenv returns "" not NULL.
    constexpr std::string_view k = "__PJH_EMPTY_RT__";
    (void)Env::unset(k);                   // defensive: stale scratch
    REQUIRE(Env::set(k, "seed").is_ok());  // S1: pre-existing variable
    REQUIRE(Env::set(k, "").is_ok());      // S2: overwrite with empty
    auto v1 = Env::get(k);
    REQUIRE(v1.is_ok());                    // A1: empty is not NotFound
    CHECK_EQ(v1.unwrap(), "");              // A2 = THE PIN (empty round-trip)
    REQUIRE(Env::set(k, "after").is_ok());  // S3: overwrite again
    auto v2 = Env::get(k);
    REQUIRE(v2.is_ok());             // A3
    CHECK_EQ(v2.unwrap(), "after");  // A4: overwrite semantics hold
    (void)Env::unset(k);
}

TEST_CASE("Env::unset succeeds for a non-existent variable")
{
    // Contract pin (task 33): env.hpp:78-79 — "Removing a variable that
    // does not exist succeeds". POSIX unsetenv returns 0 for a missing
    // name; Windows SetEnvironmentVariableW(name, NULL) is a no-op
    // success for a missing name. If a Windows lane ever reports FALSE
    // (=> IoError at env.cpp:94-95), A1 flips red and is a CONTRACT
    // finding (doc over-promise vs code mapping) — escalate to the
    // orchestrator; do not edit this test to match.
    constexpr std::string_view k = "__PJH_UNSET_MISS__";
    (void)Env::unset(k);  // ensure absence
    auto r = Env::unset(k);
    CHECK(r.is_ok());  // A1 = THE PIN
    auto v = Env::get(k);
    CHECK(v.is_err());                              // A2: still absent
    CHECK_EQ(v.unwrap_err(), ErrorCode::NotFound);  // A3: exact code
}

#if PJH_PLATFORM_UNIX
TEST_CASE("Env::get is case-sensitive (POSIX)")
{
    // Contract pin (task 33): env.hpp:33-34 — "on POSIX it is
    // case-sensitive" (getenv matches names byte-exactly).
    constexpr std::string_view k = "__PJH_CASE_A__";
    constexpr std::string_view kAlt = "__pjh_case_a__";
    (void)Env::unset(k);  // defensive: both spellings
    (void)Env::unset(kAlt);
    REQUIRE(Env::set(k, "v").is_ok());  // S1
    auto exact = Env::get(k);
    REQUIRE(exact.is_ok());         // A1 positive control
    CHECK_EQ(exact.unwrap(), "v");  // A2
    auto folded = Env::get(kAlt);
    CHECK(folded.is_err());                              // A3
    CHECK_EQ(folded.unwrap_err(), ErrorCode::NotFound);  // A4 = THE PIN
    (void)Env::unset(k);
}
#endif

#if PJH_PLATFORM_WINDOWS
TEST_CASE("Env::get is case-insensitive (Windows)")
{
    // Contract pin (task 33): env.hpp:33-34 — "On Windows the lookup is
    // case-insensitive" (GetEnvironmentVariableW folds case).
    constexpr std::string_view k = "__PJH_CASE_A__";
    constexpr std::string_view kAlt = "__pjh_case_a__";
    (void)Env::unset(k);                // defensive: stale scratch
    REQUIRE(Env::set(k, "v").is_ok());  // S1
    auto exact = Env::get(k);
    REQUIRE(exact.is_ok());         // A1
    CHECK_EQ(exact.unwrap(), "v");  // A2
    auto folded = Env::get(kAlt);
    REQUIRE(folded.is_ok());         // A3 = THE PIN (case folded)
    CHECK_EQ(folded.unwrap(), "v");  // A4: value intact
    (void)Env::unset(k);
}
#endif

TEST_CASE("Env::set truncates a value at an embedded NUL")
{
    // Reality anchor (task 33, pin discipline per task 27): the platform
    // boundary is NUL-terminated (setenv / SetEnvironmentVariableW take C
    // strings), so a std::string carrying an interior NUL is truncated at
    // the first NUL on storage. The header self-describes the
    // null-terminated buffer for the NAME (env.hpp:30-32); the value side
    // is undocumented, hence a reality anchor — it may flip only through
    // a deliberate boundary-changing commit that updates this pin.
    //
    // Lane-invariant: POSIX truncates in c_str(); Windows to_wide decodes
    // the full byte range (MultiByteToWideChar with explicit size,
    // encoding.hpp:83-90; U+0000 is valid single-byte UTF-8) and c_str()
    // truncates at the first L'\0' — both store the same prefix.
    constexpr std::string_view k = "__PJH_NUL_VAL__";
    (void)Env::unset(k);                  // defensive: stale scratch
    std::string value("abc\0def", 7);     // (char*, size) ctor keeps NUL
    REQUIRE_EQ(value.size(), 7u);         // S0: guard the literal
    REQUIRE(value.find('\0') == 3u);      // S1: NUL position
    REQUIRE(Env::set(k, value).is_ok());  // S2
    auto v = Env::get(k);
    REQUIRE(v.is_ok());               // A1
    CHECK_EQ(v.unwrap(), "abc");      // A2 = THE PIN (first-NUL cut)
    CHECK_EQ(v.unwrap().size(), 3u);  // A3: exact prefix length
    // Name side (the documented null-terminated-buffer clause):
    constexpr std::string_view kn = "__PJH_NUL_NAME__";
    (void)Env::unset(kn);
    REQUIRE(Env::set(kn, "anchor").is_ok());         // S3
    std::string probe("__PJH_NUL_NAME__\0ZZZ", 20);  // (char*, size) ctor
    REQUIRE_EQ(probe.size(), 20u);                   // S4: guard the literal
    auto n = Env::get(probe);
    REQUIRE(n.is_ok());              // A4
    CHECK_EQ(n.unwrap(), "anchor");  // A5: NUL-terminated name lookup
    (void)Env::unset(kn);
}
