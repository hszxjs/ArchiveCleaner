#include "FdEngine.h"
#include "ProcessRunner.h"
#include "PathUtils.h"
#include "ArchiveFile.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <string>

namespace ac {

namespace {
// UTF-8 ↔ UTF-16
std::wstring utf8ToW(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(n), 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), out.data(), n);
    return out;
}
std::string wToUtf8(const std::wstring& s) {
    if (s.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()),
                                nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(n), 0);
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()),
                        out.data(), n, nullptr, nullptr);
    return out;
}
std::wstring localToW(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_ACP, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(n), 0);
    MultiByteToWideChar(CP_ACP, 0, s.c_str(), static_cast<int>(s.size()), out.data(), n);
    return out;
}
} // namespace

std::wstring FdEngine::resolveExe() const {
    std::wstring p = cfg_.fdPath;
    if (p.empty()) return {};
    if (p.size() >= 2 && p[1] != L':') {
        // 相对路径：相对 appDir
        std::wstring base = path::appDir();
        if (!base.empty() && base.back() != L'\\') base.push_back(L'\\');
        p = base + p;
    }
    return p;
}

bool FdEngine::isAvailable() const {
    std::wstring exe = resolveExe();
    return !exe.empty() && path::exists(exe);
}

std::wstring FdEngine::unavailableReason() const {
    std::wstring exe = resolveExe();
    if (exe.empty()) return L"未配置 fd.exe 路径";
    if (!path::exists(exe)) return L"fd.exe 不存在：" + exe;
    return {};
}

std::pair<bool, std::wstring> FdEngine::run(
    const std::wstring& folder, bool recursive,
    const std::atomic<bool>& cancel,
    const FoundCallback& onFound,
    const ProgressCallback& onProgress) {

    std::wstring exe = resolveExe();
    if (exe.empty() || !path::exists(exe)) {
        return {false, unavailableReason()};
    }

    std::wstring normFolder = path::normalize(folder);
    if (!path::exists(normFolder)) {
        return {false, L"路径不存在：" + normFolder};
    }

    // 拼参数：-e zip -e rar ... --absolute --no-ignore --hidden "" "folder"
    // fd 输出 UTF-8
    std::wstring args;
    for (const auto& e : archiveExts()) {
        args += L"-e " + utf8ToW(e) + L" ";
    }
    args += L"--absolute --no-ignore --hidden";
    if (!recursive) {
        args += L" --max-depth 1";
    }
    // pattern 空串 + folder，都加引号
    args += L" \"\" \"" + normFolder + L"\"";

    int filesFound = 0;
    auto r = ProcessRunner::run(exe, args, cancel,
        [&](const std::string& line) {
            if (line.empty()) return;
            std::wstring wpath = path::normalize(utf8ToW(line));
            // 排除回收站和系统目录
            if (wpath.find(L"$RECYCLE.BIN") != std::wstring::npos) return;
            if (wpath.find(L"System Volume Information") != std::wstring::npos) return;
            // 纯字符串过滤（零 IO）
            std::wstring fn = path::fileName(wpath);
            if (!ArchiveFile::isArchiveExt(fn)) return;
            // 命中后查大小/时间（WIN32_FIND_DATAW，一次 IO）
            WIN32_FIND_DATAW fd{};
            HANDLE h = FindFirstFileW(wpath.c_str(), &fd);
            if (h == INVALID_HANDLE_VALUE) return;
            bool isFile = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
            if (isFile) {
                ArchiveFile af;
                af.path = wpath;
                af.name = fn;
                LARGE_INTEGER sz;
                sz.LowPart = fd.nFileSizeLow; sz.HighPart = fd.nFileSizeHigh;
                af.size = sz.QuadPart;
                af.mtimeMs = filetimeToUnixMs(fd.ftLastWriteTime.dwLowDateTime,
                                              fd.ftLastWriteTime.dwHighDateTime);
                onFound(af);
                ++filesFound;
                if (filesFound % 20 == 0) onProgress(0, filesFound);
            }
            FindClose(h);
        },
        nullptr,
        60000  // 60s 超时
    );

    onProgress(0, filesFound);
    if (r.cancelled) return {false, L"已取消"};
    if (r.timedOut)  return {false, L"fd.exe 超时（60秒无响应）"};
    return {true, {}};
}

} // namespace ac
