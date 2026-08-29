#include <pjh_platform/file_watcher.hpp>
#include <pjh_platform/platform.hpp>

#include "file_watcher_internal.hpp"

#if PJH_PLATFORM_WINDOWS
#include <windows.h>
#elif PJH_PLATFORM_LINUX
#include <sys/inotify.h>
#endif

#include <utility>

namespace pjh::platform
{
    namespace detail
    {
#if PJH_PLATFORM_WINDOWS
        namespace
        {
            // Budget for the drain wait: 1000 x 1 ms. The aborted completion
            // is already queued once CancelIoEx returns, so the first or
            // second call normally hits; only a pathological delay burns the
            // full ~1 s budget.
            constexpr auto kDrainWaitMs = 1;
            constexpr auto kDrainAttempts = 1000;
        }
#endif

        auto unregister_watch([[maybe_unused]] FileWatcherImpl &impl, WatchEntry &entry) -> void
        {
#if PJH_PLATFORM_WINDOWS
            if (entry.handle != INVALID_HANDLE_VALUE)
            {
                if (entry.io_pending)
                {
                    CancelIoEx(entry.handle, &entry.overlapped);
                    // The aborted completion is queued on the port; drain it so
                    // the entry can be destroyed without leaving a dangling
                    // completion key behind. Completions of other watches may
                    // sit ahead of ours in the queue: re-issue their reads so
                    // they are not left deaf.
                    const auto self = reinterpret_cast<ULONG_PTR>(&entry);
                    for (int i = 0; i < kDrainAttempts; ++i)
                    {
                        DWORD bytes = 0;
                        ULONG_PTR key = 0;
                        OVERLAPPED *ov = nullptr;
                        if (!GetQueuedCompletionStatus(
                                impl.port, &bytes, &key, &ov, static_cast<DWORD>(kDrainWaitMs)))
                        {
                            if (GetLastError() != WAIT_TIMEOUT)
                                break;
                            continue;
                        }
                        if (key == self)
                            break;
                        for (auto &e : impl.entries)
                        {
                            if (reinterpret_cast<ULONG_PTR>(e.get()) == key)
                            {
                                issue_read(impl, *e);
                                break;
                            }
                        }
                    }
                }
                CloseHandle(entry.handle);
                entry.handle = INVALID_HANDLE_VALUE;
            }
#elif PJH_PLATFORM_MACOS
            if (entry.stream)
            {
                FSEventStreamStop(entry.stream);
                FSEventStreamInvalidate(entry.stream);
                FSEventStreamRelease(entry.stream);
                entry.stream = nullptr;
            }
            entry.snapshots.clear();
            entry.pending_events.clear();
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
