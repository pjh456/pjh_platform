#include <algorithm>
#include <map>
#include <pjh_platform/directory_status.hpp>
#include <utility>

namespace pjh::platform
{
    auto DirectoryStatus::from(const DirectorySnapshot &snapshot) -> DirectoryStatus
    {
        std::uintmax_t total = 0;
        std::size_t files = 0;
        std::size_t dirs = 0;
        std::map<std::filesystem::path, ExtensionSummary> by_extension;
        std::vector<SizeEntry> by_size;

        for (const auto &[name, entry] : snapshot.entries())
        {
            if (entry.m_is_directory)
            {
                ++dirs;
                continue;
            }

            ++files;
            total += entry.m_file_size;
            by_size.push_back(SizeEntry{snapshot.dir_path() / name, entry.m_file_size});

            auto ext = name.extension();
            auto &summary = by_extension[ext];
            summary.m_extension = ext;
            summary.m_file_count += 1;
            summary.m_total_size += entry.m_file_size;
        }

        std::sort(
            by_size.begin(), by_size.end(),
            [](const SizeEntry &a, const SizeEntry &b)
            {
                if (a.m_size != b.m_size)
                    return a.m_size > b.m_size;
                return a.m_path < b.m_path;
            });

        std::vector<ExtensionSummary> extensions;
        extensions.reserve(by_extension.size());
        for (auto &pair : by_extension) extensions.push_back(std::move(pair.second));

        return DirectoryStatus{total, files, dirs, std::move(extensions), std::move(by_size)};
    }

    auto DirectoryStatus::total_size() const -> std::uintmax_t { return m_total_size; }

    auto DirectoryStatus::file_count() const -> std::size_t { return m_file_count; }

    auto DirectoryStatus::dir_count() const -> std::size_t { return m_dir_count; }

    auto DirectoryStatus::extension_summaries() const -> const std::vector<ExtensionSummary> &
    {
        return m_extensions;
    }

    auto DirectoryStatus::largest_files(std::size_t n) const -> std::vector<std::filesystem::path>
    {
        std::vector<std::filesystem::path> result;
        result.reserve(std::min(n, m_files_by_size.size()));
        for (std::size_t i = 0; i < m_files_by_size.size() && i < n; ++i)
            result.push_back(m_files_by_size[i].m_path);
        return result;
    }

    DirectoryStatus::DirectoryStatus(
        std::uintmax_t total_size,
        std::size_t file_count,
        std::size_t dir_count,
        std::vector<ExtensionSummary> extensions,
        std::vector<SizeEntry> files_by_size) :
        m_total_size(total_size),
        m_file_count(file_count),
        m_dir_count(dir_count),
        m_extensions(std::move(extensions)),
        m_files_by_size(std::move(files_by_size))
    {
    }

}  // namespace pjh::platform
