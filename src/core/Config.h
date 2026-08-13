#ifndef AC_CONFIG_H
#define AC_CONFIG_H

#include "ISearchEngine.h"
#include <string>

namespace ac {

// 配置项。对应 exe 同目录的 config.json。
// 字段沿用旧版结构（UTF-8）。
struct Config {
    EngineType searchEngine = EngineType::Everything;  // 默认 Everything（秒搜），不可用时自动降级到 Walk
    std::wstring fdPath = L".\\fd.exe";
    std::wstring esPath = L".\\es.exe";
    // 外观主题：系统 / 浅色 / 深色（UTF-8 存中文）
    std::string appearance = "\xE7\xB3\xBB\xE7\xBB\x9F";  // "系统"
    std::string accentColor = "#2563EB";
    // 删除方式（中文，UTF-8）
    std::string deleteMode = "\xE9\x80\x81\xE5\x85\xA5\xE5\x9B\x9E\xE6\x94\xB6\xE7\xAB\x99\xEF\xBC\x88\xE5\x8F\xAF\xE6\x81\xA2\xE5\xA4\x8D\xEF\xBC\x89"; // "送入回收站（可恢复）"
    bool includeSubfolders = true;
    std::wstring lastScanPath;

    bool permanentDelete() const {
        // deleteMode 以"永久删除"开头则永久
        return deleteMode.rfind("\xE6\xB0\xB8\xE4\xB9\x85\xE5\x88\xA0\xE9\x99\xA4", 0) == 0;  // "永久删除"
    }

    static Config load();
    bool save() const;

    static std::wstring configFile();
    static std::wstring deleteLogFile();
};

} // namespace ac

#endif // AC_CONFIG_H
