#include <pjh_platform/file_watcher.hpp>
#include <pjh_platform/platform.hpp>

#include "file_watcher_internal.hpp"

#include <cerrno>
#include <filesystem>

#if PJH_PLATFORM_MACOS
#include <fcntl.h>
#include <sys/event.h>
#include <unistd.h>
#elif PJH_PLATFORM_LINUX
#include <sys/inotify.h>
#endif

#include <cstdint>
#include <map>
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
#endif

#if PJH_PLATFORM_MACOS
    auto open_file_watch(
        FileWatcherImpl &impl, WatchEntry &entry, WatchEntry::DirWatch &dw,
        const std::filesystem::path &file_path) -> int
    {
        int fd = ::open(file_path.c_str(), O_RDONLY | O_EVTONLY);
        if (fd == -1)
            return -1;

        struct kevent ev;
        EV_SET(
            &ev, static_cast<uintptr_t>(fd), EVFILT_VNODE, EV_ADD | EV_ENABLE | EV_CLEAR,
            NOTE_WRITE | NOTE_EXTEND | NOTE_ATTRIB, 0, nullptr);
        if (::kevent(impl.fd, &ev, 1, nullptr, 0, nullptr) == -1)
        {
            ::close(fd);
            return -1;
        }

        entry.fd_to_file[fd] = file_path;
        dw.file_fds[file_path.filename()] = fd;
        return fd;
    }

    auto close_file_watch(
        WatchEntry &entry, WatchEntry::DirWatch &dw, const std::filesystem::path &name) -> void
    {
        auto it = dw.file_fds.find(name);
        if (it == dw.file_fds.end())
            return;
        int fd = it->second;
        entry.fd_to_file.erase(fd);
        dw.file_fds.erase(it);
        if (fd != -1)
            ::close(fd);
    }

    auto open_directory_watch(
        FileWatcherImpl &impl, WatchEntry &entry, const std::filesystem::path &dir)
        -> pjh::result::Result<void, ErrorCode>
    {
        std::error_code sec;
        auto real = std::filesystem::weakly_canonical(dir, sec);
        if (sec)
            real = dir;
        for (const auto &dw : entry.dirs)
            if (dw.real_path == real)
                return pjh::result::Result<void, ErrorCode>::Ok();

        int fd = ::open(dir.c_str(), O_RDONLY | O_EVTONLY);
        if (fd == -1)
            return pjh::result::Failure<ErrorCode>{map_errno_to_error(errno)};

        struct kevent ev;
        EV_SET(
            &ev, static_cast<uintptr_t>(fd), EVFILT_VNODE, EV_ADD | EV_ENABLE | EV_CLEAR,
            NOTE_WRITE | NOTE_DELETE | NOTE_RENAME, 0, nullptr);
        if (::kevent(impl.fd, &ev, 1, nullptr, 0, nullptr) == -1)
        {
            ::close(fd);
            return pjh::result::Failure<ErrorCode>{map_errno_to_error(errno)};
        }

        WatchEntry::DirWatch dw;
        dw.fd = fd;
        dw.dir_path = dir;
        dw.real_path = real;
        build_snapshot(dir, dw.snapshot);

        // A directory's NOTE_WRITE does not fire when a file's contents
        // change, so every existing file gets its own vnode watch.
        for (const auto &[name, stamp] : dw.snapshot)
            if (!stamp.is_dir)
                (void)open_file_watch(impl, entry, dw, dir / name);

        entry.fd_to_dir[fd] = entry.dirs.insert(entry.dirs.end(), std::move(dw));
        return pjh::result::Result<void, ErrorCode>::Ok();
    }

    auto is_path_watched(const WatchEntry &entry, const std::filesystem::path &dir) -> bool
    {
        for (const auto &dw : entry.dirs)
            if (dw.dir_path == dir)
                return true;
        return false;
    }

    auto close_directory_watch(WatchEntry &entry, const std::filesystem::path &dir) -> void
    {
        for (auto it = entry.dirs.begin(); it != entry.dirs.end(); ++it)
        {
            if (it->dir_path == dir)
            {
                for (const auto &[name, ffd] : it->file_fds)
                {
                    entry.fd_to_file.erase(ffd);
                    if (ffd != -1)
                        ::close(ffd);
                }
                it->file_fds.clear();
                if (it->fd != -1)
                    ::close(it->fd);
                entry.fd_to_dir.erase(it->fd);
                entry.dirs.erase(it);
                return;
            }
        }
    }

    auto build_snapshot(
        const std::filesystem::path &dir,
        std::map<std::filesystem::path, FileStamp> &snapshot) -> void
    {
        std::error_code ec;
        for (auto it = std::filesystem::directory_iterator(dir, ec);
             it != std::filesystem::directory_iterator(); ++it)
        {
            if (ec)
            {
                ec.clear();
                continue;
            }
            FileStamp stamp;
            std::error_code sec;
            auto type = it->status(sec).type();
            if (sec)
                continue;
            stamp.is_dir = type == std::filesystem::file_type::directory;
            if (!stamp.is_dir)
            {
                auto size = it->file_size(sec);
                if (!sec)
                    stamp.size = static_cast<std::intmax_t>(size);
            }
            auto mtime = it->last_write_time(sec);
            if (!sec)
                stamp.mtime_ns = static_cast<std::intmax_t>(mtime.time_since_epoch().count());
            snapshot[it->path().filename()] = stamp;
        }
    }
#endif

}  // namespace pjh::platform::detail
