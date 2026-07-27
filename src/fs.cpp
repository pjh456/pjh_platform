#include <pjh_platform/env.hpp>
#include <pjh_platform/error.hpp>
#include <pjh_platform/fs.hpp>
#include <pjh_platform/platform.hpp>

#include <cerrno>
#include <fstream>
#include <iterator>

namespace pjh::platform
{

    namespace {

        auto map_error_code(const std::error_code& ec) -> ErrorCode
        {
            if (!ec)
                return ErrorCode::Success;

            if (ec.category() == std::generic_category())
            {
                switch (ec.value())
                {
                case ENOENT:  return ErrorCode::NotFound;
                case EACCES:
                case EPERM:   return ErrorCode::PermissionDenied;
                case EEXIST:  return ErrorCode::AlreadyExists;
                case EINVAL:  return ErrorCode::InvalidArgument;
                case ENOTSUP: return ErrorCode::NotSupported;
                case EIO:     return ErrorCode::IoError;
                default:      return ErrorCode::Unknown;
                }
            }

#if defined(_WIN32)
            if (ec.category() == std::system_category())
            {
                switch (static_cast<unsigned long>(ec.value()))
                {
                case ERROR_FILE_NOT_FOUND:
                case ERROR_PATH_NOT_FOUND:  return ErrorCode::NotFound;
                case ERROR_ACCESS_DENIED:   return ErrorCode::PermissionDenied;
                case ERROR_ALREADY_EXISTS:  return ErrorCode::AlreadyExists;
                case ERROR_INVALID_PARAMETER: return ErrorCode::InvalidArgument;
                case ERROR_NOT_SUPPORTED:   return ErrorCode::NotSupported;
                default:                    return ErrorCode::Unknown;
                }
            }
#endif

            return ErrorCode::Unknown;
        }

    }

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
            return pjh::result::Failure<ErrorCode>{map_error_code(ec)};
        return pjh::result::Result<void, ErrorCode>::Ok();
    }

    auto Fs::remove_all(const std::filesystem::path& p)
        -> pjh::result::Result<std::uintmax_t, ErrorCode>
    {
        std::error_code ec;
        auto count = std::filesystem::remove_all(p, ec);
        if (ec)
            return pjh::result::Failure<ErrorCode>{map_error_code(ec)};
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
            return pjh::result::Failure<ErrorCode>{map_error_code(ec)};
        return pjh::result::Result<std::uintmax_t, ErrorCode>::Ok(sz);
    }

    auto Fs::read_file(const std::filesystem::path& p)
        -> pjh::result::Result<std::string, ErrorCode>
    {
        std::ifstream file(p, std::ios::binary | std::ios::ate);
        if (!file)
            return pjh::result::Failure<ErrorCode>{ErrorCode::NotFound};
        auto size = file.tellg();
        if (size == std::streampos(-1))
            return pjh::result::Failure<ErrorCode>{ErrorCode::IoError};
        file.seekg(0);
        if (!file)
            return pjh::result::Failure<ErrorCode>{ErrorCode::IoError};
        std::string content(static_cast<std::size_t>(size), '\0');
        file.read(content.data(), static_cast<std::streamsize>(size));
        if (!file)
            return pjh::result::Failure<ErrorCode>{ErrorCode::IoError};
        return pjh::result::Result<std::string, ErrorCode>::Ok(std::move(content));
    }

    auto Fs::write_file(
        const std::filesystem::path& p, std::string_view content)
        -> pjh::result::Result<void, ErrorCode>
    {
        std::ofstream file(p, std::ios::binary);
        if (!file)
            return pjh::result::Failure<ErrorCode>{ErrorCode::IoError};
        file.write(content.data(), static_cast<std::streamsize>(content.size()));
        if (!file)
            return pjh::result::Failure<ErrorCode>{ErrorCode::IoError};
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
            return pjh::result::Failure<ErrorCode>{map_error_code(ec)};
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
        return pjh::result::Failure<ErrorCode>{ErrorCode::NotFound};
    }

} // namespace pjh::platform
