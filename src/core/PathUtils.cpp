#include "PathUtils.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlwapi.h>
#pragma comment(lib, "Shlwapi.lib")
#pragma comment(lib, "Advapi32.lib")
#pragma comment(lib, "Version.lib")

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

// 枚举一个 Uninstall 注册表键下所有子项的 InstallLocation + DisplayName
void collectInstallLocations(HKEY hive, const wchar_t* subKeyPath,
                             std::vector<InstalledProgram>& out) {
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
                // 清理：去空格、确保尾反斜杠、转小写（前缀匹配用）
                while (!p.empty() && (p.back() == L' ')) p.pop_back();
                if (p.size() >= 2 && p[1] == L':') {  // 只接受盘符路径
                    if (p.back() != L'\\') p.push_back(L'\\');
                    for (auto& c : p) c = static_cast<wchar_t>(::towlower(c));
                    wchar_t disp[MAX_PATH] = {};
                    DWORD dsize = sizeof(disp);
                    std::wstring display;
                    if (RegQueryValueExW(sub, L"DisplayName", nullptr, &type,
                                         reinterpret_cast<LPBYTE>(disp), &dsize) == ERROR_SUCCESS
                        && (type == REG_SZ || type == REG_EXPAND_SZ) && dsize > sizeof(wchar_t)) {
                        display = disp;
                    }
                    out.push_back({p, display});
                }
            }
            RegCloseKey(sub);
        }
    }
    RegCloseKey(uninstall);
}

} // namespace

const std::vector<InstalledProgram>& installedPrograms() {
    static std::vector<InstalledProgram> progs;
    static bool loaded = false;
    if (loaded) return progs;
    loaded = true;
    // 三个注册表位置：HKLM 64位、HKLM 32位（WOW6432Node）、当前用户
    collectInstallLocations(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall", progs);
    collectInstallLocations(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall", progs);
    collectInstallLocations(HKEY_CURRENT_USER,
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall", progs);
    return progs;
}

const std::vector<std::wstring>& installedProgramDirs() {
    static std::vector<std::wstring> dirs;
    static bool loaded = false;
    if (loaded) return dirs;
    loaded = true;
    for (const auto& p : installedPrograms()) dirs.push_back(p.dir);
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

std::wstring exeDisplayName(const std::wstring& exePath) {
    DWORD handle = 0;
    DWORD size = GetFileVersionInfoSizeW(exePath.c_str(), &handle);
    if (size == 0) return {};
    std::vector<BYTE> buf(size);
    if (!GetFileVersionInfoW(exePath.c_str(), 0, size, buf.data())) return {};
    // 查翻译表拿语言/代码页，拼出 StringFileInfo 前缀
    struct LangAndCodePage { WORD lang; WORD codePage; };
    LangAndCodePage* langs = nullptr;
    UINT cb = 0;
    std::wstring prefix;
    if (VerQueryValueW(buf.data(), L"\\VarFileInfo\\Translation",
                       reinterpret_cast<LPVOID*>(&langs), &cb)
        && cb >= sizeof(LangAndCodePage) && langs) {
        wchar_t hdr[40] = {};
        swprintf(hdr, 40, L"\\StringFileInfo\\%04x%04x\\", langs[0].lang, langs[0].codePage);
        prefix = hdr;
    } else {
        prefix = L"\\StringFileInfo\\040904b0\\";  // 美式英语 Unicode 兜底
    }
    for (const wchar_t* key : {L"FileDescription", L"ProductName"}) {
        wchar_t* val = nullptr;
        UINT vlen = 0;
        if (VerQueryValueW(buf.data(), (prefix + key).c_str(),
                           reinterpret_cast<LPVOID*>(&val), &vlen)
            && val && vlen > 0 && val[0] != L'\0') {
            std::wstring s = val;
            while (!s.empty() && iswspace(s.back())) s.pop_back();
            if (!s.empty()) return s;
        }
    }
    return {};
}

std::wstring firstExeInDir(const std::wstring& dir) {
    if (dir.empty()) return {};
    std::wstring base = dir;
    if (base.back() != L'\\') base.push_back(L'\\');
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW((base + L"*.exe").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return {};
    std::wstring dirName = fileName(dir);
    std::wstring best;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        std::wstring fn = fd.cFileName;
        // 优先与目录同名的 exe（绿软惯例：DirName\DirName.exe）
        if (fn.size() > 4 && _wcsicmp(fn.substr(0, fn.size() - 4).c_str(), dirName.c_str()) == 0) {
            best = base + fn;
            break;
        }
        if (best.empty()) best = base + fn;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return best;
}

namespace {

// 无意义的目录段：data/cache 这类通用名不该当软件名展示
bool isGenericSegment(const std::wstring& seg) {
    static const wchar_t* generic[] = {
        L"data", L"assets", L"asset", L"resources", L"resource", L"res",
        L"mods", L"bin", L"lib", L"libs", L"cache", L"temp", L"tmp",
        L"update", L"updates", L"files", L"file", L"common", L"versions",
        L"download", L"downloads", L"app", L"apps", L"plugin", L"plugins",
        L"skins", L"config", L"save", L"saves", L"backup", L"backups",
        L"log", L"logs", L"output", L"out", L"pack", L"packs", L"patch",
        L"patches", L"userdata", L"user", L"content", L"media", L"image",
        L"images", L"img", L"video", L"videos", L"audio", L"sound",
        L"pool", L"addons", L"addon", L"webres", L"themes", L"theme",
        L"locale", L"locales", L"lang", L"langs", L"dict", L"dicts",
        L"win-i386", L"win-x64", L"win32", L"x64", L"x86", L"i386",
        L"driver", L"drivers",
    };
    for (const auto* g : generic) {
        if (_wcsicmp(seg.c_str(), g) == 0) return true;
    }
    return false;
}

// 版本号样式目录段：
//  纯版本号（1.1.0.2264 / 2024.6）或 组件_版本（addsignature_3.1.0.1 / foo-95）
bool isVersionSegment(const std::wstring& seg) {
    int dots = 0, digits = 0, others = 0;
    for (wchar_t c : seg) {
        if (c == L'.') { ++dots; continue; }
        if (iswdigit(c)) { ++digits; continue; }
        ++others;
    }
    if (digits > 0 && others == 0 && dots > 0) return true;   // 纯数字带点
    // 组件名_数字(.数字)* —— 最后一个 _ 或 - 之后是纯数字（可带点）
    size_t sep = seg.find_last_of(L"_-");
    if (sep != std::wstring::npos && sep + 1 < seg.size() && sep > 0) {
        bool tailNum = true;
        int tailDots = 0, tailDigits = 0, tailOthers = 0;
        for (size_t i = sep + 1; i < seg.size(); ++i) {
            wchar_t c = seg[i];
            if (c == L'.') { ++tailDots; continue; }
            if (iswdigit(c)) { ++tailDigits; continue; }
            tailNum = false;
            break;
        }
        if (tailNum && tailDigits > 0) return true;           // addsignature_3.1.0.1 / foo-95
    }
    // 名字.数字 —— 最后一个 . 之后是纯数字（knewdocs_master.95 / build.20240101）
    size_t dot = seg.find_last_of(L'.');
    if (dot != std::wstring::npos && dot > 0 && dot + 1 < seg.size()) {
        bool tailNum = true;
        for (size_t i = dot + 1; i < seg.size(); ++i) {
            if (!iswdigit(seg[i])) { tailNum = false; break; }
        }
        if (tailNum) return true;
    }
    return false;
}

// 公共/系统目录：其中的 exe（安装器残留、系统组件）不归属任何具体软件，
// 向上找 exe 时碰到这些目录必须停手，避免把文件错认给无关 exe
bool isSharedAncestor(const std::wstring& dir) {
    std::wstring lower;
    lower.reserve(dir.size());
    for (wchar_t c : dir) lower.push_back(static_cast<wchar_t>(::towlower(c)));
    if (lower.size() <= 3) return true;  // 盘根
    if (lower.find(L"\\windows\\") != std::wstring::npos) return true;
    if (lower.back() != L'\\') lower.push_back(L'\\');
    // 以这些目录本身为终点（等于判断：其下的具体软件目录不受影响）
    static const wchar_t* sharedEnds[] = {
        L"\\program files\\", L"\\program files (x86)\\", L"\\programdata\\",
        L"\\users\\public\\", L"\\appdata\\local\\temp\\",
        L"\\appdata\\locallow\\temp\\", L"\\appdata\\roaming\\temp\\",
    };
    for (const auto* s : sharedEnds) {
        size_t n = wcslen(s);
        if (lower.size() >= n && lower.compare(lower.size() - n, n, s) == 0) return true;
    }
    // 末段是下载/桌面/临时类目录
    std::wstring seg = fileName(dir);
    for (auto& c : seg) c = static_cast<wchar_t>(::towlower(c));
    static const wchar_t* lastSegs[] = {
        L"downloads", L"download", L"desktop", L"temp", L"tmp",
    };
    for (const auto* s : lastSegs) {
        if (seg == s) return true;
    }
    return false;
}

} // namespace

AppIdentity findAppIdentity(const std::wstring& fileDir) {
    // ① 向上找 exe（最多 4 级），读版本资源显示名——最具体。
    //    碰到公共/系统目录立即停手，防止错认给无关 exe（如 Temp 里的安装器残留）。
    //    例外：Program Files 的直接子目录是规范的软件安装目录，按强识别处理。
    std::wstring prev = fileDir;  // 最后一个非公共祖先
    std::wstring ancestor = fileDir;
    for (int level = 0; level < 4 && ancestor.size() > 3; ++level) {
        if (isSharedAncestor(ancestor)) {
            std::wstring lower;
            for (wchar_t c : ancestor) lower.push_back(static_cast<wchar_t>(::towlower(c)));
            if (lower.back() != L'\\') lower.push_back(L'\\');
            bool pf = lower.size() >= 15
                      && (lower.compare(lower.size() - 15, 15, L"\\program files\\") == 0
                          || (lower.size() >= 21
                              && lower.compare(lower.size() - 21, 21, L"\\program files (x86)\\") == 0));
            if (pf && prev != ancestor) {
                return {prev, fileName(prev), AppIdentity::ProgramFiles};
            }
            break;
        }
        std::wstring exe = firstExeInDir(ancestor);
        if (!exe.empty()) {
            std::wstring name = exeDisplayName(exe);
            if (name.empty()) {
                std::wstring base = fileName(exe);
                if (base.size() > 4) base = base.substr(0, base.size() - 4);
                name = base;
            }
            return {ancestor, name, AppIdentity::Exe};
        }
        std::wstring up = parentDir(ancestor);
        if (up == ancestor || up.empty()) break;
        prev = ancestor;
        ancestor = up;
    }
    // ② 反查注册表 InstallLocation（取最长匹配，子目录归属最具体的软件）
    std::wstring lower;
    lower.reserve(fileDir.size());
    for (wchar_t c : fileDir) lower.push_back(static_cast<wchar_t>(::towlower(c)));
    size_t bestLen = 0;
    const InstalledProgram* best = nullptr;
    for (const auto& p : installedPrograms()) {
        if (p.name.empty()) continue;
        if (lower.size() >= p.dir.size()
            && lower.compare(0, p.dir.size(), p.dir) == 0
            && p.dir.size() > bestLen) {
            bestLen = p.dir.size();
            best = &p;
        }
    }
    if (best) {
        std::wstring root = best->dir;
        while (root.size() > 3 && root.back() == L'\\') root.pop_back();
        return {root, best->name, AppIdentity::Registry};
    }
    // ③ 兜底：最后一个有意义的目录段（跳过通用名和版本号）
    std::wstring seg = fileDir;
    std::wstring root = fileDir;
    for (int level = 0; level < 6 && !seg.empty(); ++level) {
        std::wstring cur = fileName(seg);
        if (cur.empty()) break;
        if (!isGenericSegment(cur) && !isVersionSegment(cur)) {
            return {root, cur, AppIdentity::PathSegment};
        }
        std::wstring up = parentDir(seg);
        if (up == seg || up.empty()) break;
        seg = up;
        root = up;
    }
    return {};
}

}} // namespace ac::path
