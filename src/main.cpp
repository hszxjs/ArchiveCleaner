#include "eui_neo.h"
#include "domain/AppModel.h"
#include "core/ArchiveFile.h"
#include "core/PathUtils.h"

#include <windows.h>
#include <shobjidl.h>
#include <shlwapi.h>
#include <string>
#include <cstdio>
#include <chrono>
#include <cmath>
#include <algorithm>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

// 文件夹选择对话框（必须在 namespace app 之前 include shobjidl，避免命名冲突）
static std::wstring pickFolder() {
    std::wstring result;
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool needUninit = (hr == S_OK || hr == S_FALSE);

    IFileOpenDialog* pDlg = nullptr;
    hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL, IID_IFileOpenDialog, (void**)&pDlg);
    if (SUCCEEDED(hr) && pDlg) {
        DWORD opts = 0;
        pDlg->GetOptions(&opts);
        pDlg->SetOptions(opts | FOS_PICKFOLDERS);
        hr = pDlg->Show(nullptr);
        if (SUCCEEDED(hr)) {
            IShellItem* pItem = nullptr;
            hr = pDlg->GetResult(&pItem);
            if (SUCCEEDED(hr) && pItem) {
                PWSTR path = nullptr;
                hr = pItem->GetDisplayName(SIGDN_FILESYSPATH, &path);
                if (SUCCEEDED(hr) && path) {
                    result = path;
                    CoTaskMemFree(path);
                }
                pItem->Release();
            }
        }
        pDlg->Release();
    }

    if (needUninit) CoUninitialize();
    return result;
}

// 请求管理员权限：用 ShellExecuteW runas 重启自身（UAC 弹窗）
// 返回 true 表示已发起提权重启（当前进程应退出）
static bool requestElevation() {
    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    HINSTANCE ret = ShellExecuteW(nullptr, L"runas", exePath, L"--elevated", nullptr, SW_SHOWNORMAL);
    return reinterpret_cast<INT_PTR>(ret) > 32;  // >32 表示成功
}

namespace app {

using ac::ArchiveFile;

// 全局应用模型（EUI-NEO 的 app/domain 状态：跨 compose 存活，后台线程访问）
static ac::AppModel g_model;

// UTF-16 → UTF-8（供 EUI-NEO 显示，EUI-NEO 的 text 用 UTF-8 std::string）
static std::string w2u(const std::wstring& s) {
    if (s.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string out(n > 0 ? n - 1 : 0, 0);
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, out.data(), n, nullptr, nullptr);
    return out;
}

// 页面状态（compose 间记住的 UI 态：输入框文本、滚动偏移、对话框开关等）
struct PageState {
    std::string pathInput;       // 路径输入框文本（UTF-8）
    bool showSettings = false;
    eui::Signal<float> scrollOffset{0.0f};  // 文件列表的滚动偏移
    int deleteSel = 0;          // 删除方式：0=回收站, 1=永久（不用 Signal）
    int engineSel = -1;         // 引擎选择：0=Everything, 1=Win32（fdfind/MFT 保留代码但不暴露）
    int themeSel = 0;           // 主题：0=夜间, 1=日间
    // 删除确认对话框
    eui::Signal<bool> deleteConfirmOpen{false};
    int deleteConfirmCount = 0;
    int64_t deleteConfirmSize = 0;
    bool deleteConfirmPermanent = false;
    // 删除结果对话框
    eui::Signal<bool> deleteResultOpen{false};
    int deleteResultSuccess = 0;
    int deleteResultFail = 0;
    bool deleteResultCancelled = false;
};

const DslAppConfig& dslAppConfig() {
    static const DslAppConfig config = DslAppConfig{}
        .title("ArchiveCleaner - 压缩包清理工具")
        .windowSize(960, 640)
        .fps(60.0)
        .iconPath("assets/icon.png");
    return config;
}

void compose(eui::Ui& ui, const eui::Screen& screen) {
    using namespace components;

    PageState* page = &ui.state<PageState>("page");
    auto& m = g_model;
    auto st = m.state.load();
    // 启动时路径输入框留空（显示 placeholder 欢迎语），不填上次路径
    // 初始化删除方式
    if (page->deleteSel == 0 && m.config.permanentDelete()) {
        page->deleteSel = 1;
    }
    // 初始化引擎选择（从 config 映射）
    if (page->engineSel < 0) {
        switch (m.config.searchEngine) {
            case ac::EngineType::Walk:       page->engineSel = 1; break;
            default:                         page->engineSel = 0; break;
        }
    }

    // 周期更新扫描已用时
    m.updateScanElapsed();

    // === 响应式缩放因子：所有尺寸基于此，窗口缩小时一切等比缩小，绝不换行绝不溢出 ===
    // 基准：窗口宽 960 时 scale=1.0；窗口宽 480 时 scale=0.5。最低限制 0.4。
    const float BASE_W = 960.0f;
    const float scale = std::clamp(screen.width / BASE_W, 0.4f, 1.5f);
    const float pad = 16.0f * scale;
    const float gap = 8.0f * scale;
    const float btnH = 32.0f * scale;
    const float fontSizeTitle = 22.0f * scale;
    const float fontSizeNormal = 14.0f * scale;
    const float fontSizeSmall = 12.0f * scale;
    const float fontSizeTiny = 11.0f * scale;
    const float rowH = 52.0f * scale;

    // 外层 stack：主界面 + dialogs
    // 日间/夜间主题：背景 rect 颜色随 themeSel 切换（zIndex 最低，作为底色）
    ui.stack("appRoot")
        .size(screen.width, screen.height)
        .content([&] {

    // 主题背景（夜间=深灰，日间=浅灰白）
    // 用 dirtyKey 强制颜色变化时重绘该 rect
    {
        bool lightMode = (page->themeSel == 1);
        ui.rect(lightMode ? "theme.bg.light" : "theme.bg.dark")
            .size(screen.width, screen.height)
            .color(lightMode ? theme::color(0.93f, 0.94f, 0.96f, 1.0f)
                             : theme::color(0.16f, 0.18f, 0.20f, 1.0f))
            .zIndex(0)
            .build();
    }

    // === 绝对布局：基于 screen 尺寸计算每个区域的 Y 坐标和高度 ===
    float curY = pad;
    const float contentW = screen.width - 2 * pad;

    // 主题 token：日间/夜间切换
    bool lightMode = (page->themeSel == 1);
    auto themeTokens = lightMode ? components::theme::light() : components::theme::dark();
    auto textColor = lightMode ? theme::color(0.1f, 0.1f, 0.12f, 1.0f)   // 近黑
                               : theme::color(0.92f, 0.93f, 0.95f, 1.0f); // 近白
    auto mutedColor = lightMode ? theme::color(0.35f, 0.37f, 0.40f, 1.0f)
                                : theme::color(0.65f, 0.67f, 0.70f, 1.0f);

    // --- 标题栏 ---
    ui.text("title")
        .position(pad, curY)
        .text("压缩包清理工具")
        .fontSize(fontSizeTitle)
        .color(textColor)
        .build();
    curY += fontSizeTitle + 1.5f * gap;

    // --- 控制区（标签+输入框+按钮 用 row；dropdown 单独绝对定位避免被 row 裁剪弹出列表）---
    {
        float btnScanW = 96.0f * scale;
        float browseW = 36.0f * scale;
        float labelW = 30.0f * scale;
        float inputW = std::max(60.0f * scale, contentW - labelW - btnScanW - browseW - 3 * gap);
        float x = pad;

        ui.text("path.label").position(x, curY + 8 * scale).text("路径").fontSize(fontSizeNormal)
            .color(textColor).build();
        x += labelW + gap;

        // 路径输入框
        components::input(ui, "path.input")
            .position(x, curY)
            .size(inputW, btnH)
            .value(page->pathInput)
            .placeholder("Hello ArchiveCleaner")
            .theme(themeTokens)
            .onChange([page](std::string v){ page->pathInput = std::move(v); })
            .build();
        x += inputW + gap;

        // 浏览按钮
        components::button(ui, "path.browse")
            .position(x, curY)
            .size(browseW, btnH)
            .fontSize(fontSizeNormal)
            .text("...")
            .theme(themeTokens, false)
            .onClick([&m, page]{
                std::wstring folder = pickFolder();
                if (!folder.empty()) {
                    page->pathInput = w2u(folder);
                    m.config.lastScanPath = folder;
                    m.saveConfig();
                }
            })
            .build();
        x += browseW + gap;

        bool scanning = ac::isScanning(st);
        components::button(ui, "scan.btn")
            .position(x, curY)
            .size(btnScanW, btnH).fontSize(fontSizeNormal)
            .text(scanning ? (st == ac::AppState::ScanCancelling ? "停止中..." : "停止扫描") : "开始扫描")
            .disabled(ac::isDeleting(st))
            .theme(themeTokens, true)
            .onClick([&m, page]{
                if (ac::isScanning(m.state)) {
                    m.cancelScan();
                } else {
                    page->scrollOffset.set(0.0f);
                    // 引擎映射：0=Everything, 1=Win32
                    if (page->engineSel == 1) {
                        m.config.searchEngine = ac::EngineType::Walk;
                    } else {
                        m.config.searchEngine = ac::EngineType::Everything;
                    }
                    std::wstring folder;
                    int n = MultiByteToWideChar(CP_UTF8, 0, page->pathInput.c_str(), -1, nullptr, 0);
                    if (n > 1) {
                        folder.resize(n - 1);
                        MultiByteToWideChar(CP_UTF8, 0, page->pathInput.c_str(), -1, folder.data(), n);
                    }
                    m.config.lastScanPath = folder;
                    m.saveConfig();
                    m.startScan(folder, m.config.includeSubfolders);
                }
            })
            .build();

        curY += btnH + gap;
    }

    // --- 第二行控制区：引擎选择 + 删除方式 + 主题切换 ---
    {
        float engW = contentW * 0.42f;
        float delW = 110.0f * scale;
        float themeW = 90.0f * scale;
        float ex = pad;

        // 引擎 segmented
        ui.stack("engine.wrap").position(ex, curY).size(engW, btnH).content([&]{
            components::segmented(ui, "engine")
                .size(engW, btnH)
                .fontSize(fontSizeSmall)
                .items({"Everything", "Win32"})
                .theme(themeTokens)
                .selected(page->engineSel)
                .onChange([page, &m](int v) {
                    if (ac::isScanning(m.state)) return;
                    page->engineSel = v;
                    app::requestUpdate();
                })
                .build();
        }).build();
        ex += engW + gap;

        // 删除方式 segmented
        ui.stack("delmode.wrap").position(ex, curY).size(delW, btnH).content([&]{
            components::segmented(ui, "delmode")
                .size(delW, btnH)
                .fontSize(fontSizeSmall)
                .items({"\xE5\x9B\x9E\xE6\x94\xB6\xE7\xAB\x99", "\xE6\xB0\xB8\xE4\xB9\x85"})
                .theme(themeTokens)
                .selected(page->deleteSel)
                .onChange([page](int v) {
                    page->deleteSel = v;
                    app::requestUpdate();
                })
                .build();
        }).build();
        ex += delW + gap;

        // 主题切换 segmented
        ui.stack("theme.wrap").position(ex, curY).size(themeW, btnH).content([&]{
            components::segmented(ui, "theme")
                .size(themeW, btnH)
                .fontSize(fontSizeSmall)
                .items({"\xE5\xA4\x9C\xE9\x97\xB4", "\xE6\x97\xA5\xE9\x97\xB4"})
                .theme(themeTokens)
                .selected(page->themeSel)
                .onChange([page](int v) {
                    page->themeSel = v;
                    app::requestUpdate();
                })
                .build();
        }).build();

        curY += btnH + gap;
    }

    // --- 进度区（扫描中或完成时显示）---
    if (ac::isScanning(st)) {
        int d = m.dirsScanned.load();
        int f = m.filesFound.load();
        int64_t ms = m.scanElapsedMs.load();
        std::string msg = "搜索中 · " + std::to_string(f) + " 个文件 · 已用 " + std::to_string(ms / 1000) + "秒";
        if (m.scanDegraded) {
            msg += "（已降级到 Win32）";
            if (!m.scanDegradeReason.empty()) {
                msg += "：" + w2u(m.scanDegradeReason);
            }
        }
        ui.text("scan.progress").position(pad, curY).text(msg).fontSize(fontSizeSmall)
            .color(mutedColor).build();
        curY += fontSizeSmall + gap;
        float fullTimeMs = m.scanDegraded ? 60000.0f : 3000.0f;
        float v = std::clamp(static_cast<float>(ms) / fullTimeMs, 0.0f, 0.95f);
        ui.stack("scan.bar.wrap").position(pad, curY).size(contentW, 6.0f * scale).content([&]{
            components::progress(ui, "scan.bar")
                .size(contentW, 6.0f * scale)
                .value(v)
                .theme(themeTokens)
                .build();
        }).build();
        curY += 6.0f * scale + gap;
    } else if (st == ac::AppState::Idle && !m.files.empty()) {
        int64_t ms = m.scanElapsedMs.load();
        std::string msg = "扫描完成 · 共 " + std::to_string((int)m.files.size()) + " 个 · 耗时 " + std::to_string(ms / 1000) + "秒";
        ui.text("scan.done").position(pad, curY).text(msg).fontSize(fontSizeSmall)
            .color(mutedColor).build();
        curY += fontSizeSmall + gap;
    }

    // --- 文件列表（虚拟列表）---
    int selCount = m.selectedCount();
    bool hasFiles = !m.files.empty();
    bool deleting = ac::isDeleting(st);
    // 底部区域高度（按钮行 + 状态栏）
    float bottomH = btnH + fontSizeSmall + 2 * gap;
    float listH = std::max(60.0f, screen.height - curY - bottomH - pad);
    int64_t itemCount = 0;
    {
        std::lock_guard<std::mutex> lk(m.filesMutex);
        itemCount = static_cast<int64_t>(m.files.size());
    }

    // virtualList 不支持 position，用外层 stack 定位
    ui.stack("fileList.wrap").position(pad, curY).size(contentW, listH).content([&]{
    components::virtualList(ui, "fileList")
        .size(contentW, listH)
        .itemCount(itemCount)
        .rowHeight(rowH)
        .bind(page->scrollOffset)
        .row([&m, fontSizeNormal, fontSizeTiny, scale, rowH, lightMode](eui::Ui& ui, const std::string& rowId, int64_t index, float w, float h) {
            ArchiveFile af;
            bool selected = false;
            bool failed = false;
            {
                std::lock_guard<std::mutex> lk(m.filesMutex);
                if (index < 0 || index >= (int64_t)m.files.size()) return;
                af = m.files[index];
                selected = m.selectedPaths.count(af.path) > 0;
                failed = m.failedPaths.count(af.path) > 0;
            }
            auto rowTheme = lightMode ? components::theme::light() : components::theme::dark();
            auto nameColor = lightMode ? theme::color(0.1f, 0.1f, 0.12f, 1.0f)
                                       : theme::color(0.92f, 0.93f, 0.95f, 1.0f);
            auto pathColor = lightMode ? theme::color(0.40f, 0.42f, 0.45f, 1.0f)
                                       : theme::color(0.60f, 0.62f, 0.65f, 1.0f);
            ui.row(rowId).size(w, h).padding(8.0f * scale, 4.0f * scale).gap(10.0f * scale).content([&]{
                components::checkbox(ui, rowId + ".chk")
                    .checked(selected)
                    .theme(rowTheme)
                    .onChange([&m, path = af.path](bool v){ m.toggleSelect(path); })
                    .build();
                ui.column(rowId + ".info").gap(2.0f * scale).content([&]{
                    std::string title = w2u(af.name) + "  ·  " + ac::sizeHuman(af.size);
                    if (failed) title = "[失败] " + title;
                    ui.text(rowId + ".name").text(title).fontSize(fontSizeNormal)
                        .color(nameColor).build();
                    ui.text(rowId + ".path").text(w2u(af.path)).fontSize(fontSizeTiny)
                        .color(pathColor).build();
                }).build();
            }).build();
        })
        .build();
    }).build();  // fileList.wrap stack 结束
    curY += listH + gap;

    // --- 底部按钮区 ---
    if (hasFiles || deleting) {
        float selBtnW = 72.0f * scale;
        float delBtnW = 130.0f * scale;
        float bx = pad;
        if (deleting) {
            components::button(ui, "cancel.del")
                .position(bx, curY)
                .size(delBtnW, btnH).fontSize(fontSizeNormal)
                .text(st == ac::AppState::DeleteCancelling ? "停止中..." : "取消删除")
                .disabled(st == ac::AppState::DeleteCancelling)
                .theme(themeTokens, false)
                .onClick([&m]{ m.cancelDelete(); })
                .build();
        } else {
            components::button(ui, "sel.all").position(bx, curY).size(selBtnW, btnH).fontSize(fontSizeNormal)
                .text("全选").disabled(ac::isBusy(st)).theme(themeTokens, false)
                .onClick([&m]{ m.selectAll(); }).build();
            bx += selBtnW + gap;
            components::button(ui, "sel.none").position(bx, curY).size(selBtnW, btnH).fontSize(fontSizeNormal)
                .text("全不选").disabled(ac::isBusy(st)).theme(themeTokens, false)
                .onClick([&m]{ m.selectNone(); }).build();
            bx += selBtnW + gap;
            components::button(ui, "sel.inv").position(bx, curY).size(selBtnW, btnH).fontSize(fontSizeNormal)
                .text("反选").disabled(ac::isBusy(st)).theme(themeTokens, false)
                .onClick([&m]{ m.invertSelection(); }).build();

            if (selCount > 0) {
                float dx = pad + contentW - delBtnW;
                std::string delText = "删除选中(" + std::to_string(selCount) + "个)";
                components::button(ui, "del.btn").position(dx, curY).size(delBtnW, btnH).fontSize(fontSizeNormal)
                    .text(delText).disabled(ac::isBusy(st))
                    .theme(themeTokens, true)
                    .onClick([&m, page, selCount]{
                        page->deleteConfirmCount = selCount;
                        page->deleteConfirmSize = m.selectedTotalSize();
                        page->deleteConfirmPermanent = (page->deleteSel == 1);
                        m.saveConfig();
                        page->deleteConfirmOpen.set(true);
                    })
                    .build();
            }
        }
        curY += btnH + gap;
    }

    // --- 状态栏 ---
    {
        int total = (int)m.files.size();
        int sel = m.selectedCount();
        int64_t selSize = m.selectedTotalSize();
        std::string s = "扫描到 " + std::to_string(total) + " · 选中 " + std::to_string(sel) + " · 大小 " + ac::sizeHuman(selSize);
        if (ac::isDeleting(st)) {
            s = "删除中 · " + std::to_string(m.deleteDone.load()) + "/" + std::to_string(m.deleteTotal.load())
              + " · 成功 " + std::to_string(m.deleteSuccess.load()) + " 失败 " + std::to_string(m.deleteFail.load());
        }
        ui.text("status").position(pad, curY).text(s).fontSize(fontSizeSmall)
            .color(mutedColor).build();
    }

        // === 对话框（必须在 root column 外部，作为 appRoot stack 的兄弟节点）===

        // 删除确认对话框
        components::dialog(ui, "delConfirm")
            .bindOpen(page->deleteConfirmOpen)
            .screen(screen.width, screen.height)
            .size(400.0f * scale, 220.0f * scale)
            .title(page->deleteConfirmPermanent ? "确认永久删除" : "确认删除")
            .message(std::string("即将") + (page->deleteConfirmPermanent ? "永久删除" : "送入回收站")
                     + " " + std::to_string(page->deleteConfirmCount) + " 个文件（"
                     + ac::sizeHuman(page->deleteConfirmSize) + "），确定继续吗？"
                     + (page->deleteConfirmPermanent ? "\n\n永久删除不可恢复！" : ""))
            .primaryText(page->deleteConfirmPermanent ? "永久删除" : "删除")
            .secondaryText("取消")
            .onPrimary([&m, page]{
                page->deleteConfirmOpen.set(false);
                m.startDelete(page->deleteConfirmPermanent, [page](const std::vector<ac::DeleteResult>& results, bool cancelled){
                    int ok = 0, fail = 0;
                    for (const auto& r : results) { if (r.ok) ++ok; else ++fail; }
                    page->deleteResultSuccess = ok;
                    page->deleteResultFail = fail;
                    page->deleteResultCancelled = cancelled;
                    page->deleteResultOpen.set(true);
                });
            })
            .build();

        // 删除结果对话框（只有一个"确定"按钮，点它关闭）
        components::dialog(ui, "delResult")
            .bindOpen(page->deleteResultOpen)
            .screen(screen.width, screen.height)
            .size(380.0f * scale, 200.0f * scale)
            .title("删除完成")
            .message(std::string(page->deleteResultCancelled ? "已取消 · " : "")
                     + "成功 " + std::to_string(page->deleteResultSuccess) + " 个"
                     + (page->deleteResultFail > 0 ? " · 失败 " + std::to_string(page->deleteResultFail) + " 个" : "")
                     + (page->deleteResultFail > 0 ? "\n\n失败文件已标红，可查看占用进程后重试。" : ""))
            .primaryText("确定")
            .onPrimary([page]{ page->deleteResultOpen.set(false); })
            .build();

    })  // appRoot stack content 结束
    .build();  // appRoot stack 结束
}

} // namespace app
