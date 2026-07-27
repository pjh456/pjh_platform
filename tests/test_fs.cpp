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

TEST_CASE("Fs::remove_all removes directory and returns count")
{
    auto tmp = Fs::temp_directory() / "pjh_platform_test_remove_all";
    std::filesystem::create_directories(tmp);
    auto r = Fs::remove_all(tmp);
    CHECK(r.is_ok());
    CHECK_GE(r.unwrap(), 1);
    CHECK(!Fs::exists(tmp));
}

TEST_CASE("Fs::remove_all on non-existent path returns 0")
{
    auto r = Fs::remove_all("/nonexistent_path_12345");
    CHECK(r.is_ok());
    CHECK_EQ(r.unwrap(), 0);
}

TEST_CASE("Fs::is_regular_file distinguishes files from directories")
{
    auto tmp = Fs::temp_directory() / "pjh_platform_test_is_regular";
    std::filesystem::create_directories(tmp);
    CHECK(!Fs::is_regular_file(tmp));

    auto file = tmp / "test.txt";
    auto wr = Fs::write_file(file, "content");
    REQUIRE(wr.is_ok());
    CHECK(Fs::is_regular_file(file));

    std::filesystem::remove_all(tmp);
}

TEST_CASE("Fs::file_size returns correct size")
{
    auto file = Fs::temp_directory() / "pjh_platform_test_file_size.txt";
    std::string_view content = "hello pjh_platform";
    auto wr = Fs::write_file(file, content);
    REQUIRE(wr.is_ok());

    auto sz = Fs::file_size(file);
    REQUIRE(sz.is_ok());
    CHECK_EQ(sz.unwrap(), content.size());

    std::filesystem::remove(file);
}

TEST_CASE("Fs::file_size returns NotFound for non-existent file")
{
    auto sz = Fs::file_size("/nonexistent_path_12345");
    CHECK(sz.is_err());
    CHECK_EQ(sz.unwrap_err(), pjh::platform::ErrorCode::NotFound);
}

TEST_CASE("Fs::list_directory lists entries")
{
    auto tmp = Fs::temp_directory() / "pjh_platform_test_list_dir";
    std::filesystem::create_directories(tmp);
    auto wa = Fs::write_file(tmp / "a.txt", "aaa");
    auto wb = Fs::write_file(tmp / "b.txt", "bbb");
    REQUIRE(wa.is_ok());
    REQUIRE(wb.is_ok());
    std::filesystem::create_directory(tmp / "subdir");

    auto entries = Fs::list_directory(tmp);
    REQUIRE(entries.is_ok());
    CHECK_EQ(entries.unwrap().size(), 3);

    std::filesystem::remove_all(tmp);
}

TEST_CASE("Fs::list_directory returns NotFound for non-existent path")
{
    auto entries = Fs::list_directory("/nonexistent_path_12345");
    CHECK(entries.is_err());
    CHECK_EQ(entries.unwrap_err(), pjh::platform::ErrorCode::NotFound);
}
