#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <pjh_platform/directory_snapshot.hpp>
#include <pjh_platform/error.hpp>
#include <pjh_platform/fs.hpp>
#include <pjh_platform/platform.hpp>
#include <string>
#include <utility>
#include <vector>

using pjh::platform::DirectorySnapshot;
using pjh::platform::ErrorCode;
using pjh::platform::FileHash;
using pjh::platform::Fnv1a64Hasher;

namespace
{
    auto make_test_dir() -> std::filesystem::path
    {
        auto p = pjh::platform::Fs::temp_directory() / "pjh_platform_snapshot_test";
        std::filesystem::remove_all(p);
        std::filesystem::create_directories(p);
        return p;
    }
}  // namespace

TEST_CASE("DirectorySnapshot captures empty directory")
{
    auto p = make_test_dir();
    auto r = DirectorySnapshot::capture(p);
    REQUIRE(r.is_ok());
    auto snap = std::move(r).unwrap();
    CHECK_EQ(snap.file_count(), 0u);
    CHECK_EQ(snap.dir_count(), 0u);
    CHECK(snap.entries().empty());
    CHECK_EQ(snap.dir_path(), std::filesystem::absolute(p).lexically_normal());
}

TEST_CASE("DirectorySnapshot captures files and subdirectories")
{
    auto p = make_test_dir();
    REQUIRE(pjh::platform::Fs::write_file(p / "a.txt", "aaa").is_ok());
    REQUIRE(pjh::platform::Fs::write_file(p / "b.txt", "bbbb").is_ok());
    REQUIRE(std::filesystem::create_directories(p / "sub"));

    auto r = DirectorySnapshot::capture(p);
    REQUIRE(r.is_ok());
    auto snap = std::move(r).unwrap();

    CHECK_EQ(snap.file_count(), 2u);
    CHECK_EQ(snap.dir_count(), 1u);
    CHECK(snap.contains("a.txt"));
    CHECK(snap.contains("b.txt"));
    CHECK(snap.contains("sub"));
    CHECK_FALSE(snap.contains("missing.txt"));

    auto a = snap.get("a.txt");
    REQUIRE(a.has_value());
    CHECK_FALSE(a->m_is_directory);
    CHECK_EQ(a->m_file_size, 3u);

    auto sub = snap.get("sub");
    REQUIRE(sub.has_value());
    CHECK(sub->m_is_directory);

    auto names = snap.filenames();
    CHECK_EQ(names.size(), 3u);
    CHECK(std::find(names.begin(), names.end(), std::filesystem::path("a.txt")) != names.end());
    CHECK(std::find(names.begin(), names.end(), std::filesystem::path("sub")) != names.end());
}

TEST_CASE("DirectorySnapshot reports NotFound for a missing directory")
{
    auto r = DirectorySnapshot::capture("/nonexistent_snapshot_dir_12345");
    REQUIRE(r.is_err());
    CHECK_EQ(r.unwrap_err(), ErrorCode::NotFound);
}

TEST_CASE("DirectorySnapshot reports InvalidArgument for a regular file")
{
    auto p = make_test_dir();
    auto file = p / "file.txt";
    REQUIRE(pjh::platform::Fs::write_file(file, "x").is_ok());
    auto r = DirectorySnapshot::capture(file);
    REQUIRE(r.is_err());
    CHECK_EQ(r.unwrap_err(), ErrorCode::InvalidArgument);
}

TEST_CASE("DirectorySnapshot without hashing leaves hash empty")
{
    auto p = make_test_dir();
    REQUIRE(pjh::platform::Fs::write_file(p / "a.txt", "hello").is_ok());

    auto r = DirectorySnapshot::capture(p);
    REQUIRE(r.is_ok());
    auto snap = std::move(r).unwrap();
    auto entry = snap.get("a.txt");
    REQUIRE(entry.has_value());
    CHECK_FALSE(entry->m_hash.has_value());
}

TEST_CASE("DirectorySnapshot hashes only the listed files")
{
    auto p = make_test_dir();
    REQUIRE(pjh::platform::Fs::write_file(p / "a.txt", "hello").is_ok());
    REQUIRE(pjh::platform::Fs::write_file(p / "b.txt", "world").is_ok());
    REQUIRE(std::filesystem::create_directories(p / "sub"));

    std::vector<std::filesystem::path> hash_files{std::filesystem::path("a.txt")};
    auto r = DirectorySnapshot::capture(p, &hash_files);
    REQUIRE(r.is_ok());
    auto snap = std::move(r).unwrap();

    auto a = snap.get("a.txt");
    REQUIRE(a.has_value());
    REQUIRE(a->m_hash.has_value());

    auto b = snap.get("b.txt");
    REQUIRE(b.has_value());
    CHECK_FALSE(b->m_hash.has_value());

    auto sub = snap.get("sub");
    REQUIRE(sub.has_value());
    CHECK_FALSE(sub->m_hash.has_value());
}

TEST_CASE("DirectorySnapshot with null hash list hashes every file")
{
    auto p = make_test_dir();
    REQUIRE(pjh::platform::Fs::write_file(p / "a.txt", "hello").is_ok());
    REQUIRE(pjh::platform::Fs::write_file(p / "b.txt", "world").is_ok());

    auto r = DirectorySnapshot::capture(p, nullptr);
    REQUIRE(r.is_ok());
    auto snap = std::move(r).unwrap();
    for (const auto &name : {"a.txt", "b.txt"})
    {
        auto entry = snap.get(name);
        REQUIRE(entry.has_value());
        CHECK(entry->m_hash.has_value());
    }
}

TEST_CASE("DirectorySnapshot default hasher matches Fnv1a64Hasher")
{
    auto p = make_test_dir();
    REQUIRE(pjh::platform::Fs::write_file(p / "a.txt", "same content").is_ok());

    std::vector<std::filesystem::path> hash_files{std::filesystem::path("a.txt")};
    auto via_default = DirectorySnapshot::capture(p, &hash_files);
    REQUIRE(via_default.is_ok());
    auto via_explicit = DirectorySnapshot::capture(p, &hash_files, Fnv1a64Hasher{});
    REQUIRE(via_explicit.is_ok());

    auto lhs = via_default.unwrap().get("a.txt");
    auto rhs = via_explicit.unwrap().get("a.txt");
    REQUIRE(lhs.has_value());
    REQUIRE(rhs.has_value());
    CHECK(lhs->m_hash == rhs->m_hash);
}

TEST_CASE("DirectorySnapshot hashes same content equally")
{
    auto p = make_test_dir();
    REQUIRE(pjh::platform::Fs::write_file(p / "x1.txt", "identical").is_ok());
    REQUIRE(pjh::platform::Fs::write_file(p / "x2.txt", "identical").is_ok());
    REQUIRE(pjh::platform::Fs::write_file(p / "x3.txt", "different").is_ok());

    auto r = DirectorySnapshot::capture(p, nullptr);
    REQUIRE(r.is_ok());
    auto snap = std::move(r).unwrap();

    auto h1 = snap.get("x1.txt")->m_hash;
    auto h2 = snap.get("x2.txt")->m_hash;
    auto h3 = snap.get("x3.txt")->m_hash;
    REQUIRE(h1.has_value());
    REQUIRE(h2.has_value());
    REQUIRE(h3.has_value());
    CHECK_EQ(*h1, *h2);
    CHECK_NE(*h1, *h3);
}

TEST_CASE("DirectorySnapshot captures last-write times and sizes")
{
    auto p = make_test_dir();
    REQUIRE(pjh::platform::Fs::write_file(p / "data.bin", "0123456789").is_ok());

    std::error_code ec;
    auto mtime = std::filesystem::last_write_time(p / "data.bin", ec);
    REQUIRE_FALSE(ec);
    auto expected_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                           mtime.time_since_epoch())
                           .count();

    auto r = DirectorySnapshot::capture(p);
    REQUIRE(r.is_ok());
    auto snap = std::move(r).unwrap();
    auto entry = snap.get("data.bin");
    REQUIRE(entry.has_value());
    CHECK_EQ(entry->m_file_size, 10u);
    CHECK_EQ(entry->m_mtime_ns, expected_ns);
}

namespace
{
    struct ReverseHasher
    {
        auto operator()(const std::filesystem::path &p) const -> std::optional<FileHash>
        {
            auto content = pjh::platform::Fs::read_file(p);
            if (content.is_err())
                return std::nullopt;
            return static_cast<FileHash>(content.unwrap().size());
        }
    };
}  // namespace

TEST_CASE("DirectorySnapshot supports a custom hasher strategy")
{
    auto p = make_test_dir();
    REQUIRE(pjh::platform::Fs::write_file(p / "a.txt", "abc").is_ok());
    REQUIRE(pjh::platform::Fs::write_file(p / "b.txt", "abcde").is_ok());

    auto r = DirectorySnapshot::capture(p, nullptr, ReverseHasher{});
    REQUIRE(r.is_ok());
    auto snap = std::move(r).unwrap();

    auto a = snap.get("a.txt");
    auto b = snap.get("b.txt");
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    REQUIRE(a->m_hash.has_value());
    REQUIRE(b->m_hash.has_value());
    CHECK_EQ(*a->m_hash, 3u);
    CHECK_EQ(*b->m_hash, 5u);
}

TEST_CASE("DirectorySnapshot reports PermissionDenied for an unreadable directory")
{
    auto p = make_test_dir();
    auto locked = p / "locked";
    REQUIRE(std::filesystem::create_directories(locked));
    REQUIRE(pjh::platform::Fs::write_file(locked / "secret.txt", "top secret").is_ok());

#if PJH_PLATFORM_UNIX
    std::filesystem::permissions(locked, std::filesystem::perms::owner_exec);
    auto r = DirectorySnapshot::capture(locked);
    std::filesystem::permissions(locked, std::filesystem::perms::owner_all);
    REQUIRE(r.is_err());
    CHECK_EQ(r.unwrap_err(), ErrorCode::PermissionDenied);
#endif
}
