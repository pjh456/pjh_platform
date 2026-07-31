#ifndef INCLUDE_PJH_PLATFORM_FILE_WATCHER_INTERNAL_HPP
#define INCLUDE_PJH_PLATFORM_FILE_WATCHER_INTERNAL_HPP

#include <pjh_platform/file_watcher.hpp>
#include <pjh_platform/platform.hpp>

#if PJH_PLATFORM_WINDOWS
#include <windows.h>
#elif PJH_PLATFORM_MACOS
#include <sys/event.h>
#include <sys/stat.h>
#include <unistd.h>
#else
#include <sys/inotify.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <cstdint>
#include <filesystem>
#include <list>
#include <map>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

namespace pjh::platform
{
    struct FileWatcherImpl;

    namespace detail
    {
#if PJH_PLATFORM_MACOS
        /// @brief Snapshot of one directory entry, used for macOS diffing.
        struct FileStamp
        {
            std::intmax_t size = 0;
            std::intmax_t mtime_ns = 0;
            bool is_dir = false;
        };
#endif

#if PJH_PLATFORM_WINDOWS
        /// @brief Per-watch state on Windows: a directory handle plus the
        ///        overlapped I/O buffer that `ReadDirectoryChangesW` fills.
        struct WatchEntry
        {
            /// @brief Normalized absolute path of the watched file or directory.
            std::filesystem::path path;
            bool is_directory = false;
            bool recursive = false;
            /// @brief Directory that holds the actual watch (the parent of a
            ///        watched file, or the directory itself).
            std::filesystem::path watch_root;

            HANDLE handle = INVALID_HANDLE_VALUE;
            OVERLAPPED overlapped{};
            std::vector<unsigned char> buffer;
            bool io_pending = false;
        };
#elif PJH_PLATFORM_MACOS
        /// @brief Per-watch state on macOS: every watched directory (root or
        ///        subdirectory) has its own kqueue registration, snapshot, and
        ///        a per-file kqueue registration for content changes.
        struct WatchEntry
        {
            /// @brief A single watched directory. A directory's `NOTE_WRITE`
            ///        only fires when its *entries* change (create/delete/
            ///        rename), not when a file's contents change, so each file
            ///        also gets its own vnode watch via `file_fds`.
            struct DirWatch
            {
                int fd = -1;
                std::filesystem::path dir_path;
                /// @brief Canonical path used to detect symlink cycles.
                std::filesystem::path real_path;
                std::map<std::filesystem::path, FileStamp> snapshot;
                /// @brief Maps an entry filename to the fd watching that file.
                std::map<std::filesystem::path, int> file_fds;
            };

            /// @brief Normalized absolute path of the watched file or directory.
            std::filesystem::path path;
            bool is_directory = false;
            bool recursive = false;
            /// @brief Directory that holds the actual watch (the parent of a
            ///        watched file, or the directory itself).
            std::filesystem::path watch_root;

            /// @brief One kqueue watch per watched directory. `std::list` keeps
            ///        node addresses (and therefore iterators stored in
            ///        `fd_to_dir`) stable while subdirectory watches are added
            ///        or removed during polling.
            std::list<DirWatch> dirs;
            /// @brief Maps a directory fd to its `DirWatch`.
            std::unordered_map<int, std::list<DirWatch>::iterator> fd_to_dir;
            /// @brief Maps a per-file vnode fd to the file's absolute path.
            std::unordered_map<int, std::filesystem::path> fd_to_file;
        };
#else
        /// @brief Per-watch state on Linux: the root `inotify` watch descriptor
        ///        plus the subdirectory descriptors created for recursion.
        struct WatchEntry
        {
            /// @brief Normalized absolute path of the watched file or directory.
            std::filesystem::path path;
            bool is_directory = false;
            bool recursive = false;
            /// @brief Directory that holds the actual watch (the parent of a
            ///        watched file, or the directory itself).
            std::filesystem::path watch_root;

            int root_wd = -1;
            /// @brief Maps an `inotify` watch descriptor to the directory it
            ///        watches (root and, when recursive, each subdirectory).
            std::unordered_map<int, std::filesystem::path> wd_to_path;
        };
#endif

        /// @brief Resolves @p p to a normalized absolute path. No filesystem
        ///        access beyond resolving against the current working directory.
        [[nodiscard]] auto make_absolute(const std::filesystem::path &p)
            -> pjh::result::Result<std::filesystem::path, ErrorCode>;

#if !PJH_PLATFORM_WINDOWS
        [[nodiscard]] auto map_errno_to_error(int err) -> ErrorCode;
#else
        [[nodiscard]] auto map_windows_error(unsigned long err) -> ErrorCode;
#endif

        /// @brief Whether @p child is @p parent or lies below it (lexical).
        [[nodiscard]] auto path_is_under(
            const std::filesystem::path &child, const std::filesystem::path &parent) -> bool;

#if PJH_PLATFORM_LINUX
        /// @brief Bitmask of the `inotify` events that map to `FileEventKind`.
        [[nodiscard]] auto watch_mask() -> std::uint32_t;
#endif

#if PJH_PLATFORM_MACOS
        /// @brief Registers a kqueue vnode watch on @p dir and records its
        ///        initial snapshot, including per-file watches. Failure leaves
        ///        no resources behind.
        [[nodiscard]] auto open_directory_watch(
            FileWatcherImpl &impl, WatchEntry &entry, const std::filesystem::path &dir)
            -> pjh::result::Result<void, ErrorCode>;
        /// @brief Registers a kqueue vnode watch on the file @p file_path,
        ///        which must live inside the directory watched by @p dw.
        ///        Returns the fd, or -1 on failure.
        auto open_file_watch(
            FileWatcherImpl &impl, WatchEntry &entry, WatchEntry::DirWatch &dw,
            const std::filesystem::path &file_path) -> int;
        /// @brief Closes the watch on entry @p name inside @p dw.
        auto close_file_watch(WatchEntry &entry, WatchEntry::DirWatch &dw,
            const std::filesystem::path &name) -> void;
        [[nodiscard]] auto is_path_watched(
            const WatchEntry &entry, const std::filesystem::path &dir) -> bool;
        auto close_directory_watch(WatchEntry &entry, const std::filesystem::path &dir) -> void;
        auto build_snapshot(
            const std::filesystem::path &dir,
            std::map<std::filesystem::path, FileStamp> &snapshot) -> void;
#endif

        /// @brief Platform-specific registration of one watch.
        [[nodiscard]] auto register_watch(FileWatcherImpl &impl, WatchEntry &entry)
            -> pjh::result::Result<void, ErrorCode>;

        /// @brief Platform-specific teardown of one watch.
        auto unregister_watch(FileWatcherImpl &impl, WatchEntry &entry) -> void;

        /// @brief Platform-specific blocking poll.
        [[nodiscard]] auto platform_poll(FileWatcherImpl &impl, std::chrono::milliseconds timeout)
            -> pjh::result::Result<std::vector<FileEvent>, ErrorCode>;

    }  // namespace detail

    /// @brief Internal state of a `FileWatcher`. Defined here so the function
    ///        split across the `src/file_watcher/` translation units stays
    ///        aligned per function rather than per platform.
    struct FileWatcherImpl
    {
#if PJH_PLATFORM_WINDOWS
        HANDLE port = nullptr;
#else
        int fd = -1;
#endif
        bool closed = false;
        std::vector<std::unique_ptr<detail::WatchEntry>> entries;
    };

}  // namespace pjh::platform

#endif  // INCLUDE_PJH_PLATFORM_FILE_WATCHER_INTERNAL_HPP
