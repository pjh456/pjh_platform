#ifndef INCLUDE_PJH_PLATFORM_FILE_WATCHER_HPP
#define INCLUDE_PJH_PLATFORM_FILE_WATCHER_HPP

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <pjh_platform/error.hpp>
#include <vector>

namespace pjh::platform
{

    /// @brief Kind of filesystem change reported by `FileWatcher`.
    ///
    /// @platform Windows, Linux, macOS.
    enum class FileEventKind
    {
        /// @brief A file or directory was created.
        Created,

        /// @brief A file or directory was deleted.
        Deleted,

        /// @brief A file's contents were modified.
        Modified,

        /// @brief A file or directory was renamed/moved away from `path`.
        MovedFrom,

        /// @brief A file or directory was renamed/moved into `path`.
        MovedTo,
    };

    /// @brief A single filesystem change event.
    ///
    /// @details `path` is always an absolute path and can be used directly with
    ///          `Fs` operations. `cookie` is set on Linux for the two halves of
    ///          a rename: a `MovedFrom` and a `MovedTo` event that belong to the
    ///          same rename share the same `cookie`, so callers can pair them
    ///          up. On macOS and Windows rename halves are reported separately
    ///          and `cookie` is empty.
    ///
    /// @platform Windows, Linux, macOS.
    struct FileEvent
    {
        /// @brief Type of the change.
        FileEventKind kind;

        /// @brief Absolute path of the changed file or directory.
        std::filesystem::path path;

        /// @brief Rename pairing key (Linux only, empty elsewhere).
        std::optional<std::uint32_t> cookie;
    };

    struct FileWatcherImpl;

    /// @brief Cross-platform filesystem change monitoring.
    ///
    /// @details Watches files and directories and reports changes through
    ///          `poll()`. Under the hood:
    ///          - Linux uses `inotify`. A single file is watched directly on
    ///            the file itself: the watch follows the file's inode, so a
    ///            file renamed into the watched path is not reported, and a
    ///            deleted or renamed-away file leaves the watch inactive until
    ///            it is removed with `remove` and re-added. Sibling changes in
    ///            the parent directory generate no events at all, and each
    ///            file watch consumes one inotify watch slot.
    ///          - macOS uses `FSEvents` with file-level events, scheduled on the
    ///            current thread's run loop; individual changes are inferred by
    ///            diffing directory snapshots, so no file descriptors are held
    ///            for watched files.
    ///          - Windows uses `ReadDirectoryChangesW` over an I/O completion
    ///            port.
    ///
    ///          On Windows and macOS a single file is implemented by watching
    ///          its parent directory and discarding unrelated events. On
    ///          high-churn directories (for example `/tmp`) this filter work
    ///          grows with the directory's change rate, so prefer watching the
    ///          file's own dedicated directory when possible.
    ///
    /// @warning Not thread-safe; concurrent calls from multiple threads need
    ///          external synchronization.
    ///
    /// @platform Windows, Linux, macOS.
    class FileWatcher
    {
        FileWatcher(const FileWatcher &) = delete;
        FileWatcher &operator=(const FileWatcher &) = delete;

    public:
        /// @brief Constructs a watcher and opens the platform event source.
        ///
        /// @details If the platform event source cannot be created, the
        ///          watcher is left in a state where every `add`/`poll` returns
        ///          `Failure(Unknown)`.
        ///
        /// @exception Never throws.
        FileWatcher();

        /// @brief Releases all platform resources.
        ///
        /// @exception Never throws.
        ~FileWatcher();

        /// @brief Move constructor; transfers the underlying resources.
        ///
        /// @exception Never throws.
        FileWatcher(FileWatcher &&) noexcept;

        /// @brief Move assignment; releases the current resources and takes the
        ///        others.
        ///
        /// @details The moved-from watcher is left without resources: `add`/`poll`/
        ///          `remove` return `Failure(InvalidArgument)`; `close` is a no-op.
        ///
        /// @exception Never throws.
        FileWatcher &operator=(FileWatcher &&) noexcept;

        /**
         * @brief Starts watching @p path (a file or a directory).
         *
         * @details The path is normalized to an absolute path first. On Linux
         *          a file is watched directly on its own inode; on Windows
         *          and macOS a file is watched through its parent directory.
         *          Directories are watched directly. For directories,
         *          @p recursive controls whether subdirectories are watched
         *          too; the flag is ignored when @p path is a file.
         *          Subdirectories that cannot be opened (permission denied)
         *          are skipped: not watched, no events from inside them.
         *
         * @param path Path to watch.
         * @param recursive Watch subdirectories recursively (directories only).
         *
         * @return `Ok()` on success; `Failure(NotFound)` if @p path does not
         *         exist, `Failure(AlreadyWatched)` if @p path is already being
         *         watched, `Failure(PermissionDenied)` on access errors,
         *         `Failure(LimitReached)` if the platform watch limit is hit,
         *         or another mapped error.
         *
         * @exception Never throws.
         *
         * @platform All supported platforms.
         */
        [[nodiscard]] auto add(const std::filesystem::path &path, bool recursive = true)
            -> pjh::result::Result<void, ErrorCode>;

        /**
         * @brief Stops watching @p path.
         *
         * @param path Path previously passed to `add`.
         *
         * @return `Ok()` on success; `Failure(NotFound)` if @p path is not
         *         currently being watched.
         *
         * @exception Never throws.
         *
         * @platform All supported platforms.
         */
        [[nodiscard]] auto remove(const std::filesystem::path &path)
            -> pjh::result::Result<void, ErrorCode>;

        /**
         * @brief Blocks up to @p timeout waiting for filesystem changes.
         *
         * @details A timeout of zero performs a non-blocking check. Timing out
         *          is not an error: an empty vector is returned.
         *
         * @param timeout Maximum time to wait.
         *
         * @return `Ok(events)` on success (possibly empty on timeout);
         *         `Failure(Interrupted)` when the wait was interrupted by a
         *         signal, or another mapped error.
         *
         * @exception Never throws.
         *
         * @platform All supported platforms.
         */
        [[nodiscard]] auto poll(std::chrono::milliseconds timeout)
            -> pjh::result::Result<std::vector<FileEvent>, ErrorCode>;

        /**
         * @brief Releases all platform resources and stops watching.
         *
         * @details Called automatically by the destructor; safe to call more
         *          than once.
         *
         * @exception Never throws.
         *
         * @platform All supported platforms.
         */
        void close();

    private:
        std::unique_ptr<FileWatcherImpl> impl_;
    };

}  // namespace pjh::platform

#endif  // INCLUDE_PJH_PLATFORM_FILE_WATCHER_HPP
