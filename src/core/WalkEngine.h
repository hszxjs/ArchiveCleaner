#ifndef AC_WALK_ENGINE_H
#define AC_WALK_ENGINE_H

#include "ISearchEngine.h"

namespace ac {

// 档位1：Win32 目录遍历（os.walk 等价）。
// FindFirstFileW/FindNextFileW 递归，兼容所有盘符/文件系统，无需任何依赖。
// 永远可用，是其他三档失败时的降级目标。
class WalkEngine : public ISearchEngine {
public:
    std::wstring name() const override { return L"Win32 遍历"; }
    bool isAvailable() const override { return true; }
    std::pair<bool, std::wstring> run(
        const std::wstring& folder,
        bool recursive,
        const std::atomic<bool>& cancel,
        const FoundCallback& onFound,
        const ProgressCallback& onProgress) override;
};

} // namespace ac

#endif // AC_WALK_ENGINE_H
