#ifndef AC_FD_ENGINE_H
#define AC_FD_ENGINE_H

#include "ISearchEngine.h"
#include "Config.h"

namespace ac {

// 档位2：fdfind（fd.exe）。阶段 3 完整实现（用 ProcessRunner）。
class FdEngine : public ISearchEngine {
public:
    explicit FdEngine(const Config& cfg) : cfg_(cfg) {}
    std::wstring name() const override { return L"fdfind"; }
    bool isAvailable() const override;
    std::wstring unavailableReason() const override;
    std::pair<bool, std::wstring> run(
        const std::wstring& folder, bool recursive,
        const std::atomic<bool>& cancel,
        const FoundCallback& onFound,
        const ProgressCallback& onProgress) override;
private:
    const Config& cfg_;
    std::wstring resolveExe() const;
};

} // namespace ac

#endif // AC_FD_ENGINE_H
