#include <doctest/doctest.h>

#include <pjh_platform/env.hpp>
#include <pjh_platform/platform.hpp>

using pjh::platform::Env;
using pjh::platform::ErrorCode;

TEST_CASE("Env::get returns not_found for non-existent variable")
{
    auto val = Env::get("__NONEXISTENT_VAR_12345__");
    CHECK(val.is_err());
    // task 33 pin: doc `get` @return promises Failure(NotFound)
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
    // task 33 pin: doc `get` @return promises Failure(NotFound)
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
    // Contract pin (task 33): `set` @details — "setenv with overwrite
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
    // Contract pin (task 33): `unset` @details — "Removing a variable that
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
    // Contract pin (task 33): `get` @details — "on POSIX it is
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
    // Contract pin (task 33): `get` @details — "On Windows the lookup is
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
    // null-terminated buffer for the NAME (`get` @details); the value side
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

// ── Task 56 pins ─────────────────────────────────────────────────────────
// Freeze the Env single-source-of-truth contract (detail/48 §2.1, detail/51
// §3, detail/52 D8): empty-key rejection, set-but-empty vs missing, snapshot
// isolation with a live `get`, CJK (UTF-8) round-trip, and case-sensitivity of
// map lookups. Pure process-environment logic: no files, no timing. Scratch
// keys are defensively unset before and after; CJK is written as explicit
// UTF-8 byte escapes (test_encoding.cpp precedent) so the source is not
// dependent on the host compiler's charset.

TEST_CASE("Env::set and Env::unset reject an empty key and Env::get reports it missing")
{
    // Contract pin (task 56): env.hpp set/unset @details — an empty name is
    // rejected with Failure(IoError) and leaves the environment unchanged;
    // get @details — an empty name is never a valid variable => NotFound.
    // POSIX: setenv("")/unsetenv("") = -1/EINVAL, getenv("") = NULL.
    // Windows: SetEnvironmentVariableW(L"", ...) = FALSE,
    // GetEnvironmentVariableW(L"", ...) = 0.
    constexpr std::string_view kEmpty = "";
    auto s = Env::set(kEmpty, "v");
    REQUIRE(s.is_err());                           // A1
    CHECK_EQ(s.unwrap_err(), ErrorCode::IoError);  // A2 = THE PIN (empty key)
    auto u = Env::unset(kEmpty);
    REQUIRE(u.is_err());                           // A3
    CHECK_EQ(u.unwrap_err(), ErrorCode::IoError);  // A4 = THE PIN (empty key)
    auto g = Env::get(kEmpty);
    REQUIRE(g.is_err());                            // A5
    CHECK_EQ(g.unwrap_err(), ErrorCode::NotFound);  // A6 = THE PIN (empty name)
}

TEST_CASE("Env::get distinguishes a set-but-empty value from a missing variable")
{
    // Contract pin (task 56): env.hpp get/set @details — a set-but-empty
    // variable is present and returns Ok(""); a missing variable returns
    // Failure(NotFound). This is the pjh_cli single-source contract: is_ok()
    // must not be read as "non-empty", and snapshot() carries the key with an
    // empty value.
    constexpr std::string_view k = "__PJH_EMPTY_VS_MISSING__";
    constexpr std::string_view kAbsent = "__PJH_EMPTY_VS_MISSING_ABSENT__";
    (void)Env::unset(k);
    (void)Env::unset(kAbsent);
    REQUIRE(Env::set(k, "").is_ok());  // S1

    auto present = Env::get(k);
    REQUIRE(present.is_ok());        // A1 = THE PIN (present, not NotFound)
    CHECK_EQ(present.unwrap(), "");  // A2 = THE PIN (empty value round-trip)

    auto missing = Env::get(kAbsent);
    REQUIRE(missing.is_err());                            // A3
    CHECK_EQ(missing.unwrap_err(), ErrorCode::NotFound);  // A4 = THE PIN

    auto snap = Env::snapshot();
    auto it = snap.find(std::string(k));
    REQUIRE(it != snap.end());                       // A5: key present
    CHECK_EQ(it->second, "");                        // A6: empty value
    CHECK_EQ(snap.count(std::string(kAbsent)), 0u);  // A7: absent
    (void)Env::unset(k);
}

TEST_CASE("Env::snapshot and Env::list are isolated while Env::get reads the live environment")
{
    // Contract pin (task 56): env.hpp snapshot/list @details — independent
    // point-in-time copies; get @details — re-reads the live environment on
    // every call (get is NOT a snapshot; detail/51 R6).
    constexpr std::string_view k = "__PJH_SNAP_ISO__";
    (void)Env::unset(k);
    REQUIRE(Env::set(k, "before").is_ok());  // S1

    auto snap = Env::snapshot();  // capture point
    auto entries = Env::list();
    auto snap_it = snap.find(std::string(k));
    REQUIRE(snap_it != snap.end());       // S2
    CHECK_EQ(snap_it->second, "before");  // S3

    REQUIRE(Env::set(k, "after").is_ok());  // S4: mutate after capture

    auto live = Env::get(k);
    REQUIRE(live.is_ok());                // A1 = THE PIN (get is live)
    CHECK_EQ(live.unwrap(), "after");     // A2 = THE PIN
    CHECK_EQ(snap_it->second, "before");  // A3 = THE PIN (snapshot frozen)

    bool list_frozen = false;
    for (const auto &[key, val] : entries)
    {
        if (key == k)
            list_frozen = (val == "before");
    }
    CHECK(list_frozen);  // A4 = THE PIN (list frozen)

    REQUIRE(Env::unset(k).is_ok());       // S5: remove
    CHECK_EQ(snap_it->second, "before");  // A5: snapshot still holds it
    auto gone = Env::get(k);
    REQUIRE(gone.is_err());                            // A6: live read missing
    CHECK_EQ(gone.unwrap_err(), ErrorCode::NotFound);  // A7
}

TEST_CASE("Env::set and Env::get round-trip a CJK (UTF-8) name and value")
{
    // Contract pin (task 56): env.hpp class @details — UTF-8 is the canonical
    // boundary encoding (Windows converts through Encoding, POSIX passes bytes
    // through). Lane-invariant: the exact UTF-8 byte sequence round-trips.
    // Source bytes: 变量 / 值_値_中文_€, written as escapes for host-independence.
    const std::string k = "__PJH_CJK_\xE5\x8F\x98\xE9\x87\x8F__";
    const std::string v = "\xE5\x80\xBC_\xE5\x80\xA4_\xE4\xB8\xAD\xE6\x96\x87_\xE2\x82\xAC";
    (void)Env::unset(k);
    auto s = Env::set(k, v);
    REQUIRE(s.is_ok());  // S1
    auto g = Env::get(k);
    REQUIRE(g.is_ok());       // A1
    CHECK_EQ(g.unwrap(), v);  // A2 = THE PIN (byte-exact UTF-8 round-trip)

    auto snap = Env::snapshot();
    auto it = snap.find(k);
    REQUIRE(it != snap.end());  // A3
    CHECK_EQ(it->second, v);    // A4: snapshot carries the same bytes
    (void)Env::unset(k);
}

TEST_CASE("Env::snapshot keys preserve native case and map lookups do not fold")
{
    // Contract pin (task 56) + D8 ruling: env.hpp snapshot @details — keys keep
    // the native spelling and map lookups are case-sensitive on every platform,
    // Windows included. Consumers that need Windows case-insensitive resolution
    // must route names through Env::get (see the Windows contrast case).
    const std::string k = "__PJH_SNAP_CASE__";
    const std::string kAlt = "__pjh_snap_case__";
    (void)Env::unset(k);
    (void)Env::unset(kAlt);
    REQUIRE(Env::set(k, "v").is_ok());  // S1

    auto snap = Env::snapshot();
    CHECK(snap.find(k) != snap.end());     // A1: exact spelling present
    CHECK(snap.find(kAlt) == snap.end());  // A2 = THE PIN (no case folding)

    bool list_exact = false;
    for (const auto &[key, val] : Env::list())
    {
        if (key == k)
            list_exact = (val == "v");
        CHECK(key != kAlt);  // A3: list preserves the native spelling
    }
    CHECK(list_exact);  // A4
    (void)Env::unset(k);
}

#if PJH_PLATFORM_WINDOWS
TEST_CASE("Env::get folds case while Env::snapshot().find does not (Windows)")
{
    // Contract pin (task 56) + D8: env.hpp get @details — Windows lookup is
    // case-insensitive; snapshot @details — the map never folds. A captured
    // map must not replace Env::get for case-insensitive name resolution.
    const std::string k = "__PJH_FOLD_SNAP__";
    const std::string kAlt = "__pjh_fold_snap__";
    (void)Env::unset(k);
    REQUIRE(Env::set(k, "v").is_ok());  // S1

    auto snap = Env::snapshot();
    CHECK(snap.find(k) != snap.end());     // A1: exact spelling present
    CHECK(snap.find(kAlt) == snap.end());  // A2 = THE PIN (map is exact)

    auto folded = Env::get(kAlt);
    REQUIRE(folded.is_ok());         // A3 = THE PIN (get folds on Windows)
    CHECK_EQ(folded.unwrap(), "v");  // A4
    (void)Env::unset(k);
}

TEST_CASE("Env::snapshot and Env::list surface Windows drive pseudo entries under an empty key")
{
    // Pin (task 56, Windows lane only — NOT verified locally): the
    // GetEnvironmentStringsW block contains drive current-directory
    // pseudo-entries shaped "=C:=C:\...". for_each_env_entry finds '=' at
    // index 0 and stores the remainder under the empty-string key; list()
    // mirrors it. The block can also carry other empty-key hidden entries that
    // are not drive-shaped (e.g. cmd.exe's "=ExitCode=..."), and the snapshot
    // map can retain only one of the colliding empty-key entries, so the drive
    // shape is pinned through list() while the snapshot assertion only checks
    // that the empty key is surfaced. Shape checks are therefore scoped to the
    // drive-shaped subset. If a Windows runner reports no drive-shaped empty-key
    // entry, A1 fails: that is a contract finding (the documented OS /
    // implementation behavior changed), not a reason to relax this pin.
    const auto is_drive_shape = [](const std::string &value)
    {
        return value.size() >= 3 && value[1] == ':' && value[2] == '=';
    };

    auto snap = Env::snapshot();
    auto entries = Env::list();

    REQUIRE(snap.find(std::string()) != snap.end());  // A1: empty key surfaced

    bool list_has_drive = false;
    for (const auto &[key, val] : entries)
    {
        if (key.empty() && is_drive_shape(val))
        {
            list_has_drive = true;
            REQUIRE_GE(val.size(), 3u);  // A2: drive shape implies >= 3 bytes
            CHECK_EQ(val[1], ':');       // A3: documented "X:=" shape
            CHECK_EQ(val[2], '=');       // A4
        }
    }
    CHECK(list_has_drive);  // A5 = THE PIN (drive pseudo entry surfaced)
}
#endif
