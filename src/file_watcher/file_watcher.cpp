#include <pjh_platform/file_watcher.hpp>
#include <pjh_platform/platform.hpp>

#include "file_watcher_internal.hpp"

#if PJH_PLATFORM_WINDOWS
#include <windows.h>
#elif PJH_PLATFORM_LINUX
#include <sys/inotify.h>
#include <unistd.h>
#endif

#include <memory>
#include <utility>

namespace pjh::platform
{
    FileWatcher::FileWatcher() : impl_(std::make_unique<FileWatcherImpl>())
    {
#if PJH_PLATFORM_WINDOWS
        impl_->port = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
#elif PJH_PLATFORM_MACOS
        impl_->run_loop = CFRunLoopGetCurrent();
#else
        impl_->fd = ::inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
#endif
    }

    FileWatcher::~FileWatcher()
    {
        close();
    }

    FileWatcher::FileWatcher(FileWatcher &&) noexcept = default;

    FileWatcher &FileWatcher::operator=(FileWatcher &&) noexcept = default;

    void FileWatcher::close()
    {
        if (!impl_ || impl_->closed)
            return;
        impl_->closed = true;

        for (auto &entry : impl_->entries)
            detail::unregister_watch(*impl_, *entry);
        impl_->entries.clear();

#if PJH_PLATFORM_WINDOWS
        if (impl_->port)
        {
            CloseHandle(impl_->port);
            impl_->port = nullptr;
        }
#elif PJH_PLATFORM_LINUX
        if (impl_->fd != -1)
        {
            ::close(impl_->fd);
            impl_->fd = -1;
        }
#endif
    }
}  // namespace pjh::platform
