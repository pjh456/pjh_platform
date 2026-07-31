#ifndef INCLUDE_PJH_PLATFORM_FILE_WATCHER_INTERNAL_HPP
#define INCLUDE_PJH_PLATFORM_FILE_WATCHER_INTERNAL_HPP

#include <pjh_platform/directory_snapshot.hpp>
#include <pjh_platform/file_watcher.hpp>
#include <pjh_platform/platform.hpp>

#if PJH_PLATFORM_WINDOWS
#include <windows.h>
#elif PJH_PLATFORM_MACOS
#include <CoreServices/CoreServices.h>
#else
#include <sys/inotify.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <cstdint>
#include <filesystem>
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
        /// @brief Per-watch state on macOS: an `FSEvents` stream watching the
        ///        watch root, plus one snapshot per directory under it used to
        ///        infer individual changes.
        struct WatchEntry
        {
            /// @brief A raw FSEvents delivery awaiting processing by `poll()`.
            struct PendingEvent
            {
                std::filesystem::path path;
                FSEventStreamEventFlags flags = 0;
            };

            /// @brief Normalized absolute path of the watched file or directory.
            std::filesystem::path path;
            bool is_directory = false;
            bool recursive = false;
            /// @brief Directory that holds the actual watch (the parent of a
            ///        watched file, or the directory itself).
            std::filesystem::path watch_root;

            /// @brief FSEvents stream watching `watch_root`.
            FSEventStreamRef stream = nullptr;
            /// @brief Baseline snapshot per directory under `watch_root`.
            ///        A missing entry means the directory has been seen for the
            ///        first time since the baseline was taken.
            std::map<std::filesystem::path, DirectorySnapshot> snapshots;
            /// @brief Events delivered by the FSEvents callback; consumed by
            ///        the next `poll()`.
            std::vector<PendingEvent> pending_events;
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
#ifdef kFSEventStreamEventFlagItemIsDir
        /// @brief Bit indicating a reported FSEvents path is a directory. 0 on
        ///        SDKs that predate `kFSEventStreamCreateFlagFileEvents`.
        inline constexpr auto kFseventsItemIsDir = kFSEventStreamEventFlagItemIsDir;
#else
        inline constexpr auto kFseventsItemIsDir = FSEventStreamEventFlags{0};
#endif

        /// @brief FSEvents callback entry point; appends raw events to the
        ///        entry's pending list.
        auto fsevents_callback(
            ConstFSEventStreamRef stream, void *info, size_t num_events,
            void *event_paths, const FSEventStreamEventFlags event_flags[],
            const FSEventStreamEventId event_ids[]) -> void;
        /// @brief Creates, schedules and starts the FSEvents stream watching
        ///        `entry.watch_root`. Failure leaves `entry.stream` null.
        auto create_fsevents_stream(FileWatcherImpl &impl, WatchEntry &entry) -> void;
        /// @brief Turns the entry's pending FSEvents deliveries into
        ///        `FileEvent`s using directory snapshot diffs.
        auto process_fsevents(WatchEntry &entry, std::vector<FileEvent> &out) -> void;
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
#elif PJH_PLATFORM_LINUX
        int fd = -1;
#elif PJH_PLATFORM_MACOS
        /// @brief Run loop the FSEvents streams are scheduled on. Captured at
        ///        construction, so all calls must stay on the same thread.
        CFRunLoopRef run_loop = nullptr;
#endif
        bool closed = false;
        std::vector<std::unique_ptr<detail::WatchEntry>> entries;
    };

}  // namespace pjh::platform

#endif  // INCLUDE_PJH_PLATFORM_FILE_WATCHER_INTERNAL_HPP
