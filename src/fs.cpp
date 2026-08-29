#include <cerrno>
#include <pjh_platform/env.hpp>
#include <pjh_platform/error.hpp>
#include <pjh_platform/fs.hpp>
#include <pjh_platform/platform.hpp>

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

    namespace
    {

        auto map_error_code(const std::error_code &ec) -> ErrorCode
        {
            if (!ec)
                return ErrorCode::Success;

            if (ec.category() == std::generic_category())
            {
                switch (ec.value())
                {
                case ENOENT:
                    return ErrorCode::NotFound;
                case EACCES:
                case EPERM:
                    return ErrorCode::PermissionDenied;
                case EEXIST:
                    return ErrorCode::AlreadyExists;
                case EINVAL:
                    return ErrorCode::InvalidArgument;
#ifdef ENOTSUP
                case ENOTSUP:
                    return ErrorCode::NotSupported;
#endif
                case EIO:
                    return ErrorCode::IoError;
                default:
                    return ErrorCode::Unknown;
                }
            }

#if defined(_WIN32)
            if (ec.category() == std::system_category())
            {
                switch (static_cast<unsigned long>(ec.value()))
                {
                case ERROR_FILE_NOT_FOUND:
                case ERROR_PATH_NOT_FOUND:
                    return ErrorCode::NotFound;
                case ERROR_ACCESS_DENIED:
                    return ErrorCode::PermissionDenied;
                case ERROR_ALREADY_EXISTS:
                case ERROR_FILE_EXISTS:
                    return ErrorCode::AlreadyExists;
                case ERROR_INVALID_PARAMETER:
                    return ErrorCode::InvalidArgument;
                case ERROR_NOT_SUPPORTED:
                    return ErrorCode::NotSupported;
                default:
                    return ErrorCode::Unknown;
                }
            }
#endif

            return ErrorCode::Unknown;
        }

        auto is_cross_device(const std::error_code &ec) -> bool
        {
#if PJH_PLATFORM_WINDOWS
            if (ec.category() == std::system_category() &&
                static_cast<unsigned long>(ec.value()) == ERROR_NOT_SAME_DEVICE)
                return true;
            return false;
#else
            return ec.category() == std::generic_category() && ec.value() == EXDEV;
#endif
        }

        auto rename_via_copy(
            const std::filesystem::path &from, const std::filesystem::path &to, bool overwrite)
            -> pjh::result::Result<void, ErrorCode>
        {
            std::error_code ec;
            if (std::filesystem::is_directory(from, ec))
            {
                if (ec)
                    return pjh::result::Failure<ErrorCode>{map_error_code(ec)};
                auto copied = Fs::copy_directory(from, to, overwrite);
                if (copied.is_err())
                    return copied;
                auto removed = Fs::remove_all(from);
                if (removed.is_err())
                    return pjh::result::Failure<ErrorCode>{removed.unwrap_err()};
                return pjh::result::Result<void, ErrorCode>::Ok();
            }
            if (ec)
                return pjh::result::Failure<ErrorCode>{map_error_code(ec)};

            auto copied = Fs::copy_file(from, to, overwrite);
            if (copied.is_err())
                return copied;
            std::error_code rem_ec;
            std::filesystem::remove(from, rem_ec);
            if (rem_ec)
                return pjh::result::Failure<ErrorCode>{map_error_code(rem_ec)};
            return pjh::result::Result<void, ErrorCode>::Ok();
        }

    }

    auto Fs::current_path() -> std::filesystem::path { return std::filesystem::current_path(); }

    auto Fs::create_directories(const std::filesystem::path &p)
        -> pjh::result::Result<void, ErrorCode>
    {
        std::error_code ec;
        std::filesystem::create_directories(p, ec);
        if (ec)
            return pjh::result::Failure<ErrorCode>{map_error_code(ec)};
        return pjh::result::Result<void, ErrorCode>::Ok();
    }

    auto Fs::remove_all(const std::filesystem::path &p)
        -> pjh::result::Result<std::uintmax_t, ErrorCode>
    {
#if PJH_PLATFORM_WINDOWS
        // std::filesystem::remove_all fails on read-only files (common
        // in .git directories). Clear FILE_ATTRIBUTE_READONLY first.
        {
            std::error_code dir_ec;
            for (auto it = std::filesystem::recursive_directory_iterator(
                     p, std::filesystem::directory_options::skip_permission_denied, dir_ec);
                 it != std::filesystem::recursive_directory_iterator(); ++it)
            {
                if (dir_ec)
                {
                    dir_ec.clear();
                    continue;
                }
                DWORD attrs = GetFileAttributesW(it->path().c_str());
                if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_READONLY))
                {
                    SetFileAttributesW(it->path().c_str(), attrs & ~FILE_ATTRIBUTE_READONLY);
                }
            }
        }
#endif

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

    auto Fs::exists(const std::filesystem::path &p) -> bool { return std::filesystem::exists(p); }

    auto Fs::is_regular_file(const std::filesystem::path &p) -> bool
    {
        return std::filesystem::is_regular_file(p);
    }

    auto Fs::is_directory(const std::filesystem::path &p) -> bool
    {
        return std::filesystem::is_directory(p);
    }

    auto Fs::file_size(const std::filesystem::path &p)
        -> pjh::result::Result<std::uintmax_t, ErrorCode>
    {
        std::error_code ec;
        auto sz = std::filesystem::file_size(p, ec);
        if (ec)
            return pjh::result::Failure<ErrorCode>{map_error_code(ec)};
        return pjh::result::Result<std::uintmax_t, ErrorCode>::Ok(sz);
    }

    auto Fs::read_file(const std::filesystem::path &p)
        -> pjh::result::Result<std::string, ErrorCode>
    {
#if PJH_PLATFORM_WINDOWS
        HANDLE hFile = CreateFileW(
            p.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
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

        HANDLE hMapping = CreateFileMappingW(hFile, nullptr, PAGE_READONLY, 0, 0, nullptr);
        if (!hMapping)
        {
            CloseHandle(hFile);
            return pjh::result::Failure<ErrorCode>{ErrorCode::IoError};
        }

        void *addr = MapViewOfFile(hMapping, FILE_MAP_READ, 0, 0, 0);
        if (!addr)
        {
            CloseHandle(hMapping);
            CloseHandle(hFile);
            return pjh::result::Failure<ErrorCode>{ErrorCode::IoError};
        }

        std::string content(
            static_cast<const char *>(addr), static_cast<std::size_t>(fileSize.QuadPart));

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

        void *addr =
            ::mmap(nullptr, static_cast<std::size_t>(st.st_size), PROT_READ, MAP_PRIVATE, fd, 0);
        if (addr == MAP_FAILED)
        {
            ::close(fd);
            return pjh::result::Failure<ErrorCode>{ErrorCode::IoError};
        }

        std::string content(static_cast<const char *>(addr), static_cast<std::size_t>(st.st_size));

        ::munmap(addr, static_cast<std::size_t>(st.st_size));
        ::close(fd);
        return pjh::result::Result<std::string, ErrorCode>::Ok(std::move(content));
#endif
    }

    auto Fs::write_file(const std::filesystem::path &p, std::string_view content)
        -> pjh::result::Result<void, ErrorCode>
    {
#if PJH_PLATFORM_WINDOWS
        HANDLE hFile = CreateFileW(
            p.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hFile == INVALID_HANDLE_VALUE)
            return pjh::result::Failure<ErrorCode>{ErrorCode::IoError};

        if (!content.empty())
        {
            DWORD written;
            if (!WriteFile(
                    hFile, content.data(), static_cast<DWORD>(content.size()), &written, nullptr) ||
                written != content.size())
            {
                CloseHandle(hFile);
                return pjh::result::Failure<ErrorCode>{ErrorCode::IoError};
            }
        }

        CloseHandle(hFile);
        return pjh::result::Result<void, ErrorCode>::Ok();

#else
        int fd =
            ::open(p.c_str(), O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
        if (fd == -1)
            return pjh::result::Failure<ErrorCode>{ErrorCode::IoError};

        if (!content.empty())
        {
            const char *data = content.data();
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

    auto Fs::copy_file(
        const std::filesystem::path &from, const std::filesystem::path &to, bool overwrite)
        -> pjh::result::Result<void, ErrorCode>
    {
        std::error_code ec;
        std::filesystem::copy_file(
            from, to,
            overwrite ? std::filesystem::copy_options::overwrite_existing
                      : std::filesystem::copy_options::none,
            ec);
        if (ec)
            return pjh::result::Failure<ErrorCode>{map_error_code(ec)};
        return pjh::result::Result<void, ErrorCode>::Ok();
    }

    auto Fs::copy_directory(
        const std::filesystem::path &from, const std::filesystem::path &to, bool overwrite)
        -> pjh::result::Result<void, ErrorCode>
    {
        std::error_code ec;
        if (!std::filesystem::is_directory(from, ec))
        {
            if (ec)
                return pjh::result::Failure<ErrorCode>{map_error_code(ec)};
            return pjh::result::Failure<ErrorCode>{ErrorCode::InvalidArgument};
        }

        std::filesystem::create_directories(to, ec);
        if (ec)
            return pjh::result::Failure<ErrorCode>{map_error_code(ec)};

        auto options = overwrite ? std::filesystem::copy_options::overwrite_existing
                                 : std::filesystem::copy_options::none;
        std::filesystem::recursive_directory_iterator it(
            from, std::filesystem::directory_options::skip_permission_denied, ec);
        const std::filesystem::recursive_directory_iterator end;
        for (; it != end; ++it)
        {
            std::error_code entry_ec;
            const auto &entry = *it;
            auto dest = to / entry.path().lexically_relative(from);
            if (entry.is_directory(entry_ec))
                std::filesystem::create_directories(dest, entry_ec);
            else
                std::filesystem::copy_file(entry.path(), dest, options, entry_ec);
            if (entry_ec)
                return pjh::result::Failure<ErrorCode>{map_error_code(entry_ec)};
        }
        if (ec)
            return pjh::result::Failure<ErrorCode>{map_error_code(ec)};
        return pjh::result::Result<void, ErrorCode>::Ok();
    }

    auto Fs::rename(
        const std::filesystem::path &from, const std::filesystem::path &to, bool overwrite)
        -> pjh::result::Result<void, ErrorCode>
    {
        if (from == to)
            return pjh::result::Result<void, ErrorCode>::Ok();

        std::error_code ec;
        if (!std::filesystem::exists(from, ec))
        {
            if (ec)
                return pjh::result::Failure<ErrorCode>{map_error_code(ec)};
            return pjh::result::Failure<ErrorCode>{ErrorCode::NotFound};
        }

        if (!overwrite)
        {
            std::error_code to_ec;
            if (std::filesystem::exists(to, to_ec))
            {
                if (to_ec)
                    return pjh::result::Failure<ErrorCode>{map_error_code(to_ec)};
                return pjh::result::Failure<ErrorCode>{ErrorCode::AlreadyExists};
            }
        }
        else if (std::filesystem::is_directory(to, ec))
        {
            // A directory target can only be replaced when it is empty.
            std::error_code iter_ec;
            bool empty = std::filesystem::directory_iterator(to, iter_ec) ==
                         std::filesystem::directory_iterator();
            if (iter_ec)
                return pjh::result::Failure<ErrorCode>{map_error_code(iter_ec)};
            if (!empty)
                return pjh::result::Failure<ErrorCode>{ErrorCode::AlreadyExists};
            std::filesystem::remove(to, iter_ec);
            if (iter_ec)
                return pjh::result::Failure<ErrorCode>{map_error_code(iter_ec)};
        }
        if (ec)
            return pjh::result::Failure<ErrorCode>{map_error_code(ec)};

#if PJH_PLATFORM_WINDOWS
        DWORD flags = MOVEFILE_COPY_ALLOWED;
        if (overwrite)
            flags |= MOVEFILE_REPLACE_EXISTING;
        if (MoveFileExW(from.c_str(), to.c_str(), flags))
            return pjh::result::Result<void, ErrorCode>::Ok();

        DWORD err = GetLastError();
        if (is_cross_device(std::error_code(static_cast<int>(err), std::system_category())))
            return rename_via_copy(from, to, overwrite);
        return pjh::result::Failure<ErrorCode>{
            map_error_code(std::error_code(static_cast<int>(err), std::system_category()))};
#else
        if (::rename(from.c_str(), to.c_str()) == 0)
            return pjh::result::Result<void, ErrorCode>::Ok();

        std::error_code err_ec(errno, std::generic_category());
        if (is_cross_device(err_ec))
            return rename_via_copy(from, to, overwrite);
        return pjh::result::Failure<ErrorCode>{map_error_code(err_ec)};
#endif
    }

    auto Fs::list_directory(const std::filesystem::path &p)
        -> pjh::result::Result<std::vector<std::filesystem::path>, ErrorCode>
    {
        std::error_code ec;
        std::vector<std::filesystem::path> entries;
        for (const auto &entry : std::filesystem::directory_iterator(p, ec))
            entries.push_back(entry.path());
        if (ec)
            return pjh::result::Failure<ErrorCode>{map_error_code(ec)};
        return pjh::result::Result<std::vector<std::filesystem::path>, ErrorCode>::Ok(
            std::move(entries));
    }

    auto Fs::temp_directory() -> std::filesystem::path
    {
        return std::filesystem::temp_directory_path();
    }

    auto Fs::home_directory() -> pjh::result::Result<std::filesystem::path, ErrorCode>
    {
        auto home = Env::get("HOME");
        if (home.is_ok())
            return pjh::result::Result<std::filesystem::path, ErrorCode>::Ok(
                std::filesystem::path(home.unwrap()));
#if PJH_PLATFORM_WINDOWS
        auto userprofile = Env::get("USERPROFILE");
        if (userprofile.is_ok())
            return pjh::result::Result<std::filesystem::path, ErrorCode>::Ok(
                std::filesystem::path(userprofile.unwrap()));
#endif
        return pjh::result::Failure<ErrorCode>{ErrorCode::NotFound};
    }

    auto Fs::normalize(const std::filesystem::path &p) -> std::filesystem::path
    {
        return p.lexically_normal();
    }

    namespace
    {
        auto u8_string(const std::filesystem::path &p) -> std::string
        {
            auto u8 = p.u8string();
            return std::string(u8.begin(), u8.end());
        }
    }

    auto Fs::extension(const std::filesystem::path &p) -> std::string
    {
        return u8_string(p.extension());
    }

    auto Fs::stem(const std::filesystem::path &p) -> std::string { return u8_string(p.stem()); }

    auto Fs::relative(const std::filesystem::path &base, const std::filesystem::path &target)
        -> pjh::result::Result<std::filesystem::path, ErrorCode>
    {
        auto rel = target.lexically_normal().lexically_relative(base.lexically_normal());
        if (rel.empty())
            return pjh::result::Failure<ErrorCode>{ErrorCode::InvalidArgument};
        return pjh::result::Result<std::filesystem::path, ErrorCode>::Ok(std::move(rel));
    }

}  // namespace pjh::platform
