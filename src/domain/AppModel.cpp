#include "AppModel.h"
#include "core/ISearchEngine.h"
#include "core/Deleter.h"      // DeleteResult + Deleter
#include "core/PathUtils.h"

#include "eui/app.h"     // app::requestUpdate
#include "eui/async.h"   // app::async::restart/cancel

#include <algorithm>

namespace ac {

AppModel::AppModel() {
    loadConfig();
}

void AppModel::loadConfig() {
    config = Config::load();
}

void AppModel::saveConfig() {
    config.save();
}

// === 扫描 ===

void AppModel::launchScan(EngineType type, const std::wstring& folder, bool recursive) {
    // 统一入口（降级只是换 type 再调本函数 —— 消灭 Qt 版 D.5 代码重复）
    scanCancelFlag = false;
    state = AppState::Scanning;
    scanStartTime = std::chrono::steady_clock::now();
    scanElapsedMs = 0;
    scanDegraded = false;
    protectedSkipped = 0;
    dirsScanned = 0;
    filesFound = 0;
    {
        std::lock_guard<std::mutex> lk(filesMutex);
        files.clear();
        selectedPaths.clear();
        failedPaths.clear();
    }
    app::requestUpdate();

    // app::async::restart：回调自动回主线程（消灭 D.1 跨线程信号坑）
    app::async::restart("scan",
        [this, folder, recursive, type](const app::async::CancelToken& token) {
            auto engine = createEngine(type, config);
            bool degraded = false;
            std::wstring degradeReason;
            if (!engine->isAvailable()) {
                degradeReason = engine->unavailableReason();
                engine = createEngine(EngineType::Walk, config);
                degraded = true;
            }
            scanDegraded = degraded;
            scanDegradeReason = degradeReason;
            engine->run(folder, recursive, scanCancelFlag,
                [this, folder, protect = config.protectProgramDirs,
                 &extraPatterns = config.customProtectedPatterns]
                (const ArchiveFile& af) {
                    if (protect) {
                        // 第 1 层：内置 + 自定义目录模式（mods/材质包/Steam 等）
                        if (isProtectedPath(af.path, extraPatterns)) {
                            protectedSkipped++;
                            return;
                        }
                        // 第 2 层：注册表安装路径（国产游戏的自定义安装目录，如 D:\Games\原神\）
                        if (path::startsWithAny(af.path, path::installedProgramDirs())) {
                            protectedSkipped++;
                            return;
                        }
                        // 第 3 层：exe 同目录检测（绿色版游戏资源与 exe 同目录）。
                        // 扫描根目录本身豁免（用户明确扫的根目录，如 Downloads 里有 exe 也不影响根级 zip）
                        std::wstring parent = path::parentDir(af.path);
                        if (!parent.empty() && parent != folder && path::dirContainsExe(parent)) {
                            protectedSkipped++;
                            return;
                        }
                    }
                    std::lock_guard<std::mutex> lk(filesMutex);
                    files.push_back(af);
                },
                [this](int d, int f) {
                    // 工作线程：更新进度原子量
                    dirsScanned = d;
                    filesFound = f;
                    scanElapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - scanStartTime).count();
                    app::requestUpdate();  // 唤醒 UI 刷新（节流到进度回调频率）
                });
            return app::async::success();
        },
        [this](const app::async::Result<void>& r) {
            // 主线程回调
            state = AppState::Idle;
            scanElapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - scanStartTime).count();
            app::requestUpdate();
        });
}

void AppModel::startScan(const std::wstring& folder, bool recursive) {
    if (isBusy(state)) return;
    launchScan(config.searchEngine, folder, recursive);
}

void AppModel::cancelScan() {
    if (state != AppState::Scanning) return;
    scanCancelFlag = true;
    app::async::cancel("scan");
    // 直接回 Idle（async cancel 后回调不会触发，状态会卡在 Cancelling）
    state = AppState::Idle;
    scanElapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - scanStartTime).count();
    app::requestUpdate();
}

// === 删除 ===

void AppModel::startDelete(bool permanent, DeleteDoneCallback doneCb) {
    if (isBusy(state)) return;

    // 收集选中文件（拷贝，避免与 UI 数据竞争）
    std::vector<ArchiveFile> toDelete;
    {
        std::lock_guard<std::mutex> lk(filesMutex);
        for (const auto& f : files) {
            if (selectedPaths.count(f.path)) {
                toDelete.push_back(f);
            }
        }
    }
    if (toDelete.empty()) return;

    deleteCancelFlag = false;
    state = AppState::Deleting;
    deleteStartTime = std::chrono::steady_clock::now();
    deleteDone = 0;
    deleteTotal = static_cast<int>(toDelete.size());
    deleteSuccess = 0;
    deleteFail = 0;
    app::requestUpdate();

    app::async::restart("delete",
        [this, toDelete, permanent](const app::async::CancelToken& token) {
            Deleter d;
            d.setPermanent(permanent);
            d.setTimeoutSec(120);
            auto results = d.run(toDelete, deleteCancelFlag,
                [this](int done, int total, const std::wstring&, int64_t) {
                    deleteDone = done;
                    deleteTotal = total;
                    app::requestUpdate();
                });
            deleteSuccess = 0;
            deleteFail = 0;
            for (const auto& r : results) {
                if (r.ok) ++deleteSuccess; else ++deleteFail;
            }
            // 把结果存到成员，供 then 回调读取（result 也可直接传，但这里用 results 引用）
            // 注意：async 的 then 接受 Result<T>，我们传 vector<wstring> 不便，改用 void + 成员
            return app::async::success(results);
        },
        [this, doneCb](const app::async::Result<std::vector<DeleteResult>>& r) {
            // 主线程：应用结果（标红/移除成功项）—— 无条件执行，不依赖 doneCb
            state = AppState::Idle;
            if (r.ok) {
                applyDeleteResults(r.value);
                if (doneCb) doneCb(r.value, deleteCancelFlag.load());
            }
            app::requestUpdate();
        });
}

void AppModel::cancelDelete() {
    if (state != AppState::Deleting) return;
    state = AppState::DeleteCancelling;
    deleteCancelFlag = true;
    app::async::cancel("delete");
    app::requestUpdate();
}

// === 选中 ===

void AppModel::toggleSelect(const std::wstring& path) {
    std::lock_guard<std::mutex> lk(filesMutex);
    if (selectedPaths.count(path)) selectedPaths.erase(path);
    else selectedPaths.insert(path);
}

void AppModel::selectAll() {
    std::lock_guard<std::mutex> lk(filesMutex);
    for (const auto& f : files) {
        if (!failedPaths.count(f.path)) selectedPaths.insert(f.path);
    }
}

void AppModel::selectNone() {
    std::lock_guard<std::mutex> lk(filesMutex);
    selectedPaths.clear();
}

void AppModel::invertSelection() {
    std::lock_guard<std::mutex> lk(filesMutex);
    std::unordered_set<std::wstring> inv;
    for (const auto& f : files) {
        if (failedPaths.count(f.path)) continue;  // 失败项不参与
        if (!selectedPaths.count(f.path)) inv.insert(f.path);
    }
    selectedPaths = std::move(inv);
}

int AppModel::selectedCount() {
    std::lock_guard<std::mutex> lk(filesMutex);
    return static_cast<int>(selectedPaths.size());
}

int64_t AppModel::selectedTotalSize() {
    std::lock_guard<std::mutex> lk(filesMutex);
    int64_t total = 0;
    for (const auto& f : files) {
        if (selectedPaths.count(f.path)) total += f.size;
    }
    return total;
}

// === 删除结果应用（消灭 Qt 版 D.8 O(n²) → O(n)）===

void AppModel::applyDeleteResults(const std::vector<DeleteResult>& results) {
    std::lock_guard<std::mutex> lk(filesMutex);
    // 用 set 加速查找成功路径
    std::unordered_set<std::wstring> okPaths;
    std::unordered_set<std::wstring> failPaths;
    for (const auto& r : results) {
        if (r.ok) okPaths.insert(r.path);
        else failPaths.insert(r.path);
    }
    // 标红失败项
    for (const auto& p : failPaths) {
        failedPaths.insert(p);
        selectedPaths.erase(p);  // 失败项自动取消选中
    }
    // 移除成功项（一遍 O(n)，消灭 Qt 版倒序删除的 O(n²)）
    files.erase(
        std::remove_if(files.begin(), files.end(),
            [&](const ArchiveFile& f) { return okPaths.count(f.path) > 0; }),
        files.end());
    // 成功项的选中态也清掉
    for (const auto& p : okPaths) selectedPaths.erase(p);
}

void AppModel::updateScanElapsed() {
    if (isScanning(state)) {
        scanElapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - scanStartTime).count();
    }
}

} // namespace ac
