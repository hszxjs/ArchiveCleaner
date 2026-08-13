// 删除功能验证（去 Qt 化的 Deleter）
#include "core/ArchiveFile.h"
#include "core/Deleter.h"
#include "core/PathUtils.h"

#include <cstdio>
#include <atomic>
#include <windows.h>
#include <string>
#include <string.h>

int main(int argc, char* argv[]) {
    SetConsoleOutputCP(CP_UTF8);
    using namespace ac;
    bool permanent = (argc > 1 && std::string(argv[1]) == "permanent");

    // 创建测试文件
    std::wstring dir = path::appDir() + L"\\deltest";
    CreateDirectoryW(dir.c_str(), nullptr);
    auto mk = [&](const std::wstring& name) {
        std::wstring p = dir + L"\\" + name;
        HANDLE h = CreateFileW(p.c_str(), GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE) { const char* d = "fake"; DWORD w; WriteFile(h, d, 4, &w, nullptr); CloseHandle(h); }
        return p;
    };

    std::vector<ArchiveFile> files;
    for (const wchar_t* n : {L"a.zip", L"b.rar", L"c.7z"}) {
        ArchiveFile af;
        af.path = path::normalize(mk(n));
        af.name = n;
        af.size = 4;
        files.push_back(af);
    }

    auto w2u = [](const std::wstring& s) -> std::string {
        if (s.empty()) return {};
        int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, nullptr, 0, nullptr, nullptr);
        std::string out(n > 0 ? n - 1 : 0, 0);
        if (n > 1) WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, out.data(), n, nullptr, nullptr);
        return out;
    };

    printf("=== %s ===\n", permanent ? "永久删除测试" : "回收站删除测试");
    printf("待删: %d 个\n", (int)files.size());

    std::atomic<bool> cancel{false};
    Deleter d;
    d.setPermanent(permanent);
    d.setTimeoutSec(15);
    auto results = d.run(files, cancel,
        [&](int done, int total, const std::wstring& p, int64_t){
            printf("  进度: %d/%d %s\n", done+1, total, w2u(path::fileName(p)).c_str());
        });

    int ok = 0, fail = 0;
    for (const auto& r : results) {
        printf("  %s: ok=%d %s\n", w2u(path::fileName(r.path)).c_str(), r.ok,
               r.ok ? "" : w2u(r.reason).c_str());
        if (r.ok) ++ok; else ++fail;
    }
    printf("汇总: 成功 %d 失败 %d\n", ok, fail);

    printf("\n=== 验证文件存在性 ===\n");
    for (const wchar_t* n : {L"a.zip", L"b.rar", L"c.7z"}) {
        std::wstring p = dir + L"\\" + n;
        printf("  %s: %s\n", w2u(n).c_str(), path::exists(p) ? "仍存在" : "已删除");
    }

    // 清理测试目录
    RemoveDirectoryW(dir.c_str());
    return 0;
}
