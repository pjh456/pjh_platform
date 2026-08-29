#ifndef INCLUDE_PJH_PLATFORM_DIRECTORY_STATUS_HPP
#define INCLUDE_PJH_PLATFORM_DIRECTORY_STATUS_HPP

#include <cstdint>
#include <filesystem>
#include <pjh_platform/directory_snapshot.hpp>
#include <vector>

namespace pjh::platform
{

    /**
     * @brief Statistical summary derived from a directory snapshot.
     *
     * @details Built from a `DirectorySnapshot` with `DirectoryStatus::from`
     *          and immutable afterwards. Aggregates totals over regular files
     *          only: directories contribute to `dir_count()` but never to
     *          `total_size()` or the extension summaries, mirroring how a
     *          directory's changes surface as child entries rather than as the
     *          directory itself.
     *
     * @platform Windows, Linux, macOS.
     */
    class DirectoryStatus
    {
    public:
        /// @brief Aggregate of all regular files carrying one extension.
        struct ExtensionSummary
        {
            /// @brief Extension including the leading dot (e.g. `.cpp`);
            ///        empty for files without an extension.
            std::filesystem::path m_extension;

            /// @brief Number of regular files with this extension.
            std::size_t m_file_count = 0;

            /// @brief Sum of their sizes in bytes.
            std::uintmax_t m_total_size = 0;
        };

        /// @brief A regular file ranked by size.
        struct SizeEntry
        {
            /// @brief Absolute path of the file.
            std::filesystem::path m_path;

            /// @brief Size in bytes.
            std::uintmax_t m_size = 0;
        };

        /**
         * @brief Builds the status summary of @p snapshot.
         *
         * @param snapshot Directory snapshot to summarize.
         *
         * @return The computed summary.
         *
         * @exception Never throws.
         *
         * @platform All supported platforms.
         */
        [[nodiscard]] static auto from(const DirectorySnapshot &snapshot) -> DirectoryStatus;

        /// @brief Total size of all regular files, in bytes.
        [[nodiscard]] auto total_size() const -> std::uintmax_t;

        /// @brief Number of regular-file entries.
        [[nodiscard]] auto file_count() const -> std::size_t;

        /// @brief Number of directory entries.
        [[nodiscard]] auto dir_count() const -> std::size_t;

        /// @brief One summary per distinct extension, ordered by extension name.
        [[nodiscard]] auto extension_summaries() const -> const std::vector<ExtensionSummary> &;

        /**
         * @brief The @p n largest regular files, largest first.
         *
         * @param n Maximum number of files to return; the whole list is
         *          returned when @p n exceeds the file count.
         *
         * @return Absolute paths of the largest regular files.
         *
         * @exception Never throws.
         *
         * @platform All supported platforms.
         */
        [[nodiscard]] auto largest_files(std::size_t n) const -> std::vector<std::filesystem::path>;

    private:
        DirectoryStatus(
            std::uintmax_t total_size,
            std::size_t file_count,
            std::size_t dir_count,
            std::vector<ExtensionSummary> extensions,
            std::vector<SizeEntry> files_with_sizes);

        std::uintmax_t m_total_size;
        std::size_t m_file_count;
        std::size_t m_dir_count;
        std::vector<ExtensionSummary> m_extensions;
        std::vector<SizeEntry> m_files_with_sizes;
    };

}  // namespace pjh::platform

#endif  // INCLUDE_PJH_PLATFORM_DIRECTORY_STATUS_HPP
