#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <pjh_platform/error.hpp>
#include <pjh_platform/file_watcher.hpp>
#include <pjh_platform/fs.hpp>
#include <pjh_platform/platform.hpp>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#if PJH_PLATFORM_LINUX
#include <fcntl.h>
#include <unistd.h>

#include <cstdio>
#include <fstream>
#include <set>
#endif

using pjh::platform::ErrorCode;
using pjh::platform::FileEvent;
using pjh::platform::FileEventKind;
using pjh::platform::FileWatcher;

namespace
{
    auto make_test_dir() -> std::filesystem::path
    {
        auto p = pjh::platform::Fs::temp_directory() / "pjh_platform_watch_test";
        std::filesystem::remove_all(p);
        std::filesystem::create_directories(p);
        return p;
    }

    auto has_event(
        const std::vector<FileEvent> &events, FileEventKind kind, const std::filesystem::path &path)
        -> bool
    {
        return std::any_of(
            events.begin(), events.end(),
            [&](const FileEvent &e) { return e.kind == kind && e.path == path; });
    }

    template <typename Pred>
    auto collect_until(FileWatcher &w, Pred pred, int attempts = 200) -> std::vector<FileEvent>
    {
        std::vector<FileEvent> all;
        for (int i = 0; i < attempts; ++i)
        {
            auto r = w.poll(std::chrono::milliseconds(10));
            if (r.is_ok())
            {
                auto batch = std::move(r).unwrap();
                for (auto &e : batch) all.push_back(std::move(e));
                if (pred(all))
                    break;
            }
        }
        return all;
    }
}  // namespace

TEST_CASE("FileWatcher poll with no watch returns empty")
{
    FileWatcher w;
    auto r = w.poll(std::chrono::milliseconds(10));
    REQUIRE(r.is_ok());
    CHECK(r.unwrap().empty());
}

TEST_CASE("FileWatcher poll times out with empty result")
{
    auto p = make_test_dir();
    FileWatcher w;
    REQUIRE(w.add(p, false).is_ok());
    auto r = w.poll(std::chrono::milliseconds(50));
    REQUIRE(r.is_ok());
    CHECK(r.unwrap().empty());
}

TEST_CASE("FileWatcher add returns NotFound for a missing path")
{
    FileWatcher w;
    auto r = w.add("/nonexistent_path_12345");
    CHECK(r.is_err());
    CHECK_EQ(r.unwrap_err(), ErrorCode::NotFound);
}

TEST_CASE("FileWatcher add rejects duplicate watches")
{
    auto p = make_test_dir();
    FileWatcher w;
    REQUIRE(w.add(p, false).is_ok());
    auto dup = w.add(p, false);
    CHECK(dup.is_err());
    CHECK_EQ(dup.unwrap_err(), ErrorCode::AlreadyWatched);
}

TEST_CASE("FileWatcher reports file creation")
{
    auto p = make_test_dir();
    FileWatcher w;
    REQUIRE(w.add(p, false).is_ok());

    auto file = p / "created.txt";
    REQUIRE(pjh::platform::Fs::write_file(file, "hello").is_ok());

    auto events = collect_until(
        w, [&](const auto &all) { return has_event(all, FileEventKind::Created, file); });
    CHECK(has_event(events, FileEventKind::Created, file));
}

TEST_CASE("FileWatcher reports file modification")
{
    auto p = make_test_dir();
    FileWatcher w;
    REQUIRE(w.add(p, false).is_ok());

    auto file = p / "data.txt";
    REQUIRE(pjh::platform::Fs::write_file(file, "version one").is_ok());
    collect_until(w, [&](const auto &all) { return has_event(all, FileEventKind::Created, file); });

    REQUIRE(pjh::platform::Fs::write_file(file, "a longer version two").is_ok());
    auto events = collect_until(
        w, [&](const auto &all) { return has_event(all, FileEventKind::Modified, file); });
    CHECK(has_event(events, FileEventKind::Modified, file));
}

TEST_CASE("FileWatcher reports file deletion")
{
    auto p = make_test_dir();
    FileWatcher w;
    REQUIRE(w.add(p, false).is_ok());

    auto file = p / "doomed.txt";
    REQUIRE(pjh::platform::Fs::write_file(file, "temp").is_ok());
    collect_until(w, [&](const auto &all) { return has_event(all, FileEventKind::Created, file); });

    REQUIRE(std::filesystem::remove(file));
    auto events = collect_until(
        w, [&](const auto &all) { return has_event(all, FileEventKind::Deleted, file); });
    CHECK(has_event(events, FileEventKind::Deleted, file));
}

TEST_CASE("FileWatcher watching a file only reports that file")
{
    auto p = make_test_dir();
    auto file = p / "watched.txt";
    REQUIRE(pjh::platform::Fs::write_file(file, "content").is_ok());

    FileWatcher w;
    REQUIRE(w.add(file, false).is_ok());

    REQUIRE(pjh::platform::Fs::write_file(file, "updated content").is_ok());
    auto events = collect_until(
        w, [&](const auto &all) { return has_event(all, FileEventKind::Modified, file); });
    CHECK(has_event(events, FileEventKind::Modified, file));

    // A sibling is created in the same parent directory; a file watch must
    // not report it.
    auto sibling = p / "sibling.txt";
    REQUIRE(pjh::platform::Fs::write_file(sibling, "x").is_ok());
    for (int i = 0; i < 5; ++i)
    {
        auto r = w.poll(std::chrono::milliseconds(10));
        REQUIRE(r.is_ok());
        for (const auto &e : r.unwrap())
        {
            CHECK(e.path != sibling);
            CHECK(e.path != p);
        }
    }
}

TEST_CASE("FileWatcher file watch reports deletion of the watched file")
{
    auto p = make_test_dir();
    auto file = p / "doomed_watched.txt";
    REQUIRE(pjh::platform::Fs::write_file(file, "content").is_ok());

    FileWatcher w;
    REQUIRE(w.add(file, false).is_ok());

    REQUIRE(std::filesystem::remove(file));
    auto events = collect_until(
        w, [&](const auto &all) { return has_event(all, FileEventKind::Deleted, file); });
    CHECK(has_event(events, FileEventKind::Deleted, file));
}

TEST_CASE("FileWatcher file watch reports rename of the watched file")
{
    auto p = make_test_dir();
    auto src = p / "old_watched.txt";
    auto dst = p / "new_watched.txt";
    REQUIRE(pjh::platform::Fs::write_file(src, "content").is_ok());

    FileWatcher w;
    REQUIRE(w.add(src, false).is_ok());

    REQUIRE(pjh::platform::Fs::rename(src, dst).is_ok());
    auto events = collect_until(
        w,
        [&](const auto &all)
        {
            return std::any_of(
                all.begin(), all.end(),
                [&](const FileEvent &e)
                {
                    return (e.kind == FileEventKind::MovedFrom ||
                            e.kind == FileEventKind::Deleted) &&
                           e.path == src;
                });
        });
    CHECK(
        std::any_of(
            events.begin(), events.end(),
            [&](const FileEvent &e)
            {
                return (e.kind == FileEventKind::MovedFrom || e.kind == FileEventKind::Deleted) &&
                       e.path == src;
            }));
}

TEST_CASE("FileWatcher recursive watch reports events in subdirectories")
{
    auto p = make_test_dir();
    FileWatcher w;
    REQUIRE(w.add(p, true).is_ok());

    auto sub = p / "sub";
    std::filesystem::create_directories(sub);
    collect_until(w, [&](const auto &all) { return has_event(all, FileEventKind::Created, sub); });

    auto file = sub / "nested.txt";
    REQUIRE(pjh::platform::Fs::write_file(file, "nested").is_ok());
    auto events = collect_until(
        w, [&](const auto &all) { return has_event(all, FileEventKind::Created, file); });
    CHECK(has_event(events, FileEventKind::Created, file));
}

TEST_CASE("FileWatcher reports file rename")
{
    auto p = make_test_dir();
    FileWatcher w;
    REQUIRE(w.add(p, false).is_ok());

    auto src = p / "old.txt";
    auto dst = p / "new.txt";
    REQUIRE(pjh::platform::Fs::write_file(src, "rename me").is_ok());
    collect_until(w, [&](const auto &all) { return has_event(all, FileEventKind::Created, src); });

    REQUIRE(pjh::platform::Fs::rename(src, dst).is_ok());
    auto events = collect_until(
        w,
        [&](const auto &all)
        {
            bool old_seen = std::any_of(
                all.begin(), all.end(),
                [&](const FileEvent &e)
                {
                    return (e.kind == FileEventKind::MovedFrom ||
                            e.kind == FileEventKind::Deleted) &&
                           e.path == src;
                });
            bool new_seen = std::any_of(
                all.begin(), all.end(),
                [&](const FileEvent &e)
                {
                    return (e.kind == FileEventKind::MovedTo || e.kind == FileEventKind::Created) &&
                           e.path == dst;
                });
            return old_seen && new_seen;
        });
    bool old_seen = std::any_of(
        events.begin(), events.end(),
        [&](const FileEvent &e)
        {
            return (e.kind == FileEventKind::MovedFrom || e.kind == FileEventKind::Deleted) &&
                   e.path == src;
        });
    bool new_seen = std::any_of(
        events.begin(), events.end(),
        [&](const FileEvent &e)
        {
            return (e.kind == FileEventKind::MovedTo || e.kind == FileEventKind::Created) &&
                   e.path == dst;
        });
    CHECK(old_seen);
    CHECK(new_seen);
}

TEST_CASE("FileWatcher remove stops watching")
{
    auto p = make_test_dir();
    FileWatcher w;
    REQUIRE(w.add(p, false).is_ok());

    auto r = w.remove(p);
    CHECK(r.is_ok());
    CHECK(w.remove(p).is_err());

    auto file = p / "after_remove.txt";
    REQUIRE(pjh::platform::Fs::write_file(file, "x").is_ok());
    auto events = collect_until(
        w, [&](const auto &all) { return has_event(all, FileEventKind::Created, file); }, 10);
    CHECK_FALSE(has_event(events, FileEventKind::Created, file));
}

TEST_CASE("FileWatcher removing a directory watch keeps the file watch alive")
{
    auto p = make_test_dir();
    auto file = p / "shared_watched.txt";
    REQUIRE(pjh::platform::Fs::write_file(file, "content").is_ok());

    FileWatcher w;
    REQUIRE(w.add(p, false).is_ok());
    REQUIRE(w.add(file, false).is_ok());
    CHECK(w.remove(p).is_ok());

    // The file watch must survive the removal of the sibling directory watch.
    REQUIRE(pjh::platform::Fs::write_file(file, "updated content").is_ok());
    auto events = collect_until(
        w, [&](const auto &all) { return has_event(all, FileEventKind::Modified, file); });
    CHECK(has_event(events, FileEventKind::Modified, file));

    // ...and it must still filter sibling changes.
    auto sibling = p / "sibling_after_dir_remove.txt";
    REQUIRE(pjh::platform::Fs::write_file(sibling, "x").is_ok());
    for (int i = 0; i < 5; ++i)
    {
        auto r = w.poll(std::chrono::milliseconds(10));
        REQUIRE(r.is_ok());
        for (const auto &e : r.unwrap()) CHECK(e.path != sibling);
    }
}

TEST_CASE("FileWatcher removing a file watch keeps the directory watch alive")
{
    auto p = make_test_dir();
    auto file = p / "shared_watched.txt";
    REQUIRE(pjh::platform::Fs::write_file(file, "content").is_ok());

    FileWatcher w;
    REQUIRE(w.add(p, false).is_ok());
    REQUIRE(w.add(file, false).is_ok());
    CHECK(w.remove(file).is_ok());

    // The directory watch must survive the removal of the sibling file watch.
    auto created = p / "created_after_file_remove.txt";
    REQUIRE(pjh::platform::Fs::write_file(created, "x").is_ok());
    auto events = collect_until(
        w, [&](const auto &all) { return has_event(all, FileEventKind::Created, created); });
    CHECK(has_event(events, FileEventKind::Created, created));
}

TEST_CASE("FileWatcher close is idempotent and releases resources")
{
    auto p = make_test_dir();
    FileWatcher w;
    REQUIRE(w.add(p, false).is_ok());
    w.close();
    w.close();
    auto r = w.poll(std::chrono::milliseconds(10));
    CHECK(r.is_err());
}

TEST_CASE("FileWatcher move transfers the watch")
{
    auto p = make_test_dir();
    FileWatcher w;
    REQUIRE(w.add(p, false).is_ok());

    FileWatcher moved(std::move(w));
    auto file = p / "moved.txt";
    REQUIRE(pjh::platform::Fs::write_file(file, "moved").is_ok());
    auto events = collect_until(
        moved, [&](const auto &all) { return has_event(all, FileEventKind::Created, file); });
    CHECK(has_event(events, FileEventKind::Created, file));
}

TEST_CASE("FileWatcher move-assignment into a live watcher takes the other's watches")
{
    auto p = make_test_dir();
    FileWatcher w;
    REQUIRE(w.add(p, false).is_ok());

    auto before = p / "before.txt";
    REQUIRE(pjh::platform::Fs::write_file(before, "x").is_ok());
    collect_until(
        w, [&](const auto &all) { return has_event(all, FileEventKind::Created, before); });

    FileWatcher w2;
    REQUIRE(w2.add(p, false).is_ok());
    w = std::move(w2);  // w's own watch must be released, not destroyed

    auto after = p / "after.txt";
    REQUIRE(pjh::platform::Fs::write_file(after, "y").is_ok());
    auto events = collect_until(
        w, [&](const auto &all) { return has_event(all, FileEventKind::Created, after); });
    CHECK(has_event(events, FileEventKind::Created, after));

    // The moved-from watcher is dead by contract: poll/remove fail with
    // InvalidArgument, close() is a no-op, and the destructor is safe.
    auto r = w2.poll(std::chrono::milliseconds(10));
    CHECK(r.is_err());
    CHECK_EQ(r.unwrap_err(), ErrorCode::InvalidArgument);
    CHECK(w2.remove(p).is_err());
    w2.close();
    CHECK(w2.poll(std::chrono::milliseconds(10)).is_err());
}

TEST_CASE("FileWatcher add on a moved-from watcher fails with InvalidArgument")
{
    auto p = make_test_dir();
    FileWatcher w;
    REQUIRE(w.add(p, false).is_ok());

    FileWatcher w2;
    w2 = std::move(w);  // w is moved-from; the watch now lives in w2

    auto r = w.add(p, false);
    CHECK(r.is_err());
    CHECK_EQ(r.unwrap_err(), ErrorCode::InvalidArgument);

    // The destination keeps the live watch.
    auto f = p / "dst.txt";
    REQUIRE(pjh::platform::Fs::write_file(f, "x").is_ok());
    auto events = collect_until(
        w2, [&](const auto &all) { return has_event(all, FileEventKind::Created, f); });
    CHECK(has_event(events, FileEventKind::Created, f));
}

#if PJH_PLATFORM_WINDOWS
TEST_CASE("FileWatcher remove of one watch leaves the other watch active")
{
    auto p = make_test_dir();
    auto dir_a = p / "a";
    auto dir_b = p / "b";
    REQUIRE(std::filesystem::create_directories(dir_a));
    REQUIRE(std::filesystem::create_directories(dir_b));

    FileWatcher w;
    REQUIRE(w.add(dir_a, false).is_ok());
    REQUIRE(w.add(dir_b, false).is_ok());

    // Change 1 on A: consumed by poll, so A's read is re-issued.
    auto f1 = dir_a / "f1.txt";
    REQUIRE(pjh::platform::Fs::write_file(f1, "one").is_ok());
    collect_until(w, [&](const auto &all) { return has_event(all, FileEventKind::Created, f1); });

    // Change 2 on A completes A's in-flight read; give the completion packet
    // time to queue on the shared port before remove(dir_b) drains it.
    auto f2 = dir_a / "f2.txt";
    REQUIRE(pjh::platform::Fs::write_file(f2, "two").is_ok());
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    REQUIRE(w.remove(dir_b).is_ok());

    // A must still be listening: change 3 is reportable.
    auto f3 = dir_a / "f3.txt";
    REQUIRE(pjh::platform::Fs::write_file(f3, "three").is_ok());
    auto events = collect_until(
        w, [&](const auto &all) { return has_event(all, FileEventKind::Created, f3); });
    CHECK(has_event(events, FileEventKind::Created, f3));

    std::error_code ec;
    std::filesystem::remove_all(p, ec);
}

TEST_CASE("FileWatcher reports NotFound when the watched directory is removed")
{
    auto p = make_test_dir();
    auto dir = p / "gone";
    REQUIRE(std::filesystem::create_directories(dir));

    FileWatcher w;
    REQUIRE(w.add(dir, false).is_ok());

    REQUIRE(std::filesystem::remove_all(dir));

    // The in-flight read on the removed directory fails with a path-gone code
    // (ERROR_BROKEN_PIPE on NTFS); the failure must map to NotFound, not
    // Unknown, and the watch must then stay quietly dead instead of going
    // deaf or re-failing on every poll.
    ErrorCode err = ErrorCode::Success;
    for (int i = 0; i < 200; ++i)
    {
        auto r = w.poll(std::chrono::milliseconds(10));
        if (r.is_err())
        {
            err = r.unwrap_err();
            break;
        }
    }
    CHECK(err != ErrorCode::Success);
    CHECK_EQ(err, ErrorCode::NotFound);
    CHECK(w.poll(std::chrono::milliseconds(10)).is_ok());

    // The dead watch can be removed and the path re-registered.
    CHECK(w.remove(dir).is_ok());
    REQUIRE(std::filesystem::create_directories(dir));
    REQUIRE(w.add(dir, false).is_ok());
    auto f = dir / "after_readd.txt";
    REQUIRE(pjh::platform::Fs::write_file(f, "x").is_ok());
    auto events = collect_until(
        w, [&](const auto &all) { return has_event(all, FileEventKind::Created, f); });
    CHECK(has_event(events, FileEventKind::Created, f));

    std::error_code ec;
    std::filesystem::remove_all(p, ec);
}
#endif

#if PJH_PLATFORM_LINUX
namespace
{
    // The per-instance inotify queue limit (max_queued_events); 0 if it
    // cannot be read from /proc.
    auto read_inotify_limit() -> int
    {
        std::ifstream limit_file("/proc/sys/fs/inotify/max_queued_events");
        int limit = 0;
        if (!(limit_file >> limit) || limit <= 0)
            return 0;
        return limit;
    }

    // Poll until a batch comes back empty, appending every batch to `out`;
    // returns false when a poll fails or 200 polls (2 s) did not empty the
    // queue.
    auto drain_until_empty(FileWatcher &w, std::vector<FileEvent> &out) -> bool
    {
        for (int i = 0; i < 200; ++i)
        {
            auto r = w.poll(std::chrono::milliseconds(10));
            if (r.is_err())
                return false;
            auto batch = std::move(r).unwrap();
            for (auto &e : batch) out.push_back(std::move(e));
            if (batch.empty())
                return true;
        }
        return false;
    }
}  // namespace

TEST_CASE("FileWatcher recovers events lost to an inotify queue overflow")
{
    // Real inotify queue overflow, self-checking. N = limit + 200
    // distinct-named creates are enqueued without any poll() call:
    // FileWatcher is single-threaded (the consumer polls, no internal
    // thread), so the burst is a deterministic queue-depth effect, not a
    // race. Each distinct name is exactly one IN_CREATE record (distinct
    // names are never coalesced, man7 NOTES); the per-instance queue
    // capacity is limit (max_queued_events at inotify_init time), so the
    // excess is dropped and an IN_Q_OVERFLOW record is always generated
    // (man7). The queue can hold at most limit < N records, so at least 200
    // flood names can only be reported through the resync diff: if that path
    // ever stops running, A1 below fails — the pin is self-validating. (The
    // replaced old case appended to a single file, whose burst the kernel
    // coalesced to ~1 record, so it never overflowed.)
    auto p = make_test_dir();
    FileWatcher w;
    REQUIRE(w.add(p, false).is_ok());

    // Seed phase: one file, then drain the queue to empty. The add()
    // baseline B0 = {} is never refreshed by the normal path (lazy
    // baseline), so the seed's Created is delivered directly while B0 still
    // lacks it — the pre-condition of the R1 Created-duplicate.
    auto seed = p / "seed.txt";
    REQUIRE(pjh::platform::Fs::write_file(seed, "seed").is_ok());
    std::vector<FileEvent> all;
    {
        auto pre = collect_until(
            w, [&](const auto &a) { return has_event(a, FileEventKind::Created, seed); });
        CHECK(has_event(pre, FileEventKind::Created, seed));
        for (auto &e : pre) all.push_back(std::move(e));
    }
    std::vector<FileEvent> rest;
    REQUIRE(drain_until_empty(w, rest));
    for (auto &e : rest) all.push_back(std::move(e));

    int seed_created = 0;
    for (const auto &e : all)
        if (e.kind == FileEventKind::Created && e.path == seed)
            ++seed_created;
    CHECK_EQ(seed_created, 1);

    int limit = read_inotify_limit();
    if (limit == 0 || limit > 131072)
        return;  // limit unreadable, or far above the default 16384:
                 // documented silent skip that bounds the worst-case wall
                 // clock; the default CI lane never hits this branch
    const int n = limit + 200;

    for (int i = 0; i < n; ++i)
    {
        char name[16] = {};
        std::snprintf(name, sizeof(name), "f_%06d", i);
        int fd = ::open((p / name).c_str(), O_CREAT | O_WRONLY, 0644);
        REQUIRE(fd >= 0);
        ::close(fd);
    }

    const std::size_t post = all.size();
    REQUIRE(drain_until_empty(w, all));

    // Index the post-flood Created events by path before the per-name
    // checks: a linear has_event over ~limit records per name would be
    // quadratic.
    std::set<std::filesystem::path> created_paths;
    for (std::size_t i = post; i < all.size(); ++i)
        if (all[i].kind == FileEventKind::Created)
            created_paths.insert(all[i].path);

    // A1: every flood name appears as Created — the self-check (see the
    // case comment).
    for (int i = 0; i < n; ++i)
    {
        char name[16] = {};
        std::snprintf(name, sizeof(name), "f_%06d", i);
        CHECK(created_paths.count(p / name) != 0);
    }

    // A2: the seed's Created was delivered directly once, and the resync
    // diff against B0 = {} re-reports it exactly once more — the R1
    // Created-duplicate degradation, independent of where the
    // IN_Q_OVERFLOW record sits in the drained stream.
    seed_created = 0;
    for (const auto &e : all)
        if (e.kind == FileEventKind::Created && e.path == seed)
            ++seed_created;
    CHECK_EQ(seed_created, 2);

    // A3: nothing was deleted in this case, so no Deleted may appear.
    CHECK_FALSE(
        std::any_of(
            all.begin(), all.end(),
            [](const FileEvent &e) { return e.kind == FileEventKind::Deleted; }));

    // A4: the flood only creates and the resync diff runs against B0 = {},
    // so the post-flood stream carries no Modified at all.
    CHECK_FALSE(
        std::any_of(
            all.begin() + post, all.end(),
            [](const FileEvent &e) { return e.kind == FileEventKind::Modified; }));

    // A5: the watcher survived the overflow resync — a fresh create is
    // delivered directly again.
    auto after = p / "after.txt";
    REQUIRE(pjh::platform::Fs::write_file(after, "after").is_ok());
    auto alive = collect_until(
        w, [&](const auto &a) { return has_event(a, FileEventKind::Created, after); });
    CHECK(has_event(alive, FileEventKind::Created, after));

    std::error_code ec;
    std::filesystem::remove_all(p, ec);
}

TEST_CASE("FileWatcher file watch is resynced when the shared inotify queue overflows")
{
    // A file entry has no baseline of its own: its overflow recovery is the
    // stat fallback of the resync early exit. A same-file burst cannot
    // overflow the shared queue (identical records coalesce into ~1
    // IN_MODIFY; see the same-file burst case further below), so the
    // overflow is triggered by the sibling directory entry's records on the
    // shared inotify instance. Self-checking: watched.txt is never
    // modified, so the pinned Modified below can only come from the file
    // branch's resync.
    auto p = make_test_dir();
    auto file = p / "watched.txt";
    REQUIRE(pjh::platform::Fs::write_file(file, "seed").is_ok());

    FileWatcher w;
    REQUIRE(w.add(p, false).is_ok());
    REQUIRE(w.add(file, false).is_ok());

    int limit = read_inotify_limit();
    if (limit == 0 || limit > 131072)
        return;  // limit unreadable, or far above the default 16384:
                 // documented silent skip that bounds the worst-case wall
                 // clock; the default CI lane never hits this branch
    const int n = limit + 200;

    // Same burst as the directory case: N distinct-named creates, no
    // poll() call, deterministic single-threaded overflow of the shared
    // queue.
    for (int i = 0; i < n; ++i)
    {
        char name[16] = {};
        std::snprintf(name, sizeof(name), "f_%06d", i);
        int fd = ::open((p / name).c_str(), O_CREAT | O_WRONLY, 0644);
        REQUIRE(fd >= 0);
        ::close(fd);
    }

    std::vector<FileEvent> all;
    REQUIRE(drain_until_empty(w, all));

    // B1: watched.txt is never modified, and the directory entry's diff
    // sees it unchanged against the add() baseline, so Modified(file) can
    // only be the file entry's resync early exit (the file exists).
    // Without a real shared-queue overflow, no Modified can appear at all.
    CHECK(has_event(all, FileEventKind::Modified, file));

    // B2: nothing was deleted in this case, so no Deleted may appear.
    CHECK_FALSE(
        std::any_of(
            all.begin(), all.end(),
            [](const FileEvent &e) { return e.kind == FileEventKind::Deleted; }));

    // B3: the resync re-attached the file watch to the same inode
    // (idempotent, same descriptor), so a live modification is still
    // delivered.
    REQUIRE(pjh::platform::Fs::write_file(file, "modified").is_ok());
    auto alive = collect_until(
        w, [&](const auto &a) { return has_event(a, FileEventKind::Modified, file); });
    CHECK(has_event(alive, FileEventKind::Modified, file));

    std::error_code ec;
    std::filesystem::remove_all(p, ec);
}

TEST_CASE("FileWatcher Linux file watch ignores rename-in and stays inactive after the file moves")
{
    auto p = make_test_dir();
    auto file = p / "watched.txt";
    auto moved = p / "moved.txt";
    auto other = p / "other.txt";
    auto other2 = p / "other2.txt";
    REQUIRE(pjh::platform::Fs::write_file(file, "content").is_ok());
    REQUIRE(pjh::platform::Fs::write_file(other, "seed").is_ok());
    REQUIRE(pjh::platform::Fs::write_file(other2, "seed2").is_ok());

    FileWatcher w;
    REQUIRE(w.add(file, false).is_ok());

    // Prime the watch so the rename-out below is the first removal event.
    REQUIRE(pjh::platform::Fs::write_file(file, "primed").is_ok());
    collect_until(
        w, [&](const auto &all) { return has_event(all, FileEventKind::Modified, file); });

    // 1) Renaming the watched file away reports MovedFrom.
    std::error_code ec;
    std::filesystem::rename(file, moved, ec);
    REQUIRE_FALSE(ec);
    auto events = collect_until(
        w, [&](const auto &all) { return has_event(all, FileEventKind::MovedFrom, file); });
    CHECK(has_event(events, FileEventKind::MovedFrom, file));

    // 2) Renaming a different file onto the watched path is NOT reported:
    // the inotify watch follows the original inode and died with the rename.
    std::filesystem::rename(other2, file, ec);
    REQUIRE_FALSE(ec);
    for (int i = 0; i < 5; ++i)
    {
        auto r = w.poll(std::chrono::milliseconds(10));
        REQUIRE(r.is_ok());
        for (const auto &e : r.unwrap()) CHECK(e.path != file);
    }

    // 3) Modifying the fresh file now occupying the path is NOT reported
    // either; the watch stays inactive until the consumer re-adds it.
    REQUIRE(pjh::platform::Fs::write_file(file, "fresh content").is_ok());
    for (int i = 0; i < 5; ++i)
    {
        auto r = w.poll(std::chrono::milliseconds(10));
        REQUIRE(r.is_ok());
        for (const auto &e : r.unwrap()) CHECK(e.path != file);
    }
}

TEST_CASE("FileWatcher file watch recovers events lost to an inotify queue overflow")
{
    auto p = make_test_dir();
    auto file = p / "burst_watched.txt";
    REQUIRE(pjh::platform::Fs::write_file(file, "seed").is_ok());

    FileWatcher w;
    REQUIRE(w.add(file, false).is_ok());

    // The kernel coalesces successive identical records (same wd, mask,
    // cookie, name) that have not been read yet, so this same-file burst
    // collapses to about one IN_MODIFY: the queue is never filled and no
    // IN_Q_OVERFLOW is generated (man7 NOTES). The case anchors the direct
    // Modified delivery under a large burst; real overflow recovery is
    // anchored by the two "queue overflow" cases above in this file.
    std::ifstream limit_file("/proc/sys/fs/inotify/max_queued_events");
    int limit = 0;
    if (!(limit_file >> limit) || limit <= 0)
        return;  // cannot determine the queue size; skip

    int fd = ::open(file.c_str(), O_WRONLY | O_APPEND);
    REQUIRE(fd >= 0);
    char chunk[64] = {};
    for (int i = 0; i < limit + 200; ++i)
    {
        ssize_t rc = ::write(fd, chunk, sizeof(chunk));
        REQUIRE(rc == static_cast<ssize_t>(sizeof(chunk)));
    }
    ::close(fd);

    auto events = collect_until(
        w, [&](const auto &all) { return has_event(all, FileEventKind::Modified, file); });
    CHECK(has_event(events, FileEventKind::Modified, file));
}

TEST_CASE("FileWatcher file watch does not report the renamed-away inode after an overflow re-arm")
{
    // Zombie wd pin: the file entry's overflow re-arm must detach the
    // descriptor of the inode that left the path. I1 is renamed away and a
    // fresh I2 takes the path while the shared queue is full, so the
    // rename-out records (IN_MOVED_FROM/IN_MOVED_TO/IN_MOVE_SELF and the
    // re-create) are all dropped; on resync the re-arm resolves the path to
    // I2 and returns a new descriptor. Pre-fix the old descriptor kept
    // living on the moved inode with its map entry intact, so later changes
    // of I1 were delivered under the old path until I1 died. A2b below is
    // the discriminator: red pre-fix, green post-fix.
    auto p = make_test_dir();
    auto file = p / "zombie.txt";
    auto moved = p / "zombie_moved.txt";
    REQUIRE(pjh::platform::Fs::write_file(file, "zombie old").is_ok());

    FileWatcher w;
    REQUIRE(w.add(p, false).is_ok());     // directory entry, baseline {file}
    REQUIRE(w.add(file, false).is_ok());  // file entry, descriptor W1

    int limit = read_inotify_limit();
    if (limit == 0 || limit > 131072)
        return;  // limit unreadable, or far above the default 16384:
                 // documented silent skip that bounds the worst-case wall
                 // clock; the default CI lane never hits this branch

    // Flood 1: limit + 100 distinct-named creates, no poll(): the shared
    // queue overflows and stays pinned at full (drop-new), so every record
    // enqueued until the drain is dropped.
    for (int i = 0; i < limit + 100; ++i)
    {
        char name[16] = {};
        std::snprintf(name, sizeof(name), "zf_%06d", i);
        int fd = ::open((p / name).c_str(), O_CREAT | O_WRONLY, 0644);
        REQUIRE(fd >= 0);
        ::close(fd);
    }

    // Rename-out and re-creation of the path land while the queue is full:
    // all their records are dropped, so no MovedFrom(file) can be
    // delivered (the A1 self-check below relies on that).
    std::error_code ec;
    std::filesystem::rename(file, moved, ec);
    REQUIRE_FALSE(ec);
    REQUIRE(pjh::platform::Fs::write_file(file, "zombie fresh").is_ok());

    // Flood 2: keep the queue full past the rename-out moment.
    for (int i = 0; i < 200; ++i)
    {
        char name[16] = {};
        std::snprintf(name, sizeof(name), "zg_%06d", i);
        int fd = ::open((p / name).c_str(), O_CREAT | O_WRONLY, 0644);
        REQUIRE(fd >= 0);
        ::close(fd);
    }

    std::vector<FileEvent> all;
    REQUIRE(drain_until_empty(w, all));

    // A1: self-check that the scenario triggered as designed. A delivered
    // MovedFrom(file) means the rename-out records survived the full queue
    // (a drop-oldest kernel policy, unspecified by man7): the scenario is
    // not constructible on this kernel, so skip the remainder silently
    // (documented skip, the overflow cases' precedent). A missing
    // Created(moved) is a loud failure: the resync diff itself is broken.
    if (has_event(all, FileEventKind::MovedFrom, file))
        return;
    CHECK(has_event(all, FileEventKind::Created, moved));

    // A2a: liveness pin — the directory entry still reports the moved inode
    // after the overflow resync (the watcher survived the overflow).
    REQUIRE(pjh::platform::Fs::write_file(moved, "zombie moved again").is_ok());
    auto post = collect_until(
        w, [&](const auto &a) { return has_event(a, FileEventKind::Modified, moved); });
    CHECK(has_event(post, FileEventKind::Modified, moved));
    for (int i = 0; i < 10; ++i)
    {
        auto r = w.poll(std::chrono::milliseconds(10));
        REQUIRE(r.is_ok());
        for (auto &e : r.unwrap()) post.push_back(std::move(e));
    }

    // A2b: discriminator pin — nothing after the drain may be reported
    // under the old path. Pre-fix the zombie descriptor delivers
    // Modified(file) here (red); the resync's own Modified(file) during the
    // drain is the accepted relaxation and is excluded by scope. CHECK
    // level so A3 still evaluates on a red run.
    CHECK_FALSE(
        std::any_of(post.begin(), post.end(), [&](const FileEvent &e) { return e.path == file; }));

    // A3: re-arm liveness pin — the adopted descriptor must be the
    // surviving one: a modification of the fresh inode at the path is still
    // delivered. A reversed implementation (detaching the new descriptor)
    // deafens the re-arm and fails here.
    REQUIRE(pjh::platform::Fs::write_file(file, "zombie fresh again").is_ok());
    auto alive = collect_until(
        w, [&](const auto &a) { return has_event(a, FileEventKind::Modified, file); });
    CHECK(has_event(alive, FileEventKind::Modified, file));

    std::filesystem::remove_all(p, ec);
}

TEST_CASE("FileWatcher removing a recursive watch keeps a separate sub-directory watch alive")
{
    auto p = make_test_dir();
    auto sub = p / "sub";
    REQUIRE(std::filesystem::create_directories(sub));

    FileWatcher w;
    REQUIRE(w.add(p, true).is_ok());
    REQUIRE(w.add(sub, false).is_ok());
    CHECK(w.remove(p).is_ok());

    // The two entries share the kernel watch on `sub` (inotify_add_watch is
    // idempotent per fd+inode); removing the recursive entry must not kill
    // the sub-directory entry's share of it.
    auto file = sub / "inside.txt";
    REQUIRE(pjh::platform::Fs::write_file(file, "nested").is_ok());
    auto events = collect_until(
        w, [&](const auto &all) { return has_event(all, FileEventKind::Created, file); });
    CHECK(has_event(events, FileEventKind::Created, file));
}

TEST_CASE("FileWatcher removing a sub-directory watch keeps the recursive watch alive")
{
    auto p = make_test_dir();
    auto sub = p / "sub";
    REQUIRE(std::filesystem::create_directories(sub));

    FileWatcher w;
    REQUIRE(w.add(p, true).is_ok());
    REQUIRE(w.add(sub, false).is_ok());
    CHECK(w.remove(sub).is_ok());

    // Mirror of the previous case: the recursive entry must keep its
    // sub-directory watch after the separate sub-directory entry is removed.
    auto file = sub / "inside2.txt";
    REQUIRE(pjh::platform::Fs::write_file(file, "nested").is_ok());
    auto events = collect_until(
        w, [&](const auto &all) { return has_event(all, FileEventKind::Created, file); });
    CHECK(has_event(events, FileEventKind::Created, file));
}

TEST_CASE("FileWatcher directory deletion tears down a shared watch for all holders")
{
    auto p = make_test_dir();
    auto sub = p / "sub";
    auto other = p / "other";
    REQUIRE(std::filesystem::create_directories(sub));
    REQUIRE(std::filesystem::create_directories(other));

    FileWatcher w;
    REQUIRE(w.add(p, true).is_ok());
    REQUIRE(w.add(sub, false).is_ok());

    // Deleting `sub` destroys the shared kernel watch; every holder must
    // drop its record at once (the IN_IGNORED is consumed per entry) while
    // the sibling directory keeps reporting through the recursive entry.
    REQUIRE(std::filesystem::remove_all(sub));
    auto alive = other / "alive.txt";
    REQUIRE(pjh::platform::Fs::write_file(alive, "x").is_ok());
    auto events = collect_until(
        w, [&](const auto &all) { return has_event(all, FileEventKind::Created, alive); });
    CHECK(has_event(events, FileEventKind::Created, alive));

    // Zombie cycle: the now-dead sub entry is removable, and the path can be
    // re-registered after re-creation with no stale IN_IGNORED residue.
    CHECK(w.remove(sub).is_ok());
    REQUIRE(std::filesystem::create_directories(sub));
    REQUIRE(w.add(sub, false).is_ok());
    auto again = sub / "again.txt";
    REQUIRE(pjh::platform::Fs::write_file(again, "x").is_ok());
    events = collect_until(
        w, [&](const auto &all) { return has_event(all, FileEventKind::Created, again); });
    CHECK(has_event(events, FileEventKind::Created, again));
}

TEST_CASE("FileWatcher close releases shared watches without error")
{
    auto p = make_test_dir();
    auto sub = p / "sub";
    REQUIRE(std::filesystem::create_directories(sub));

    FileWatcher w;
    REQUIRE(w.add(p, true).is_ok());
    REQUIRE(w.add(sub, false).is_ok());

    // Both entries share the kernel watch on `sub`; close() must walk the
    // shared descriptor exactly once and leave no observable error.
    w.close();
    w.close();
    CHECK(w.poll(std::chrono::milliseconds(10)).is_err());
}

TEST_CASE("FileWatcher move-assignment does not leak the previous inotify instance")
{
    auto p = make_test_dir();
    FileWatcher w;
    REQUIRE(w.add(p, false).is_ok());

    // Every FileWatcher owns exactly one inotify instance fd, visible in
    // /proc/self/fd. Each move-assignment must tear the target's instance
    // down through close() (not the raw destructor), so the process-wide
    // count of inotify fds stays stable across assignments.
    auto count_inotify_fds = []() -> int
    {
        int n = 0;
        std::error_code ec;
        for (const auto &e : std::filesystem::directory_iterator("/proc/self/fd", ec))
        {
            std::error_code fec;
            auto target = std::filesystem::read_symlink(e.path(), fec);
            if (!fec && target.string().find("inotify") != std::string::npos)
                ++n;
        }
        return n;
    };

    int before = count_inotify_fds();
    REQUIRE(before >= 1);  // w's own instance fd must be visible

    FileWatcher t1;
    w = std::move(t1);
    FileWatcher t2;
    w = std::move(t2);
    FileWatcher t3;
    w = std::move(t3);
    int after = count_inotify_fds();

    // Pre-fix, each assignment leaked the target's instance fd (never
    // closed); post-fix exactly one instance is live in both worlds.
    CHECK_EQ(after, before);

    // The target is a live watcher in an empty state, not a moved-from one.
    auto r = w.poll(std::chrono::milliseconds(10));
    REQUIRE(r.is_ok());
    CHECK(r.unwrap().empty());
}
#endif

#if PJH_PLATFORM_UNIX
TEST_CASE("FileWatcher add skips unreadable subdirectories")
{
    // A subdirectory that cannot be opened (mode 000) must be skipped by the
    // recursive walks instead of making add()/poll() throw: no watch, no
    // baseline, no events from inside it, the readable parts watch normally.
    // CI runners run unprivileged; when the mode 000 is not enforced for the
    // current process (e.g. root) the case self-skips with a note.
    auto p = make_test_dir();
    auto secret = p / "secret";
    auto open_dir = p / "open";
    REQUIRE(std::filesystem::create_directories(secret));
    REQUIRE(std::filesystem::create_directories(open_dir));

    std::error_code sec;
    auto original = std::filesystem::status(secret, sec).permissions();
    REQUIRE_FALSE(sec);
    std::filesystem::permissions(secret, std::filesystem::perms::none, sec);
    REQUIRE_FALSE(sec);

    // Self-skip probe: if the 000 directory is still openable, EACCES cannot
    // be manufactured here (privileged process); restore and skip.
    {
        std::error_code pec;
        auto probe = std::filesystem::directory_iterator(secret, pec);
        if (!pec)
        {
            std::error_code rec;
            std::filesystem::permissions(secret, original, rec);
            std::filesystem::remove_all(p, rec);
            return;
        }
    }

    {
        // Restores the mode on every exit path (including a REQUIRE
        // failure's unwind); a leaked 000 directory would break the next
        // case's remove_all on the shared sandbox path.
        struct RestorePermissions
        {
            std::filesystem::path dir;
            std::filesystem::perms perms;

            ~RestorePermissions()
            {
                std::error_code ec;
                std::filesystem::permissions(dir, perms, ec);
            }
        } guard{secret, original};

        FileWatcher w;
        // P1 (main pin): pre-fix the recursive walk in add() threw a
        // filesystem_error (EACCES) from operator++; the case passing is the
        // Never-throws pin.
        REQUIRE(w.add(p, true).is_ok());

        // P2: the readable part is alive — skipping is not deafness.
        auto inside = open_dir / "inside.txt";
        REQUIRE(pjh::platform::Fs::write_file(inside, "inside").is_ok());
        auto events = collect_until(
            w, [&](const auto &all) { return has_event(all, FileEventKind::Created, inside); });
        CHECK(has_event(events, FileEventKind::Created, inside));

#if PJH_PLATFORM_LINUX
        // P3/P4 pin the overflow-resync walk, reachable only through a real
        // IN_Q_OVERFLOW (the same mechanism as the overflow cases above):
        // a seed, a drained queue, then N = limit + 200 distinct-named
        // creates with no poll, then a drain in which every poll must
        // succeed and in which at least 200 flood names can only be
        // reported through the resync diff.
        auto burst = open_dir / "burst.txt";
        REQUIRE(pjh::platform::Fs::write_file(burst, "burst").is_ok());
        std::vector<FileEvent> all;
        {
            auto pre = collect_until(
                w, [&](const auto &a) { return has_event(a, FileEventKind::Created, burst); });
            for (auto &e : pre) all.push_back(std::move(e));
        }
        REQUIRE(drain_until_empty(w, all));

        int limit = read_inotify_limit();
        if (limit == 0 || limit > 131072)
            return;  // limit unreadable, or far above the default 16384:
                     // documented silent skip that bounds the worst-case wall
                     // clock; the guard above restores the permissions

        const int n = limit + 200;
        for (int i = 0; i < n; ++i)
        {
            char name[16] = {};
            std::snprintf(name, sizeof(name), "f_%06d", i);
            int fd = ::open((p / name).c_str(), O_CREAT | O_WRONLY, 0644);
            REQUIRE(fd >= 0);
            ::close(fd);
        }

        // P3: every poll during the drain succeeds — pre-fix the resync
        // walk threw from inside poll().
        REQUIRE(drain_until_empty(w, all));

        // P4: index the Created flood names by path (a linear has_event per
        // name over ~limit records would be quadratic). The queue holds at
        // most `limit` records, so with N = limit + 200 distinct names the
        // excess can only have been reported by the resync diff: the walk
        // ran over the tree containing the unreadable subdirectory.
        std::set<std::filesystem::path> flood_created;
        for (const auto &e : all)
        {
            if (e.kind != FileEventKind::Created || e.path.parent_path() != p)
                continue;
            auto name = e.path.filename().string();
            if (name.size() == 8 && name.compare(0, 2, "f_") == 0)
                flood_created.insert(e.path);
        }
        CHECK(flood_created.size() >= static_cast<std::size_t>(n));
#endif
    }

    std::error_code rec;
    std::filesystem::remove_all(p, rec);
}
#endif

TEST_CASE("FileWatcher benchmark gated by PJH_WATCH_BENCH_FILES")
{
    // Baseline / regression benchmark for the FileWatcher poll drain path.
    // The measurement and its stdout output happen only when
    // PJH_WATCH_BENCH_FILES names the number of seed files to pre-create;
    // a plain `ctest` run is a no-op and stays silent.
    const char *raw = std::getenv("PJH_WATCH_BENCH_FILES");
    if (raw == nullptr)
        return;
    const auto n = static_cast<std::size_t>(std::strtoull(raw, nullptr, 10));
    REQUIRE(n >= 100);

    auto p = make_test_dir();
    // The seed files double as the sibling population for the file-watch
    // scenarios; creating them counts as warm-up and stays out of the
    // measurement.
    for (std::size_t i = 0; i < n; ++i)
        REQUIRE(
            pjh::platform::Fs::write_file(p / ("seed" + std::to_string(i) + ".bin"), "x").is_ok());

    // Pump poll() until a batch comes back empty; returns the drain wall
    // time in microseconds plus the number of delivered events, or
    // {-1, -1} when a poll call fails.
    auto drain = [](FileWatcher &w) -> std::pair<long long, long long>
    {
        auto t0 = std::chrono::steady_clock::now();
        long long events = 0;
        for (;;)
        {
            auto r = w.poll(std::chrono::milliseconds(10));
            if (r.is_err())
                return {-1, -1};
            auto batch = std::move(r).unwrap();
            events += static_cast<long long>(batch.size());
            if (batch.empty())
                break;
        }
        auto t1 = std::chrono::steady_clock::now();
        return {std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count(), events};
    };

    // S1: directory watch, 500 queued create/modify/delete cycles. Before the
    // lazy baseline each event-bearing poll re-captured the whole watched
    // directory; after, the normal path captures nothing.
    {
        FileWatcher w;
        REQUIRE(w.add(p, false).is_ok());
        const int rounds = 500;
        for (int i = 0; i < rounds; ++i)
        {
            auto f = p / ("churn" + std::to_string(i) + ".tmp");
            REQUIRE(pjh::platform::Fs::write_file(f, "one").is_ok());
            REQUIRE(pjh::platform::Fs::write_file(f, "two two").is_ok());
            REQUIRE(std::filesystem::remove(f));
        }
        auto [us, events] = drain(w);
        REQUIRE(us >= 0);
        std::cout << "watch-bench S1 dir-watch churn: N=" << n << " rounds=" << rounds
                  << " drain_us=" << us << " events=" << events << "\n";
    }

    // S2: file watch, the watched file itself modified 200 times while the
    // parent directory holds the N seed siblings.
    {
        auto file = p / "self_watched.txt";
        REQUIRE(pjh::platform::Fs::write_file(file, "seed").is_ok());
        FileWatcher w;
        REQUIRE(w.add(file, false).is_ok());
        const int rounds = 200;
        for (int i = 0; i < rounds; ++i)
            REQUIRE(pjh::platform::Fs::write_file(file, "payload " + std::to_string(i)).is_ok());
        auto [us, events] = drain(w);
        REQUIRE(us >= 0);
        std::cout << "watch-bench S2 file-watch self-modify: N=" << n << " rounds=" << rounds
                  << " drain_us=" << us << " events=" << events << "\n";
    }

    // S3: file watch, sibling churn only (the watched file is untouched); the
    // filter must drop every event, so the drain is expected to see none.
    // On Linux the file watch is attached to the file itself, so no sibling
    // event ever reaches the inotify queue and the drain is a plain 10 ms
    // timeout; on Windows/macOS the parent-directory filter still has to read
    // and discard every sibling event.
    {
        auto file = p / "sib_watched.txt";
        REQUIRE(pjh::platform::Fs::write_file(file, "seed").is_ok());
        FileWatcher w;
        REQUIRE(w.add(file, false).is_ok());
        const int rounds = 200;
        for (int i = 0; i < rounds; ++i)
        {
            auto f = p / ("sibling" + std::to_string(i) + ".tmp");
            REQUIRE(pjh::platform::Fs::write_file(f, "x").is_ok());
            REQUIRE(std::filesystem::remove(f));
        }
        auto [us, events] = drain(w);
        REQUIRE(us >= 0);
        std::cout << "watch-bench S3 file-watch sibling churn: N=" << n << " rounds=" << rounds
                  << " drain_us=" << us << " events=" << events << "\n";
    }

    // S4: recursive watch, churn localized to one subdirectory; the other
    // subdirs must stay out of the per-batch rescan (C2; Linux/Windows numbers
    // are reference).
    {
        const int subs = 10;
        for (int i = 0; i < subs; ++i)
        {
            auto sub = p / ("sub" + std::to_string(i));
            REQUIRE(std::filesystem::create_directories(sub));
            for (std::size_t j = 0; j < n / subs; ++j)
                REQUIRE(
                    pjh::platform::Fs::write_file(sub / ("s" + std::to_string(j) + ".bin"), "x")
                        .is_ok());
        }
        FileWatcher w;
        REQUIRE(w.add(p, true).is_ok());
        const int rounds = 100;
        for (int i = 0; i < rounds; ++i)
        {
            auto f = p / "sub0" / ("churn" + std::to_string(i) + ".tmp");
            REQUIRE(pjh::platform::Fs::write_file(f, "one").is_ok());
            REQUIRE(pjh::platform::Fs::write_file(f, "two two").is_ok());
            REQUIRE(std::filesystem::remove(f));
        }
        auto [us, events] = drain(w);
        REQUIRE(us >= 0);
        std::cout << "watch-bench S4 recursive subdir churn: N=" << n << " rounds=" << rounds
                  << " drain_us=" << us << " events=" << events << "\n";
    }

    std::error_code ec;
    std::filesystem::remove_all(p, ec);
}
