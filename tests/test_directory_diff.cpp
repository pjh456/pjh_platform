#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <pjh_platform/directory_diff.hpp>
#include <pjh_platform/directory_snapshot.hpp>
#include <pjh_platform/error.hpp>
#include <pjh_platform/fs.hpp>
#include <string>
#include <vector>

using pjh::platform::DirectoryDiff;
using pjh::platform::DirectorySnapshot;
using pjh::platform::ErrorCode;

namespace
{
    auto make_test_dir(const char *name = "main") -> std::filesystem::path
    {
        auto p =
            pjh::platform::Fs::temp_directory() / (std::string("pjh_platform_diff_test_") + name);
        std::filesystem::remove_all(p);
        std::filesystem::create_directories(p);
        return p;
    }

    auto has_change(
        const std::vector<DirectoryDiff::Change> &changes,
        DirectoryDiff::ChangeKind kind,
        const std::filesystem::path &full_path) -> bool
    {
        return std::any_of(
            changes.begin(), changes.end(),
            [&](const auto &c) { return c.m_kind == kind && c.m_full_path == full_path; });
    }
}  // namespace

TEST_CASE("DirectoryDiff of identical snapshots is empty")
{
    auto p = make_test_dir();
    REQUIRE(pjh::platform::Fs::write_file(p / "a.txt", "aaa").is_ok());

    auto before = DirectorySnapshot::capture(p);
    auto after = DirectorySnapshot::capture(p);
    REQUIRE(before.is_ok());
    REQUIRE(after.is_ok());

    auto diff = DirectoryDiff::compare(before.unwrap(), after.unwrap());
    REQUIRE(diff.is_ok());
    CHECK(diff.unwrap().empty());
    CHECK(diff.unwrap().changes().empty());
}

TEST_CASE("DirectoryDiff reports created entries")
{
    auto p = make_test_dir();
    auto before = DirectorySnapshot::capture(p);
    REQUIRE(before.is_ok());

    REQUIRE(pjh::platform::Fs::write_file(p / "new.txt", "new").is_ok());
    auto after = DirectorySnapshot::capture(p);
    REQUIRE(after.is_ok());

    auto diff = DirectoryDiff::compare(before.unwrap(), after.unwrap());
    REQUIRE(diff.is_ok());
    auto changes = diff.unwrap().changes();
    REQUIRE_EQ(changes.size(), 1u);
    CHECK_EQ(changes[0].m_kind, DirectoryDiff::ChangeKind::Created);
    CHECK_EQ(changes[0].m_filename, std::filesystem::path("new.txt"));
    CHECK_EQ(changes[0].m_full_path, p / "new.txt");
}

TEST_CASE("DirectoryDiff reports deleted entries")
{
    auto p = make_test_dir();
    auto doomed = p / "doomed.txt";
    REQUIRE(pjh::platform::Fs::write_file(doomed, "bye").is_ok());
    auto before = DirectorySnapshot::capture(p);
    REQUIRE(before.is_ok());

    REQUIRE(std::filesystem::remove(doomed));
    auto after = DirectorySnapshot::capture(p);
    REQUIRE(after.is_ok());

    auto diff = DirectoryDiff::compare(before.unwrap(), after.unwrap());
    REQUIRE(diff.is_ok());
    auto changes = diff.unwrap().changes();
    REQUIRE_EQ(changes.size(), 1u);
    CHECK_EQ(changes[0].m_kind, DirectoryDiff::ChangeKind::Deleted);
    CHECK_EQ(changes[0].m_full_path, doomed);
}

TEST_CASE("DirectoryDiff reports modified files by size")
{
    auto p = make_test_dir();
    auto file = p / "data.txt";
    REQUIRE(pjh::platform::Fs::write_file(file, "a").is_ok());
    auto before = DirectorySnapshot::capture(p);
    REQUIRE(before.is_ok());

    REQUIRE(pjh::platform::Fs::write_file(file, "much longer content").is_ok());
    auto after = DirectorySnapshot::capture(p);
    REQUIRE(after.is_ok());

    auto diff = DirectoryDiff::compare(before.unwrap(), after.unwrap());
    REQUIRE(diff.is_ok());
    auto changes = diff.unwrap().changes();
    REQUIRE_EQ(changes.size(), 1u);
    CHECK_EQ(changes[0].m_kind, DirectoryDiff::ChangeKind::Modified);
    CHECK_EQ(changes[0].m_full_path, file);
}

TEST_CASE("DirectoryDiff reports modified files by hash")
{
    auto p = make_test_dir();
    auto file = p / "data.txt";
    REQUIRE(pjh::platform::Fs::write_file(file, "hello").is_ok());

    std::vector<std::filesystem::path> hash_files{std::filesystem::path("data.txt")};
    auto before = DirectorySnapshot::capture(p, &hash_files);
    REQUIRE(before.is_ok());

    REQUIRE(pjh::platform::Fs::write_file(file, "world").is_ok());
    auto after = DirectorySnapshot::capture(p, &hash_files);
    REQUIRE(after.is_ok());

    auto diff = DirectoryDiff::compare(before.unwrap(), after.unwrap());
    REQUIRE(diff.is_ok());
    auto changes = diff.unwrap().changes();
    REQUIRE_EQ(changes.size(), 1u);
    CHECK_EQ(changes[0].m_kind, DirectoryDiff::ChangeKind::Modified);
}

TEST_CASE("DirectoryDiff ignores mtime touches when hashes match")
{
    auto p = make_test_dir();
    auto file = p / "data.txt";
    REQUIRE(pjh::platform::Fs::write_file(file, "stable").is_ok());

    std::vector<std::filesystem::path> hash_files{std::filesystem::path("data.txt")};
    auto before = DirectorySnapshot::capture(p, &hash_files);
    REQUIRE(before.is_ok());

    std::filesystem::last_write_time(file, std::filesystem::file_time_type::clock::now());
    auto after = DirectorySnapshot::capture(p, &hash_files);
    REQUIRE(after.is_ok());

    auto diff = DirectoryDiff::compare(before.unwrap(), after.unwrap());
    REQUIRE(diff.is_ok());
    CHECK(diff.unwrap().empty());
}

TEST_CASE("DirectoryDiff never reports directories as modified")
{
    auto p = make_test_dir();
    REQUIRE(std::filesystem::create_directories(p / "sub"));
    auto before = DirectorySnapshot::capture(p);
    REQUIRE(before.is_ok());

    REQUIRE(pjh::platform::Fs::write_file(p / "sub" / "inner.txt", "x").is_ok());

    // Not every platform bumps a directory's mtime when a child is added
    // (Windows keeps directory last-write-time lazily), so touch it explicitly
    // to guarantee the setup below exercises a changed directory mtime. The
    // target sits 2 s in the future: at least one timestamp quantum past the
    // creation-time value on every supported filesystem (worst quantum is
    // FAT's 2 s), so the REQUIRE_NE below is deterministic rather than
    // bucket-dependent.
    std::error_code ec;
    std::filesystem::last_write_time(
        p / "sub", std::filesystem::file_time_type::clock::now() + std::chrono::seconds(2), ec);
    REQUIRE_FALSE(ec);

    auto after = DirectorySnapshot::capture(p);
    REQUIRE(after.is_ok());

    auto before_snap = before.unwrap();
    auto after_snap = after.unwrap();
    REQUIRE_NE(before_snap.get("sub")->m_mtime_ns, after_snap.get("sub")->m_mtime_ns);

    auto diff = DirectoryDiff::compare(before_snap, after_snap);
    REQUIRE(diff.is_ok());
    CHECK(diff.unwrap().empty());
}

TEST_CASE("DirectoryDiff rejects snapshots of different directories")
{
    auto p1 = make_test_dir("one");
    auto p2 = make_test_dir("two");
    auto s1 = DirectorySnapshot::capture(p1);
    auto s2 = DirectorySnapshot::capture(p2);
    REQUIRE(s1.is_ok());
    REQUIRE(s2.is_ok());

    auto diff = DirectoryDiff::compare(s1.unwrap(), s2.unwrap());
    REQUIRE(diff.is_err());
    CHECK_EQ(diff.unwrap_err(), ErrorCode::InvalidArgument);
}

TEST_CASE("DirectoryDiff detects a file rename via content hash")
{
    auto p = make_test_dir();
    auto old_file = p / "old.txt";
    auto new_file = p / "new.txt";
    REQUIRE(pjh::platform::Fs::write_file(old_file, "rename me please").is_ok());

    std::vector<std::filesystem::path> before_list{std::filesystem::path("old.txt")};
    auto before = DirectorySnapshot::capture(p, &before_list);
    REQUIRE(before.is_ok());

    std::error_code rn_ec;
    std::filesystem::rename(old_file, new_file, rn_ec);
    REQUIRE_FALSE(rn_ec);

    std::vector<std::filesystem::path> after_list{std::filesystem::path("new.txt")};
    auto after = DirectorySnapshot::capture(p, &after_list);
    REQUIRE(after.is_ok());

    auto before_snap = before.unwrap();
    auto after_snap = after.unwrap();
    auto diff = DirectoryDiff::compare(before_snap, after_snap);
    REQUIRE(diff.is_ok());
    auto diff_snap = diff.unwrap();

    CHECK_EQ(diff_snap.changes().size(), 2u);
    CHECK(has_change(diff_snap.changes(), DirectoryDiff::ChangeKind::Deleted, old_file));
    CHECK(has_change(diff_snap.changes(), DirectoryDiff::ChangeKind::Created, new_file));

    auto renames = diff_snap.detect_renames(before_snap, after_snap);
    REQUIRE_EQ(renames.size(), 1u);
    CHECK_EQ(renames[0].m_old_filename, std::filesystem::path("old.txt"));
    CHECK_EQ(renames[0].m_new_filename, std::filesystem::path("new.txt"));
}

TEST_CASE("DirectoryDiff detects a file rename via size and mtime")
{
    auto p = make_test_dir();
    auto old_file = p / "old.bin";
    auto new_file = p / "new.bin";
    REQUIRE(pjh::platform::Fs::write_file(old_file, "content").is_ok());

    auto before = DirectorySnapshot::capture(p);
    REQUIRE(before.is_ok());

    std::error_code rn_ec;
    std::filesystem::rename(old_file, new_file, rn_ec);
    REQUIRE_FALSE(rn_ec);
    auto after = DirectorySnapshot::capture(p);
    REQUIRE(after.is_ok());

    auto before_snap = before.unwrap();
    auto after_snap = after.unwrap();
    auto diff = DirectoryDiff::compare(before_snap, after_snap);
    REQUIRE(diff.is_ok());

    auto renames = diff.unwrap().detect_renames(before_snap, after_snap);
    REQUIRE_EQ(renames.size(), 1u);
    CHECK_EQ(renames[0].m_old_filename, std::filesystem::path("old.bin"));
    CHECK_EQ(renames[0].m_new_filename, std::filesystem::path("new.bin"));
}

TEST_CASE("DirectoryDiff does not pair files with different content")
{
    auto p = make_test_dir();
    auto old_file = p / "old.txt";
    auto new_file = p / "new.txt";
    REQUIRE(pjh::platform::Fs::write_file(old_file, "alpha").is_ok());
    auto before = DirectorySnapshot::capture(p);
    REQUIRE(before.is_ok());

    std::error_code rn_ec;
    std::filesystem::rename(old_file, new_file, rn_ec);
    REQUIRE_FALSE(rn_ec);
    REQUIRE(pjh::platform::Fs::write_file(new_file, "beta").is_ok());
    auto after = DirectorySnapshot::capture(p);
    REQUIRE(after.is_ok());

    auto before_snap = before.unwrap();
    auto after_snap = after.unwrap();
    auto diff = DirectoryDiff::compare(before_snap, after_snap);
    REQUIRE(diff.is_ok());

    auto renames = diff.unwrap().detect_renames(before_snap, after_snap);
    CHECK(renames.empty());
}

TEST_CASE("DirectoryDiff does not pair directory renames")
{
    auto p = make_test_dir();
    REQUIRE(std::filesystem::create_directories(p / "old_dir"));
    auto before = DirectorySnapshot::capture(p);
    REQUIRE(before.is_ok());

    std::error_code rn_ec;
    std::filesystem::rename(p / "old_dir", p / "new_dir", rn_ec);
    REQUIRE_FALSE(rn_ec);
    auto after = DirectorySnapshot::capture(p);
    REQUIRE(after.is_ok());

    auto before_snap = before.unwrap();
    auto after_snap = after.unwrap();
    auto diff = DirectoryDiff::compare(before_snap, after_snap);
    REQUIRE(diff.is_ok());

    auto renames = diff.unwrap().detect_renames(before_snap, after_snap);
    CHECK(renames.empty());
}

TEST_CASE("DirectoryDiff pairs multiple renames by size in deleted-name order")
{
    auto p = make_test_dir();
    REQUIRE(pjh::platform::Fs::write_file(p / "o1.bin", "a").is_ok());
    REQUIRE(pjh::platform::Fs::write_file(p / "o2.bin", "bb").is_ok());
    REQUIRE(pjh::platform::Fs::write_file(p / "o3.bin", "ccc").is_ok());

    auto before = DirectorySnapshot::capture(p);
    REQUIRE(before.is_ok());

    // Distinct sizes make the (size, mtime) pairing unambiguous even if the
    // filesystem timestamps coincide; names cross on purpose.
    std::error_code rn_ec;
    std::filesystem::rename(p / "o1.bin", p / "n3.bin", rn_ec);
    REQUIRE_FALSE(rn_ec);
    std::filesystem::rename(p / "o2.bin", p / "n1.bin", rn_ec);
    REQUIRE_FALSE(rn_ec);
    std::filesystem::rename(p / "o3.bin", p / "n2.bin", rn_ec);
    REQUIRE_FALSE(rn_ec);

    auto after = DirectorySnapshot::capture(p);
    REQUIRE(after.is_ok());

    auto before_snap = before.unwrap();
    auto after_snap = after.unwrap();
    auto diff = DirectoryDiff::compare(before_snap, after_snap);
    REQUIRE(diff.is_ok());

    auto renames = diff.unwrap().detect_renames(before_snap, after_snap);
    REQUIRE_EQ(renames.size(), 3u);
    CHECK_EQ(renames[0].m_old_filename, std::filesystem::path("o1.bin"));
    CHECK_EQ(renames[0].m_new_filename, std::filesystem::path("n3.bin"));
    CHECK_EQ(renames[1].m_old_filename, std::filesystem::path("o2.bin"));
    CHECK_EQ(renames[1].m_new_filename, std::filesystem::path("n1.bin"));
    CHECK_EQ(renames[2].m_old_filename, std::filesystem::path("o3.bin"));
    CHECK_EQ(renames[2].m_new_filename, std::filesystem::path("n2.bin"));
}

TEST_CASE("DirectoryDiff rename pairing is greedy first-match in created-name order")
{
    auto p = make_test_dir();
    auto a = p / "old_a.bin";
    auto b = p / "old_b.bin";
    REQUIRE(pjh::platform::Fs::write_file(a, "xy").is_ok());
    REQUIRE(pjh::platform::Fs::write_file(b, "xy").is_ok());

    // Identical content and an identical last-write time make the two files
    // indistinguishable without hashes, so the pairing is a pure first-match
    // question: old_a must grab the lexicographically smallest candidate.
    std::error_code lw_ec;
    auto t = std::filesystem::last_write_time(a);
    std::filesystem::last_write_time(b, t, lw_ec);
    REQUIRE_FALSE(lw_ec);

    auto before = DirectorySnapshot::capture(p);
    REQUIRE(before.is_ok());

    // Cross rename: old_a's bytes physically end up at new_b, but nothing
    // distinguishes the two files, so the greedy pairing pairs old_a->new_a.
    std::error_code rn_ec;
    std::filesystem::rename(a, p / "new_b.bin", rn_ec);
    REQUIRE_FALSE(rn_ec);
    std::filesystem::rename(b, p / "new_a.bin", rn_ec);
    REQUIRE_FALSE(rn_ec);

    auto after = DirectorySnapshot::capture(p);
    REQUIRE(after.is_ok());

    auto before_snap = before.unwrap();
    auto after_snap = after.unwrap();
    auto diff = DirectoryDiff::compare(before_snap, after_snap);
    REQUIRE(diff.is_ok());

    auto renames = diff.unwrap().detect_renames(before_snap, after_snap);
    REQUIRE_EQ(renames.size(), 2u);
    CHECK_EQ(renames[0].m_old_filename, std::filesystem::path("old_a.bin"));
    CHECK_EQ(renames[0].m_new_filename, std::filesystem::path("new_a.bin"));
    CHECK_EQ(renames[1].m_old_filename, std::filesystem::path("old_b.bin"));
    CHECK_EQ(renames[1].m_new_filename, std::filesystem::path("new_b.bin"));
}

TEST_CASE("DirectoryDiff pairs a one-sided hashed rename via the size and mtime fallback")
{
    auto p = make_test_dir();
    auto old_a = p / "old_a.txt";
    auto old_b = p / "old_b.txt";
    REQUIRE(pjh::platform::Fs::write_file(old_a, "alpha").is_ok());   // 5 B
    REQUIRE(pjh::platform::Fs::write_file(old_b, "bravo2").is_ok());  // 6 B

    // Asymmetric per-capture hash lists: old_a is hashed only in `before`,
    // new_b only in `after`, so each renamed file carries a hash on exactly
    // one side of the pair and the predicate must take its size+mtime
    // fallback branch. Distinct sizes keep the (size, mtime) keys apart even
    // when coarse-quantum filesystems collapse the two write mtimes.
    std::vector<std::filesystem::path> before_list{std::filesystem::path("old_a.txt")};
    auto before = DirectorySnapshot::capture(p, &before_list);
    REQUIRE(before.is_ok());
    auto before_snap = before.unwrap();
    REQUIRE(before_snap.get("old_a.txt")->m_hash.has_value());
    REQUIRE_FALSE(before_snap.get("old_b.txt")->m_hash.has_value());

    std::error_code rn_ec;
    std::filesystem::rename(old_a, p / "new_a.txt", rn_ec);
    REQUIRE_FALSE(rn_ec);
    std::filesystem::rename(old_b, p / "new_b.txt", rn_ec);
    REQUIRE_FALSE(rn_ec);

    std::vector<std::filesystem::path> after_list{std::filesystem::path("new_b.txt")};
    auto after = DirectorySnapshot::capture(p, &after_list);
    REQUIRE(after.is_ok());
    auto after_snap = after.unwrap();
    REQUIRE_FALSE(after_snap.get("new_a.txt")->m_hash.has_value());
    REQUIRE(after_snap.get("new_b.txt")->m_hash.has_value());

    auto diff = DirectoryDiff::compare(before_snap, after_snap);
    REQUIRE(diff.is_ok());
    auto diff_snap = diff.unwrap();
    auto &changes = diff_snap.changes();
    REQUIRE_EQ(changes.size(), 4u);
    CHECK(has_change(changes, DirectoryDiff::ChangeKind::Deleted, old_a));
    CHECK(has_change(changes, DirectoryDiff::ChangeKind::Deleted, old_b));
    CHECK(has_change(changes, DirectoryDiff::ChangeKind::Created, p / "new_a.txt"));
    CHECK(has_change(changes, DirectoryDiff::ChangeKind::Created, p / "new_b.txt"));

    auto renames = diff_snap.detect_renames(before_snap, after_snap);
    REQUIRE_EQ(renames.size(), 2u);
    CHECK_EQ(renames[0].m_old_filename, std::filesystem::path("old_a.txt"));
    CHECK_EQ(renames[0].m_new_filename, std::filesystem::path("new_a.txt"));
    CHECK_EQ(renames[1].m_old_filename, std::filesystem::path("old_b.txt"));
    CHECK_EQ(renames[1].m_new_filename, std::filesystem::path("new_b.txt"));
}

TEST_CASE("DirectoryDiff detect_renames benchmark gated by PJH_RENAME_STORM_BENCH_FILES")
{
    // Baseline / regression benchmark for the rename-storm cost of
    // detect_renames. The measurement and its stdout output happen only when
    // PJH_RENAME_STORM_BENCH_FILES names the number of files to rename
    // (recommend 10000; 5000 on slow machines, e.g. Windows CI); a plain
    // `ctest` run is a no-op and stays silent.
    const char *raw = std::getenv("PJH_RENAME_STORM_BENCH_FILES");
    if (raw == nullptr)
        return;
    const auto n = static_cast<std::size_t>(std::strtoull(raw, nullptr, 10));
    REQUIRE(n >= 1000u);

    auto p = make_test_dir("rename_storm");

    // Fixed 64-byte payload, distinct per index: one size class keeps every
    // file in the same (size, mtime) bucket for the unhashed scenario while
    // distinct content keeps the hash buckets apart in the hashed one.
    auto payload = [](std::size_t i) -> std::string
    {
        std::string buf(64, '\0');
        for (int k = 0; k < 8; ++k)
            buf[static_cast<std::size_t>(k)] = static_cast<char>((i >> (8 * k)) & 0xFF);
        for (std::size_t k = 8; k < buf.size(); ++k) buf[k] = static_cast<char>(i * 31 + k);
        return buf;
    };

    // One clock value for every renamed file on both sides of the rename so
    // the unhashed scenario is a full one-bucket storm and the pairs still
    // match across the rename (set/get round-trip of the same value).
    auto t = std::filesystem::file_time_type::clock::now();
    for (std::size_t i = 0; i < n; ++i)
    {
        auto name = "old_" + std::to_string(i) + ".bin";
        REQUIRE(pjh::platform::Fs::write_file(p / name, payload(i)).is_ok());
        std::error_code ec;
        std::filesystem::last_write_time(p / name, t, ec);
        REQUIRE_FALSE(ec);
    }
    for (std::size_t j = 0; j < n; ++j)
    {
        auto name = "stay_" + std::to_string(j) + ".bin";
        REQUIRE(pjh::platform::Fs::write_file(p / name, payload(n + j)).is_ok());
    }

    auto before = DirectorySnapshot::capture(p);
    REQUIRE(before.is_ok());
    auto before_h = DirectorySnapshot::capture(p, nullptr);
    REQUIRE(before_h.is_ok());

    for (std::size_t i = 0; i < n; ++i)
    {
        std::error_code ec;
        auto old_name = "old_" + std::to_string(i) + ".bin";
        auto new_name = "new_" + std::to_string(i) + ".bin";
        std::filesystem::rename(p / old_name, p / new_name, ec);
        REQUIRE_FALSE(ec);
        std::filesystem::last_write_time(p / new_name, t, ec);
        REQUIRE_FALSE(ec);
    }

    auto after = DirectorySnapshot::capture(p);
    REQUIRE(after.is_ok());
    auto after_h = DirectorySnapshot::capture(p, nullptr);
    REQUIRE(after_h.is_ok());

    auto &before_snap = before.unwrap();
    auto &after_snap = after.unwrap();
    auto diff = DirectoryDiff::compare(before_snap, after_snap);
    REQUIRE(diff.is_ok());
    auto &diff_snap = diff.unwrap();
    auto &before_h_snap = before_h.unwrap();
    auto &after_h_snap = after_h.unwrap();
    auto diff_h = DirectoryDiff::compare(before_h_snap, after_h_snap);
    REQUIRE(diff_h.is_ok());
    auto &diff_h_snap = diff_h.unwrap();

    const int iters = 3;
    // S1: unhashed capture — every pair shares one (size, mtime) bucket, the
    // adversarial rename storm (the watcher's production shape).
    {
        auto renames = diff_snap.detect_renames(before_snap, after_snap);  // warm-up
        REQUIRE_EQ(renames.size(), n);
        auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < iters; ++i)
        {
            auto r = diff_snap.detect_renames(before_snap, after_snap);
            REQUIRE_EQ(r.size(), n);
        }
        auto t1 = std::chrono::steady_clock::now();
        const auto total_us =
            std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
        std::cout << "rename-storm-bench S1 unhashed: files=" << n << " unchanged=" << n
                  << " iters=" << iters << " total_us=" << total_us
                  << " us_per_run=" << total_us / iters << "\n";
    }
    // S2: hashed capture — the same (size, mtime) storm, but distinct content
    // isolates every pair in its own hash bucket.
    {
        auto renames = diff_h_snap.detect_renames(before_h_snap, after_h_snap);  // warm-up
        REQUIRE_EQ(renames.size(), n);
        auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < iters; ++i)
        {
            auto r = diff_h_snap.detect_renames(before_h_snap, after_h_snap);
            REQUIRE_EQ(r.size(), n);
        }
        auto t1 = std::chrono::steady_clock::now();
        const auto total_us =
            std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
        std::cout << "rename-storm-bench S2 hashed: files=" << n << " unchanged=" << n
                  << " iters=" << iters << " total_us=" << total_us
                  << " us_per_run=" << total_us / iters << "\n";
    }
}
