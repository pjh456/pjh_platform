#include <pjh_platform/file_watcher.hpp>
#include <pjh_platform/platform.hpp>

#include "../error_mapping.hpp"
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
        // An unresolvable target that is itself a symbolic link (a loop, or
        // a link whose resolution the OS cannot complete) is discoverable
        // but not a directory: NotFound, the same boundary semantics as the
        // ELOOP -> NotFound ruling (task 31, .w1mer CHANGES). POSIX loops
        // already land NotFound through the mapping table (no-op there); on
        // Windows the query arm observed an unmapped resolution code
        // (CI run 34070853078) that lands Unknown. is_symlink uses
        // symlink_status (does not follow the link), so it is loop-safe on
        // the loop root itself.
        auto refine_symlink_unknown(ErrorCode mapped, const std::filesystem::path &target)
            -> ErrorCode
        {
            if (mapped != ErrorCode::Unknown)
                return mapped;
            std::error_code sec;
            if (std::filesystem::is_symlink(target, sec))
                return ErrorCode::NotFound;
            return mapped;
        }

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
                for (auto it = std::filesystem::recursive_directory_iterator(
                         entry.watch_root,
                         std::filesystem::directory_options::skip_permission_denied, ec);
                     it != std::filesystem::recursive_directory_iterator(); it.increment(ec))
                {
                    std::error_code sec;
                    if (!std::filesystem::is_directory(it->path(), sec))
                        continue;
                    auto cap = DirectorySnapshot::capture(it->path());
                    if (cap.is_ok())
                        entry.snapshots[it->path()] = std::move(cap).unwrap();
                }
                // A truncated walk would leave a partial baseline model; fail
                // the registration instead of reporting Ok for it. No kernel
                // resource is registered yet, so no rollback is needed.
                if (ec)
                    return pjh::result::Failure<ErrorCode>{map_error_code(ec)};
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
            // The port was created by the constructor; this call only
            // associates `entry.handle` with it. On the CI image the returned
            // handle is the stored port handle itself (no new reference), so
            // closing it unconditionally destroyed the port (2026-08 Windows
            // lane regression): close only a genuinely different handle, and
            // store it only when the constructor's port creation failed.
            if (port != impl.port)
            {
                if (impl.port)
                    CloseHandle(port);
                else
                    impl.port = port;
            }

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
            if (cec)
                return pjh::result::Failure<ErrorCode>{detail::map_error_code(cec)};
            if (entry.canonical_root.empty())
                return pjh::result::Failure<ErrorCode>{ErrorCode::Unknown};

            auto root_cap = DirectorySnapshot::capture(entry.watch_root);
            if (root_cap.is_err())
                return pjh::result::Failure<ErrorCode>{root_cap.unwrap_err()};
            entry.snapshots[entry.watch_root] = std::move(root_cap).unwrap();

            if (entry.recursive)
            {
                std::error_code ec;
                for (auto it = std::filesystem::recursive_directory_iterator(
                         entry.watch_root,
                         std::filesystem::directory_options::skip_permission_denied, ec);
                     it != std::filesystem::recursive_directory_iterator(); it.increment(ec))
                {
                    std::error_code sec;
                    if (!std::filesystem::is_directory(it->path(), sec))
                        continue;
                    auto cap = DirectorySnapshot::capture(it->path());
                    if (cap.is_ok())
                        entry.snapshots[it->path()] = std::move(cap).unwrap();
                }
                // A truncated walk would leave a partial baseline model; fail
                // the registration before the stream is created instead of
                // reporting Ok for it.
                if (ec)
                    return pjh::result::Failure<ErrorCode>{map_error_code(ec)};
            }

            create_fsevents_stream(impl, entry);
            if (!entry.stream)
                return pjh::result::Failure<ErrorCode>{ErrorCode::Unknown};
            return pjh::result::Result<void, ErrorCode>::Ok();

#else
            if (impl.fd == -1)
                return pjh::result::Failure<ErrorCode>{ErrorCode::Unknown};

            if (entry.is_directory)
            {
                auto baselines = capture_baselines(entry);
                if (baselines.is_err())
                    return pjh::result::Failure<ErrorCode>{baselines.unwrap_err()};

                entry.root_wd =
                    ::inotify_add_watch(impl.fd, entry.watch_root.c_str(), watch_mask());
                if (entry.root_wd == -1)
                    return pjh::result::Failure<ErrorCode>{map_errno_to_error(errno)};
                entry.wd_to_path[entry.root_wd] = entry.watch_root;

                if (entry.recursive)
                {
                    int first_errno = 0;
                    std::error_code ec;
                    std::error_code walk_sec;
                    for (auto it = std::filesystem::recursive_directory_iterator(
                             entry.watch_root,
                             std::filesystem::directory_options::skip_permission_denied, ec);
                         it != std::filesystem::recursive_directory_iterator(); it.increment(ec))
                    {
                        std::error_code sec;
                        if (!it->is_directory(sec))
                        {
                            // A genuine non-directory has a clear sec and is
                            // skipped as before. An entry whose type query
                            // failed (sec set) could not be classified: its
                            // subtree may still need a watch, so skipping it
                            // silently would expose a partial watch set.
                            // Record it and let the shared rollback below fail
                            // the registration instead.
                            if (sec && !walk_sec)
                                walk_sec = sec;
                            continue;
                        }
                        int wd = ::inotify_add_watch(impl.fd, it->path().c_str(), watch_mask());
                        if (wd == -1)
                        {
                            // EACCES/EPERM: the documented skip (header
                            // @details, unreadable subdirectories); any other
                            // errno (notably ENOSPC, mid-walk slot
                            // exhaustion) aborts: a silently unwatched
                            // subtree is a coverage loss, and add()
                            // must not return Ok for it.
                            if (errno == EACCES || errno == EPERM)
                                continue;
                            if (!first_errno)
                                first_errno = errno;
                            break;
                        }
                        entry.wd_to_path[wd] = it->path();
                    }
                    if (first_errno || ec || walk_sec)
                    {
                        // A truncated walk or an unclassifiable entry leaves a
                        // partial watch set behind (the same coverage loss as
                        // mid-walk ENOSPC): release everything this
                        // registration took (root wd included: it is in
                        // wd_to_path from the root registration above).
                        // Shared wds survive: release_watch's holder scan
                        // skips any wd another live entry still holds. An
                        // inotify failure breaks out before the next increment,
                        // so first_errno is authoritative when set; otherwise
                        // map whichever enumeration failure is present.
                        const ErrorCode code = first_errno ? map_errno_to_error(first_errno)
                                                           : (walk_sec ? map_error_code(walk_sec)
                                                                       : map_error_code(ec));
                        for (const auto &[wd, unused] : entry.wd_to_path)
                            release_watch(impl, entry, wd);
                        entry.wd_to_path.clear();
                        entry.root_wd = -1;  // hygiene; the entry is destroyed by the caller
                        return pjh::result::Failure<ErrorCode>{code};
                    }
                }
            }
            else
            {
                // The single file is watched directly on its own inode, which
                // keeps sibling churn entirely out of the inotify queue. The
                // trade-offs (a rename into the path is not reported; a
                // deleted or moved file leaves the watch inactive until it is
                // re-added) are documented in file_watcher.hpp. No baseline
                // snapshots: the file entry has no directory to capture.
                entry.root_wd = ::inotify_add_watch(impl.fd, entry.path.c_str(), file_watch_mask());
                if (entry.root_wd == -1)
                    return pjh::result::Failure<ErrorCode>{map_errno_to_error(errno)};
                entry.wd_to_path[entry.root_wd] = entry.path;
            }
            return pjh::result::Result<void, ErrorCode>::Ok();
#endif
        }
    }  // namespace detail

    auto FileWatcher::add(const std::filesystem::path &path, bool recursive)
        -> pjh::result::Result<void, ErrorCode>
    {
        if (!impl_ || impl_->closed)
            return pjh::result::Failure<ErrorCode>{ErrorCode::InvalidArgument};
        auto &impl = *impl_;

        auto abs = detail::make_absolute(path);
        if (abs.is_err())
            return pjh::result::Failure<ErrorCode>{abs.unwrap_err()};
        std::filesystem::path absolute = std::move(abs).unwrap();

        // Identity is canonical, not lexical: a different spelling of the
        // same file or directory (a symbolic link and its target, the OS
        // alias pair /tmp vs /private/tmp) would register a second entry,
        // and the platform routing then delivers every change once per
        // entry, one event per spelling, which the per-batch (kind, path)
        // tables cannot suppress because the spellings differ. Reject the
        // second spelling at registration. weakly_canonical resolves the
        // existing prefix without requiring the path to exist and cannot
        // throw; on a query error (symlink loop, unreadable component) fall
        // open to the lexical comparison above, which is today's behavior.
        std::error_code cec;
        auto canonical = std::filesystem::weakly_canonical(absolute, cec);
        const bool canonical_ok = !cec;
        for (const auto &existing : impl.entries)
        {
            if (existing->path == absolute)
                return pjh::result::Failure<ErrorCode>{ErrorCode::AlreadyWatched};
            if (!canonical_ok)
                continue;
            std::error_code xec;
            auto existing_canonical = std::filesystem::weakly_canonical(existing->path, xec);
            if (xec)
                continue;
            if (canonical == existing_canonical)
                return pjh::result::Failure<ErrorCode>{ErrorCode::AlreadyWatched};
        }

        std::error_code ec;
        if (!std::filesystem::exists(absolute, ec))
        {
            if (ec)
            {
                auto mapped = detail::refine_symlink_unknown(detail::map_error_code(ec), absolute);
                return pjh::result::Failure<ErrorCode>{mapped};
            }
            return pjh::result::Failure<ErrorCode>{ErrorCode::NotFound};
        }
        bool is_dir = std::filesystem::is_directory(absolute, ec);
        if (ec)
        {
            auto mapped = detail::refine_symlink_unknown(detail::map_error_code(ec), absolute);
            return pjh::result::Failure<ErrorCode>{mapped};
        }

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
