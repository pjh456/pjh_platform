#include <pjh_platform/directory_snapshot.hpp>
#include <pjh_platform/platform.hpp>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <fstream>
#include <iterator>
#include <utility>

#if PJH_PLATFORM_WINDOWS
#include <windows.h>
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

#if PJH_PLATFORM_WINDOWS
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
    }  // namespace

    auto Fnv1a64Hasher::operator()(const std::filesystem::path &path) const
        -> std::optional<FileHash>
    {
        std::ifstream in(path, std::ios::binary);
        if (!in)
            return std::nullopt;

        std::uint64_t hash = 0xcbf29ce484222325ULL;
        char buf[64 * 1024];
        while (in)
        {
            in.read(buf, static_cast<std::streamsize>(sizeof(buf)));
            std::streamsize got = in.gcount();
            if (got <= 0)
                break;
            for (std::streamsize i = 0; i < got; ++i)
            {
                hash ^= static_cast<unsigned char>(buf[i]);
                hash *= 0x100000001b3ULL;
            }
        }
        if (!in.eof())
            return std::nullopt;
        return hash;
    }

    auto DirectorySnapshot::capture(const std::filesystem::path &dir)
        -> pjh::result::Result<DirectorySnapshot, ErrorCode>
    {
        return capture_impl(dir, nullptr, std::nullopt);
    }

    auto DirectorySnapshot::capture(
        const std::filesystem::path &dir,
        const std::vector<std::filesystem::path> *hash_files)
        -> pjh::result::Result<DirectorySnapshot, ErrorCode>
    {
        return capture(dir, hash_files, Fnv1a64Hasher{});
    }

    auto DirectorySnapshot::capture_impl(
        const std::filesystem::path &dir,
        const std::vector<std::filesystem::path> *hash_files,
        std::optional<HashFn> hasher) -> pjh::result::Result<DirectorySnapshot, ErrorCode>
    {
        std::error_code ec;
        auto abs = std::filesystem::absolute(dir, ec);
        if (ec)
            return pjh::result::Failure<ErrorCode>{map_error_code(ec)};
        abs = abs.lexically_normal();

        auto is_dir = std::filesystem::is_directory(abs, ec);
        if (ec)
            return pjh::result::Failure<ErrorCode>{map_error_code(ec)};
        if (!is_dir)
        {
            if (std::filesystem::exists(abs, ec))
            {
                if (ec)
                    return pjh::result::Failure<ErrorCode>{map_error_code(ec)};
                return pjh::result::Failure<ErrorCode>{ErrorCode::InvalidArgument};
            }
            return pjh::result::Failure<ErrorCode>{ErrorCode::NotFound};
        }

        // Precompute the set of basenames to hash so matching stays O(1).
        std::optional<std::vector<std::filesystem::path>> hash_names;
        if (hash_files)
        {
            hash_names.emplace();
            hash_names->reserve(hash_files->size());
            for (const auto &p : *hash_files)
                hash_names->push_back(p.filename());
        }

        DirectorySnapshot snap;
        snap.m_dir_path = std::move(abs);

        std::filesystem::directory_iterator it(snap.m_dir_path, ec);
        if (ec)
            return pjh::result::Failure<ErrorCode>{map_error_code(ec)};
        std::filesystem::directory_iterator end;

        for (; it != end; it.increment(ec))
        {
            if (ec)
            {
                ec.clear();
                continue;
            }

            Entry entry;
            std::error_code sec;
            auto type = it->symlink_status(sec).type();
            if (sec)
                continue;
            entry.m_is_directory = type == std::filesystem::file_type::directory;

            if (!entry.m_is_directory)
            {
                auto size = it->file_size(sec);
                if (!sec)
                    entry.m_file_size = static_cast<std::uintmax_t>(size);
            }
            auto mtime = it->last_write_time(sec);
            if (!sec)
                entry.m_mtime_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                    mtime.time_since_epoch())
                                       .count();

            auto filename = it->path().filename();
            if (!entry.m_is_directory && hasher)
            {
                bool should_hash =
                    !hash_names ||
                    std::any_of(
                        hash_names->begin(), hash_names->end(),
                        [&filename](const auto &n) { return n == filename; });
                if (should_hash)
                    entry.m_hash = (*hasher)(it->path());
            }

            snap.m_entries[filename] = std::move(entry);
        }

        return pjh::result::Result<DirectorySnapshot, ErrorCode>::Ok(std::move(snap));
    }

    auto DirectorySnapshot::dir_path() const -> const std::filesystem::path &
    {
        return m_dir_path;
    }

    auto DirectorySnapshot::file_count() const -> std::size_t
    {
        return std::count_if(
            m_entries.begin(), m_entries.end(),
            [](const auto &pair) { return !pair.second.m_is_directory; });
    }

    auto DirectorySnapshot::dir_count() const -> std::size_t
    {
        return std::count_if(
            m_entries.begin(), m_entries.end(),
            [](const auto &pair) { return pair.second.m_is_directory; });
    }

    auto DirectorySnapshot::get(const std::filesystem::path &filename) const
        -> std::optional<Entry>
    {
        auto it = m_entries.find(filename);
        if (it == m_entries.end())
            return std::nullopt;
        return it->second;
    }

    auto DirectorySnapshot::contains(const std::filesystem::path &filename) const -> bool
    {
        return m_entries.find(filename) != m_entries.end();
    }

    auto DirectorySnapshot::filenames() const -> std::vector<std::filesystem::path>
    {
        std::vector<std::filesystem::path> names;
        names.reserve(m_entries.size());
        for (const auto &pair : m_entries)
            names.push_back(pair.first);
        return names;
    }

    auto DirectorySnapshot::entries() const -> const DirectorySnapshot::EntryMap &
    {
        return m_entries;
    }

}  // namespace pjh::platform
