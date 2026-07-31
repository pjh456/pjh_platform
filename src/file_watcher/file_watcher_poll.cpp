#include <pjh_platform/file_watcher.hpp>
#include <pjh_platform/platform.hpp>

#include "file_watcher_internal.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#if PJH_PLATFORM_WINDOWS
#include <windows.h>
#elif PJH_PLATFORM_MACOS
#include <sys/event.h>
#include <unistd.h>
#else
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
#if PJH_PLATFORM_MACOS
            auto push_filtered(
                WatchEntry &entry, FileEventKind kind, const std::filesystem::path &path,
                std::vector<FileEvent> &out) -> void
            {
                if (!entry.is_directory && path != entry.path)
                    return;
                out.push_back(FileEvent{kind, path, std::nullopt});
            }

            auto diff_directory(
                FileWatcherImpl &impl, WatchEntry &entry, WatchEntry::DirWatch &dw,
                std::vector<FileEvent> &out) -> void
            {
                std::map<std::filesystem::path, FileStamp> current;
                build_snapshot(dw.dir_path, current);

                for (const auto &[name, stamp] : current)
                {
                    auto old = dw.snapshot.find(name);
                    if (old == dw.snapshot.end())
                        push_filtered(entry, FileEventKind::Created, dw.dir_path / name, out);
                    else if (!stamp.is_dir && !old->second.is_dir &&
                             (stamp.size != old->second.size ||
                              stamp.mtime_ns != old->second.mtime_ns))
                        push_filtered(entry, FileEventKind::Modified, dw.dir_path / name, out);
                }
                for (const auto &[name, stamp] : dw.snapshot)
                    if (!current.count(name))
                        push_filtered(entry, FileEventKind::Deleted, dw.dir_path / name, out);

                if (entry.recursive)
                {
                    for (const auto &[name, stamp] : current)
                        if (stamp.is_dir && !dw.snapshot.count(name) &&
                            !is_path_watched(entry, dw.dir_path / name))
                            (void)open_directory_watch(impl, entry, dw.dir_path / name);
                    for (const auto &[name, stamp] : dw.snapshot)
                        if (stamp.is_dir && !current.count(name))
                            close_directory_watch(entry, dw.dir_path / name);
                }

                dw.snapshot = std::move(current);
            }
#elif PJH_PLATFORM_LINUX
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

            auto remove_subdir_watches(
                int fd, WatchEntry &entry, const std::filesystem::path &dir) -> void
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
#else  // PJH_PLATFORM_WINDOWS
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
                        entry.handle, entry.buffer.data(),
                        static_cast<DWORD>(entry.buffer.size()),
                        entry.recursive ? TRUE : FALSE, filter, nullptr, &entry.overlapped,
                        nullptr))
                    entry.io_pending = true;
            }
#endif
        }  // namespace

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
            BOOL ok = GetQueuedCompletionStatus(
                impl.port, &bytes, &key, &ov, static_cast<DWORD>(ms));
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
                return pjh::result::Result<std::vector<FileEvent>, ErrorCode>::Ok(
                    std::move(out));

            std::size_t off = 0;
            while (off + sizeof(FILE_NOTIFY_INFORMATION) <= static_cast<std::size_t>(bytes))
            {
                auto *rec = reinterpret_cast<FILE_NOTIFY_INFORMATION *>(
                    entry->buffer.data() + off);
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
                if (rec->NextEntryOffset == 0)
                    break;
                off += rec->NextEntryOffset;
            }

            entry->io_pending = false;
            issue_read(impl, *entry);
            return pjh::result::Result<std::vector<FileEvent>, ErrorCode>::Ok(std::move(out));

#elif PJH_PLATFORM_MACOS
            if (impl.fd == -1)
                return pjh::result::Failure<ErrorCode>{ErrorCode::Unknown};

            long long ms = timeout.count();
            if (ms < 0)
                ms = 0;
            struct timespec ts;
            ts.tv_sec = static_cast<time_t>(ms / 1000);
            ts.tv_nsec = static_cast<long>((ms % 1000) * 1000000L);
            struct kevent events[16];
            int n = ::kevent(impl.fd, nullptr, 0, events, 16, &ts);
            if (n == -1)
            {
                if (errno == EINTR)
                    return pjh::result::Failure<ErrorCode>{ErrorCode::Interrupted};
                return pjh::result::Failure<ErrorCode>{map_errno_to_error(errno)};
            }
            for (int i = 0; i < n; ++i)
            {
                int fd = static_cast<int>(events[i].ident);
                for (auto &e : impl.entries)
                {
                    auto it = e->fd_to_dir.find(fd);
                    if (it == e->fd_to_dir.end())
                        continue;
                    auto &dw = *it->second;
                    if (events[i].fflags & NOTE_DELETE)
                        push_filtered(*e, FileEventKind::Deleted, dw.dir_path, out);
                    if (events[i].fflags & NOTE_RENAME)
                        push_filtered(*e, FileEventKind::MovedFrom, dw.dir_path, out);
                    if (events[i].fflags & (NOTE_WRITE | NOTE_EXTEND | NOTE_ATTRIB))
                        diff_directory(impl, *e, dw, out);
                }
            }
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
                return pjh::result::Result<std::vector<FileEvent>, ErrorCode>::Ok(
                    std::move(out));

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

                    for (auto &e : impl.entries)
                    {
                        auto wit = e->wd_to_path.find(ev->wd);
                        if (wit == e->wd_to_path.end())
                            continue;
                        if (ev->mask & IN_IGNORED)
                        {
                            e->wd_to_path.erase(wit);
                            continue;
                        }
                        auto full = (ev->len > 0) ? wit->second / ev->name : wit->second;
                        if (!e->is_directory && full != e->path)
                            continue;
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
                                int wd = ::inotify_add_watch(
                                    impl.fd, full.c_str(), watch_mask());
                                if (wd != -1)
                                    e->wd_to_path[wd] = full;
                            }
                            else if (ev->mask & IN_DELETE)
                            {
                                remove_subdir_watches(impl.fd, *e, full);
                            }
                        }
                    }
                }
                if (n < static_cast<ssize_t>(sizeof(buf)))
                    break;
            }
            return pjh::result::Result<std::vector<FileEvent>, ErrorCode>::Ok(std::move(out));
#endif
        }
    }  // namespace detail

    auto FileWatcher::poll(std::chrono::milliseconds timeout)
        -> pjh::result::Result<std::vector<FileEvent>, ErrorCode>
    {
        auto &impl = *impl_;
        if (impl.closed)
            return pjh::result::Failure<ErrorCode>{ErrorCode::InvalidArgument};
        if (impl.entries.empty())
            return pjh::result::Result<std::vector<FileEvent>, ErrorCode>::Ok(
                std::vector<FileEvent>{});
        return detail::platform_poll(impl, timeout);
    }
}  // namespace pjh::platform
