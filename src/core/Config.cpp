#include "Config.h"
#include "PathUtils.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <fstream>
#include <sstream>
#include <string>

namespace ac {

namespace {
// === 极简 JSON 读写（字段少且固定，手写避免引入 json 库）===
// 只支持本项目 config.json 的扁平 key:value 结构。

// 从 JSON 文本提取字符串字段的值（"key": "value" → value，unescape 基本的）
std::string extractString(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\"";
    size_t k = json.find(needle);
    if (k == std::string::npos) return {};
    size_t colon = json.find(':', k + needle.size());
    if (colon == std::string::npos) return {};
    size_t q1 = json.find('"', colon + 1);
    if (q1 == std::string::npos) return {};
    std::string out;
    size_t i = q1 + 1;
    while (i < json.size() && json[i] != '"') {
        if (json[i] == '\\' && i + 1 < json.size()) {
            char c = json[i + 1];
            if (c == 'n') out += '\n';
            else if (c == 't') out += '\t';
            else if (c == '\\') out += '\\';
            else if (c == '"') out += '"';
            else out += c;
            i += 2;
        } else {
            out += json[i];
            ++i;
        }
    }
    return out;
}

// 提取布尔字段
bool extractBool(const std::string& json, const std::string& key, bool def) {
    std::string needle = "\"" + key + "\"";
    size_t k = json.find(needle);
    if (k == std::string::npos) return def;
    size_t colon = json.find(':', k + needle.size());
    if (colon == std::string::npos) return def;
    size_t i = colon + 1;
    while (i < json.size() && (json[i] == ' ' || json[i] == '\t')) ++i;
    if (i + 4 <= json.size() && json.compare(i, 4, "true") == 0) return true;
    if (i + 5 <= json.size() && json.compare(i, 5, "false") == 0) return false;
    return def;
}

// 转义字符串为 JSON 字符串字面量（不含外层引号）
std::string escapeJson(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"':  out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\t': out += "\\t"; break;
            default:   out += c;
        }
    }
    return out;
}

// UTF-8 字符串 ↔ UTF-16 wstring
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
} // namespace

std::wstring Config::configFile() {
    return path::appDir() + L"\\config.json";
}

std::wstring Config::deleteLogFile() {
    return path::appDir() + L"\\delete_log.txt";
}

Config Config::load() {
    Config c;  // 默认值
    std::wstring path = configFile();
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return c;
    std::stringstream ss;
    ss << f.rdbuf();
    std::string json = ss.str();

    c.searchEngine      = engineFromKey(extractString(json, "search_engine"));
    c.fdPath            = utf8ToW(extractString(json, "fd_path"));
    c.esPath            = utf8ToW(extractString(json, "es_path"));
    {
        std::string a = extractString(json, "appearance");
        if (!a.empty()) c.appearance = a;
    }
    {
        std::string ac_ = extractString(json, "accent_color");
        if (!ac_.empty()) c.accentColor = ac_;
    }
    {
        std::string d = extractString(json, "delete_mode");
        if (!d.empty()) c.deleteMode = d;
    }
    c.includeSubfolders = extractBool(json, "include_subfolders", true);
    c.lastScanPath      = utf8ToW(extractString(json, "last_scan_path"));
    c.protectProgramDirs = extractBool(json, "protect_program_dirs", true);
    // 自定义保护模式：简化解析 "custom_protected": ["\\path1\\", "\\path2\\"]
    {
        std::string needle = "\"custom_protected\"";
        size_t k = json.find(needle);
        if (k != std::string::npos) {
            size_t open = json.find('[', k);
            size_t close = json.find(']', open);
            if (open != std::string::npos && close != std::string::npos) {
                std::string arr = json.substr(open + 1, close - open - 1);
                size_t pos = 0;
                while (pos < arr.size()) {
                    size_t q1 = arr.find('"', pos);
                    if (q1 == std::string::npos) break;
                    size_t q2 = arr.find('"', q1 + 1);
                    if (q2 == std::string::npos) break;
                    std::string item = arr.substr(q1 + 1, q2 - q1 - 1);
                    if (!item.empty()) {
                        // 转小写
                        for (auto& ch : item) ch = static_cast<char>(::tolower(static_cast<unsigned char>(ch)));
                        c.customProtectedPatterns.push_back(utf8ToW(item));
                    }
                    pos = q2 + 1;
                }
            }
        }
    }
    return c;
}

bool Config::save() const {
    std::wstring path = configFile();
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f.is_open()) return false;
    f << "{\n";
    f << "    \"search_engine\": \""      << engineKey(searchEngine) << "\",\n";
    f << "    \"fd_path\": \""            << escapeJson(wToUtf8(fdPath)) << "\",\n";
    f << "    \"es_path\": \""            << escapeJson(wToUtf8(esPath)) << "\",\n";
    f << "    \"appearance\": \""         << escapeJson(appearance) << "\",\n";
    f << "    \"accent_color\": \""       << escapeJson(accentColor) << "\",\n";
    f << "    \"delete_mode\": \""        << escapeJson(deleteMode) << "\",\n";
    f << "    \"include_subfolders\": "   << (includeSubfolders ? "true" : "false") << ",\n";
    f << "    \"protect_program_dirs\": " << (protectProgramDirs ? "true" : "false") << ",\n";
    f << "    \"custom_protected\": [";
    for (size_t i = 0; i < customProtectedPatterns.size(); ++i) {
        if (i) f << ", ";
        f << "\"" << escapeJson(wToUtf8(customProtectedPatterns[i])) << "\"";
    }
    f << "],\n";
    f << "    \"last_scan_path\": \""     << escapeJson(wToUtf8(lastScanPath)) << "\"\n";
    f << "}\n";
    return static_cast<bool>(f);
}

} // namespace ac
