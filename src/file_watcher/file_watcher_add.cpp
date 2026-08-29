#include <pjh_platform/file_watcher.hpp>
#include <pjh_platform/platform.hpp>

#include "file_watcher_internal.hpp"

#if PJH_PLATFORM_WINDOWS
#include <windows.h>
#else
#include <cerrno>
#endif

#include <filesystem>
#include <memory>
#include <utility>

namespace pjh::platform
{
    namespace detail
    {
#if PJH_PLATFORM_WINDOWS
        namespace
        {
            auto watch_filter() -> DWORD
            {
                return FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
                       FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE;
            }
        }
#endif

#if PJH_PLATFORM_LINUX || PJH_PLATFORM_WINDOWS
        namespace
        {
            /// @brief Captures a baseline snapshot of `watch_root` and, for
            ///        recursive watches, of every subdirectory below it. A
            ///        failure to capture the root aborts the registration;
            ///        per-subdirectory captures are best-effort.
            auto capture_baselines(WatchEntry &entry) -> pjh::result::Result<void, ErrorCode>
            {
                entry.snapshots.clear();
                auto root_cap = DirectorySnapshot::capture(entry.watch_root);
                if (root_cap.is_err())
                    return pjh::result::Failure<ErrorCode>{root_cap.unwrap_err()};
                entry.snapshots[entry.watch_root] = std::move(root_cap).unwrap();

                if (!entry.recursive)
                    return pjh::result::Result<void, ErrorCode>::Ok();

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
                    auto cap = DirectorySnapshot::capture(it->path());
                    if (cap.is_ok())
                        entry.snapshots[it->path()] = std::move(cap).unwrap();
                }
                return pjh::result::Result<void, ErrorCode>::Ok();
            }
        }
#endif

        auto register_watch(FileWatcherImpl &impl, WatchEntry &entry)
            -> pjh::result::Result<void, ErrorCode>
        {
#if PJH_PLATFORM_WINDOWS
            auto baselines = capture_baselines(entry);
            if (baselines.is_err())
                return pjh::result::Failure<ErrorCode>{baselines.unwrap_err()};

            entry.handle = CreateFileW(
                entry.watch_root.c_str(), FILE_LIST_DIRECTORY,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
            if (entry.handle == INVALID_HANDLE_VALUE)
                return pjh::result::Failure<ErrorCode>{map_windows_error(GetLastError())};

            HANDLE port = CreateIoCompletionPort(
                entry.handle, impl.port, reinterpret_cast<ULONG_PTR>(&entry), 0);
            if (!port)
            {
                DWORD err = GetLastError();
                CloseHandle(entry.handle);
                entry.handle = INVALID_HANDLE_VALUE;
                return pjh::result::Failure<ErrorCode>{map_windows_error(err)};
            }
            if (!impl.port)
                impl.port = port;

            entry.buffer.resize(64 * 1024);
            BOOL ok = ReadDirectoryChangesW(
                entry.handle, entry.buffer.data(), static_cast<DWORD>(entry.buffer.size()),
                entry.recursive ? TRUE : FALSE, watch_filter(), nullptr, &entry.overlapped,
                nullptr);
            if (!ok)
            {
                DWORD err = GetLastError();
                CloseHandle(entry.handle);
                entry.handle = INVALID_HANDLE_VALUE;
                return pjh::result::Failure<ErrorCode>{map_windows_error(err)};
            }
            entry.io_pending = true;
            return pjh::result::Result<void, ErrorCode>::Ok();

#elif PJH_PLATFORM_MACOS
            std::error_code cec;
            entry.canonical_root = std::filesystem::weakly_canonical(entry.watch_root, cec);
            if (cec || entry.canonical_root.empty())
                return pjh::result::Failure<ErrorCode>{ErrorCode::Unknown};

            auto root_cap = DirectorySnapshot::capture(entry.watch_root);
            if (root_cap.is_err())
                return pjh::result::Failure<ErrorCode>{root_cap.unwrap_err()};
            entry.snapshots[entry.watch_root] = std::move(root_cap).unwrap();

            if (entry.recursive)
            {
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
                    auto cap = DirectorySnapshot::capture(it->path());
                    if (cap.is_ok())
                        entry.snapshots[it->path()] = std::move(cap).unwrap();
                }
            }

            create_fsevents_stream(impl, entry);
            if (!entry.stream)
                return pjh::result::Failure<ErrorCode>{ErrorCode::Unknown};
            return pjh::result::Result<void, ErrorCode>::Ok();

#else
            if (impl.fd == -1)
                return pjh::result::Failure<ErrorCode>{ErrorCode::Unknown};

            auto baselines = capture_baselines(entry);
            if (baselines.is_err())
                return pjh::result::Failure<ErrorCode>{baselines.unwrap_err()};

            entry.root_wd = ::inotify_add_watch(impl.fd, entry.watch_root.c_str(), watch_mask());
            if (entry.root_wd == -1)
                return pjh::result::Failure<ErrorCode>{map_errno_to_error(errno)};
            entry.wd_to_path[entry.root_wd] = entry.watch_root;

            if (entry.recursive)
            {
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
                    if (!it->is_directory(sec))
                        continue;
                    int wd = ::inotify_add_watch(impl.fd, it->path().c_str(), watch_mask());
                    if (wd != -1)
                        entry.wd_to_path[wd] = it->path();
                }
            }
            return pjh::result::Result<void, ErrorCode>::Ok();
#endif
        }
    }  // namespace detail

    auto FileWatcher::add(const std::filesystem::path &path, bool recursive)
        -> pjh::result::Result<void, ErrorCode>
    {
        auto &impl = *impl_;
        if (impl.closed)
            return pjh::result::Failure<ErrorCode>{ErrorCode::InvalidArgument};

        auto abs = detail::make_absolute(path);
        if (abs.is_err())
            return pjh::result::Failure<ErrorCode>{abs.unwrap_err()};
        std::filesystem::path absolute = std::move(abs).unwrap();

        for (const auto &existing : impl.entries)
            if (existing->path == absolute)
                return pjh::result::Failure<ErrorCode>{ErrorCode::AlreadyWatched};

        std::error_code ec;
        if (!std::filesystem::exists(absolute, ec))
        {
            if (ec)
                return pjh::result::Failure<ErrorCode>{ErrorCode::Unknown};
            return pjh::result::Failure<ErrorCode>{ErrorCode::NotFound};
        }
        bool is_dir = std::filesystem::is_directory(absolute, ec);
        if (ec)
            return pjh::result::Failure<ErrorCode>{ErrorCode::Unknown};

        auto entry = std::make_unique<detail::WatchEntry>();
        entry->path = absolute;
        entry->is_directory = is_dir;
        entry->recursive = is_dir && recursive;
        entry->watch_root = is_dir ? absolute : absolute.parent_path();

        auto registered = detail::register_watch(impl, *entry);
        if (registered.is_err())
            return pjh::result::Failure<ErrorCode>{registered.unwrap_err()};

        impl.entries.push_back(std::move(entry));
        return pjh::result::Result<void, ErrorCode>::Ok();
    }
}  // namespace pjh::platform
