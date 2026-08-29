#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <pjh_platform/error.hpp>
#include <pjh_platform/file_watcher.hpp>
#include <pjh_platform/fs.hpp>
#include <pjh_platform/platform.hpp>
#include <utility>
#include <vector>

#if PJH_PLATFORM_LINUX
#include <fcntl.h>
#include <unistd.h>

#include <fstream>
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

#if PJH_PLATFORM_LINUX
TEST_CASE("FileWatcher recovers events lost to an inotify queue overflow")
{
    auto p = make_test_dir();
    FileWatcher w;
    REQUIRE(w.add(p, false).is_ok());

    auto file = p / "burst.txt";
    REQUIRE(pjh::platform::Fs::write_file(file, "seed").is_ok());
    collect_until(w, [&](const auto &all) { return has_event(all, FileEventKind::Created, file); });

    // Flood the inotify queue with far more IN_MODIFY events than it can hold.
    // The kernel discards the surplus and queues a single IN_Q_OVERFLOW marker;
    // the modifications must then be recovered from the snapshot diff.
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
#endif
