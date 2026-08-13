#ifndef AC_ISEARCH_ENGINE_H
#define AC_ISEARCH_ENGINE_H

#include "ArchiveFile.h"
#include <string>
#include <vector>
#include <functional>
#include <atomic>
#include <utility>
#include <memory>

namespace ac {

// 引擎类型（对应 config 的 search_engine 字段）
enum class EngineType { Walk, Fdfind, Everything, Mft };

// 字符串 <-> 枚举（与 config.json 的值一致）
std::string engineKey(EngineType t);
EngineType engineFromKey(const std::string& key);

// 搜索引擎抽象接口。四档引擎（Win32/fdfind/Everything/MFT）都实现它。
// UI/Domain 层只依赖此接口，不关心具体实现，可平滑切换/降级。
//
// 工作模型：在工作线程中调用 run()，通过回调上报进度和结果。
// 取消通过 cancel 标志（原子量）实现软取消。
class ISearchEngine {
public:
    virtual ~ISearchEngine() = default;
    virtual std::wstring name() const = 0;
    virtual bool isAvailable() const = 0;
    virtual std::wstring unavailableReason() const { return {}; }

    using FoundCallback    = std::function<void(const ArchiveFile&)>;
    using ProgressCallback = std::function<void(int dirsScanned, int filesFound)>;

    // 返回 (ok, errorMsg)。ok=false 时 errorMsg 给降级/提示用。
    virtual std::pair<bool, std::wstring> run(
        const std::wstring& folder,
        bool recursive,
        const std::atomic<bool>& cancel,
        const FoundCallback& onFound,
        const ProgressCallback& onProgress) = 0;
};

// 引擎工厂（统一创建，降级只是换 type 再 create）
// 实现需 Config，前置声明；完整 createEngine 在 Config.h 之后/单独头里。
struct Config;
std::unique_ptr<ISearchEngine> createEngine(EngineType t, const Config& cfg);

} // namespace ac

#endif // AC_ISEARCH_ENGINE_H
