#include <doctest/doctest.h>

#include <pjh_platform/env.hpp>

using pjh::platform::Env;

TEST_CASE("Env::get returns nullopt for non-existent variable")
{
    auto val = Env::get("__NONEXISTENT_VAR_12345__");
    CHECK(!val.has_value());
}

TEST_CASE("Env::set and Env::get round-trip")
{
    auto err = Env::set("__TEST_PJH_VAR__", "hello_world");
    CHECK(!err);

    auto val = Env::get("__TEST_PJH_VAR__");
    REQUIRE(val.has_value());
    CHECK_EQ(*val, "hello_world");
}

TEST_CASE("Env::unset removes variable")
{
    Env::set("__TEST_PJH_VAR__", "temp");
    auto val = Env::get("__TEST_PJH_VAR__");
    REQUIRE(val.has_value());

    auto err = Env::unset("__TEST_PJH_VAR__");
    CHECK(!err);

    val = Env::get("__TEST_PJH_VAR__");
    CHECK(!val.has_value());
}

TEST_CASE("Env::snapshot contains expected variables")
{
    Env::set("__TEST_PJH_SNAP__", "snap_value");

    auto snap = Env::snapshot();
    CHECK_GE(snap.size(), 1);

    auto it = snap.find("__TEST_PJH_SNAP__");
    REQUIRE(it != snap.end());
    CHECK_EQ(it->second, "snap_value");
}

TEST_CASE("Env::list returns all environment variables")
{
    Env::set("__TEST_PJH_LIST__", "list_value");

    auto entries = Env::list();
    CHECK_GE(entries.size(), 1);

    bool found = false;
    for (const auto& [key, val] : entries)
    {
        if (key == "__TEST_PJH_LIST__")
        {
            CHECK_EQ(val, "list_value");
            found = true;
        }
    }
    CHECK(found);
}
