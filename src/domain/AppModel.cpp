#include "AppModel.h"
#include "core/ISearchEngine.h"
#include "core/Deleter.h"      // DeleteResult + Deleter
#include "core/PathUtils.h"

#include "eui/app.h"     // app::requestUpdate
#include "eui/async.h"   // app::async::restart/cancel

#include <algorithm>
#include <unordered_map>

namespace ac {

AppModel::AppModel() {
    loadConfig();
}

void AppModel::loadConfig() {
    config = Config::load();
    // 扫描格式开关同步到核心层全局（isArchiveExt 过滤用）
    auto& dis = scanDisabledKeys();
    dis.clear();
    for (const auto& k : config.scanDisabled) dis.insert(k);
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
        protectedFiles.clear();
        protectedReasons.clear();
        selectedPaths.clear();
        failedPaths.clear();
    }
    app::requestUpdate();

    // app::async::restart：回调自动回主线程（消灭 D.1 跨线程信号坑）
    app::async::restart("scan",
        [this, folder, recursive, type](const app::async::CancelToken& token) mutable {
            auto engine = createEngine(type, config);
            bool degraded = false;
            std::wstring degradeReason;
            if (!engine->isAvailable()) {
                degradeReason = engine->unavailableReason();
                engine = createEngine(EngineType::Walk, config);
                degraded = true;
            }
            // 目录风险检测缓存：0=安全 1=含exe 2=项目标记（本次扫描内有效）
            std::unordered_map<std::wstring, int> dirCheckCache;
            scanDegraded = degraded;
            scanDegradeReason = degradeReason;
            engine->run(folder, recursive, scanCancelFlag,
                [this, folder, protect = config.protectProgramDirs,
                 &extraPatterns = config.customProtectedPatterns, &dirCheckCache]
                (const ArchiveFile& af) {
                    std::string reason;  // 非空 = 受保护
                    if (protect) {
                        if (isProtectedPath(af.path, extraPatterns)) {
                            reason = "\xE4\xBF\x9D\xE6\x8A\xA4\xE7\x9B\xAE\xE5\xBD\x95";           // 保护目录
                        } else if (path::startsWithAny(af.path, path::installedProgramDirs())) {
                            reason = "\xE5\xB7\xB2\xE5\xAE\x89\xE8\xA3\x85\xE7\xA8\x8B\xE5\xBA\x8F"; // 已安装程序
                        } else {
                            std::wstring parent = path::parentDir(af.path);
                            if (!parent.empty() && parent != folder) {
                                auto it = dirCheckCache.find(parent);
                                int risky;
                                if (it != dirCheckCache.end()) {
                                    risky = it->second;
                                } else {
                                    risky = path::dirContainsExe(parent) ? 1
                                          : (path::dirContainsProjectMarker(parent) ? 2 : 0);
                                    dirCheckCache[parent] = risky;
                                }
                                if (risky == 1) {
                                    reason = "\xE6\xB8\xB8\xE6\x88\x8F/\xE7\xA8\x8B\xE5\xBA\x8F\xE7\x9B\xAE\xE5\xBD\x95"; // 游戏/程序目录
                                } else if (risky == 2) {
                                    reason = "\xE6\xBA\x90\xE7\xA0\x81\xE9\xA1\xB9\xE7\x9B\xAE";   // 源码项目
                                }
                            }
                        }
                    }
                    if (!reason.empty()) {
                        protectedSkipped++;
                        std::lock_guard<std::mutex> lk(filesMutex);
                        protectedFiles.push_back(af);
                        protectedReasons[af.path] = reason;
                        return;
                    }
                    {
                        std::lock_guard<std::mutex> lk(filesMutex);
                        files.push_back(af);
                    }
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

    // 收集选中文件（普通列表 + 受保护列表，拷贝避免数据竞争）
    std::vector<ArchiveFile> toDelete;
    {
        std::lock_guard<std::mutex> lk(filesMutex);
        for (const auto& f : files) {
            if (selectedPaths.count(f.path)) {
                toDelete.push_back(f);
            }
        }
        for (const auto& f : protectedFiles) {
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
    protectedFiles.erase(
        std::remove_if(protectedFiles.begin(), protectedFiles.end(),
            [&](const ArchiveFile& f) { return okPaths.count(f.path) > 0; }),
        protectedFiles.end());
    for (const auto& p : okPaths) {
        selectedPaths.erase(p);
        protectedReasons.erase(p);
    }
}

void AppModel::updateScanElapsed() {
    if (isScanning(state)) {
        scanElapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - scanStartTime).count();
    }
}

} // namespace ac
