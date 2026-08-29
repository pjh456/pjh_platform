#include <pjh_platform/directory_diff.hpp>
#include <unordered_map>
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

        // Bucket key for the (file size, mtime) fallback branch of entries_match.
        using SmKey = std::pair<std::uintmax_t, std::intmax_t>;

        // std::hash provides no pair specialization in libstdc++, so combine
        // the two integral keys manually (quality affects speed, not equality).
        struct SmKeyHash
        {
            auto operator()(const SmKey &key) const noexcept -> std::size_t
            {
                const auto h1 = std::hash<std::uintmax_t>{}(key.first);
                const auto h2 = std::hash<std::intmax_t>{}(key.second);
                return h1 ^ (h2 + 0x9e3779b97f4a7c15ULL + (h1 << 6) + (h1 >> 2));
            }
        };
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

        // Bucket the created files by the keys entries_match() can test, so a
        // deleted file only scans its true candidates instead of the full
        // created list. Bucket vectors keep created order (= after-name
        // order), which preserves the former full scan's first-match pairing.
        struct Candidate
        {
            std::size_t m_index;
            const DirectorySnapshot::Entry *m_entry;
        };
        std::unordered_map<FileHash, std::vector<Candidate>> by_hash;
        std::unordered_map<SmKey, std::vector<Candidate>, SmKeyHash> by_sm_all;
        std::unordered_map<SmKey, std::vector<Candidate>, SmKeyHash> by_sm_unhashed;
        const auto &after_entries = after.entries();
        for (std::size_t i = 0; i < created.size(); ++i)
        {
            auto it = after_entries.find(created[i]->m_filename);
            if (it == after_entries.end() || it->second.m_is_directory)
                continue;
            const auto &entry = it->second;
            auto sm = SmKey{entry.m_file_size, entry.m_mtime_ns};
            by_sm_all[sm].push_back(Candidate{i, &entry});
            if (entry.m_hash)
                by_hash[*entry.m_hash].push_back(Candidate{i, &entry});
            else
                by_sm_unhashed[sm].push_back(Candidate{i, &entry});
        }

        std::vector<char> used(created.size(), 0);
        for (const auto *d : deleted)
        {
            auto old_entry = before.get(d->m_filename);
            if (!old_entry || old_entry->m_is_directory)
                continue;
            // Unhashed old: the fallback branch tests every created file by
            // (size, mtime). Hashed old: equal-hash candidates plus the
            // fallback's unhashed (size, mtime) candidates; the two lists are
            // disjoint by construction.
            const std::vector<Candidate> *l1 = nullptr;
            const std::vector<Candidate> *l2 = nullptr;
            auto sm = SmKey{old_entry->m_file_size, old_entry->m_mtime_ns};
            if (old_entry->m_hash)
            {
                auto h = by_hash.find(*old_entry->m_hash);
                if (h != by_hash.end())
                    l1 = &h->second;
                auto u = by_sm_unhashed.find(sm);
                if (u != by_sm_unhashed.end())
                    l2 = &u->second;
            }
            else
            {
                auto a = by_sm_all.find(sm);
                if (a != by_sm_all.end())
                    l1 = &a->second;
            }
            if (!l1 && !l2)
                continue;
            std::size_t p1 = 0;
            std::size_t p2 = 0;
            while (true)
            {
                const Candidate *pick = nullptr;
                const bool e1 = l1 != nullptr && p1 < l1->size();
                const bool e2 = l2 != nullptr && p2 < l2->size();
                if (e1 && (!e2 || (*l1)[p1].m_index < (*l2)[p2].m_index))
                    pick = &(*l1)[p1++];
                else if (e2)
                    pick = &(*l2)[p2++];
                else
                    break;
                if (used[pick->m_index])
                    continue;
                if (entries_match(*old_entry, *pick->m_entry))
                {
                    renames.push_back(Rename{d->m_filename, created[pick->m_index]->m_filename});
                    used[pick->m_index] = 1;
                    break;
                }
            }
        }
        return renames;
    }

    DirectoryDiff::DirectoryDiff(std::vector<Change> changes) : m_changes(std::move(changes)) {}

}  // namespace pjh::platform
