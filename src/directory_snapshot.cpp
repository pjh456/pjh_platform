#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <iterator>
#include <pjh_platform/directory_snapshot.hpp>
#include <pjh_platform/platform.hpp>
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

        // FNV-1a 64-bit: offset basis and prime (FNV spec).
        constexpr std::uint64_t kFnv1a64OffsetBasis = 0xcbf29ce484222325ULL;
        constexpr std::uint64_t kFnv1a64Prime = 0x00000100000001b3ULL;

        /**
         * @brief Folds the bytes of @p data into a running FNV-1a 64-bit @p hash.
         *
         * @details Unrolled 8 bytes per round: each round feeds 8 bytes to the
         *          state in file order, bit-identical to the classic per-byte
         *          FNV-1a loop, with one loop branch per 8 bytes instead of per
         *          byte. The bytes are consumed directly (never reinterpreted
         *          as a wider integer), so the result is
         *          endianness-independent.
         */
        auto fnv1a64_update(std::uint64_t &hash, const void *data, std::size_t size) -> void
        {
            const auto *bytes = static_cast<const std::uint8_t *>(data);
            const auto full = size - (size % 8);
            std::size_t i = 0;
            for (; i < full; i += 8)
            {
                hash ^= bytes[i];
                hash *= kFnv1a64Prime;
                hash ^= bytes[i + 1];
                hash *= kFnv1a64Prime;
                hash ^= bytes[i + 2];
                hash *= kFnv1a64Prime;
                hash ^= bytes[i + 3];
                hash *= kFnv1a64Prime;
                hash ^= bytes[i + 4];
                hash *= kFnv1a64Prime;
                hash ^= bytes[i + 5];
                hash *= kFnv1a64Prime;
                hash ^= bytes[i + 6];
                hash *= kFnv1a64Prime;
                hash ^= bytes[i + 7];
                hash *= kFnv1a64Prime;
            }
            for (; i < size; ++i)
            {
                hash ^= bytes[i];
                hash *= kFnv1a64Prime;
            }
        }
    }  // namespace

    auto Fnv1a64Hasher::operator()(const std::filesystem::path &path) const
        -> std::optional<FileHash>
    {
        // stdio into a fixed 64 KiB buffer: no per-hash heap allocation and no
        // stream layer (no std::ifstream).
#if PJH_PLATFORM_WINDOWS
        std::FILE *in = ::_wfopen(path.c_str(), L"rb");
#else
        std::FILE *in = std::fopen(path.c_str(), "rb");
#endif
        if (in == nullptr)
            return std::nullopt;

        auto hash = kFnv1a64OffsetBasis;
        char buf[64 * 1024];
        bool failed = false;
        for (;;)
        {
            auto got = std::fread(buf, 1, sizeof(buf), in);
            if (got > 0)
                fnv1a64_update(hash, buf, static_cast<std::size_t>(got));
            if (got < sizeof(buf))
            {
                failed = std::ferror(in);
                break;
            }
        }
        std::fclose(in);
        if (failed)
            return std::nullopt;
        return hash;
    }

    auto DirectorySnapshot::capture(const std::filesystem::path &dir)
        -> pjh::result::Result<DirectorySnapshot, ErrorCode>
    {
        return capture_impl(dir, nullptr, std::nullopt);
    }

    auto DirectorySnapshot::capture(
        const std::filesystem::path &dir, const std::vector<std::filesystem::path> *hash_files)
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
            for (const auto &p : *hash_files) hash_names->push_back(p.filename());
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
            auto type = it->status(sec).type();
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
                entry.m_mtime_ns =
                    std::chrono::duration_cast<std::chrono::nanoseconds>(mtime.time_since_epoch())
                        .count();

            auto filename = it->path().filename();
            if (!entry.m_is_directory && hasher)
            {
                bool should_hash =
                    !hash_names || std::any_of(
                                       hash_names->begin(), hash_names->end(),
                                       [&filename](const auto &n) { return n == filename; });
                if (should_hash)
                    entry.m_hash = (*hasher)(it->path());
            }

            snap.m_entries[filename] = std::move(entry);
        }

        return pjh::result::Result<DirectorySnapshot, ErrorCode>::Ok(std::move(snap));
    }

    auto DirectorySnapshot::dir_path() const -> const std::filesystem::path & { return m_dir_path; }

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

    auto DirectorySnapshot::get(const std::filesystem::path &filename) const -> std::optional<Entry>
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
        for (const auto &pair : m_entries) names.push_back(pair.first);
        return names;
    }

    auto DirectorySnapshot::entries() const -> const DirectorySnapshot::EntryMap &
    {
        return m_entries;
    }

}  // namespace pjh::platform
