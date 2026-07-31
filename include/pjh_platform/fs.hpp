#ifndef INCLUDE_PJH_PLATFORM_FS_HPP
#define INCLUDE_PJH_PLATFORM_FS_HPP

#include <pjh_platform/error.hpp>

#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace pjh::platform
{

    class Fs
    {
        Fs() = delete;

    public:
        [[nodiscard]] static auto current_path() -> std::filesystem::path;

        static auto create_directories(const std::filesystem::path& p)
            -> pjh::result::Result<void, ErrorCode>;

        static auto remove_all(const std::filesystem::path& p)
            -> pjh::result::Result<std::uintmax_t, ErrorCode>;

        [[nodiscard]] static auto exists(const std::filesystem::path& p) -> bool;

        [[nodiscard]] static auto is_regular_file(const std::filesystem::path& p) -> bool;

        [[nodiscard]] static auto is_directory(const std::filesystem::path& p) -> bool;

        [[nodiscard]] static auto file_size(const std::filesystem::path& p)
            -> pjh::result::Result<std::uintmax_t, ErrorCode>;

        [[nodiscard]] static auto read_file(const std::filesystem::path& p)
            -> pjh::result::Result<std::string, ErrorCode>;

        static auto write_file(
            const std::filesystem::path& p, std::string_view content)
            -> pjh::result::Result<void, ErrorCode>;

        [[nodiscard]] static auto list_directory(const std::filesystem::path& p)
            -> pjh::result::Result<std::vector<std::filesystem::path>, ErrorCode>;

        [[nodiscard]] static auto temp_directory() -> std::filesystem::path;

        [[nodiscard]] static auto home_directory()
            -> pjh::result::Result<std::filesystem::path, ErrorCode>;

        // ── Path utilities ─────────────────────────────────────────────

        /// Lexically normalizes `p`: collapses ".", "..", and duplicate
        /// separators without touching the filesystem. Works on paths that
        /// do not exist yet.
        [[nodiscard]] static auto normalize(const std::filesystem::path& p)
            -> std::filesystem::path;

        /// Joins `base` with any number of path parts using the platform
        /// separator. An absolute part replaces everything before it,
        /// matching `operator/` semantics.
        template <typename... Parts>
        [[nodiscard]] static auto join(
            const std::filesystem::path& base, Parts&&... parts)
            -> std::filesystem::path
        {
            std::filesystem::path result = base;
            ((result /= std::filesystem::path(std::forward<Parts>(parts))), ...);
            return result;
        }

        /// Returns the file extension including the leading dot, or an empty
        /// string when `p` has no extension. UTF-8 encoded.
        [[nodiscard]] static auto extension(const std::filesystem::path& p)
            -> std::string;

        /// Returns the file name without the extension. UTF-8 encoded.
        [[nodiscard]] static auto stem(const std::filesystem::path& p)
            -> std::string;

        /// Computes the path of `target` relative to `base` without touching
        /// the filesystem (works for non-existent paths). Fails with
        /// `InvalidArgument` when the two paths share no common root (e.g.
        /// different drive letters or a relative vs. absolute mix).
        [[nodiscard]] static auto relative(
            const std::filesystem::path& base,
            const std::filesystem::path& target)
            -> pjh::result::Result<std::filesystem::path, ErrorCode>;
    };

} // namespace pjh::platform

#endif // INCLUDE_PJH_PLATFORM_FS_HPP
