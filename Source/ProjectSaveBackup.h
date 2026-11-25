#pragma once
#include <windows.h>
#include <filesystem>
#include <algorithm>
#include <regex>
#include <string>
#include <vector>
#include <stdexcept>

namespace HwsProjectBackup {
namespace fs = std::filesystem;
inline bool VersionLess(const std::wstring& a, const std::wstring& b) {
    return a.size() != b.size() ? a.size() < b.size() : a < b;
}
inline std::wstring NextVersion(std::wstring value) {
    for (size_t i = value.size(); i > 0; --i) {
        if (value[i - 1] != L'9') { ++value[i - 1]; return value; }
        value[i - 1] = L'0';
    }
    return L"1" + value;
}
struct Lock {
    HANDLE handle;
    explicit Lock(const fs::path& path) : handle(CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr)) {
        if (handle == INVALID_HANDLE_VALUE) throw std::runtime_error("Backup folder is locked or not writable.");
    }
    ~Lock() { CloseHandle(handle); }
    Lock(const Lock&) = delete;
    Lock& operator=(const Lock&) = delete;
};
inline void RequirePlainDirectory(const fs::path& path) {
    fs::create_directories(path);
    DWORD attr = GetFileAttributesW(path.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_REPARSE_POINT))
        throw std::runtime_error("Backup folders must be ordinary directories, not links.");
}
inline fs::path Create(const fs::path& source, const fs::path& root) {
    RequirePlainDirectory(root);
    const fs::path folder = root / source.stem();
    RequirePlainDirectory(folder);
    Lock lock(folder / L".backup.lock");
    const std::wstring prefix = source.stem().wstring() + L"_v1.";
    const std::wstring extension = source.extension().wstring();
    const std::wregex suffix(L"(0|[1-9][0-9]*)_[0-9]{4}-[0-9]{2}-[0-9]{2}_[0-9]{2}-[0-9]{2}-[0-9]{2}");
    struct Entry { std::wstring version; fs::path path; };
    std::vector<Entry> entries;
    for (const auto& file : fs::directory_iterator(folder)) {
        if (!file.is_regular_file() || file.is_symlink()) continue;
        const std::wstring name = file.path().filename().wstring();
        if (name.size() <= prefix.size() + extension.size() || name.compare(0, prefix.size(), prefix) != 0 || file.path().extension().wstring() != extension) continue;
        const std::wstring tail = name.substr(prefix.size(), name.size() - prefix.size() - extension.size());
        std::wsmatch match;
        if (std::regex_match(tail, match, suffix)) entries.push_back({match[1].str(), file.path()});
    }
    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) { return VersionLess(a.version, b.version); });
    const std::wstring version = entries.empty() ? L"0" : NextVersion(entries.back().version);
    SYSTEMTIME now;
    GetLocalTime(&now);
    wchar_t timestamp[32];
    swprintf_s(timestamp, L"%04u-%02u-%02u_%02u-%02u-%02u", now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);
    const fs::path destination = folder / (prefix + version + L"_" + timestamp + extension);
    const fs::path temporary = folder / L".pending-backup.tmp";
    // Never prune history until the copy is complete and atomically published.
    if (!CopyFileW(source.c_str(), temporary.c_str(), FALSE))
        throw std::runtime_error("Could not copy the saved project. Existing backups were retained.");
    if (!MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temporary.c_str());
        throw std::runtime_error("Could not finish the backup. Existing backups were retained.");
    }
    entries.push_back({version, destination});
    for (size_t i = 0; i + 10 < entries.size(); ++i) {
        if (!DeleteFileW(entries[i].path.c_str()))
            throw std::runtime_error("The new backup was saved, but an older backup could not be removed.");
    }
    return destination;
}
}
