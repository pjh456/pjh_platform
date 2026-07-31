#ifndef INCLUDE_PJH_PLATFORM_FS_HPP
#define INCLUDE_PJH_PLATFORM_FS_HPP

#include <filesystem>
#include <pjh_platform/error.hpp>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace pjh::platform
{

    /// @brief Cross-platform filesystem operations and path utilities.
    ///
    /// @details Static-only utility class. Operations that can fail return a
    ///          `pjh::result::Result` and never throw; a small number of
    ///          pure-query helpers delegate to the throwing overloads of
    ///          `std::filesystem` and may propagate `filesystem_error`.
    ///
    /// @platform Windows, Linux, macOS.
    class Fs
    {
        Fs() = delete;

    public:
        /**
         * @brief Returns the current working directory of the process.
         *
         * @return Absolute path to the process's current working directory.
         *
         * @exception std::filesystem::filesystem_error if the working directory
         *            cannot be determined.
         *
         * @sideeffect None.
         *
         * @platform All supported platforms. Thin wrapper over
         *           `std::filesystem::current_path()`.
         */
        [[nodiscard]] static auto current_path() -> std::filesystem::path;

        /**
         * @brief Creates every directory in @p p that does not already exist.
         *
         * @details Equivalent to `std::filesystem::create_directories` with the
         *          `error_code` overload. Succeeds (`Ok`) even when @p p already
         *          exists as a directory. Fails if a component of @p p is not a
         *          directory (for example an existing regular file in the
         *          middle of the path).
         *
         * @param p Directory path to create, including all missing parents.
         *
         * @return `Ok()` on success, whether or not the directory already
         *         existed; otherwise `Failure` with the mapped error
         *         (`NotFound`, `PermissionDenied`, `AlreadyExists`,
         *         `InvalidArgument`, `NotSupported`, `IoError` or `Unknown`).
         *
         * @exception Never throws.
         *
         * @sideeffect Creates directories on the filesystem.
         *
         * @platform All supported platforms.
         */
        static auto create_directories(const std::filesystem::path &p)
            -> pjh::result::Result<void, ErrorCode>;

        /**
         * @brief Recursively removes the file or directory at @p p.
         *
         * @details On Windows, recursively clears `FILE_ATTRIBUTE_READONLY`
         *          first, because `std::filesystem::remove_all` cannot delete
         *          read-only files (common inside `.git` directories). A
         *          non-existent path is not an error: it returns `Ok(0)`.
         *
         * @param p Path to remove.
         *
         * @return `Ok(count)` where @p count is the number of files and
         *         directories removed (0 when @p p did not exist); otherwise
         *         `Failure` with the mapped error.
         *
         * @exception Never throws.
         *
         * @sideeffect Permanently deletes @p p and everything below it.
         *
         * @platform All supported platforms; Windows additionally handles
         *           read-only attributes.
         */
        static auto remove_all(const std::filesystem::path &p)
            -> pjh::result::Result<std::uintmax_t, ErrorCode>;

        /**
         * @brief Checks whether @p p exists.
         *
         * @details True for regular files, directories, symlinks (following
         *          their target), special files, and any other object whose
         *          status can be obtained. Uses the throwing overload of
         *          `std::filesystem::exists`.
         *
         * @param p Path to test.
         *
         * @return `true` if @p p exists, `false` otherwise.
         *
         * @exception std::filesystem::filesystem_error if the file status cannot
         *            be determined (for example permission denied on a parent
         *            directory).
         *
         * @sideeffect None.
         *
         * @platform All supported platforms.
         */
        [[nodiscard]] static auto exists(const std::filesystem::path &p) -> bool;

        /**
         * @brief Checks whether @p p is a regular file.
         *
         * @details Returns `false` for directories, symlinks, and special files.
         *
         * @param p Path to test.
         *
         * @return `true` if @p p names a regular file, `false` otherwise
         *         (including when @p p does not exist).
         *
         * @exception std::filesystem::filesystem_error if the file status cannot
         *            be determined.
         *
         * @sideeffect None.
         *
         * @platform All supported platforms.
         */
        [[nodiscard]] static auto is_regular_file(const std::filesystem::path &p) -> bool;

        /**
         * @brief Checks whether @p p is a directory.
         *
         * @param p Path to test.
         *
         * @return `true` if @p p names a directory, `false` otherwise
         *         (including when @p p does not exist).
         *
         * @exception std::filesystem::filesystem_error if the file status cannot
         *            be determined.
         *
         * @sideeffect None.
         *
         * @platform All supported platforms.
         */
        [[nodiscard]] static auto is_directory(const std::filesystem::path &p) -> bool;

        /**
         * @brief Returns the size in bytes of the regular file @p p.
         *
         * @param p Path to the file.
         *
         * @return `Ok(size)` on success, or `Failure(NotFound)` when @p p does
         *         not exist, `Failure(PermissionDenied)` on access errors, and
         *         other mapped errors otherwise.
         *
         * @exception Never throws.
         *
         * @sideeffect None.
         *
         * @platform All supported platforms.
         */
        [[nodiscard]] static auto file_size(const std::filesystem::path &p)
            -> pjh::result::Result<std::uintmax_t, ErrorCode>;

        /**
         * @brief Reads the entire contents of @p p into a `std::string`.
         *
         * @details Windows: `CreateFileW` plus a memory-mapped file. POSIX:
         *          `open` + `fstat` + `mmap`. Empty files yield `Ok("")`. The
         *          returned string is a copy and remains valid after the mapping
         *          is released. On Windows the file is opened with
         *          `FILE_SHARE_READ` so concurrent readers are permitted.
         *
         * @param p Path to read.
         *
         * @return `Ok(contents)` on success; `Failure(NotFound)` if @p p does
         *         not exist, `Failure(PermissionDenied)` on access errors, or
         *         `Failure(IoError)` on any other failure.
         *
         * @exception Never throws platform errors; may throw `std::bad_alloc`
         *            if allocation for the full file fails.
         *
         * @sideeffect None; the file is opened read-only.
         *
         * @platform All supported platforms, with native
         *           `mmap`/`CreateFileMapping` implementations.
         */
        [[nodiscard]] static auto read_file(const std::filesystem::path &p)
            -> pjh::result::Result<std::string, ErrorCode>;

        /**
         * @brief Writes @p content to @p p, overwriting any existing contents.
         *
         * @details Creates @p p if it does not exist and truncates it if it
         *          does; the parent directory must already exist. On POSIX the
         *          file is created with mode `0644` (owner read/write, group
         *          and others read). Writes are looped to handle partial writes.
         *
         * @param p Destination path.
         * @param content Bytes to write.
         *
         * @return `Ok()` on success; `Failure(IoError)` if the file cannot be
         *         opened or a write fails.
         *
         * @exception Never throws.
         *
         * @sideeffect Creates or truncates @p p and updates its modification
         *            time. Existing contents are lost.
         *
         * @platform All supported platforms, with native
         *           `WriteFile`/`write` implementations.
         */
        static auto write_file(const std::filesystem::path &p, std::string_view content)
            -> pjh::result::Result<void, ErrorCode>;

        /**
         * @brief Returns the immediate entries of directory @p p (non-recursive).
         *
         * @details Uses `std::filesystem::directory_iterator` with an
         *          `error_code`. The order of the entries is unspecified.
         *
         * @param p Directory to list.
         *
         * @return `Ok(entries)` with one path per entry; `Failure(NotFound)` if
         *         @p p does not exist, `Failure(PermissionDenied)` on access
         *         errors, or other mapped errors otherwise.
         *
         * @exception Never throws.
         *
         * @sideeffect None.
         *
         * @platform All supported platforms.
         */
        [[nodiscard]] static auto list_directory(const std::filesystem::path &p)
            -> pjh::result::Result<std::vector<std::filesystem::path>, ErrorCode>;

        /**
         * @brief Returns the system temporary directory path.
         *
         * @details Thin wrapper over `std::filesystem::temp_directory_path()`
         *          (for example `/tmp` on Linux, `$TMPDIR` on macOS,
         *          `%TEMP%` on Windows).
         *
         * @return Path to the system temporary directory.
         *
         * @exception std::filesystem::filesystem_error if no temporary
         *            directory can be determined.
         *
         * @sideeffect None.
         *
         * @platform All supported platforms.
         */
        [[nodiscard]] static auto temp_directory() -> std::filesystem::path;

        /**
         * @brief Returns the current user's home directory.
         *
         * @details Reads the `HOME` environment variable; on Windows falls back
         *          to `USERPROFILE` when `HOME` is not set.
         *
         * @return `Ok(home)` on success; `Failure(NotFound)` when neither
         *         variable is set.
         *
         * @exception Never throws.
         *
         * @sideeffect None.
         *
         * @platform All supported platforms. `HOME` on POSIX; `HOME` then
         *           `USERPROFILE` on Windows.
         */
        [[nodiscard]] static auto home_directory()
            -> pjh::result::Result<std::filesystem::path, ErrorCode>;

        // ── Path utilities ─────────────────────────────────────────────

        /**
         * @brief Lexically normalizes @p p without touching the filesystem.
         *
         * @details Collapses `"."` elements, duplicate separators, and resolves
         *          `".."` where possible. `".."` cannot climb above the root of
         *          an absolute path. A trailing separator is preserved. Works on
         *          paths that do not exist yet. Equivalent to
         *          `std::filesystem::path::lexically_normal()`.
         *
         * @param p Path to normalize.
         *
         * @return Normalized copy of @p p.
         *
         * @exception Never throws; no filesystem access.
         *
         * @sideeffect None.
         *
         * @platform All supported platforms; uses native separator semantics.
         */
        [[nodiscard]] static auto normalize(const std::filesystem::path &p)
            -> std::filesystem::path;

        /**
         * @brief Joins @p base with one or more path parts.
         *
         * @details Each part is appended using the platform separator via
         *          `operator/=`. If a part is an absolute path it replaces
         *          everything accumulated so far, matching `operator/`
         *          semantics. Parts may be `std::filesystem::path`,
         *          `std::string`, `std::string_view`, or `const char*`.
         *
         * @param base Base path.
         * @param parts One or more components to append.
         *
         * @return The joined path.
         *
         * @exception No filesystem access; may throw `std::bad_alloc` on
         *            allocation failure.
         *
         * @sideeffect None.
         *
         * @platform All supported platforms.
         */
        template <typename... Parts>
        [[nodiscard]] static auto join(
            const std::filesystem::path &base, Parts &&...parts) -> std::filesystem::path
        {
            std::filesystem::path result = base;
            ((result /= std::filesystem::path(std::forward<Parts>(parts))), ...);
            return result;
        }

        /**
         * @brief Returns the file extension of @p p including the leading dot.
         *
         * @details Returns an empty string when @p p has no extension. A
         *          trailing-dot name such as `"file."` yields `"."`; hidden
         *          files such as `".bashrc"` have no extension. For a
         *          directory-only path such as `"dir/"` the extension is empty.
         *          The result is UTF-8 encoded on every platform.
         *
         * @param p Path to inspect.
         *
         * @return Extension string (for example `".txt"`), or empty when none.
         *
         * @exception None; pure lexical operation, no filesystem access.
         *
         * @sideeffect None.
         *
         * @platform All supported platforms; UTF-8 regardless of the native
         *           wide encoding.
         */
        [[nodiscard]] static auto extension(const std::filesystem::path &p)
            -> std::string;

        /**
         * @brief Returns the filename of @p p without its extension.
         *
         * @details For `"a/b/c.txt"` returns `"c"`; for `"archive.tar.gz"`
         *          returns `"archive.tar"`; for `"noext"` returns `"noext"`;
         *          for `"dir/"` (a directory path) the stem is empty. The
         *          result is UTF-8 encoded on every platform.
         *
         * @param p Path to inspect.
         *
         * @return Filename stem, or empty when @p p is a directory path with a
         *         trailing separator.
         *
         * @exception None; pure lexical operation, no filesystem access.
         *
         * @sideeffect None.
         *
         * @platform All supported platforms; UTF-8 regardless of the native
         *           wide encoding.
         */
        [[nodiscard]] static auto stem(const std::filesystem::path &p) -> std::string;

        /**
         * @brief Computes the path of @p target relative to @p base.
         *
         * @details Purely lexical: both inputs are normalized first, so
         *          non-existent and unnormalized paths work. The result is
         *          `"."` when @p base and @p target are equal. Fails when the
         *          two paths share no common root (different drive letters on
         *          Windows, or a relative vs. absolute mix).
         *
         * @param base Base directory.
         * @param target Path to express relative to @p base.
         *
         * @return `Ok(relative_path)` on success; `Failure(InvalidArgument)`
         *         when no common root exists.
         *
         * @exception Never throws; no filesystem access.
         *
         * @sideeffect None.
         *
         * @platform All supported platforms.
         */
        [[nodiscard]] static auto relative(
            const std::filesystem::path &base, const std::filesystem::path &target)
            -> pjh::result::Result<std::filesystem::path, ErrorCode>;
    };

}  // namespace pjh::platform

#endif  // INCLUDE_PJH_PLATFORM_FS_HPP
