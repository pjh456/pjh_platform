#include <pjh_platform/directory_diff.hpp>
#include <utility>
#include <vector>

namespace pjh::platform
{
    namespace
    {
        auto entries_match(const DirectorySnapshot::Entry &x, const DirectorySnapshot::Entry &y)
            -> bool
        {
            if (x.m_hash && y.m_hash)
                return *x.m_hash == *y.m_hash;
            return x.m_file_size == y.m_file_size && x.m_mtime_ns == y.m_mtime_ns;
        }
    }  // namespace

    auto DirectoryDiff::compare(const DirectorySnapshot &before, const DirectorySnapshot &after)
        -> pjh::result::Result<DirectoryDiff, ErrorCode>
    {
        if (before.dir_path() != after.dir_path())
            return pjh::result::Failure<ErrorCode>{ErrorCode::InvalidArgument};

        std::vector<Change> changes;
        const auto &old_entries = before.entries();
        const auto &new_entries = after.entries();

        for (const auto &[name, entry] : new_entries)
        {
            auto old = old_entries.find(name);
            if (old == old_entries.end())
            {
                changes.push_back(Change{ChangeKind::Created, name, after.dir_path() / name});
            }
            else if (!entry.m_is_directory && !entries_match(old->second, entry))
            {
                changes.push_back(Change{ChangeKind::Modified, name, after.dir_path() / name});
            }
        }

        for (const auto &[name, entry] : old_entries)
        {
            if (new_entries.find(name) == new_entries.end())
            {
                changes.push_back(Change{ChangeKind::Deleted, name, before.dir_path() / name});
            }
        }

        return pjh::result::Result<DirectoryDiff, ErrorCode>::Ok(DirectoryDiff{std::move(changes)});
    }

    auto DirectoryDiff::changes() const -> const std::vector<Change> & { return m_changes; }

    auto DirectoryDiff::empty() const -> bool { return m_changes.empty(); }

    auto DirectoryDiff::detect_renames(
        const DirectorySnapshot &before, const DirectorySnapshot &after) const
        -> std::vector<Rename>
    {
        std::vector<Rename> renames;
        std::vector<const Change *> created;
        std::vector<const Change *> deleted;
        for (const auto &ch : m_changes)
        {
            if (ch.m_kind == ChangeKind::Created)
                created.push_back(&ch);
            else if (ch.m_kind == ChangeKind::Deleted)
                deleted.push_back(&ch);
        }
        if (created.empty() || deleted.empty())
            return renames;

        std::vector<bool> used(created.size(), false);
        for (const auto *d : deleted)
        {
            auto old_entry = before.get(d->m_filename);
            if (!old_entry || old_entry->m_is_directory)
                continue;
            for (std::size_t i = 0; i < created.size(); ++i)
            {
                if (used[i])
                    continue;
                auto new_entry = after.get(created[i]->m_filename);
                if (!new_entry || new_entry->m_is_directory)
                    continue;
                if (entries_match(*old_entry, *new_entry))
                {
                    renames.push_back(Rename{d->m_filename, created[i]->m_filename});
                    used[i] = true;
                    break;
                }
            }
        }
        return renames;
    }

    DirectoryDiff::DirectoryDiff(std::vector<Change> changes) : m_changes(std::move(changes)) {}

}  // namespace pjh::platform
