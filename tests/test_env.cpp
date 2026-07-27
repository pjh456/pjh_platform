#include "pjh_platform/env.hpp"
#include "doctest/doctest.h"

namespace env = pjh::platform::env;

TEST_CASE("env::get returns nullopt for non-existent variable") {
    auto val = env::get("__NONEXISTENT_VAR_12345__");
    CHECK(!val.has_value());
}

TEST_CASE("env::set and env::get round-trip") {
    auto err = env::set("__TEST_PJH_VAR__", "hello_world");
    CHECK(!err);

    auto val = env::get("__TEST_PJH_VAR__");
    REQUIRE(val.has_value());
    CHECK_EQ(*val, "hello_world");
}

TEST_CASE("env::unset removes variable") {
    env::set("__TEST_PJH_VAR__", "temp");
    auto val = env::get("__TEST_PJH_VAR__");
    REQUIRE(val.has_value());

    auto err = env::unset("__TEST_PJH_VAR__");
    CHECK(!err);

    val = env::get("__TEST_PJH_VAR__");
    CHECK(!val.has_value());
}
