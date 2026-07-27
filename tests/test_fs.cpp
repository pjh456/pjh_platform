#include <doctest/doctest.h>

#include <fstream>
#include <iostream>

#include <pjh_platform/fs.hpp>

using pjh::platform::Fs;

TEST_CASE("Fs::current_path returns non-empty path")
{
    auto cwd = Fs::current_path();
    CHECK(!cwd.empty());
}

TEST_CASE("Fs::temp_directory returns non-empty path")
{
    auto tmp = Fs::temp_directory();
    CHECK(!tmp.empty());
}

TEST_CASE("Fs::create_directories and Fs::exists")
{
    auto tmp = Fs::temp_directory() / "pjh_platform_test_dir";
    auto r = Fs::create_directories(tmp);
    CHECK(r.is_ok());
    CHECK(Fs::exists(tmp));
    CHECK(Fs::is_directory(tmp));
    std::filesystem::remove_all(tmp);
}

TEST_CASE("Fs::write_file and Fs::read_file round-trip")
{
    auto tmp = Fs::temp_directory() / "pjh_platform_test_file.txt";
    auto r = Fs::write_file(tmp, "hello pjh_platform");
    CHECK(r.is_ok());

    auto content = Fs::read_file(tmp);
    REQUIRE(content.is_ok());
    CHECK_EQ(content.unwrap(), "hello pjh_platform");

    std::filesystem::remove(tmp);
}

TEST_CASE("Fs::read_file returns not_found for non-existent file")
{
    auto content = Fs::read_file("/nonexistent/path/file.txt");
    CHECK(content.is_err());
}

TEST_CASE("Fs::home_directory returns something on typical systems")
{
    auto home = Fs::home_directory();
    CHECK(home.is_ok());
}
