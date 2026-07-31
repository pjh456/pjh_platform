#ifndef INCLUDE_PJH_PLATFORM_DIRECTORY_SNAPSHOT_HPP
#define INCLUDE_PJH_PLATFORM_DIRECTORY_SNAPSHOT_HPP

#include <concepts>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <pjh_platform/error.hpp>
#include <vector>

namespace pjh::platform
{

    /// @brief Content hash of a file.
    using FileHash = std::uint64_t;

    /**
     * @brief Hash strategy concept: a callable that hashes a file's contents.
     *
     * @details A conforming type must be callable as `std::optional<FileHash>(path)`
     *          and return `std::nullopt` when the file cannot be read.
     *
     * @platform All supported platforms.
     */
    template <typename Hasher>
    concept FileHasher = requires(Hasher hasher, const std::filesystem::path &path) {
        { hasher(path) } -> std::convertible_to<std::optional<FileHash>>;
    };

    /// @brief Default content hasher: FNV-1a 64-bit over the file's bytes.
    struct Fnv1a64Hasher
    {
        /**
         * @brief Computes the FNV-1a 64-bit hash of @p path.
         *
         * @return `Ok(hash)` on success; `std::nullopt` if the file cannot be
         *         opened or read.
         */
        [[nodiscard]] auto operator()(const std::filesystem::path &path) const
            -> std::optional<FileHash>;
    };

    /**
     * @brief Point-in-time capture of a directory's entries.
     *
     * @details Represents the immediate children of a single directory: one
     *          `Entry` per child, keyed by its filename. Content hashing is
     *          optional and pluggable through the `FileHasher` strategy; when
     *          enabled it only applies to regular files whose filename appears
     *          in the caller-supplied list (or to all files when no list is
     *          given), mirroring the file-matching behaviour of the file
     *          watcher.
     *
     * @platform Windows, Linux, macOS.
     */
    class DirectorySnapshot
    {
    public:
        /// @brief Metadata for a single directory entry.
        struct Entry
        {
            /// @brief Whether the entry is a directory.
            bool m_is_directory = false;

            /// @brief Size of a regular file in bytes (0 for directories).
            std::uintmax_t m_file_size = 0;

            /// @brief Last write time in nanoseconds since the file-clock epoch.
            std::intmax_t m_mtime_ns = 0;

            /// @brief Content hash when hashing was requested and succeeded;
            ///        `std::nullopt` otherwise.
            std::optional<FileHash> m_hash;
        };

        /// @brief Maps a child filename to its `Entry`.
        using EntryMap = std::map<std::filesystem::path, Entry>;

        /**
         * @brief Captures @p dir without computing any content hashes.
         *
         * @param dir Directory to capture.
         *
         * @return `Ok(snapshot)` on success; `Failure(NotFound)` if @p dir does
         *         not exist, `Failure(InvalidArgument)` if @p dir is not a
         *         directory, `Failure(PermissionDenied)` on access errors, or
         *         other mapped errors.
         *
         * @exception Never throws.
         *
         * @platform All supported platforms.
         */
        [[nodiscard]] static auto capture(const std::filesystem::path &dir)
            -> pjh::result::Result<DirectorySnapshot, ErrorCode>;

        /**
         * @brief Captures @p dir and hashes files with the default
         *        `Fnv1a64Hasher`.
         *
         * @param dir Directory to capture.
         * @param hash_files When non-null, only files whose filename appears in
         *                   this list are hashed. When null, every file is
         *                   hashed. Directories are never hashed.
         *
         * @return `Ok(snapshot)` on success; `Failure(NotFound)` if @p dir does
         *         not exist, `Failure(InvalidArgument)` if @p dir is not a
         *         directory, `Failure(PermissionDenied)` on access errors, or
         *         other mapped errors.
         *
         * @exception Never throws.
         *
         * @platform All supported platforms.
         */
        [[nodiscard]] static auto capture(
            const std::filesystem::path &dir,
            const std::vector<std::filesystem::path> *hash_files)
            -> pjh::result::Result<DirectorySnapshot, ErrorCode>;

        /**
         * @brief Captures @p dir and hashes files with the given strategy.
         *
         * @param dir Directory to capture.
         * @param hash_files When non-null, only files whose filename appears in
         *                   this list are hashed. When null, every file is
         *                   hashed. Directories are never hashed.
         * @param hasher FileHasher strategy used to hash file contents.
         *
         * @return `Ok(snapshot)` on success; `Failure(NotFound)` if @p dir does
         *         not exist, `Failure(InvalidArgument)` if @p dir is not a
         *         directory, `Failure(PermissionDenied)` on access errors, or
         *         other mapped errors.
         *
         * @exception Never throws.
         *
         * @platform All supported platforms.
         */
        template <FileHasher Hasher>
        [[nodiscard]] static auto capture(
            const std::filesystem::path &dir,
            const std::vector<std::filesystem::path> *hash_files,
            Hasher hasher) -> pjh::result::Result<DirectorySnapshot, ErrorCode>
        {
            HashFn fn = [hasher](const std::filesystem::path &p) { return hasher(p); };
            return capture_impl(dir, hash_files, std::optional<HashFn>{std::move(fn)});
        }

        /// @brief Returns the normalized absolute path of the captured directory.
        [[nodiscard]] auto dir_path() const -> const std::filesystem::path &;

        /// @brief Returns the number of regular-file entries.
        [[nodiscard]] auto file_count() const -> std::size_t;

        /// @brief Returns the number of directory entries.
        [[nodiscard]] auto dir_count() const -> std::size_t;

        /// @brief Returns the entry for @p filename (a basename), or `nullopt`.
        [[nodiscard]] auto get(const std::filesystem::path &filename) const
            -> std::optional<Entry>;

        /// @brief Whether @p filename (a basename) is present in this snapshot.
        [[nodiscard]] auto contains(const std::filesystem::path &filename) const -> bool;

        /// @brief Returns all child filenames (basenames) in the snapshot.
        [[nodiscard]] auto filenames() const -> std::vector<std::filesystem::path>;

        /// @brief Returns the underlying entry map keyed by child filename.
        [[nodiscard]] auto entries() const -> const EntryMap &;

    private:
        using HashFn =
            std::function<std::optional<FileHash>(const std::filesystem::path &)>;

        [[nodiscard]] static auto capture_impl(
            const std::filesystem::path &dir,
            const std::vector<std::filesystem::path> *hash_files,
            std::optional<HashFn> hasher) -> pjh::result::Result<DirectorySnapshot, ErrorCode>;

        std::filesystem::path m_dir_path;
        EntryMap m_entries;
    };

}  // namespace pjh::platform

#endif  // INCLUDE_PJH_PLATFORM_DIRECTORY_SNAPSHOT_HPP
