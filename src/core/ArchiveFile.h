#ifndef AC_ARCHIVE_FILE_H
#define AC_ARCHIVE_FILE_H

#include <string>
#include <cstdint>
#include <vector>

namespace ac {

// 压缩包扩展名白名单（14 种，不含点，全小写）。
// inline 函数返回引用，避免全局变量的初始化顺序问题。
inline const std::vector<std::string>& archiveExts() {
    static const std::vector<std::string> exts = {
        "zip", "rar", "7z", "tar", "gz", "bz2", "tgz",
        "xz", "iso", "cab", "z", "lz", "lzma", "tbz2"
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
    static bool isArchiveExt(const std::wstring& fileName) {
        size_t i = fileName.find_last_of(L'.');
        if (i == std::wstring::npos) return false;
        std::wstring we = fileName.substr(i + 1);
        // 逐字节窄化 + 转小写（扩展名假定 ASCII）
        std::string e;
        e.reserve(we.size());
        for (wchar_t wc : we) {
            e.push_back(static_cast<char>(::tolower(static_cast<unsigned char>(wc & 0xFF))));
        }
        for (const auto& x : archiveExts()) {
            if (e == x) return true;
        }
        return false;
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
