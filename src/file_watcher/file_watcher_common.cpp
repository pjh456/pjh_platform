#include <filesystem>
#include <pjh_platform/file_watcher.hpp>
#include <pjh_platform/platform.hpp>

#include "../error_mapping.hpp"
#include "file_watcher_internal.hpp"

#if PJH_PLATFORM_LINUX
#include <sys/inotify.h>
#endif

#include <cstdint>
#include <utility>

namespace pjh::platform::detail
{
    auto make_absolute(const std::filesystem::path &p)
        -> pjh::result::Result<std::filesystem::path, ErrorCode>
    {
        std::error_code ec;
        auto abs = std::filesystem::absolute(p, ec);
        if (ec)
            return pjh::result::Failure<ErrorCode>{ErrorCode::Unknown};
        return pjh::result::Result<std::filesystem::path, ErrorCode>::Ok(abs.lexically_normal());
    }

#if PJH_PLATFORM_WINDOWS
    namespace
    {
        // A directory read completing with one of these codes means the
        // watched path itself is gone: ERROR_BROKEN_PIPE is the stale-handle
        // error the in-flight read reports when its directory is removed,
        // the rest are the not-found family. The handle is dead and the
        // watch can never recover.
        auto is_path_gone(DWORD err) -> bool
        {
            switch (err)
            {
            case ERROR_FILE_NOT_FOUND:
            case ERROR_PATH_NOT_FOUND:
            case ERROR_DIR_NOT_FOUND:
            case ERROR_BROKEN_PIPE:
                return true;
            default:
                return false;
            }
        }

        auto mark_watch_dead(WatchEntry &entry) -> void
        {
            entry.io_pending = false;
            if (entry.handle != INVALID_HANDLE_VALUE)
            {
                CloseHandle(entry.handle);
                entry.handle = INVALID_HANDLE_VALUE;
            }
        }
    }

    auto issue_read([[maybe_unused]] FileWatcherImpl &impl, WatchEntry &entry) -> bool
    {
        entry.overlapped = OVERLAPPED{};
        entry.io_pending = false;
        DWORD filter = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
                       FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE;
        if (ReadDirectoryChangesW(
                entry.handle, entry.buffer.data(), static_cast<DWORD>(entry.buffer.size()),
                entry.recursive ? TRUE : FALSE, filter, nullptr, &entry.overlapped, nullptr))
            entry.io_pending = true;
        return entry.io_pending;
    }

    auto on_read_failed(FileWatcherImpl &impl, WatchEntry &entry, DWORD oserr) -> ErrorCode
    {
        if (is_path_gone(oserr))
        {
            // The watched path is gone; the watch is dead and must not be
            // re-issued on the stale handle.
            mark_watch_dead(entry);
            return map_windows_error(oserr);
        }
        // Transient failure: re-issue the read so the watch stays alive.
        if (issue_read(impl, entry))
            return map_windows_error(oserr);
        // The re-issue itself failed: classify that error too so a dead
        // handle is torn down instead of leaving the watch silently deaf.
        DWORD reissue_err = GetLastError();
        if (is_path_gone(reissue_err))
        {
            mark_watch_dead(entry);
            return map_windows_error(reissue_err);
        }
        return map_windows_error(oserr);
    }
#endif

    auto path_is_under(const std::filesystem::path &child, const std::filesystem::path &parent)
        -> bool
    {
        auto rel = child.lexically_normal().lexically_relative(parent.lexically_normal());
        if (rel.empty())
            return false;
        for (const auto &comp : rel)
            if (comp == std::filesystem::path(".."))
                return false;
        return true;
    }

#if PJH_PLATFORM_LINUX
    auto watch_mask() -> std::uint32_t
    {
        return static_cast<std::uint32_t>(
            IN_CREATE | IN_DELETE | IN_MODIFY | IN_MOVED_FROM | IN_MOVED_TO);
    }

    auto file_watch_mask() -> std::uint32_t
    {
        // A single file watched directly on its own inode only ever receives
        // the self-deletion/move-out signals for its deletion or rename-out;
        // directory watches keep the plain mask (their "watched directory was
        // removed" behavior stays unchanged).
        return watch_mask() | IN_DELETE_SELF | IN_MOVE_SELF;
    }

    auto release_watch(FileWatcherImpl &impl, const WatchEntry &self, int wd) -> void
    {
        // inotify has no per-entry reference counting: one inotify_rm_watch
        // destroys the watch for every entry referencing the wd. The wd may
        // be shared (inotify_add_watch is idempotent per fd+inode, so a
        // recursive walk and a separate add of the same directory return the
        // same descriptor); only the last holder removes it from the kernel.
        for (const auto &other : impl.entries)
        {
            if (other.get() == &self)
                continue;
            if (other->wd_to_path.find(wd) != other->wd_to_path.end())
                return;
        }
        ::inotify_rm_watch(impl.fd, wd);
    }
#endif

#if PJH_PLATFORM_MACOS
    auto fsevents_callback(
        ConstFSEventStreamRef,
        void *info,
        size_t num_events,
        void *event_paths,
        const FSEventStreamEventFlags event_flags[],
        const FSEventStreamEventId[]) -> void
    {
        auto *entry = static_cast<WatchEntry *>(info);
        if (!entry)
            return;
        auto **paths = static_cast<char **>(event_paths);
        entry->pending_events.reserve(entry->pending_events.size() + num_events);
        for (size_t i = 0; i < num_events; ++i)
        {
            entry->pending_events.push_back(
                WatchEntry::PendingEvent{std::filesystem::path(paths[i]), event_flags[i]});
        }
    }

    auto create_fsevents_stream(FileWatcherImpl &impl, WatchEntry &entry) -> void
    {
        CFStringRef cf_path = CFStringCreateWithCString(
            kCFAllocatorDefault, entry.watch_root.c_str(), kCFStringEncodingUTF8);
        if (!cf_path)
            return;
        const void *cf_paths_values[] = {cf_path};
        CFArrayRef cf_paths =
            CFArrayCreate(kCFAllocatorDefault, cf_paths_values, 1, &kCFTypeArrayCallBacks);
        CFRelease(cf_path);
        if (!cf_paths)
            return;

        FSEventStreamContext ctx{};
        ctx.info = &entry;

        FSEventStreamCreateFlags flags = 0;
#ifdef kFSEventStreamCreateFlagNoDefer
        flags |= kFSEventStreamCreateFlagNoDefer;
#endif
#ifdef kFSEventStreamCreateFlagFileEvents
        flags |= kFSEventStreamCreateFlagFileEvents;
#endif

        FSEventStreamRef stream = FSEventStreamCreate(
            kCFAllocatorDefault, &fsevents_callback, &ctx, cf_paths, kFSEventStreamEventIdSinceNow,
            0.0, flags);
        CFRelease(cf_paths);
        if (!stream)
            return;

        FSEventStreamScheduleWithRunLoop(stream, impl.run_loop, kCFRunLoopDefaultMode);
        FSEventStreamStart(stream);
        entry.stream = stream;
    }
#endif

}  // namespace pjh::platform::detail
