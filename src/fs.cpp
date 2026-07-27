#include <pjh_platform/env.hpp>
#include <pjh_platform/error.hpp>
#include <pjh_platform/fs.hpp>
#include <pjh_platform/platform.hpp>

#include <fstream>
#include <iterator>

namespace pjh::platform
{

    auto Fs::current_path() -> std::filesystem::path
    {
        return std::filesystem::current_path();
    }

    auto Fs::create_directories(const std::filesystem::path& p)
        -> pjh::result::Result<void, ErrorCode>
    {
        std::error_code ec;
        std::filesystem::create_directories(p, ec);
        if (ec)
            return pjh::result::Failure<ErrorCode>{ErrorCode::io_error};
        return pjh::result::Result<void, ErrorCode>::Ok();
    }

    auto Fs::remove_all(const std::filesystem::path& p)
        -> pjh::result::Result<std::uintmax_t, ErrorCode>
    {
        std::error_code ec;
        auto count = std::filesystem::remove_all(p, ec);
        if (ec)
            return pjh::result::Failure<ErrorCode>{ErrorCode::io_error};
        return pjh::result::Result<std::uintmax_t, ErrorCode>::Ok(count);
    }

    auto Fs::exists(const std::filesystem::path& p) -> bool
    {
        return std::filesystem::exists(p);
    }

    auto Fs::is_regular_file(const std::filesystem::path& p) -> bool
    {
        return std::filesystem::is_regular_file(p);
    }

    auto Fs::is_directory(const std::filesystem::path& p) -> bool
    {
        return std::filesystem::is_directory(p);
    }

    auto Fs::file_size(const std::filesystem::path& p)
        -> pjh::result::Result<std::uintmax_t, ErrorCode>
    {
        std::error_code ec;
        auto sz = std::filesystem::file_size(p, ec);
        if (ec)
            return pjh::result::Failure<ErrorCode>{ErrorCode::not_found};
        return pjh::result::Result<std::uintmax_t, ErrorCode>::Ok(sz);
    }

    auto Fs::read_file(const std::filesystem::path& p)
        -> pjh::result::Result<std::string, ErrorCode>
    {
        std::ifstream file(p, std::ios::binary | std::ios::ate);
        if (!file)
            return pjh::result::Failure<ErrorCode>{ErrorCode::not_found};
        auto size = file.tellg();
        file.seekg(0);
        std::string content(static_cast<std::size_t>(size), '\0');
        file.read(content.data(), size);
        return pjh::result::Result<std::string, ErrorCode>::Ok(content);
    }

    auto Fs::write_file(
        const std::filesystem::path& p, std::string_view content)
        -> pjh::result::Result<void, ErrorCode>
    {
        std::ofstream file(p, std::ios::binary);
        if (!file)
            return pjh::result::Failure<ErrorCode>{ErrorCode::io_error};
        file.write(content.data(), static_cast<std::streamsize>(content.size()));
        if (!file)
            return pjh::result::Failure<ErrorCode>{ErrorCode::io_error};
        return pjh::result::Result<void, ErrorCode>::Ok();
    }

    auto Fs::list_directory(const std::filesystem::path& p)
        -> pjh::result::Result<std::vector<std::filesystem::path>, ErrorCode>
    {
        std::error_code ec;
        std::vector<std::filesystem::path> entries;
        for (const auto& entry : std::filesystem::directory_iterator(p, ec))
            entries.push_back(entry.path());
        if (ec)
            return pjh::result::Failure<ErrorCode>{ErrorCode::not_found};
        return pjh::result::Result<std::vector<std::filesystem::path>, ErrorCode>::Ok(std::move(entries));
    }

    auto Fs::temp_directory() -> std::filesystem::path
    {
        return std::filesystem::temp_directory_path();
    }

    auto Fs::home_directory()
        -> pjh::result::Result<std::filesystem::path, ErrorCode>
    {
        auto home = Env::get("HOME");
        if (home.is_ok())
            return pjh::result::Result<std::filesystem::path, ErrorCode>::Ok(std::filesystem::path(home.unwrap()));
#if PJH_PLATFORM_WINDOWS
        auto userprofile = Env::get("USERPROFILE");
        if (userprofile.is_ok())
            return pjh::result::Result<std::filesystem::path, ErrorCode>::Ok(
                std::filesystem::path(userprofile.unwrap()));
#endif
        return pjh::result::Failure<ErrorCode>{ErrorCode::not_found};
    }

} // namespace pjh::platform
