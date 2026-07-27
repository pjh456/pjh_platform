#include <pjh_platform/env.hpp>
#include <pjh_platform/error.hpp>
#include <pjh_platform/fs.hpp>
#include <pjh_platform/platform.hpp>

#include <cerrno>

#if PJH_PLATFORM_WINDOWS
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

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
#ifdef ENOTSUP
                case ENOTSUP: return ErrorCode::NotSupported;
#endif
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
        {
            auto mapped = map_error_code(ec);
            if (mapped == ErrorCode::NotFound)
                return pjh::result::Result<std::uintmax_t, ErrorCode>::Ok(0);
            return pjh::result::Failure<ErrorCode>{mapped};
        }
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
#if PJH_PLATFORM_WINDOWS
        HANDLE hFile = CreateFileW(
            p.c_str(),
            GENERIC_READ,
            FILE_SHARE_READ,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
        if (hFile == INVALID_HANDLE_VALUE)
        {
            DWORD err = GetLastError();
            if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND)
                return pjh::result::Failure<ErrorCode>{ErrorCode::NotFound};
            if (err == ERROR_ACCESS_DENIED)
                return pjh::result::Failure<ErrorCode>{ErrorCode::PermissionDenied};
            return pjh::result::Failure<ErrorCode>{ErrorCode::IoError};
        }

        LARGE_INTEGER fileSize;
        if (!GetFileSizeEx(hFile, &fileSize))
        {
            CloseHandle(hFile);
            return pjh::result::Failure<ErrorCode>{ErrorCode::IoError};
        }

        if (fileSize.QuadPart == 0)
        {
            CloseHandle(hFile);
            return pjh::result::Result<std::string, ErrorCode>::Ok(std::string());
        }

        HANDLE hMapping = CreateFileMappingW(
            hFile,
            nullptr,
            PAGE_READONLY,
            0,
            0,
            nullptr);
        if (!hMapping)
        {
            CloseHandle(hFile);
            return pjh::result::Failure<ErrorCode>{ErrorCode::IoError};
        }

        void* addr = MapViewOfFile(hMapping, FILE_MAP_READ, 0, 0, 0);
        if (!addr)
        {
            CloseHandle(hMapping);
            CloseHandle(hFile);
            return pjh::result::Failure<ErrorCode>{ErrorCode::IoError};
        }

        std::string content(
            static_cast<const char*>(addr),
            static_cast<std::size_t>(fileSize.QuadPart));

        UnmapViewOfFile(addr);
        CloseHandle(hMapping);
        CloseHandle(hFile);
        return pjh::result::Result<std::string, ErrorCode>::Ok(std::move(content));

#else
        int fd = ::open(p.c_str(), O_RDONLY);
        if (fd == -1)
        {
            if (errno == ENOENT)
                return pjh::result::Failure<ErrorCode>{ErrorCode::NotFound};
            if (errno == EACCES)
                return pjh::result::Failure<ErrorCode>{ErrorCode::PermissionDenied};
            return pjh::result::Failure<ErrorCode>{ErrorCode::IoError};
        }

        struct stat st;
        if (::fstat(fd, &st) == -1)
        {
            ::close(fd);
            return pjh::result::Failure<ErrorCode>{ErrorCode::IoError};
        }

        if (st.st_size == 0)
        {
            ::close(fd);
            return pjh::result::Result<std::string, ErrorCode>::Ok(std::string());
        }

        void* addr = ::mmap(
            nullptr,
            static_cast<std::size_t>(st.st_size),
            PROT_READ,
            MAP_PRIVATE,
            fd,
            0);
        if (addr == MAP_FAILED)
        {
            ::close(fd);
            return pjh::result::Failure<ErrorCode>{ErrorCode::IoError};
        }

        std::string content(
            static_cast<const char*>(addr),
            static_cast<std::size_t>(st.st_size));

        ::munmap(addr, static_cast<std::size_t>(st.st_size));
        ::close(fd);
        return pjh::result::Result<std::string, ErrorCode>::Ok(std::move(content));
#endif
    }

    auto Fs::write_file(
        const std::filesystem::path& p, std::string_view content)
        -> pjh::result::Result<void, ErrorCode>
    {
#if PJH_PLATFORM_WINDOWS
        HANDLE hFile = CreateFileW(
            p.c_str(),
            GENERIC_WRITE,
            0,
            nullptr,
            CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
        if (hFile == INVALID_HANDLE_VALUE)
            return pjh::result::Failure<ErrorCode>{ErrorCode::IoError};

        if (!content.empty())
        {
            DWORD written;
            if (!WriteFile(
                    hFile,
                    content.data(),
                    static_cast<DWORD>(content.size()),
                    &written,
                    nullptr) ||
                written != content.size())
            {
                CloseHandle(hFile);
                return pjh::result::Failure<ErrorCode>{ErrorCode::IoError};
            }
        }

        CloseHandle(hFile);
        return pjh::result::Result<void, ErrorCode>::Ok();

#else
        int fd = ::open(
            p.c_str(),
            O_WRONLY | O_CREAT | O_TRUNC,
            S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
        if (fd == -1)
            return pjh::result::Failure<ErrorCode>{ErrorCode::IoError};

        if (!content.empty())
        {
            const char* data = content.data();
            std::size_t remaining = content.size();
            while (remaining > 0)
            {
                ssize_t written = ::write(fd, data, remaining);
                if (written == -1)
                {
                    ::close(fd);
                    return pjh::result::Failure<ErrorCode>{ErrorCode::IoError};
                }
                data += written;
                remaining -= static_cast<std::size_t>(written);
            }
        }

        ::close(fd);
        return pjh::result::Result<void, ErrorCode>::Ok();
#endif
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
