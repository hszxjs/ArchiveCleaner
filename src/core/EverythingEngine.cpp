#include "EverythingEngine.h"
#include "ProcessRunner.h"
#include "PathUtils.h"
#include "ArchiveFile.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#pragma comment(lib, "Advapi32.lib")

#include <string>
#include <algorithm>

namespace ac {

namespace {
std::wstring utf8ToW(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(n), 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), out.data(), n);
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

std::wstring EverythingEngine::resolveExe() const {
    std::wstring p = cfg_.esPath;
    if (p.empty()) return {};
    if (p.size() >= 2 && p[1] != L':') {
        std::wstring base = path::appDir();
        if (!base.empty() && base.back() != L'\\') base.push_back(L'\\');
        p = base + p;
    }
    return p;
}

bool EverythingEngine::everythingRunning() {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    bool found = false;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, L"Everything.exe") == 0) {
                found = true;
                break;
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return found;
}

bool EverythingEngine::isAvailable() const {
    std::wstring exe = resolveExe();
    if (exe.empty() || !path::exists(exe)) return false;
    return everythingRunning();
}

std::wstring EverythingEngine::unavailableReason() const {
    std::wstring exe = resolveExe();
    if (exe.empty()) return L"未配置 es.exe 路径";
    if (!path::exists(exe)) return L"es.exe 不存在：" + exe;
    if (!everythingRunning()) return L"Everything 主程序未运行（es.exe 依赖它的索引服务）";
    return {};
}

std::pair<bool, std::wstring> EverythingEngine::run(
    const std::wstring& folder, bool recursive,
    const std::atomic<bool>& cancel,
    const FoundCallback& onFound,
    const ProgressCallback& onProgress) {

    std::wstring exe = resolveExe();
    if (exe.empty() || !path::exists(exe)) {
        return {false, unavailableReason()};
    }
    if (!everythingRunning()) {
        return {false, unavailableReason()};
    }

    std::wstring normFolder = path::normalize(folder);
    if (!path::exists(normFolder)) {
        return {false, L"路径不存在：" + normFolder};
    }

    // es.exe 语法（实测确认）：-path "目录\" 列全部，程序内过滤扩展名
    // es 默认输出本地编码（GBK），用 localToW 解析
    std::wstring base = normFolder;
    if (!base.empty() && base.back() != L'\\') base.push_back(L'\\');

    std::wstring args = L"-n -1 -path \"" + base + L"\"";

    int filesFound = 0;
    auto r = ProcessRunner::run(exe, args, cancel,
        [&](const std::string& line) {
            if (line.empty()) return;
            std::wstring wpath = path::normalize(localToW(line));
            // 排除回收站和系统目录
            if (wpath.find(L"$RECYCLE.BIN") != std::wstring::npos) return;
            if (wpath.find(L"System Volume Information") != std::wstring::npos) return;
            // 纯字符串过滤扩展名（零 IO）
            std::wstring fn = path::fileName(wpath);
            if (!ArchiveFile::isArchiveExt(fn)) return;
            // 命中后查大小/时间
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
        60000
    );

    onProgress(0, filesFound);
    if (r.cancelled) return {false, L"已取消"};
    if (r.timedOut)  return {false, L"es.exe 超时（60秒无响应）"};
    return {true, {}};
}

} // namespace ac
