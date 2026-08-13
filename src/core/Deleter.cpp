#include "Deleter.h"
#include "PathUtils.h"
#include "ProcessChecker.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <objbase.h>
#include <shlwapi.h>
#include <fileapi.h>
#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Shlwapi.lib")
#pragma comment(lib, "Ole32.lib")

#include <fstream>
#include <mutex>
#include <future>
#include <memory>
#include <string>
#include <chrono>

namespace ac {

namespace {
std::mutex g_logMutex;

std::string wToUtf8(const std::wstring& s) {
    if (s.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()),
                                nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(n), 0);
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()),
                        out.data(), n, nullptr, nullptr);
    return out;
}

std::wstring utf8ToW(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(n), 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), out.data(), n);
    return out;
}

// 格式化占用进程串
std::wstring lockersToString(const std::vector<LockingProcess>& lockers) {
    if (lockers.empty()) return L"占用进程未知（可能是系统服务或权限不足）";
    std::wstring s = L"被占用: ";
    for (size_t i = 0; i < lockers.size(); ++i) {
        if (i) s += L"；";
        s += lockers[i].name + L" (PID " + std::to_wstring(lockers[i].pid) + L")";
    }
    return s;
}

// 当前时间字符串（YYYY-MM-DD HH:MM:SS）→ UTF-8
std::string nowString() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
                   st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return buf;
}
} // namespace

void logDeleteAction(const std::wstring& path, bool permanent, bool success, const std::wstring& reason) {
    std::lock_guard<std::mutex> lk(g_logMutex);
    std::wstring logPath = path::appDir() + L"\\delete_log.txt";
    std::ofstream f(logPath, std::ios::app | std::ios::binary);
    if (!f.is_open()) return;
    std::string ts = nowString();
    std::string status = success ? "成功" : "失败";
    // 中文转 UTF-8
    auto toU8 = [](const wchar_t* ws) {
        std::wstring s(ws);
        return wToUtf8(s);
    };
    f << "[" << ts << "] [" << toU8(status == "成功" ? L"成功" : L"失败").c_str()
      << "] [" << (permanent ? "永久删除" : "回收站") << "] "
      << wToUtf8(path);
    if (!reason.empty()) {
        f << "  ||  " << wToUtf8(reason);
    }
    f << "\n";
}

DeleteResult Deleter::deleteOne(const std::wstring& normPath) {
    DeleteResult r;
    r.path = normPath;

    // COM 初始化（RAII，任意 return 路径都 CoUninitialize）
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    struct ComGuard {
        HRESULT hr;
        ~ComGuard() { if (hr == S_OK || hr == S_FALSE) CoUninitialize(); }
    } guard{ hr };

    if (permanent_) {
        // 永久删除：DeleteFileW
        if (DeleteFileW(normPath.c_str())) {
            r.ok = true;
            return r;
        }
        DWORD err = GetLastError();
        if (err == ERROR_ACCESS_DENIED) {
            SetFileAttributesW(normPath.c_str(), FILE_ATTRIBUTE_NORMAL);
            if (DeleteFileW(normPath.c_str())) {
                r.ok = true;
                return r;
            }
            err = GetLastError();
        }
        if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND) {
            r.reason = L"文件已不存在（可能已被移动或删除）";
        } else if (err == ERROR_SHARING_VIOLATION || err == ERROR_ACCESS_DENIED) {
            r.lockers = findLockers(normPath);
            r.reason = lockersToString(r.lockers);
        } else {
            r.reason = L"删除失败，错误码 " + std::to_wstring(err);
        }
        return r;
    }

    // 送回收站：SHFileOperationW（双 \0 结尾）
    std::wstring buf = normPath + L'\0' + L'\0';
    SHFILEOPSTRUCTW op{};
    op.hwnd = nullptr;
    op.wFunc = FO_DELETE;
    op.pFrom = buf.c_str();
    op.pTo = nullptr;
    op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;

    int ret = SHFileOperationW(&op);
    if (ret == 0 && !op.fAnyOperationsAborted) {
        r.ok = true;
        return r;
    }
    if (!PathFileExistsW(normPath.c_str())) {
        r.reason = L"文件已不存在";
    } else {
        r.lockers = findLockers(normPath);
        r.reason = lockersToString(r.lockers);
    }
    return r;
}

std::vector<DeleteResult> Deleter::run(const std::vector<ArchiveFile>& files,
                                       const std::atomic<bool>& cancel,
                                       const ProgressCallback& onProgress) {
    std::vector<DeleteResult> results;
    results.reserve(files.size());

    int total = static_cast<int>(files.size());
    for (int i = 0; i < total; ++i) {
        if (cancel.load(std::memory_order_relaxed)) break;

        const ArchiveFile& af = files[i];
        if (onProgress) onProgress(i, total, af.path, af.size);

        // 超时保护：std::async + wait_for
        std::wstring pathCopy = af.path;
        std::future<DeleteResult> fut = std::async(std::launch::async, [this, pathCopy]() {
            return this->deleteOne(pathCopy);
        });

        std::future_status st = fut.wait_for(std::chrono::seconds(timeoutSec_));
        DeleteResult r;
        if (st == std::future_status::timeout) {
            if (!path::exists(af.path)) {
                r.path = af.path;
                r.ok = true;
            } else {
                r.path = af.path;
                r.reason = L"删除超时（可能路径非法或系统忙）";
            }
        } else {
            r = fut.get();
        }

        logDeleteAction(r.path, permanent_, r.ok, r.ok ? L"" : r.reason);
        results.push_back(r);
    }

    return results;
}

} // namespace ac
