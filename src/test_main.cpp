// Core 层验证程序（命令行）。
// 阶段 1-5 的去 Qt 化验证：扫描 + 删除 + 占用检测 + 配置读写。
#include "core/ISearchEngine.h"
#include "core/Config.h"
#include "core/Deleter.h"
#include "core/PathUtils.h"
#include "core/ArchiveFile.h"

#include <cstdio>
#include <atomic>
#include <chrono>
#include <windows.h>
#include <string>

// 宽字符串 ↔ 控制台输出（用 UTF-8 + printf）
std::string wToUtf8(const std::wstring& s) {
    if (s.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string out(n > 0 ? n - 1 : 0, 0);
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, out.data(), n, nullptr, nullptr);
    return out;
}

int main(int argc, char* argv[]) {
    SetConsoleOutputCP(CP_UTF8);
    std::string folderUtf8 = argc > 1 ? argv[1] : "A:\\ArchiveCleanerQt\\dist";
    std::string engineName = argc > 2 ? argv[2] : "walk";

    // UTF-8 → UTF-16
    int n = MultiByteToWideChar(CP_UTF8, 0, folderUtf8.c_str(), -1, nullptr, 0);
    std::wstring folder(n > 0 ? n - 1 : 0, 0);
    if (n > 1) MultiByteToWideChar(CP_UTF8, 0, folderUtf8.c_str(), -1, folder.data(), n);

    printf("扫描目录: %s  引擎: %s\n", folderUtf8.c_str(), engineName.c_str());

    ac::Config cfg;
    cfg.esPath = L"A:\\ArchiveCleanerQt\\dist\\es.exe";
    cfg.fdPath = L"A:\\ArchiveCleanerQt\\dist\\fd.exe";

    ac::EngineType type = ac::EngineType::Walk;
    if (engineName == "everything") type = ac::EngineType::Everything;
    else if (engineName == "fdfind") type = ac::EngineType::Fdfind;
    else if (engineName == "mft")    type = ac::EngineType::Mft;

    auto engine = ac::createEngine(type, cfg);
    printf("引擎可用: %s\n", engine->isAvailable() ? "是" : "否");
    if (!engine->unavailableReason().empty()) {
        printf("不可用原因: %s\n", wToUtf8(engine->unavailableReason()).c_str());
    }

    std::atomic<bool> cancel{false};
    int count = 0;
    auto start = std::chrono::steady_clock::now();
    auto r = engine->run(folder, true, cancel,
        [&count](const ac::ArchiveFile& af){
            if (count < 5) {
                printf("  找到: %s  %s\n", wToUtf8(af.name).c_str(), ac::sizeHuman(af.size).c_str());
            }
            count++;
        },
        [&start](int d, int f){
            if (f % 100 == 0 || f < 3) {
                auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - start).count();
                printf("  进度: 目录=%d 文件=%d 耗时=%lldms\n", d, f, static_cast<long long>(ms));
            }
        });

    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();
    printf("结果: %s  %s\n", r.first ? "成功" : "失败", wToUtf8(r.second).c_str());
    printf("总计找到: %d 个, 耗时 %lld ms\n", count, static_cast<long long>(ms));

    // Config 读写测试
    printf("\n=== Config 测试 ===\n");
    auto loaded = ac::Config::load();
    printf("search_engine=%s fd_path=%s es_path=%s\n",
           ac::engineKey(loaded.searchEngine).c_str(),
           wToUtf8(loaded.fdPath).c_str(),
           wToUtf8(loaded.esPath).c_str());

    return 0;
}
