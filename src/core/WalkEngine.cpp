#include "WalkEngine.h"
#include "PathUtils.h"
#include "ArchiveFile.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <vector>
#include <string>

namespace ac {

namespace {

void walkDir(const std::wstring& dir,
             bool recursive,
             const std::atomic<bool>& cancel,
             const ISearchEngine::FoundCallback& onFound,
             const ISearchEngine::ProgressCallback& onProgress,
             int& dirsScanned,
             int& filesFound) {
    if (cancel.load(std::memory_order_relaxed)) return;

    // 构造搜索通配符：dir\*
    std::wstring pattern = dir;
    if (!pattern.empty() && pattern.back() != L'\\') pattern.push_back(L'\\');
    pattern.push_back(L'*');

    WIN32_FIND_DATAW fd{};
    HANDLE hFind = FindFirstFileW(pattern.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;

    std::vector<std::wstring> subdirs;  // 收集子目录，稍后递归

    do {
        if (cancel.load(std::memory_order_relaxed)) break;

        std::wstring name = fd.cFileName;
        if (name == L"." || name == L"..") continue;

        bool isDir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;

        // 跳过回收站和系统目录（否则删进回收站的文件又会出现在列表里）
        if (isDir && recursive) {
            if (_wcsicmp(name.c_str(), L"$RECYCLE.BIN") == 0) continue;
            if (_wcsicmp(name.c_str(), L"System Volume Information") == 0) continue;
        }

        std::wstring fullPath = dir;
        if (!fullPath.empty() && fullPath.back() != L'\\') fullPath.push_back(L'\\');
        fullPath += name;

        if (isDir) {
            if (recursive) {
                subdirs.push_back(fullPath);
            }
        } else {
            if (ArchiveFile::isArchiveExt(name)) {
                ArchiveFile af;
                af.path = path::normalize(fullPath);
                af.name = name;
                LARGE_INTEGER sz;
                sz.LowPart  = fd.nFileSizeLow;
                sz.HighPart = fd.nFileSizeHigh;
                af.size = sz.QuadPart;
                // ⚠️ 修复 D.9：用独立的 filetimeToUnixMs（正确处理优先级）
                af.mtimeMs = filetimeToUnixMs(fd.ftLastWriteTime.dwLowDateTime,
                                              fd.ftLastWriteTime.dwHighDateTime);
                onFound(af);
                ++filesFound;
            }
        }
    } while (FindNextFileW(hFind, &fd));

    FindClose(hFind);

    ++dirsScanned;
    onProgress(dirsScanned, filesFound);

    for (const auto& sub : subdirs) {
        if (cancel.load(std::memory_order_relaxed)) break;
        walkDir(sub, recursive, cancel, onFound, onProgress, dirsScanned, filesFound);
    }
}

} // namespace

std::pair<bool, std::wstring> WalkEngine::run(
    const std::wstring& folder,
    bool recursive,
    const std::atomic<bool>& cancel,
    const FoundCallback& onFound,
    const ProgressCallback& onProgress) {

    if (folder.empty()) {
        return {false, L"扫描路径为空"};
    }
    std::wstring norm = path::normalize(folder);
    if (!path::exists(norm)) {
        return {false, L"路径不存在：" + norm};
    }

    int dirsScanned = 0, filesFound = 0;
    walkDir(norm, recursive, cancel, onFound, onProgress, dirsScanned, filesFound);

    if (cancel.load()) {
        return {false, L"已取消"};
    }
    return {true, {}};
}

} // namespace ac
