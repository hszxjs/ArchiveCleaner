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

std::wstring parentDir(const std::wstring& normalizedPath) {
    size_t pos = normalizedPath.find_last_of(L'\\');
    if (pos == std::wstring::npos || pos == 0) return {};
    return normalizedPath.substr(0, pos);
}

// === 程序目录保护 ===

namespace {

// 枚举一个 Uninstall 注册表键下所有子项的 InstallLocation
void collectInstallLocations(HKEY hive, const wchar_t* subKeyPath, std::vector<std::wstring>& out) {
    HKEY uninstall = nullptr;
    if (RegOpenKeyExW(hive, subKeyPath, 0, KEY_READ, &uninstall) != ERROR_SUCCESS) {
        return;
    }
    DWORD index = 0;
    wchar_t name[256] = {};
    DWORD nameLen = 256;
    while (RegEnumKeyExW(uninstall, index++, name, &nameLen,
                         nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
        nameLen = 256;  // 重置供下次使用
        HKEY sub = nullptr;
        if (RegOpenKeyExW(uninstall, name, 0, KEY_READ, &sub) == ERROR_SUCCESS) {
            wchar_t loc[MAX_PATH] = {};
            DWORD size = sizeof(loc);
            DWORD type = 0;
            if (RegQueryValueExW(sub, L"InstallLocation", nullptr, &type,
                                 reinterpret_cast<LPBYTE>(loc), &size) == ERROR_SUCCESS
                && (type == REG_SZ || type == REG_EXPAND_SZ) && size > sizeof(wchar_t)) {
                std::wstring p(loc);
                // 清理：去空格、确保尾反斜杠、转小写
                while (!p.empty() && (p.back() == L' ')) p.pop_back();
                if (p.size() >= 2 && p[1] == L':') {  // 只接受盘符路径
                    if (p.back() != L'\\') p.push_back(L'\\');
                    for (auto& c : p) c = static_cast<wchar_t>(::towlower(c));
                    out.push_back(p);
                }
            }
            RegCloseKey(sub);
        }
    }
    RegCloseKey(uninstall);
}

} // namespace

const std::vector<std::wstring>& installedProgramDirs() {
    static std::vector<std::wstring> dirs;
    static bool loaded = false;
    if (loaded) return dirs;
    loaded = true;
    // 三个注册表位置：HKLM 64位、HKLM 32位（WOW6432Node）、当前用户
    collectInstallLocations(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall", dirs);
    collectInstallLocations(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall", dirs);
    collectInstallLocations(HKEY_CURRENT_USER,
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall", dirs);
    return dirs;
}

bool startsWithAny(const std::wstring& path, const std::vector<std::wstring>& prefixes) {
    if (path.empty() || prefixes.empty()) return false;
    std::wstring lower;
    lower.reserve(path.size());
    for (wchar_t c : path) lower.push_back(static_cast<wchar_t>(::towlower(c)));
    for (const auto& p : prefixes) {
        if (lower.size() >= p.size() && lower.compare(0, p.size(), p) == 0) {
            return true;
        }
    }
    return false;
}

bool dirContainsExe(const std::wstring& dir) {
    if (dir.empty()) return false;
    std::wstring pat = dir;
    if (pat.back() != L'\\') pat.push_back(L'\\');
    pat += L"*.exe";
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW(pat.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return false;
    FindClose(h);
    return true;
}

bool dirContainsProjectMarker(const std::wstring& dir) {
    if (dir.empty()) return false;
    std::wstring base = dir;
    if (base.back() != L'\\') base.push_back(L'\\');
    // 精确名标记（目录或文件均可，GetFileAttributesW 一次调用）
    const wchar_t* markers[] = {
        L".git", L"package.json", L"Cargo.toml", L"go.mod",
        L"pom.xml", L"build.gradle", L"CMakeLists.txt", L".svn",
    };
    for (const auto* m : markers) {
        if (GetFileAttributesW((base + m).c_str()) != INVALID_FILE_ATTRIBUTES) {
            return true;
        }
    }
    // *.sln 需要通配符（一个项目可能有多个）
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW((base + L"*.sln").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        FindClose(h);
        return true;
    }
    return false;
}

}} // namespace ac::path
