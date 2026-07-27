#ifndef INCLUDE_PJH_PLATFORM_FS_HPP
#define INCLUDE_PJH_PLATFORM_FS_HPP

#include <pjh_platform/error.hpp>

#include <filesystem>
#include <string>
#include <string_view>
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
    };

} // namespace pjh::platform

#endif // INCLUDE_PJH_PLATFORM_FS_HPP
