#include <doctest/doctest.h>

#include <pjh_platform/env.hpp>

using pjh::platform::Env;

TEST_CASE("Env::get returns not_found for non-existent variable")
{
    auto val = Env::get("__NONEXISTENT_VAR_12345__");
    CHECK(val.is_err());
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
