#ifndef AC_ARCHIVE_FILE_H
#define AC_ARCHIVE_FILE_H

#include <string>
#include <cstdint>
#include <cwctype>
#include <vector>
#include <set>

namespace ac {

// 保护目录模式（小写，两侧带反斜杠匹配路径段）。
// 这些目录下的压缩格式文件是程序/游戏资源（mods、材质包、Steam 游戏文件等），
// 扫描时跳过，防止误删导致游戏/程序损坏。
inline const std::vector<std::wstring>& protectedDirPatterns() {
    static const std::vector<std::wstring> patterns = {
        // 游戏
        L"\\.minecraft\\",       // Minecraft 本体
        L"\\mods\\",             // 通用 mod 目录（Minecraft/Terraria 等）
        L"\\mod\\",
        L"\\resourcepacks\\",    // Minecraft 材质包
        L"\\shaderpacks\\",      // Minecraft 光影包
        L"\\texturepacks\\",     // 旧版材质包
        L"\\steamapps\\",        // Steam 游戏库
        // 程序安装位置
        L"\\program files\\",    // 已安装程序
        L"\\program files (x86)\\",
        L"\\windows\\",          // 系统目录
        // 开发依赖（无 exe、无注册表，其他层覆盖不到）
        L"\\node_modules\\",     // npm 依赖（大量 .tgz）
        L"\\.m2\\",              // Maven 本地仓库（大量 .jar）
        L"\\.gradle\\",          // Gradle 缓存
        L"\\.nuget\\",           // NuGet 包
        L"\\site-packages\\",    // Python 安装包
        L"\\venv\\",             // Python 虚拟环境
        L"\\.venv\\",
        L"\\vendor\\",           // 项目第三方依赖（PHP/Go 等）
        L"\\third_party\\",      // 项目内嵌依赖源码
        L"\\3rdparty\\",
        L"\\__pycache__\\",      // Python 编译缓存
    };
    return patterns;
}

// 判断路径是否在保护目录内（大小写不敏感的子串匹配）。
// extraPatterns：用户自定义的额外模式（来自 config）。
inline bool isProtectedPath(const std::wstring& normalizedPath,
                            const std::vector<std::wstring>& extraPatterns = {}) {
    // 路径转小写（只做一次）
    std::wstring lower;
    lower.reserve(normalizedPath.size());
    for (wchar_t c : normalizedPath) {
        lower.push_back(static_cast<wchar_t>(::towlower(c)));
    }
    // 确保末尾有反斜杠（匹配目录段需要，如 path\mods\file → 已含 \mods\）
    for (const auto& p : protectedDirPatterns()) {
        if (lower.find(p) != std::wstring::npos) return true;
    }
    for (const auto& p : extraPatterns) {
        if (p.empty()) continue;
        if (lower.find(p) != std::wstring::npos) return true;
    }
    return false;
}

// === 扫描格式开关（用户可在"扫描目标"弹窗中勾选，进程级全局，启动时从 Config 加载）===
// 存"被禁用"的 key（空 = 全部启用）。key = 小写扩展名，或分卷组键：
//   "zvol" = ZIP 分卷 .z01-.z99；"rvol" = RAR 分卷 .r00-.r99；"nvol" = 数字序号分卷 xxx.zip.001
inline std::set<std::string>& scanDisabledKeys() {
    static std::set<std::string> s;
    return s;
}
inline bool scanKeyEnabled(const std::string& key) {
    return scanDisabledKeys().find(key) == scanDisabledKeys().end();
}
// 扩展名 → 开关 key（分卷扩展名归并到组键）
inline std::string scanKeyOf(const std::string& lowerExt) {
    auto isDigits = [](const std::string& s) {
        for (char c : s) if (c < '0' || c > '9') return false;
        return true;
    };
    if (lowerExt.size() == 3 && lowerExt[0] == 'z' && isDigits(lowerExt.substr(1))) return "zvol";
    if (lowerExt.size() == 3 && lowerExt[0] == 'r' && isDigits(lowerExt.substr(1))) return "rvol";
    return lowerExt;
}
inline bool extScanEnabled(const std::string& lowerExt) {
    return scanKeyEnabled(scanKeyOf(lowerExt));
}

// 压缩包扩展名白名单（不含点，全小写）。
inline const std::vector<std::string>& archiveExts() {
    static const std::vector<std::string> exts = {
        // 标准压缩格式
        "zip", "rar", "7z", "tar", "gz", "bz2", "tgz", "xz", "iso", "cab",
        "z", "lz", "lzma", "tbz2", "zst", "lz4", "br", "bz",
        // 封装/打包格式
        "jar", "war", "ear", "apk", "xapk", "wim", "rpm", "deb",
        "arj", "lzh", "ace", "arc", "pak", "cpio", "squashfs",
        // ZIP 分卷（.z01-.z99）
        "z01", "z02", "z03", "z04", "z05", "z06", "z07", "z08", "z09",
        "z10", "z11", "z12", "z13", "z14", "z15", "z16", "z17", "z18", "z19",
        "z20", "z21", "z22", "z23", "z24", "z25", "z26", "z27", "z28", "z29",
        "z30", "z31", "z32", "z33", "z34", "z35", "z36", "z37", "z38", "z39",
        "z40", "z41", "z42", "z43", "z44", "z45", "z46", "z47", "z48", "z49",
        "z50", "z51", "z52", "z53", "z54", "z55", "z56", "z57", "z58", "z59",
        "z60", "z61", "z62", "z63", "z64", "z65", "z66", "z67", "z68", "z69",
        "z70", "z71", "z72", "z73", "z74", "z75", "z76", "z77", "z78", "z79",
        "z80", "z81", "z82", "z83", "z84", "z85", "z86", "z87", "z88", "z89",
        "z90", "z91", "z92", "z93", "z94", "z95", "z96", "z97", "z98", "z99",
        // RAR 旧分卷（.r00-.r99）
        "r00", "r01", "r02", "r03", "r04", "r05", "r06", "r07", "r08", "r09",
        "r10", "r11", "r12", "r13", "r14", "r15", "r16", "r17", "r18", "r19",
        "r20", "r21", "r22", "r23", "r24", "r25", "r26", "r27", "r28", "r29",
        "r30", "r31", "r32", "r33", "r34", "r35", "r36", "r37", "r38", "r39",
        "r40", "r41", "r42", "r43", "r44", "r45", "r46", "r47", "r48", "r49",
        "r50", "r51", "r52", "r53", "r54", "r55", "r56", "r57", "r58", "r59",
        "r60", "r61", "r62", "r63", "r64", "r65", "r66", "r67", "r68", "r69",
        "r70", "r71", "r72", "r73", "r74", "r75", "r76", "r77", "r78", "r79",
        "r80", "r81", "r82", "r83", "r84", "r85", "r86", "r87", "r88", "r89",
        "r90", "r91", "r92", "r93", "r94", "r95", "r96", "r97", "r98", "r99"
    };
    return exts;
}

// 单个压缩包文件的数据载体（纯数据，无 UI 态）。
// ⚠️ 与 Qt 版的区别：去掉了 selected/failed 字段（它们是 UI 态，
//   应由 domain 层的 selectedPaths/failedPaths 集合管理，不污染数据结构）。
struct ArchiveFile {
    std::wstring path;       // 已归一化的绝对路径（反斜杠）
    std::wstring name;       // 文件名（含扩展名）
    int64_t size = 0;        // 字节数
    int64_t mtimeMs = 0;     // 修改时间，Unix 毫秒（1970-01-01 至今的毫秒数）

    // 唯一标识（用于 selectedPaths/failedPaths 的 key）
    const std::wstring& id() const { return path; }

    // 扩展名（不含点，小写）。从 name 提取。
    std::string ext() const {
        size_t i = name.find_last_of(L'.');
        if (i == std::wstring::npos) return {};
        std::wstring we = name.substr(i + 1);
        // 扩展名假定 ASCII，逐字节窄化（避免 wstring→string 迭代器构造的 wchar→char 截断警告）
        std::string e;
        e.reserve(we.size());
        for (wchar_t c : we) e.push_back(static_cast<char>(static_cast<unsigned char>(c & 0xFF)));
        return e;
    }

    // 是否是压缩包扩展名（从任意文件名判断，用于扫描时过滤）
    // 处理三类：
    // 1. 标准扩展名（.zip .rar .7z ...）
    // 2. 分卷专用扩展名（.z01-.z99 .r00-.r99）
    // 3. 数字序号分卷（.001-.999）—— 仅当倒数第二段扩展名是压缩格式时才算
    //    （如 xxx.zip.001 算，但 photo.001 不算）
    static bool isArchiveExt(const std::wstring& fileName) {
        // 取最后一个扩展名
        size_t i = fileName.find_last_of(L'.');
        if (i == std::wstring::npos) return false;
        std::string last = toLowerAscii(fileName.substr(i + 1));

        // 快速路径：标准扩展名或分卷专用扩展名直接查表（同时受用户格式开关约束）
        for (const auto& x : archiveExts()) {
            if (last == x) return extScanEnabled(last);
        }

        // 慢路径：数字序号（如 "001"），检查倒数第二段是否是压缩格式（独立开关）
        if (last.size() == 3 && last[0] >= '0' && last[0] <= '9'
            && last[1] >= '0' && last[1] <= '9' && last[2] >= '0' && last[2] <= '9') {
            if (!scanKeyEnabled("nvol")) return false;
            std::wstring base = fileName.substr(0, i);
            size_t j = base.find_last_of(L'.');
            if (j == std::wstring::npos) return false;
            std::string second = toLowerAscii(base.substr(j + 1));
            // 倒数第二段必须是标准压缩扩展名（不含分卷扩展名，避免 .z01.001 这种嵌套）
            static const std::vector<std::string> baseExts = {
                "zip", "rar", "7z", "tar", "gz", "bz2", "xz", "zst", "lz4", "br"
            };
            for (const auto& x : baseExts) {
                if (second == x) return true;
            }
        }
        return false;
    }

    // 辅助：wstring 扩展名转小写 ASCII string
    static std::string toLowerAscii(const std::wstring& we) {
        std::string e;
        e.reserve(we.size());
        for (wchar_t wc : we) {
            e.push_back(static_cast<char>(::tolower(static_cast<unsigned char>(wc & 0xFF))));
        }
        return e;
    }
};

// FILETIME → Unix 毫秒。
// ⚠️ 修复 Qt 版 D.9 的运算符优先级 bug：
//   Qt 版写的是 (high<<32 | low - epoch)，因 | 优先级低于 -，实际算成 high<<32 | (low-epoch)。
//   正确写法是先拼 64 位再减 epoch。
inline int64_t filetimeToUnixMs(uint32_t low, uint32_t high) {
    const uint64_t FILETIME_PER_MS = 10000ULL;
    const uint64_t EPOCH_DIFF_100NS = 116444736000000000ULL;  // 1601→1970 的 100ns 数
    uint64_t ft = (static_cast<uint64_t>(high) << 32) | low;  // 先拼完整 64 位
    return static_cast<int64_t>((ft - EPOCH_DIFF_100NS) / FILETIME_PER_MS);  // 再减 epoch
}

// 人类可读的大小（KB/MB/GB）。返回 UTF-8 字符串供 EUI-NEO 显示。
inline std::string sizeHuman(int64_t bytes) {
    const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    int u = 0;
    double s = static_cast<double>(bytes);
    while (s >= 1024.0 && u < 4) { s /= 1024.0; ++u; }
    char buf[32];
    if (u == 0) {
        std::snprintf(buf, sizeof(buf), "%lld B", static_cast<long long>(bytes));
    } else {
        std::snprintf(buf, sizeof(buf), "%.1f %s", s, units[u]);
    }
    return buf;
}

} // namespace ac

#endif // AC_ARCHIVE_FILE_H
