#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <pjh_platform/directory_snapshot.hpp>
#include <pjh_platform/error.hpp>
#include <pjh_platform/fs.hpp>
#include <pjh_platform/platform.hpp>
#include <string>
#include <string_view>
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

TEST_CASE("DirectorySnapshot follows symbolic links to directories")
{
#if PJH_PLATFORM_UNIX
    auto p = make_test_dir();
    REQUIRE(std::filesystem::create_directories(p / "real"));
    std::error_code sec;
    std::filesystem::create_symlink(p / "real", p / "link_to_dir", sec);
    REQUIRE_FALSE(sec);
    REQUIRE(pjh::platform::Fs::write_file(p / "real" / "inner.txt", "x").is_ok());

    auto r = DirectorySnapshot::capture(p);
    REQUIRE(r.is_ok());
    auto snap = std::move(r).unwrap();

    auto link = snap.get("link_to_dir");
    REQUIRE(link.has_value());
    CHECK(link->m_is_directory);
#endif
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
    auto expected_ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(mtime.time_since_epoch()).count();

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

namespace
{
    // Deterministic pseudo-random fill (xorshift64) so every run hashes the
    // same bytes; expected vectors below were generated independently in
    // Python against the same rule.
    void fill_pattern(std::size_t n, std::vector<std::uint8_t> *out)
    {
        out->resize(n);
        std::uint64_t state = 0x9E3779B97F4A7C15ULL;
        for (auto &b : *out)
        {
            state ^= state << 13;
            state ^= state >> 7;
            state ^= state << 17;
            b = static_cast<std::uint8_t>(state & 0xFFU);
        }
    }

    auto write_pattern_file(const std::filesystem::path &path, std::size_t n) -> bool
    {
        std::vector<std::uint8_t> pattern;
        fill_pattern(n, &pattern);
        return pjh::platform::Fs::write_file(
                   path, std::string_view(reinterpret_cast<const char *>(pattern.data()), n))
            .is_ok();
    }
}  // namespace

TEST_CASE("Fnv1a64Hasher matches known FNV-1a 64-bit vectors")
{
    auto p = make_test_dir();

    struct V
    {
        const char *name;
        std::uint64_t n;
        std::uint64_t expected;
    };
    // The first three are the canonical FNV-1a 64-bit vectors from the FNV
    // specification; the pattern files exercise 8-byte chunk boundaries and
    // the tail (8 = one full chunk, 9 = chunk + one tail byte).
    const V vectors[] = {
        {"empty.bin", 0, 0xcbf29ce484222325ULL},
        {"a.bin", 1, 0xaf63dc4c8601ec8cULL},
        {"foobar.bin", 6, 0x85944171f73967e8ULL},
        {"pat8.bin", 8, 0x67fefdd5a8579d36ULL},
        {"pat9.bin", 9, 0x0de70f0d0ce10827ULL},
        {"pat1000.bin", 1000, 0xab06f176b84e76d3ULL},
        {"pat65539.bin", 65539, 0xd1a1b8d872d3fb5dULL},
        {"pat1mib.bin", 1048576, 0xf0feed127f45c95dULL},
    };

    Fnv1a64Hasher hasher;
    for (const auto &v : vectors)
    {
        auto path = p / v.name;
        if (v.n == 1)
            REQUIRE(pjh::platform::Fs::write_file(path, "a").is_ok());
        else if (v.n == 6)
            REQUIRE(pjh::platform::Fs::write_file(path, "foobar").is_ok());
        else
            REQUIRE(write_pattern_file(path, v.n));
        auto h = hasher(path);
        REQUIRE(h.has_value());
        CHECK_EQ(*h, v.expected);
    }
}

TEST_CASE("Fnv1a64Hasher benchmark gated by PJH_HASH_BENCH_BYTES")
{
    // Baseline / regression benchmark for Fnv1a64Hasher. The measurement and
    // its stdout output happen only when PJH_HASH_BENCH_BYTES names the file
    // size to use; a plain `ctest` run is a no-op and stays silent.
    const char *raw = std::getenv("PJH_HASH_BENCH_BYTES");
    if (raw == nullptr)
        return;
    const auto bytes = static_cast<std::size_t>(std::strtoull(raw, nullptr, 10));
    REQUIRE(bytes >= 8u * 1024u);

    auto p = make_test_dir();
    REQUIRE(write_pattern_file(p / "bench.bin", bytes));

    Fnv1a64Hasher hasher;
    (void)hasher(p / "bench.bin");  // warm-up

    const int iters = 3;
    auto t0 = std::chrono::steady_clock::now();
    FileHash last = 0;
    for (int i = 0; i < iters; ++i)
    {
        auto h = hasher(p / "bench.bin");
        REQUIRE(h.has_value());
        last = *h;
    }
    auto t1 = std::chrono::steady_clock::now();
    const auto total_us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
    const double mb_per_s =
        static_cast<double>(bytes) / (static_cast<double>(total_us) * 1e-6) / (1024.0 * 1024.0);
    std::cout << "hash-bench single-file: bytes=" << bytes << " iters=" << iters
              << " total_us=" << total_us << " us_per_run=" << total_us / iters
              << " mb_per_s=" << std::fixed << std::setprecision(1) << mb_per_s << " last_hash=0x"
              << std::hex << last << std::dec << "\n";

    // Multi-file: a fixed 4 MiB total as 16 KiB files (independent of the
    // single-file size above), hashed through DirectorySnapshot::capture —
    // the realistic snapshot path.
    const std::size_t file_size = 16 * 1024;
    const std::size_t multi_total = 4 * 1024 * 1024;
    auto subdir = p / "multi";
    REQUIRE(std::filesystem::create_directories(subdir));
    std::size_t written = 0;
    int files = 0;
    while (written + file_size <= multi_total)
    {
        REQUIRE(write_pattern_file(subdir / ("f" + std::to_string(files) + ".bin"), file_size));
        written += file_size;
        ++files;
    }
    REQUIRE(files > 0);
    (void)DirectorySnapshot::capture(subdir, nullptr);  // warm-up

    auto m0 = std::chrono::steady_clock::now();
    for (int i = 0; i < iters; ++i)
    {
        auto r = DirectorySnapshot::capture(subdir, nullptr);
        REQUIRE(r.is_ok());
    }
    auto m1 = std::chrono::steady_clock::now();
    const auto mtotal_us = std::chrono::duration_cast<std::chrono::microseconds>(m1 - m0).count();
    const double mmb_per_s =
        static_cast<double>(written) / (static_cast<double>(mtotal_us) * 1e-6) / (1024.0 * 1024.0);
    std::cout << "hash-bench multi-file: files=" << files << " bytes=" << written
              << " iters=" << iters << " total_us=" << mtotal_us
              << " us_per_run=" << mtotal_us / iters << " mb_per_s=" << mmb_per_s << "\n";
}
