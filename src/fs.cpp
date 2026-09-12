#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <pjh_platform/env.hpp>
#include <pjh_platform/error.hpp>
#include <pjh_platform/fs.hpp>
#include <pjh_platform/platform.hpp>

#include "error_mapping.hpp"

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
                    return pjh::result::Failure<ErrorCode>{detail::map_error_code(ec)};
                auto copied = Fs::copy_directory(from, to, overwrite);
                if (copied.is_err())
                    return copied;
                auto removed = Fs::remove_all(from);
                if (removed.is_err())
                    return pjh::result::Failure<ErrorCode>{removed.unwrap_err()};
                return pjh::result::Result<void, ErrorCode>::Ok();
            }
            if (ec)
                return pjh::result::Failure<ErrorCode>{detail::map_error_code(ec)};

            auto copied = Fs::copy_file(from, to, overwrite);
            if (copied.is_err())
                return copied;
            std::error_code rem_ec;
            std::filesystem::remove(from, rem_ec);
            if (rem_ec)
                return pjh::result::Failure<ErrorCode>{detail::map_error_code(rem_ec)};
            return pjh::result::Result<void, ErrorCode>::Ok();
        }

        // Creates an empty regular file at @p tmp exclusively (O_EXCL on POSIX,
        // CREATE_NEW on Windows), so two writers can never claim the same
        // temporary name. Failure(AlreadyExists) signals that the name is taken
        // (a stale temporary file or a reused process id); the caller retries
        // with the next counter value. Errors go through the shared mapping
        // table (task 59 §4), not a per-TU table.
        auto create_exclusive_empty(const std::filesystem::path &tmp)
            -> pjh::result::Result<void, ErrorCode>
        {
#if PJH_PLATFORM_WINDOWS
            HANDLE h = CreateFileW(
                tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (h == INVALID_HANDLE_VALUE)
                return pjh::result::Failure<ErrorCode>{detail::map_windows_error(GetLastError())};
            CloseHandle(h);
            return pjh::result::Result<void, ErrorCode>::Ok();
#else
            int fd = ::open(
                tmp.c_str(), O_WRONLY | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
            if (fd == -1)
                return pjh::result::Failure<ErrorCode>{detail::map_errno_to_error(errno)};
            ::close(fd);
            return pjh::result::Result<void, ErrorCode>::Ok();
#endif
        }

        // Best-effort removal that never changes the caller's primary error:
        // the temp file is always a regular file created by this call, so
        // remove (not remove_all) is correct.
        void remove_temp_best_effort(const std::filesystem::path &tmp)
        {
            std::error_code ec;
            std::filesystem::remove(tmp, ec);
        }

    }

    auto Fs::current_path() -> std::filesystem::path { return std::filesystem::current_path(); }

    auto Fs::create_directories(const std::filesystem::path &p)
        -> pjh::result::Result<void, ErrorCode>
    {
        std::error_code ec;
        std::filesystem::create_directories(p, ec);
        if (ec)
            return pjh::result::Failure<ErrorCode>{detail::map_error_code(ec)};
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
                 it != std::filesystem::recursive_directory_iterator(); it.increment(dir_ec))
            {
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
            auto mapped = detail::map_error_code(ec);
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
            return pjh::result::Failure<ErrorCode>{detail::map_error_code(ec)};
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
            {
                // A directory opened for reading reports access denied on
                // Windows; classify it like the POSIX S_ISDIR branch.
                DWORD attrs = GetFileAttributesW(p.c_str());
                if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY))
                    return pjh::result::Failure<ErrorCode>{ErrorCode::InvalidArgument};
                return pjh::result::Failure<ErrorCode>{ErrorCode::PermissionDenied};
            }
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

        std::string content;
        content.resize(static_cast<std::size_t>(fileSize.QuadPart));
        std::size_t total = 0;
        while (total < content.size())
        {
            DWORD to_read = static_cast<DWORD>(
                std::min<std::uint64_t>(content.size() - total, 1u << 30));  // cap 1 GiB per call
            DWORD got = 0;
            if (!ReadFile(hFile, content.data() + total, to_read, &got, nullptr))
            {
                CloseHandle(hFile);
                return pjh::result::Failure<ErrorCode>{ErrorCode::IoError};
            }
            if (got == 0)
                break;  // truncated concurrently: keep what we read
            total += got;
        }
        content.resize(total);
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

        if (S_ISDIR(st.st_mode))
        {
            ::close(fd);
            return pjh::result::Failure<ErrorCode>{ErrorCode::InvalidArgument};
        }

        if (st.st_size == 0)
        {
            ::close(fd);
            return pjh::result::Result<std::string, ErrorCode>::Ok(std::string());
        }

        std::string content;
        content.resize(static_cast<std::size_t>(st.st_size));
        std::size_t total = 0;
        while (total < content.size())
        {
            ssize_t n = ::read(fd, content.data() + total, content.size() - total);
            if (n == -1)
            {
                if (errno == EINTR)
                    continue;
                ::close(fd);
                return pjh::result::Failure<ErrorCode>{ErrorCode::IoError};
            }
            if (n == 0)
                break;  // truncated concurrently: keep what we read
            total += static_cast<std::size_t>(n);
        }
        content.resize(total);
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
        {
            DWORD err = GetLastError();
            if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND)
                return pjh::result::Failure<ErrorCode>{ErrorCode::NotFound};
            if (err == ERROR_ACCESS_DENIED)
                return pjh::result::Failure<ErrorCode>{ErrorCode::PermissionDenied};
            return pjh::result::Failure<ErrorCode>{ErrorCode::IoError};
        }

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
        {
            if (errno == ENOENT)
                return pjh::result::Failure<ErrorCode>{ErrorCode::NotFound};
            if (errno == EACCES)
                return pjh::result::Failure<ErrorCode>{ErrorCode::PermissionDenied};
            return pjh::result::Failure<ErrorCode>{ErrorCode::IoError};
        }

        if (!content.empty())
        {
            const char *data = content.data();
            std::size_t remaining = content.size();
            while (remaining > 0)
            {
                ssize_t written = ::write(fd, data, remaining);
                if (written == -1)
                {
                    if (errno == EINTR)
                        continue;
                    ::close(fd);
                    return pjh::result::Failure<ErrorCode>{ErrorCode::IoError};
                }
                if (written == 0)
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

    // Fs::append routes every failure through the shared mapping table
    // (detail::map_errno_to_error / detail::map_windows_error), per the task
    // text. The existing write_file/read_file open arms classify their errors
    // with a per-family inline form instead; that divergence is legacy and is
    // deliberately NOT changed here (task 30 ruling: full-table routing for
    // those two APIs is out of scope). Consequence: append reports a few errno
    // values differently from write_file (for example ENOSPC -> LimitReached
    // and ENOTDIR -> NotFound).
    auto Fs::append(const std::filesystem::path &p, std::string_view content)
        -> pjh::result::Result<void, ErrorCode>
    {
#if PJH_PLATFORM_WINDOWS
        // FILE_APPEND_DATA (not GENERIC_WRITE) gives end-of-file semantics;
        // OPEN_ALWAYS creates when missing and never truncates. FILE_SHARE_READ
        // lets a later read_file reader open the file concurrently, unlike
        // write_file's exclusive share mode.
        HANDLE hFile = CreateFileW(
            p.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hFile == INVALID_HANDLE_VALUE)
            return pjh::result::Failure<ErrorCode>{detail::map_windows_error(GetLastError())};

        if (!content.empty())
        {
            const char *data = content.data();
            std::size_t remaining = content.size();
            while (remaining > 0)
            {
                // Cap each call at 1 GiB so a content larger than a DWORD does
                // not truncate (the defect write_file inherits from its single
                // untruncated WriteFile).
                DWORD chunk = static_cast<DWORD>(std::min<std::uint64_t>(remaining, 1u << 30));
                DWORD written = 0;
                if (!WriteFile(hFile, data, chunk, &written, nullptr))
                {
                    DWORD err = GetLastError();
                    CloseHandle(hFile);
                    return pjh::result::Failure<ErrorCode>{detail::map_windows_error(err)};
                }
                if (written == 0)
                {
                    // A successful call that wrote nothing makes no progress;
                    // fail instead of spinning (GetLastError is not meaningful
                    // here). Mirrors the POSIX zero-write guard.
                    CloseHandle(hFile);
                    return pjh::result::Failure<ErrorCode>{ErrorCode::IoError};
                }
                data += written;
                remaining -= static_cast<std::size_t>(written);
            }
        }

        CloseHandle(hFile);
        return pjh::result::Result<void, ErrorCode>::Ok();

#else
        int fd =
            ::open(p.c_str(), O_WRONLY | O_CREAT | O_APPEND, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
        if (fd == -1)
            return pjh::result::Failure<ErrorCode>{detail::map_errno_to_error(errno)};

        if (!content.empty())
        {
            const char *data = content.data();
            std::size_t remaining = content.size();
            while (remaining > 0)
            {
                ssize_t written = ::write(fd, data, remaining);
                if (written == -1)
                {
                    if (errno == EINTR)
                        continue;
                    auto mapped = detail::map_errno_to_error(errno);
                    ::close(fd);
                    return pjh::result::Failure<ErrorCode>{mapped};
                }
                if (written == 0)
                {
                    // A zero-length write on a non-empty buffer makes no
                    // progress; bail out instead of spinning forever.
                    ::close(fd);
                    return pjh::result::Failure<ErrorCode>{ErrorCode::IoError};
                }
                data += written;
                remaining -= static_cast<std::size_t>(written);
            }
        }

        // The close result is intentionally ignored: a failed close cannot
        // roll back bytes already written (same as write_file).
        ::close(fd);
        return pjh::result::Result<void, ErrorCode>::Ok();
#endif
    }

    auto Fs::write_file_atomic(const std::filesystem::path &p, std::string_view content)
        -> pjh::result::Result<void, ErrorCode>
    {
        // 1) A directory target cannot be replaced by a file; reject it before
        //    creating any temporary file (symlinks-to-directories included,
        //    since is_directory follows links). This is a TOCTOU pre-check: if
        //    the target type changes concurrently, Fs::rename's native result
        //    is the fallback.
        std::error_code dir_ec;
        if (std::filesystem::is_directory(p, dir_ec))
            return pjh::result::Failure<ErrorCode>{ErrorCode::InvalidArgument};

        // 2) Reserve a unique sibling temp name (process id + process-wide
        //    counter). The counter is atomic because concurrent callers may
        //    race even though the library itself creates no threads.
#if PJH_PLATFORM_WINDOWS
        const auto pid = static_cast<unsigned long>(GetCurrentProcessId());
#else
        const auto pid = static_cast<unsigned long>(::getpid());
#endif
        static std::atomic<unsigned long> counter{0};

        constexpr int kMaxAttempts = 128;
        std::filesystem::path tmp;
        bool reserved = false;
        for (int attempt = 0; attempt < kMaxAttempts; ++attempt)
        {
            const unsigned long seq = counter.fetch_add(1, std::memory_order_relaxed);
            std::filesystem::path candidate = p;
            candidate += std::filesystem::path(
                "." + std::to_string(pid) + "." + std::to_string(seq) + ".tmp");
            auto created = create_exclusive_empty(candidate);
            if (created.is_ok())
            {
                tmp = std::move(candidate);
                reserved = true;
                break;
            }
            if (created.unwrap_err() != ErrorCode::AlreadyExists)
                return created;  // NotFound / PermissionDenied / mapped
            // Name taken (stale temp or reused pid): try the next counter value.
        }
        if (!reserved)
            return pjh::result::Failure<ErrorCode>{ErrorCode::AlreadyExists};

        // 3) Fill the reserved temp. Reusing write_file keeps one byte-writing
        //    implementation, per the task text (§3.2 reserve-then-write). The
        //    temp name is already owned by this call, so the reopen cannot
        //    collide; only an external delete+recreate could interfere, which is
        //    outside the threat model.
        auto written = Fs::write_file(tmp, content);
        if (written.is_err())
        {
            remove_temp_best_effort(tmp);  // best effort; never masks `written`
            return written;
        }

        // 4) Atomically replace the target. The temp is a sibling, so the
        //    rename stays on one filesystem and never takes the copy fallback.
        auto renamed = Fs::rename(tmp, p, /*overwrite=*/true);
        if (renamed.is_err())
        {
            remove_temp_best_effort(tmp);  // best effort; never masks `renamed`
            return renamed;
        }
        return pjh::result::Result<void, ErrorCode>::Ok();
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
            return pjh::result::Failure<ErrorCode>{detail::map_error_code(ec)};
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
                return pjh::result::Failure<ErrorCode>{detail::map_error_code(ec)};
            return pjh::result::Failure<ErrorCode>{ErrorCode::InvalidArgument};
        }

        std::filesystem::create_directories(to, ec);
        if (ec)
            return pjh::result::Failure<ErrorCode>{detail::map_error_code(ec)};

        auto options = overwrite ? std::filesystem::copy_options::overwrite_existing
                                 : std::filesystem::copy_options::none;
        std::filesystem::recursive_directory_iterator it(
            from, std::filesystem::directory_options::skip_permission_denied, ec);
        const std::filesystem::recursive_directory_iterator end;
        for (; it != end; it.increment(ec))
        {
            std::error_code entry_ec;
            const auto &entry = *it;
            auto dest = to / entry.path().lexically_relative(from);
            if (entry.is_directory(entry_ec))
                std::filesystem::create_directories(dest, entry_ec);
            else
                std::filesystem::copy_file(entry.path(), dest, options, entry_ec);
            if (entry_ec)
                return pjh::result::Failure<ErrorCode>{detail::map_error_code(entry_ec)};
        }
        if (ec)
            return pjh::result::Failure<ErrorCode>{detail::map_error_code(ec)};
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
                return pjh::result::Failure<ErrorCode>{detail::map_error_code(ec)};
            return pjh::result::Failure<ErrorCode>{ErrorCode::NotFound};
        }

        if (!overwrite)
        {
            std::error_code to_ec;
            if (std::filesystem::exists(to, to_ec))
            {
                if (to_ec)
                    return pjh::result::Failure<ErrorCode>{detail::map_error_code(to_ec)};
                return pjh::result::Failure<ErrorCode>{ErrorCode::AlreadyExists};
            }
        }
        else
        {
            std::error_code dir_ec;
            const auto to_status = std::filesystem::status(to, dir_ec);
            if (std::filesystem::is_directory(to_status))
            {
                // A directory target can only be replaced when it is empty.
                std::error_code iter_ec;
                bool empty = std::filesystem::directory_iterator(to, iter_ec) ==
                             std::filesystem::directory_iterator();
                if (iter_ec)
                    return pjh::result::Failure<ErrorCode>{detail::map_error_code(iter_ec)};
                if (!empty)
                    return pjh::result::Failure<ErrorCode>{ErrorCode::AlreadyExists};
                std::filesystem::remove(to, iter_ec);
                if (iter_ec)
                    return pjh::result::Failure<ErrorCode>{detail::map_error_code(iter_ec)};
            }
            else if (std::filesystem::exists(to_status))
            {
                // A directory source cannot replace a non-directory target:
                // POSIX reports ENOTDIR and Windows ERROR_ACCESS_DENIED, so
                // reject the pair here to keep both lanes identical. Use
                // symlink_status: POSIX rename(2) does not follow a trailing
                // symlink on oldpath, so a symlink to a directory is renamed
                // itself and must not be treated as a directory source. A
                // status-query error is left to the native rename below.
                std::error_code from_ec;
                const auto from_status = std::filesystem::symlink_status(from, from_ec);
                if (!from_ec && std::filesystem::is_directory(from_status))
                    return pjh::result::Failure<ErrorCode>{ErrorCode::InvalidArgument};
            }
            // else: the target does not exist (file_type::not_found) or its
            // status could not be determined; leave the outcome to the native
            // rename below, which reports the real error code.
        }

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
            detail::map_error_code(std::error_code(static_cast<int>(err), std::system_category()))};
#else
        if (::rename(from.c_str(), to.c_str()) == 0)
            return pjh::result::Result<void, ErrorCode>::Ok();

        std::error_code err_ec(errno, std::generic_category());
        if (is_cross_device(err_ec))
            return rename_via_copy(from, to, overwrite);
        return pjh::result::Failure<ErrorCode>{detail::map_error_code(err_ec)};
#endif
    }

    auto Fs::list_directory(const std::filesystem::path &p)
        -> pjh::result::Result<std::vector<std::filesystem::path>, ErrorCode>
    {
        std::error_code ec;
        std::filesystem::directory_iterator it(p, ec);
        if (ec)
            return pjh::result::Failure<ErrorCode>{detail::map_error_code(ec)};
        std::filesystem::directory_iterator end;
        std::vector<std::filesystem::path> entries;
        for (; it != end; it.increment(ec)) entries.push_back(it->path());
        if (ec)
            return pjh::result::Failure<ErrorCode>{detail::map_error_code(ec)};
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
