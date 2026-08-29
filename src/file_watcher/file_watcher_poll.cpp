#include <algorithm>
#include <cerrno>
#include <chrono>
#include <filesystem>
#include <map>
#include <optional>
#include <pjh_platform/directory_diff.hpp>
#include <pjh_platform/file_watcher.hpp>
#include <pjh_platform/platform.hpp>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "file_watcher_internal.hpp"

#if PJH_PLATFORM_WINDOWS
#include <windows.h>
#elif PJH_PLATFORM_LINUX
#include <poll.h>
#include <sys/inotify.h>
#include <unistd.h>
#endif

namespace pjh::platform
{
    namespace detail
    {
        namespace
        {
            auto to_kind(DirectoryDiff::ChangeKind kind) -> FileEventKind
            {
                switch (kind)
                {
                case DirectoryDiff::ChangeKind::Created:
                    return FileEventKind::Created;
                case DirectoryDiff::ChangeKind::Deleted:
                    return FileEventKind::Deleted;
                case DirectoryDiff::ChangeKind::Modified:
                    return FileEventKind::Modified;
                }
                return FileEventKind::Modified;
            }

            auto emit_if_new(
                std::vector<FileEvent> &out, FileEventKind kind, const std::filesystem::path &path)
                -> void
            {
                for (const auto &e : out)
                    if (e.kind == kind && e.path == path)
                        return;
                out.push_back(FileEvent{kind, path, std::nullopt});
            }

            auto diff_and_emit(
                WatchEntry &entry,
                const std::filesystem::path &dir,
                const DirectorySnapshot &before,
                DirectorySnapshot &&current,
                std::vector<FileEvent> &out) -> DirectorySnapshot
            {
                auto dr = DirectoryDiff::compare(before, current);
                if (dr.is_ok())
                {
                    DirectoryDiff diff = std::move(dr).unwrap();
                    auto renames = diff.detect_renames(before, current);

                    std::vector<std::filesystem::path> suppressed;
                    suppressed.reserve(renames.size() * 2);
                    for (const auto &r : renames)
                    {
                        suppressed.push_back(r.m_old_filename);
                        suppressed.push_back(r.m_new_filename);
                    }

                    for (const auto &ch : diff.changes())
                    {
                        if (std::find(suppressed.begin(), suppressed.end(), ch.m_filename) !=
                            suppressed.end())
                            continue;
                        if (!entry.is_directory && ch.m_full_path != entry.path)
                            continue;
                        emit_if_new(out, to_kind(ch.m_kind), ch.m_full_path);
                    }
                    // A directory reported as Created is a brand-new subtree;
                    // establish its baseline right away so changes inside it
                    // are diffed against a pre-children snapshot instead of
                    // being swallowed into its first baseline.
                    for (const auto &ch : diff.changes())
                    {
                        if (ch.m_kind != DirectoryDiff::ChangeKind::Created)
                            continue;
                        std::error_code ec;
                        if (!std::filesystem::is_directory(ch.m_full_path, ec) || ec)
                            continue;
                        if (entry.snapshots.find(ch.m_full_path) != entry.snapshots.end())
                            continue;
                        auto cap = DirectorySnapshot::capture(ch.m_full_path);
                        if (cap.is_ok())
                            entry.snapshots[ch.m_full_path] = std::move(cap).unwrap();
                    }
                    for (const auto &r : renames)
                    {
                        auto old_full = dir / r.m_old_filename;
                        auto new_full = dir / r.m_new_filename;
                        if (entry.is_directory)
                        {
                            emit_if_new(out, FileEventKind::MovedFrom, old_full);
                            emit_if_new(out, FileEventKind::MovedTo, new_full);
                        }
                        else
                        {
                            if (old_full == entry.path)
                                emit_if_new(out, FileEventKind::MovedFrom, entry.path);
                            if (new_full == entry.path)
                                emit_if_new(out, FileEventKind::MovedTo, entry.path);
                        }
                    }
                }
                return current;
            }

#if PJH_PLATFORM_LINUX || PJH_PLATFORM_WINDOWS
            auto prune_snapshots(WatchEntry &entry, const std::filesystem::path &dir) -> void
            {
                for (auto sit = entry.snapshots.begin(); sit != entry.snapshots.end();)
                {
                    if (sit->first == dir || path_is_under(sit->first, dir))
                        sit = entry.snapshots.erase(sit);
                    else
                        ++sit;
                }
            }

            auto tree_dirs(WatchEntry &entry) -> std::vector<std::filesystem::path>
            {
                std::vector<std::filesystem::path> dirs;
                dirs.push_back(entry.watch_root);
                if (!entry.recursive)
                    return dirs;
                std::error_code ec;
                for (auto it = std::filesystem::recursive_directory_iterator(entry.watch_root, ec);
                     it != std::filesystem::recursive_directory_iterator(); ++it)
                {
                    if (ec)
                    {
                        ec.clear();
                        continue;
                    }
                    std::error_code sec;
                    if (!std::filesystem::is_directory(it->path(), sec))
                        continue;
                    dirs.push_back(it->path());
                }
                return dirs;
            }

            auto resync_directory(
                WatchEntry &entry, const std::filesystem::path &dir, std::vector<FileEvent> &out)
                -> void
            {
                auto captured = DirectorySnapshot::capture(dir);
                if (captured.is_err())
                {
                    auto sit = entry.snapshots.find(dir);
                    if (sit != entry.snapshots.end() && entry.is_directory)
                        emit_if_new(out, FileEventKind::Deleted, dir);
                    prune_snapshots(entry, dir);
                    return;
                }
                auto current = std::move(captured).unwrap();
                auto sit = entry.snapshots.find(dir);
                if (sit == entry.snapshots.end())
                    entry.snapshots[dir] = std::move(current);
                else
                    sit->second = diff_and_emit(entry, dir, sit->second, std::move(current), out);
            }
#endif  // PJH_PLATFORM_LINUX || PJH_PLATFORM_WINDOWS

#if PJH_PLATFORM_LINUX
            auto mask_to_kind(std::uint32_t mask) -> std::optional<FileEventKind>
            {
                if (mask & IN_MOVED_FROM)
                    return FileEventKind::MovedFrom;
                if (mask & IN_MOVED_TO)
                    return FileEventKind::MovedTo;
                if (mask & IN_CREATE)
                    return FileEventKind::Created;
                if (mask & IN_DELETE)
                    return FileEventKind::Deleted;
                if (mask & IN_MODIFY)
                    return FileEventKind::Modified;
                if (mask & IN_DELETE_SELF)
                    return FileEventKind::Deleted;
                if (mask & IN_MOVE_SELF)
                    return FileEventKind::MovedFrom;
                return std::nullopt;
            }

            auto remove_subdir_watches(int fd, WatchEntry &entry, const std::filesystem::path &dir)
                -> void
            {
                for (auto it = entry.wd_to_path.begin(); it != entry.wd_to_path.end();)
                {
                    if (it->first != entry.root_wd && path_is_under(it->second, dir))
                    {
                        ::inotify_rm_watch(fd, it->first);
                        it = entry.wd_to_path.erase(it);
                    }
                    else
                    {
                        ++it;
                    }
                }
            }

            auto resync_linux(FileWatcherImpl &impl, WatchEntry &entry, std::vector<FileEvent> &out)
                -> void
            {
                auto current_dirs = tree_dirs(entry);

                // Reconcile the inotify watch set with the live directory tree:
                // drop watches for directories that are gone, add watches for
                // directories that appeared while events were being dropped.
                auto watched = [&entry](const std::filesystem::path &dir) -> bool
                {
                    for (const auto &[wd, p] : entry.wd_to_path)
                        if (p == dir)
                            return true;
                    return false;
                };
                for (auto it = entry.wd_to_path.begin(); it != entry.wd_to_path.end();)
                {
                    if (it->first != entry.root_wd &&
                        std::find(current_dirs.begin(), current_dirs.end(), it->second) ==
                            current_dirs.end())
                    {
                        ::inotify_rm_watch(impl.fd, it->first);
                        it = entry.wd_to_path.erase(it);
                    }
                    else
                    {
                        ++it;
                    }
                }
                for (const auto &dir : current_dirs)
                {
                    if (dir == entry.watch_root || watched(dir))
                        continue;
                    int wd = ::inotify_add_watch(impl.fd, dir.c_str(), watch_mask());
                    if (wd != -1)
                        entry.wd_to_path[wd] = dir;
                }

                for (const auto &dir : current_dirs) resync_directory(entry, dir, out);

                for (auto sit = entry.snapshots.begin(); sit != entry.snapshots.end();)
                {
                    if (std::find(current_dirs.begin(), current_dirs.end(), sit->first) ==
                        current_dirs.end())
                        sit = entry.snapshots.erase(sit);
                    else
                        ++sit;
                }
            }
#elif PJH_PLATFORM_WINDOWS
            auto action_to_kind(DWORD action) -> std::optional<FileEventKind>
            {
                switch (action)
                {
                case FILE_ACTION_ADDED:
                    return FileEventKind::Created;
                case FILE_ACTION_REMOVED:
                    return FileEventKind::Deleted;
                case FILE_ACTION_MODIFIED:
                    return FileEventKind::Modified;
                case FILE_ACTION_RENAMED_OLD_NAME:
                    return FileEventKind::MovedFrom;
                case FILE_ACTION_RENAMED_NEW_NAME:
                    return FileEventKind::MovedTo;
                default:
                    return std::nullopt;
                }
            }

            auto issue_read([[maybe_unused]] FileWatcherImpl &impl, WatchEntry &entry) -> void
            {
                entry.overlapped = OVERLAPPED{};
                entry.io_pending = false;
                DWORD filter = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
                               FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE;
                if (ReadDirectoryChangesW(
                        entry.handle, entry.buffer.data(), static_cast<DWORD>(entry.buffer.size()),
                        entry.recursive ? TRUE : FALSE, filter, nullptr, &entry.overlapped,
                        nullptr))
                    entry.io_pending = true;
            }

            auto resync_windows(WatchEntry &entry, std::vector<FileEvent> &out) -> void
            {
                auto current_dirs = tree_dirs(entry);
                for (const auto &dir : current_dirs) resync_directory(entry, dir, out);
                for (auto sit = entry.snapshots.begin(); sit != entry.snapshots.end();)
                {
                    if (std::find(current_dirs.begin(), current_dirs.end(), sit->first) ==
                        current_dirs.end())
                        sit = entry.snapshots.erase(sit);
                    else
                        ++sit;
                }
            }
#endif
        }  // namespace

#if PJH_PLATFORM_MACOS
        auto process_fsevents(WatchEntry &entry, std::vector<FileEvent> &out) -> void
        {
            std::vector<std::filesystem::path> diffed;
            diffed.reserve(entry.pending_events.size());

            for (const auto &pe : entry.pending_events)
            {
                // FSEvents reports paths with symlinks resolved (e.g.
                // /private/var/...); remap back into the lexical watch_root
                // space so filters and snapshots stay consistent with the
                // paths reported to callers.
                // FSEvents reports paths with symlinks resolved (e.g.
                // /private/var/...) and may append a trailing slash when the
                // reported path is the watched directory itself. lexically_normal
                // keeps that trailing slash, so drop it to make the path compare
                // equal to the canonical root.
                std::filesystem::path p = pe.path.lexically_normal();
                while (p.has_relative_path() && p.filename().empty()) p = p.parent_path();
                if (!entry.canonical_root.empty())
                {
                    // FSEvents may coalesce several changes onto the watched
                    // directory itself. path_is_under(X, X) is false, so handle
                    // the equality case explicitly.
                    if (p == entry.canonical_root)
                        p = entry.watch_root;
                    else if (path_is_under(p, entry.canonical_root))
                        p = entry.watch_root / p.lexically_relative(entry.canonical_root);
                }

                if (p != entry.watch_root && !path_is_under(p, entry.watch_root))
                    continue;
                // A file watch accepts events for the watched file itself or
                // for its parent directory (FSEvents may coalesce a change
                // onto the parent); changes are filtered to the file below.
                if (!entry.is_directory && p != entry.path && p != entry.watch_root)
                    continue;

                std::filesystem::path d;
                if (entry.is_directory && entry.recursive)
                {
                    std::error_code ec;
                    bool is_dir = (pe.flags & kFseventsItemIsDir) != 0 ||
                                  std::filesystem::is_directory(p, ec);
                    d = is_dir ? p : p.parent_path();
                }
                else
                {
                    d = entry.watch_root;
                }

                auto captured = DirectorySnapshot::capture(d);
                if (captured.is_err())
                {
                    // The directory itself is gone. Report its removal and drop
                    // every snapshot beneath it.
                    auto it = entry.snapshots.find(d);
                    if (it != entry.snapshots.end())
                    {
                        if (entry.is_directory)
                            emit_if_new(out, FileEventKind::Deleted, d);
                        for (auto sit = entry.snapshots.begin(); sit != entry.snapshots.end();)
                        {
                            if (sit->first == d || path_is_under(sit->first, d))
                                sit = entry.snapshots.erase(sit);
                            else
                                ++sit;
                        }
                    }
                    continue;
                }
                DirectorySnapshot current = std::move(captured).unwrap();

                auto it = entry.snapshots.find(d);
                if (it == entry.snapshots.end())
                {
                    // First event for a directory created after the baseline
                    // was taken. Diff its parent so the creation is reported,
                    // then establish this directory's own baseline.
                    if (d != entry.watch_root)
                    {
                        auto pit = entry.snapshots.find(d.parent_path());
                        if (pit != entry.snapshots.end())
                        {
                            auto pc = DirectorySnapshot::capture(d.parent_path());
                            if (pc.is_ok())
                                pit->second = diff_and_emit(
                                    entry, d.parent_path(), pit->second, std::move(pc).unwrap(),
                                    out);
                        }
                    }
                    entry.snapshots[d] = std::move(current);
                }
                else
                {
                    it->second = diff_and_emit(entry, d, it->second, std::move(current), out);
                }
                diffed.push_back(d);
            }

            if (entry.is_directory && entry.recursive)
            {
                // FSEvents may coalesce several changes onto a single reported
                // path, so re-diff every established subdirectory that was not
                // already diffed above.
                std::vector<std::filesystem::path> dirs;
                dirs.reserve(entry.snapshots.size());
                for (const auto &kv : entry.snapshots) dirs.push_back(kv.first);
                for (const auto &dir : dirs)
                {
                    if (std::find(diffed.begin(), diffed.end(), dir) != diffed.end())
                        continue;
                    auto captured = DirectorySnapshot::capture(dir);
                    if (captured.is_err())
                    {
                        auto it = entry.snapshots.find(dir);
                        if (it != entry.snapshots.end())
                        {
                            emit_if_new(out, FileEventKind::Deleted, dir);
                            for (auto sit = entry.snapshots.begin(); sit != entry.snapshots.end();)
                            {
                                if (sit->first == dir || path_is_under(sit->first, dir))
                                    sit = entry.snapshots.erase(sit);
                                else
                                    ++sit;
                            }
                        }
                        continue;
                    }
                    auto cur = std::move(captured).unwrap();
                    auto dit = entry.snapshots.find(dir);
                    if (dit != entry.snapshots.end())
                        dit->second = diff_and_emit(entry, dir, dit->second, std::move(cur), out);
                }
            }
        }
#endif

        auto platform_poll(FileWatcherImpl &impl, std::chrono::milliseconds timeout)
            -> pjh::result::Result<std::vector<FileEvent>, ErrorCode>
        {
            std::vector<FileEvent> out;

#if PJH_PLATFORM_WINDOWS
            if (!impl.port)
                return pjh::result::Failure<ErrorCode>{ErrorCode::Unknown};

            long long ms = timeout.count();
            if (ms < 0)
                ms = 0;
            DWORD bytes = 0;
            ULONG_PTR key = 0;
            OVERLAPPED *ov = nullptr;
            BOOL ok =
                GetQueuedCompletionStatus(impl.port, &bytes, &key, &ov, static_cast<DWORD>(ms));
            if (!ok)
            {
                DWORD err = GetLastError();
                if (err == WAIT_TIMEOUT || err == ERROR_OPERATION_ABORTED)
                    return pjh::result::Result<std::vector<FileEvent>, ErrorCode>::Ok(
                        std::move(out));
                return pjh::result::Failure<ErrorCode>{map_windows_error(err)};
            }

            WatchEntry *entry = nullptr;
            for (auto &e : impl.entries)
                if (reinterpret_cast<ULONG_PTR>(e.get()) == key)
                {
                    entry = e.get();
                    break;
                }
            if (!entry)
                return pjh::result::Result<std::vector<FileEvent>, ErrorCode>::Ok(std::move(out));

            // ReadDirectoryChangesW can complete with a failure that still
            // produces a completion packet (a buffer overflow); resolve the
            // real result of the overlapped operation.
            DWORD notify_bytes = 0;
            if (GetOverlappedResult(entry->handle, &entry->overlapped, &notify_bytes, FALSE))
            {
                std::set<std::filesystem::path> affected;
                std::size_t off = 0;
                while (off + sizeof(FILE_NOTIFY_INFORMATION) <=
                       static_cast<std::size_t>(notify_bytes))
                {
                    auto *rec =
                        reinterpret_cast<FILE_NOTIFY_INFORMATION *>(entry->buffer.data() + off);
                    std::size_t name_chars = rec->FileNameLength / sizeof(wchar_t);
                    std::filesystem::path rel(std::wstring(rec->FileName, name_chars));
                    auto full = entry->watch_root / rel;
                    if (!entry->is_directory && entry->path.filename() != rel)
                    {
                        // Unrelated event; filtered out for a file watch.
                    }
                    else if (auto kind = action_to_kind(rec->Action))
                    {
                        out.push_back(FileEvent{*kind, full, std::nullopt});
                    }
                    affected.insert(full.parent_path());
                    if (rec->Action == FILE_ACTION_ADDED)
                    {
                        // A new directory gets its own baseline so changes made
                        // inside it are recoverable after a later overflow.
                        std::error_code ec;
                        if (std::filesystem::is_directory(full, ec) && !ec)
                        {
                            auto cap = DirectorySnapshot::capture(full);
                            if (cap.is_ok())
                                entry->snapshots[full] = std::move(cap).unwrap();
                        }
                    }
                    else if (rec->Action == FILE_ACTION_REMOVED)
                    {
                        prune_snapshots(*entry, full);
                    }
                    if (rec->NextEntryOffset == 0)
                        break;
                    off += rec->NextEntryOffset;
                }

                // Refresh the baselines of directories touched by this batch so
                // a later buffer overflow only reports changes made after it.
                for (const auto &dir : affected)
                {
                    auto sit = entry->snapshots.find(dir);
                    if (sit == entry->snapshots.end())
                        continue;
                    auto captured = DirectorySnapshot::capture(dir);
                    if (captured.is_err())
                    {
                        prune_snapshots(*entry, dir);
                        continue;
                    }
                    sit->second = std::move(captured).unwrap();
                }
            }
            else
            {
                DWORD oserr = GetLastError();
                entry->io_pending = false;
                if (oserr == ERROR_NOTIFY_ENUM_DIR)
                {
                    // The change buffer overflowed and events were lost;
                    // recover by diffing every watched directory against its
                    // stored baseline snapshot.
                    resync_windows(*entry, out);
                }
                else
                {
                    return pjh::result::Failure<ErrorCode>{map_windows_error(oserr)};
                }
            }

            entry->io_pending = false;
            issue_read(impl, *entry);
            return pjh::result::Result<std::vector<FileEvent>, ErrorCode>::Ok(std::move(out));

#elif PJH_PLATFORM_MACOS
            if (impl.run_loop == nullptr)
                return pjh::result::Failure<ErrorCode>{ErrorCode::Unknown};

            long long ms = timeout.count();
            if (ms < 0)
                ms = 0;
            for (auto &e : impl.entries) e->pending_events.clear();
            // Pump the run loop the FSEvents streams are scheduled on. Events
            // delivered while pumping land in each entry's pending list; anything
            // not delivered by the deadline stays buffered by FSEvents.
            CFRunLoopRunInMode(kCFRunLoopDefaultMode, static_cast<double>(ms) / 1000.0, true);
            for (auto &e : impl.entries)
                if (!e->pending_events.empty())
                    process_fsevents(*e, out);
            return pjh::result::Result<std::vector<FileEvent>, ErrorCode>::Ok(std::move(out));

#else  // PJH_PLATFORM_LINUX
            if (impl.fd == -1)
                return pjh::result::Failure<ErrorCode>{ErrorCode::Unknown};

            long long ms = timeout.count();
            if (ms < 0)
                ms = 0;
            struct pollfd pfd{impl.fd, POLLIN, 0};
            int rc = ::poll(&pfd, 1, static_cast<int>(ms));
            if (rc == -1)
            {
                if (errno == EINTR)
                    return pjh::result::Failure<ErrorCode>{ErrorCode::Interrupted};
                return pjh::result::Failure<ErrorCode>{map_errno_to_error(errno)};
            }
            if (rc == 0)
                return pjh::result::Result<std::vector<FileEvent>, ErrorCode>::Ok(std::move(out));

            std::map<WatchEntry *, std::set<std::filesystem::path> > affected;
            alignas(struct inotify_event) unsigned char buf[64 * 1024];
            for (;;)
            {
                ssize_t n = ::read(impl.fd, buf, sizeof(buf));
                if (n == -1)
                {
                    if (errno == EINTR)
                        continue;
                    break;
                }
                if (n <= 0)
                    break;

                std::size_t off = 0;
                while (off + sizeof(struct inotify_event) <= static_cast<std::size_t>(n))
                {
                    auto *ev = reinterpret_cast<struct inotify_event *>(buf + off);
                    off += sizeof(struct inotify_event) + ev->len;
                    if (off > static_cast<std::size_t>(n))
                        break;

                    // The inotify queue overflowed and events were dropped;
                    // recover by diffing every watched directory against its
                    // stored baseline snapshot.
                    if (ev->mask & IN_Q_OVERFLOW)
                    {
                        for (auto &e : impl.entries) resync_linux(impl, *e, out);
                        continue;
                    }

                    for (auto &e : impl.entries)
                    {
                        auto wit = e->wd_to_path.find(ev->wd);
                        if (wit == e->wd_to_path.end())
                            continue;
                        if (ev->mask & IN_IGNORED)
                        {
                            auto gone = wit->second;
                            e->wd_to_path.erase(wit);
                            prune_snapshots(*e, gone);
                            continue;
                        }
                        auto full = (ev->len > 0) ? wit->second / ev->name : wit->second;
                        if (!e->is_directory && full != e->path)
                            continue;
                        affected[e.get()].insert(wit->second);
                        if (auto kind = mask_to_kind(ev->mask))
                        {
                            FileEvent fe;
                            fe.kind = *kind;
                            fe.path = full;
                            if (*kind == FileEventKind::MovedFrom ||
                                *kind == FileEventKind::MovedTo)
                                fe.cookie = static_cast<std::uint32_t>(ev->cookie);
                            out.push_back(std::move(fe));
                        }
                        if (e->recursive && (ev->mask & IN_ISDIR))
                        {
                            if (ev->mask & IN_CREATE)
                            {
                                int wd = ::inotify_add_watch(impl.fd, full.c_str(), watch_mask());
                                if (wd != -1)
                                    e->wd_to_path[wd] = full;
                                auto cap = DirectorySnapshot::capture(full);
                                if (cap.is_ok())
                                    e->snapshots[full] = std::move(cap).unwrap();
                            }
                            else if (ev->mask & IN_DELETE)
                            {
                                remove_subdir_watches(impl.fd, *e, full);
                                prune_snapshots(*e, full);
                            }
                        }
                    }
                }
                if (n < static_cast<ssize_t>(sizeof(buf)))
                    break;
            }

            // Refresh the baselines of directories touched by this batch so a
            // later queue overflow only reports changes made after this batch.
            for (auto &[e, dirs] : affected)
            {
                for (const auto &dir : dirs)
                {
                    auto sit = e->snapshots.find(dir);
                    if (sit == e->snapshots.end())
                        continue;
                    auto captured = DirectorySnapshot::capture(dir);
                    if (captured.is_err())
                    {
                        prune_snapshots(*e, dir);
                        continue;
                    }
                    sit->second = std::move(captured).unwrap();
                }
            }
            return pjh::result::Result<std::vector<FileEvent>, ErrorCode>::Ok(std::move(out));
#endif
        }
    }  // namespace detail

    auto FileWatcher::poll(std::chrono::milliseconds timeout)
        -> pjh::result::Result<std::vector<FileEvent>, ErrorCode>
    {
        if (!impl_ || impl_->closed)
            return pjh::result::Failure<ErrorCode>{ErrorCode::InvalidArgument};
        auto &impl = *impl_;
        if (impl.entries.empty())
            return pjh::result::Result<std::vector<FileEvent>, ErrorCode>::Ok(
                std::vector<FileEvent>{});
        return detail::platform_poll(impl, timeout);
    }
}  // namespace pjh::platform
