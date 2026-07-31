#include <pjh_platform/file_watcher.hpp>
#include <pjh_platform/platform.hpp>

#include "file_watcher_internal.hpp"

#if PJH_PLATFORM_WINDOWS
#include <windows.h>
#elif PJH_PLATFORM_MACOS
#include <unistd.h>
#else
#include <sys/inotify.h>
#endif

#include <utility>

namespace pjh::platform
{
    namespace detail
    {
        auto unregister_watch([[maybe_unused]] FileWatcherImpl &impl, WatchEntry &entry)
            -> void
        {
#if PJH_PLATFORM_WINDOWS
            if (entry.handle != INVALID_HANDLE_VALUE)
            {
                if (entry.io_pending)
                {
                    CancelIoEx(entry.handle, &entry.overlapped);
                    // The aborted completion is queued on the port; drain it so
                    // the entry can be destroyed without leaving a dangling
                    // completion key behind.
                    for (;;)
                    {
                        DWORD bytes = 0;
                        ULONG_PTR key = 0;
                        OVERLAPPED *ov = nullptr;
                        BOOL ok =
                            GetQueuedCompletionStatus(impl.port, &bytes, &key, &ov, 0);
                        if (!ok)
                            break;
                        if (key == reinterpret_cast<ULONG_PTR>(&entry))
                            break;
                    }
                }
                CloseHandle(entry.handle);
                entry.handle = INVALID_HANDLE_VALUE;
            }
#elif PJH_PLATFORM_MACOS
            for (auto &dw : entry.dirs)
            {
                for (const auto &[name, ffd] : dw.file_fds)
                    if (ffd != -1)
                        ::close(ffd);
                if (dw.fd != -1)
                    ::close(dw.fd);
            }
            entry.dirs.clear();
            entry.fd_to_dir.clear();
            entry.fd_to_file.clear();
#else
            for (auto &[wd, unused] : entry.wd_to_path) ::inotify_rm_watch(impl.fd, wd);
            entry.wd_to_path.clear();
            entry.root_wd = -1;
#endif
        }
    }  // namespace detail

    auto FileWatcher::remove(const std::filesystem::path &path)
        -> pjh::result::Result<void, ErrorCode>
    {
        if (!impl_ || impl_->closed)
            return pjh::result::Failure<ErrorCode>{ErrorCode::InvalidArgument};
        auto &impl = *impl_;

        auto abs = detail::make_absolute(path);
        if (abs.is_err())
            return pjh::result::Failure<ErrorCode>{abs.unwrap_err()};
        auto absolute = std::move(abs).unwrap();

        for (auto it = impl.entries.begin(); it != impl.entries.end(); ++it)
        {
            if ((*it)->path == absolute)
            {
                detail::unregister_watch(impl, **it);
                impl.entries.erase(it);
                return pjh::result::Result<void, ErrorCode>::Ok();
            }
        }
        return pjh::result::Failure<ErrorCode>{ErrorCode::NotFound};
    }
}  // namespace pjh::platform
