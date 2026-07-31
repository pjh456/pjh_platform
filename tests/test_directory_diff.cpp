#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
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
        auto p = pjh::platform::Fs::temp_directory() /
                 (std::string("pjh_platform_diff_test_") + name);
        std::filesystem::remove_all(p);
        std::filesystem::create_directories(p);
        return p;
    }

    auto has_change(
        const std::vector<DirectoryDiff::Change> &changes,
        DirectoryDiff::ChangeKind kind,
        const std::filesystem::path &full_path) -> bool
    {
        return std::any_of(changes.begin(), changes.end(), [&](const auto &c) {
            return c.m_kind == kind && c.m_full_path == full_path;
        });
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
    // to guarantee the setup below exercises a changed directory mtime.
    std::error_code ec;
    std::filesystem::last_write_time(
        p / "sub", std::filesystem::file_time_type::clock::now(), ec);
    REQUIRE_FALSE(ec);

    auto after = DirectorySnapshot::capture(p);
    REQUIRE(after.is_ok());

    auto before_snap = before.unwrap();
    auto after_snap = after.unwrap();
    REQUIRE_NE(
        before_snap.get("sub")->m_mtime_ns, after_snap.get("sub")->m_mtime_ns);

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
