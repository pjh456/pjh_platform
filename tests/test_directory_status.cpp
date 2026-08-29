#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
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
        auto p =
            pjh::platform::Fs::temp_directory() / (std::string("pjh_platform_status_test_") + name);
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

    auto txt = std::find_if(
        summaries.begin(), summaries.end(),
        [](const auto &s) { return s.m_extension == std::filesystem::path(".txt"); });
    REQUIRE(txt != summaries.end());
    CHECK_EQ(txt->m_file_count, 2u);
    CHECK_EQ(txt->m_total_size, 10u);

    auto cpp = std::find_if(
        summaries.begin(), summaries.end(),
        [](const auto &s) { return s.m_extension == std::filesystem::path(".cpp"); });
    REQUIRE(cpp != summaries.end());
    CHECK_EQ(cpp->m_file_count, 1u);
    CHECK_EQ(cpp->m_total_size, 2u);

    auto noext = std::find_if(
        summaries.begin(), summaries.end(), [](const auto &s) { return s.m_extension.empty(); });
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

TEST_CASE("DirectoryStatus largest_files orders equal sizes by ascending path")
{
    // Pins the (size descending, path ascending) total order: entries that tie
    // on size must come out in ascending path order, and a larger file must
    // still rank first. This is the determinism guarantee of the top-n
    // selection in largest_files.
    auto p = make_test_dir();
    REQUIRE(pjh::platform::Fs::write_file(p / "b.txt", "12345").is_ok());
    REQUIRE(pjh::platform::Fs::write_file(p / "a.txt", "12345").is_ok());
    REQUIRE(pjh::platform::Fs::write_file(p / "c.txt", "12345").is_ok());
    REQUIRE(pjh::platform::Fs::write_file(p / "d.txt", "12345").is_ok());
    REQUIRE(pjh::platform::Fs::write_file(p / "big.bin", "0123456789").is_ok());

    auto status = DirectoryStatus::from(DirectorySnapshot::capture(p).unwrap());
    auto all = status.largest_files(100);
    REQUIRE_EQ(all.size(), 5u);
    CHECK_EQ(all[0], p / "big.bin");
    CHECK_EQ(all[1], p / "a.txt");
    CHECK_EQ(all[2], p / "b.txt");
    CHECK_EQ(all[3], p / "c.txt");
    CHECK_EQ(all[4], p / "d.txt");
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

TEST_CASE("DirectoryStatus from + largest_files benchmark gated by PJH_STATUS_TOPN_BENCH_FILES")
{
    // Baseline / regression benchmark for DirectoryStatus::from +
    // largest_files(10). The measurement and its stdout output happen only
    // when PJH_STATUS_TOPN_BENCH_FILES names the file count to create
    // (recommend 10000; 5000 on slow machines, e.g. Windows CI); a plain
    // `ctest` run is a no-op and stays silent. File sizes cycle over 64
    // values so many entries tie on size, amplifying the comparator's path
    // comparisons (worst case for a full sort).
    const char *raw = std::getenv("PJH_STATUS_TOPN_BENCH_FILES");
    if (raw == nullptr)
        return;
    const auto n = static_cast<std::size_t>(std::strtoull(raw, nullptr, 10));
    REQUIRE(n >= 1000u);

    auto p = make_test_dir("topn_bench");
    for (std::size_t i = 0; i < n; ++i)
    {
        auto name = "f_" + std::to_string(i) + ".txt";
        REQUIRE(pjh::platform::Fs::write_file(p / name, std::string(i % 64, 'x')).is_ok());
    }

    auto s = DirectorySnapshot::capture(p);
    REQUIRE(s.is_ok());
    auto snap = std::move(s).unwrap();

    (void)DirectoryStatus::from(snap).largest_files(10);  // warm-up

    const int iters = 50;
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < iters; ++i)
    {
        auto v = DirectoryStatus::from(snap).largest_files(10);
        REQUIRE_EQ(v.size(), 10u);
    }
    auto t1 = std::chrono::steady_clock::now();
    const auto total_us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
    std::cout << "status-topn-bench: files=" << n << " iters=" << iters << " total_us=" << total_us
              << " us_per_run=" << total_us / iters << "\n";
}
