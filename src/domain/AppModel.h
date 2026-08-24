#ifndef AC_APP_MODEL_H
#define AC_APP_MODEL_H

#include "core/ArchiveFile.h"
#include "core/Config.h"
#include "core/Deleter.h"      // DeleteResult
#include <string>
#include <vector>
#include <unordered_set>
#include <mutex>
#include <atomic>
#include <chrono>
#include <functional>

namespace ac {

// 应用状态机（单一变量，消灭 Qt 版 D.3 状态散乱）。
// 所有 UI 控件的 enabled/文案/可见性从 state 派生（见 ui_state.h 的查表函数）。
enum class AppState {
    Idle,               // 空闲，可扫描可删除
    Scanning,           // 扫描中
    ScanCancelling,     // 已请求取消扫描，等引擎退出
    Deleting,           // 删除中
    DeleteCancelling,   // 已请求取消删除
};

inline bool isBusy(AppState s) {
    return s != AppState::Idle;
}
inline bool isScanning(AppState s) {
    return s == AppState::Scanning || s == AppState::ScanCancelling;
}
inline bool isDeleting(AppState s) {
    return s == AppState::Deleting || s == AppState::DeleteCancelling;
}

// 删除完成回调（UI 层注册，用于显示结果汇总）
using DeleteDoneCallback = std::function<void(const std::vector<DeleteResult>&, bool cancelled)>;

// 应用领域模型（消灭 Qt 版 D.2 上帝对象 + D.4 缓冲 hack + D.7 数据/UI 态耦合）。
// 持有：状态机、配置、文件列表（纯数据）、选中态集合（独立于数据）、扫描/删除进度（原子量）。
// 所有业务操作在这里编排，UI 只读 model 渲染。
class AppModel {
public:
    AppModel();

    // === 状态 ===
    std::atomic<AppState> state{AppState::Idle};

    // === 配置 ===
    Config config;
    void loadConfig();
    void saveConfig();

    // === 文件列表（工作线程写、UI 读，必须加锁） ===
    std::vector<ArchiveFile> files;
    mutable std::mutex filesMutex;
    // UI 态独立存储（不污染 ArchiveFile）
    std::unordered_set<std::wstring> selectedPaths;
    std::unordered_set<std::wstring> failedPaths;
    // 两者也需在 filesMutex 保护下访问（UI 线程改，工作线程不碰）

    // === 扫描进度（工作线程写，原子量） ===
    std::atomic<int> dirsScanned{0};
    std::atomic<int> filesFound{0};
    std::atomic<bool> scanCancelFlag{false};
    std::chrono::steady_clock::time_point scanStartTime;
    std::atomic<int64_t> scanElapsedMs{0};
    std::atomic<bool> scanDegraded{false};  // 是否降级过（提示用户）
    std::wstring scanDegradeReason;         // 降级原因
    std::atomic<int> protectedSkipped{0};   // 因保护目录跳过的文件数（mods/游戏资源等）

    // === 删除进度（工作线程写，原子量） ===
    std::atomic<int> deleteDone{0};
    std::atomic<int> deleteTotal{0};
    std::atomic<int> deleteSuccess{0};
    std::atomic<int> deleteFail{0};
    std::atomic<bool> deleteCancelFlag{false};
    std::chrono::steady_clock::time_point deleteStartTime;

    // === 业务操作（UI 调用，内部启动 app::async） ===
    void startScan(const std::wstring& folder, bool recursive);
    void cancelScan();
    void startDelete(bool permanent, DeleteDoneCallback doneCb);
    void cancelDelete();

    // === 选中操作（UI 线程调用，加锁） ===
    void toggleSelect(const std::wstring& path);
    void selectAll();
    void selectNone();
    void invertSelection();
    int  selectedCount();           // 加锁
    int64_t selectedTotalSize();    // 加锁

    // 删除完成后：标红失败项、移除成功项（调用方在主线程调）
    void applyDeleteResults(const std::vector<DeleteResult>& results);

    // UI 线程周期性更新扫描已用时（compose 时调）
    void updateScanElapsed();

private:
    // 内部：启动引擎扫描（统一入口，降级只是换 type 再调）
    void launchScan(EngineType type, const std::wstring& folder, bool recursive);
};

} // namespace ac

#endif // AC_APP_MODEL_H
