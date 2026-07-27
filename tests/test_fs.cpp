#include <doctest/doctest.h>

#include <fstream>
#include <iostream>

#include <pjh_platform/fs.hpp>

namespace fs = pjh::platform::fs;

TEST_CASE("fs::current_path returns non-empty path") {
    auto cwd = fs::current_path();
    CHECK(!cwd.empty());
}

TEST_CASE("fs::temp_directory returns non-empty path") {
    auto tmp = fs::temp_directory();
    CHECK(!tmp.empty());
}

TEST_CASE("fs::create_directories and fs::exists") {
    auto tmp = fs::temp_directory() / "pjh_platform_test_dir";
    bool created = fs::create_directories(tmp);
    CHECK(created);
    CHECK(fs::exists(tmp));
    CHECK(fs::is_directory(tmp));
    std::filesystem::remove_all(tmp);
}

TEST_CASE("fs::write_file and fs::read_file round-trip") {
    auto tmp = fs::temp_directory() / "pjh_platform_test_file.txt";
    auto err = fs::write_file(tmp, "hello pjh_platform");
    CHECK(!err);

    auto content = fs::read_file(tmp);
    REQUIRE(content.has_value());
    CHECK_EQ(*content, "hello pjh_platform");

    std::filesystem::remove(tmp);
}

TEST_CASE("fs::read_file returns nullopt for non-existent file") {
    auto content = fs::read_file("/nonexistent/path/file.txt");
    CHECK(!content.has_value());
}

TEST_CASE("fs::home_directory returns something on typical systems") {
    auto home = fs::home_directory();
    CHECK(home.has_value());
}
