#include "PathUtils.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlwapi.h>
#pragma comment(lib, "Shlwapi.lib")
#pragma comment(lib, "Advapi32.lib")

#include <algorithm>
#include <vector>
#include <cwctype>

namespace ac {
namespace path {

std::wstring normalize(const std::wstring& input) {
    if (input.empty()) return input;

    // ① 正斜杠全部替换为反斜杠
    std::wstring s = input;
    std::replace(s.begin(), s.end(), L'/', L'\\');

    // ② GetFullPathNameW 解析为绝对路径，消解 .. 和 .
    //    先查询所需缓冲区大小，再分配。
    DWORD need = GetFullPathNameW(s.c_str(), 0, nullptr, nullptr);
    if (need == 0) {
        return s;  // 失败：退回只替换斜杠的结果
    }
    std::vector<wchar_t> buf(static_cast<size_t>(need) + 1);
    DWORD got = GetFullPathNameW(s.c_str(), static_cast<DWORD>(buf.size()), buf.data(), nullptr);
    if (got == 0 || got >= buf.size()) {
        return s;
    }
    return std::wstring(buf.data(), got);
}

bool exists(const std::wstring& path) {
    std::wstring n = normalize(path);
    return PathFileExistsW(n.c_str()) != FALSE;
}

std::wstring driveOf(const std::wstring& normalizedPath) {
    if (normalizedPath.length() >= 2 && normalizedPath[1] == L':') {
        std::wstring d(1, static_cast<wchar_t>(::towupper(normalizedPath[0])));
        return d;
    }
    return {};
}

bool isNtfs(const std::wstring& pathOnVolume) {
    std::wstring drv = driveOf(pathOnVolume);
    if (drv.empty()) return false;
    std::wstring root = drv + L":\\";
    wchar_t fsName[MAX_PATH + 1] = {};
    if (GetVolumeInformationW(root.c_str(), nullptr, 0, nullptr, nullptr,
                              nullptr, fsName, MAX_PATH)) {
        return _wcsicmp(fsName, L"NTFS") == 0;
    }
    return false;
}

bool isElevated() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token) || !token) {
        return false;
    }
    TOKEN_ELEVATION elev = {};
    DWORD ret = 0;
    BOOL ok = GetTokenInformation(token, TokenElevation, &elev, sizeof(elev), &ret);
    CloseHandle(token);
    return ok && elev.TokenIsElevated != 0;
}

std::wstring appDir() {
    wchar_t exePath[MAX_PATH] = {};
    DWORD len = GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return L".";
    std::wstring p(exePath, len);
    size_t pos = p.find_last_of(L"\\/");
    if (pos == std::wstring::npos) return L".";
    return p.substr(0, pos);
}

std::wstring fileName(const std::wstring& path) {
    size_t pos = path.find_last_of(L"\\/");
    if (pos == std::wstring::npos) return path;
    return path.substr(pos + 1);
}

}} // namespace ac::path
