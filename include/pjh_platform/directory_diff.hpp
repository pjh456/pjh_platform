#ifndef INCLUDE_PJH_PLATFORM_DIRECTORY_DIFF_HPP
#define INCLUDE_PJH_PLATFORM_DIRECTORY_DIFF_HPP

#include <filesystem>
#include <pjh_platform/directory_snapshot.hpp>
#include <pjh_platform/error.hpp>
#include <utility>
#include <vector>

namespace pjh::platform
{

    /**
     * @brief Result of comparing two snapshots of the same directory.
     *
     * @details Produced by `DirectoryDiff::compare` from two
     *          `DirectorySnapshot` captures taken at different points in time.
     *          Reports which entries appeared (`Created`), disappeared
     *          (`Deleted`), and which regular files changed (`Modified`).
     *          Directories are never reported as `Modified`; a directory's
     *          changes surface as `Created`/`Deleted` of the affected children.
     *
     * @platform Windows, Linux, macOS.
     */
    class DirectoryDiff
    {
    public:
        /// @brief Kind of change an entry underwent between two snapshots.
        enum class ChangeKind
        {
            /// @brief The entry exists in the later snapshot but not the earlier.
            Created,

            /// @brief The entry exists in the earlier snapshot but not the later.
            Deleted,

            /// @brief A regular file present in both snapshots whose content
            ///        (hash) or size/mtime metadata differs.
            Modified,
        };

        /// @brief A single directory-entry change between two snapshots.
        struct Change
        {
            /// @brief Kind of change.
            ChangeKind m_kind = ChangeKind::Created;

            /// @brief Basename of the affected entry.
            std::filesystem::path m_filename;

            /// @brief Absolute path of the affected entry (directory + filename).
            std::filesystem::path m_full_path;
        };

        /// @brief A file identified as having been renamed within the directory.
        struct Rename
        {
            /// @brief Basename before the rename.
            std::filesystem::path m_old_filename;

            /// @brief Basename after the rename.
            std::filesystem::path m_new_filename;
        };

        /**
         * @brief Diffs @p before against @p after.
         *
         * @param before Earlier snapshot.
         * @param after  Later snapshot.
         *
         * @return `Ok(diff)` on success; `Failure(InvalidArgument)` if the two
         *         snapshots capture different directories.
         *
         * @exception Never throws.
         *
         * @platform All supported platforms.
         */
        [[nodiscard]] static auto compare(
            const DirectorySnapshot &before, const DirectorySnapshot &after)
            -> pjh::result::Result<DirectoryDiff, ErrorCode>;

        /// @brief The list of detected changes.
        [[nodiscard]] auto changes() const -> const std::vector<Change> &;

        /// @brief Whether no changes were detected.
        [[nodiscard]] auto empty() const -> bool;

        /**
         * @brief Pairs up `Created`/`Deleted` file changes that are likely
         *        renames.
         *
         * @details When both sides of a pair carry a content hash, the hash must
         *          match. When hashes are unavailable, the fallback heuristic is
         *          equality of size and last-write time. Directories are never
         *          considered; a directory rename is reported as an unrelated
         *          `Created`/`Deleted` pair at this layer.
         *
         * @param before The earlier snapshot passed to `compare`.
         * @param after  The later snapshot passed to `compare`.
         *
         * @return Detected rename pairs, possibly empty. The underlying
         *         `Created`/`Deleted` changes are left untouched.
         *
         * @exception Never throws.
         *
         * @platform All supported platforms.
         */
        [[nodiscard]] auto detect_renames(
            const DirectorySnapshot &before, const DirectorySnapshot &after) const
            -> std::vector<Rename>;

    private:
        explicit DirectoryDiff(std::vector<Change> changes);

        std::vector<Change> m_changes;
    };

}  // namespace pjh::platform

#endif  // INCLUDE_PJH_PLATFORM_DIRECTORY_DIFF_HPP
