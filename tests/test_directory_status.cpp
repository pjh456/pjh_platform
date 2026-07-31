#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <pjh_platform/directory_snapshot.hpp>
#include <pjh_platform/directory_status.hpp>
#include <pjh_platform/fs.hpp>
#include <string>
#include <vector>

using pjh::platform::DirectorySnapshot;
using pjh::platform::DirectoryStatus;

namespace
{
    auto make_test_dir(const char *name = "main") -> std::filesystem::path
    {
        auto p = pjh::platform::Fs::temp_directory() /
                 (std::string("pjh_platform_status_test_") + name);
        std::filesystem::remove_all(p);
        std::filesystem::create_directories(p);
        return p;
    }
}  // namespace

TEST_CASE("DirectoryStatus of an empty directory is all zeros")
{
    auto p = make_test_dir();
    auto status = DirectoryStatus::from(DirectorySnapshot::capture(p).unwrap());
    CHECK_EQ(status.total_size(), 0u);
    CHECK_EQ(status.file_count(), 0u);
    CHECK_EQ(status.dir_count(), 0u);
    CHECK(status.extension_summaries().empty());
    CHECK(status.largest_files(10).empty());
}

TEST_CASE("DirectoryStatus totals files and directories")
{
    auto p = make_test_dir();
    REQUIRE(pjh::platform::Fs::write_file(p / "a.txt", "aaa").is_ok());
    REQUIRE(pjh::platform::Fs::write_file(p / "b.bin", "bbbbbb").is_ok());
    REQUIRE(std::filesystem::create_directories(p / "sub"));

    auto status = DirectoryStatus::from(DirectorySnapshot::capture(p).unwrap());
    CHECK_EQ(status.total_size(), 9u);
    CHECK_EQ(status.file_count(), 2u);
    CHECK_EQ(status.dir_count(), 1u);
}

TEST_CASE("DirectoryStatus groups files by extension")
{
    auto p = make_test_dir();
    REQUIRE(pjh::platform::Fs::write_file(p / "a.txt", "aaaa").is_ok());
    REQUIRE(pjh::platform::Fs::write_file(p / "b.txt", "bbbbbb").is_ok());
    REQUIRE(pjh::platform::Fs::write_file(p / "c.cpp", "cc").is_ok());
    REQUIRE(pjh::platform::Fs::write_file(p / "Makefile", "all").is_ok());

    auto status = DirectoryStatus::from(DirectorySnapshot::capture(p).unwrap());
    auto summaries = status.extension_summaries();
    REQUIRE_EQ(summaries.size(), 3u);

    auto txt = std::find_if(summaries.begin(), summaries.end(), [](const auto &s) {
        return s.m_extension == std::filesystem::path(".txt");
    });
    REQUIRE(txt != summaries.end());
    CHECK_EQ(txt->m_file_count, 2u);
    CHECK_EQ(txt->m_total_size, 10u);

    auto cpp = std::find_if(summaries.begin(), summaries.end(), [](const auto &s) {
        return s.m_extension == std::filesystem::path(".cpp");
    });
    REQUIRE(cpp != summaries.end());
    CHECK_EQ(cpp->m_file_count, 1u);
    CHECK_EQ(cpp->m_total_size, 2u);

    auto noext = std::find_if(summaries.begin(), summaries.end(), [](const auto &s) {
        return s.m_extension.empty();
    });
    REQUIRE(noext != summaries.end());
    CHECK_EQ(noext->m_file_count, 1u);
    CHECK_EQ(noext->m_total_size, 3u);
}

TEST_CASE("DirectoryStatus reports largest files in descending order")
{
    auto p = make_test_dir();
    REQUIRE(pjh::platform::Fs::write_file(p / "small.txt", "a").is_ok());
    REQUIRE(pjh::platform::Fs::write_file(p / "big.txt", "0123456789").is_ok());
    REQUIRE(pjh::platform::Fs::write_file(p / "mid.txt", "abcde").is_ok());

    auto status = DirectoryStatus::from(DirectorySnapshot::capture(p).unwrap());

    auto top2 = status.largest_files(2);
    REQUIRE_EQ(top2.size(), 2u);
    CHECK_EQ(top2[0], p / "big.txt");
    CHECK_EQ(top2[1], p / "mid.txt");

    auto all = status.largest_files(100);
    REQUIRE_EQ(all.size(), 3u);
    CHECK_EQ(all[2], p / "small.txt");

    CHECK(status.largest_files(0).empty());
}

TEST_CASE("DirectoryStatus excludes directories from sizes and extensions")
{
    auto p = make_test_dir();
    REQUIRE(std::filesystem::create_directories(p / "sub"));
    REQUIRE(pjh::platform::Fs::write_file(p / "sub" / "inner.txt", "x").is_ok());

    auto status = DirectoryStatus::from(DirectorySnapshot::capture(p).unwrap());
    CHECK_EQ(status.total_size(), 0u);
    CHECK_EQ(status.file_count(), 0u);
    CHECK_EQ(status.dir_count(), 1u);
    CHECK(status.extension_summaries().empty());
    CHECK(status.largest_files(10).empty());
}
