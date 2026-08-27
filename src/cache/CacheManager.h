// NppS3 — Notepad++ plugin for S3-compatible object storage
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <functional>
#include <string>

namespace npps3 {

// Maps remote objects to files under a managed cache directory.
// Object keys are NOT valid Windows paths; local names are derived from a
// hash of bucket+key plus a sanitized display leaf, which sidesteps Unicode,
// reserved characters, duplicate-after-sanitization and path-length issues.
class CacheManager
{
public:
    void SetRoot(const std::wstring& root) { m_root = root; }
    const std::wstring& Root() const { return m_root; }

    // Deterministic local path for a remote object. Does not touch the disk.
    std::wstring LocalPathFor(const std::string& profileId,
                              const std::string& bucket,
                              const std::string& key) const;

    // Creates the parent directory chain for a cache file path.
    static bool EnsureParentDirs(const std::wstring& filePath);

    // Deletes cache files whose last write time is older than `days`, except
    // those for which keepPredicate returns true (e.g. still-open documents).
    // Returns the number of files removed.
    int CleanupStale(int days, const std::function<bool(const std::wstring&)>& keepPredicate) const;

    // Same, but ignores file age.
    int RemoveAll(const std::function<bool(const std::wstring&)>& keepPredicate) const;

    // Exposed for unit tests.
    static std::wstring SanitizeComponent(const std::wstring& name, size_t maxLen);

private:
    // Deletes cache files older than the given FILETIME value (as a 64-bit
    // count), skipping anything keepPredicate protects.
    int Sweep(unsigned long long cutoffFileTime,
              const std::function<bool(const std::wstring&)>& keepPredicate) const;

    std::wstring m_root;
};

} // namespace npps3
