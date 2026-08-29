#include <cerrno>
#include <filesystem>
#include <pjh_platform/file_watcher.hpp>
#include <pjh_platform/platform.hpp>

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

#if !PJH_PLATFORM_WINDOWS
    auto map_errno_to_error(int err) -> ErrorCode
    {
        switch (err)
        {
        case ENOENT:
            return ErrorCode::NotFound;
        case EACCES:
        case EPERM:
            return ErrorCode::PermissionDenied;
        case EEXIST:
            return ErrorCode::AlreadyExists;
        case EINVAL:
            return ErrorCode::InvalidArgument;
        case ENOSPC:
            return ErrorCode::LimitReached;
        case EINTR:
            return ErrorCode::Interrupted;
        default:
            return ErrorCode::Unknown;
        }
    }
#else
    auto map_windows_error(unsigned long err) -> ErrorCode
    {
        switch (err)
        {
        case ERROR_FILE_NOT_FOUND:
        case ERROR_PATH_NOT_FOUND:
            return ErrorCode::NotFound;
        case ERROR_ACCESS_DENIED:
            return ErrorCode::PermissionDenied;
        case ERROR_ALREADY_EXISTS:
            return ErrorCode::AlreadyExists;
        case ERROR_INVALID_PARAMETER:
        case ERROR_INVALID_HANDLE:
            return ErrorCode::InvalidArgument;
        case ERROR_NOT_SUPPORTED:
            return ErrorCode::NotSupported;
        default:
            return ErrorCode::Unknown;
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
                entry.recursive ? TRUE : FALSE, filter, nullptr, &entry.overlapped, nullptr))
            entry.io_pending = true;
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
