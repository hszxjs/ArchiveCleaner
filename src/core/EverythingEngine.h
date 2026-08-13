#ifndef AC_EVERYTHING_ENGINE_H
#define AC_EVERYTHING_ENGINE_H

#include "ISearchEngine.h"
#include "Config.h"

namespace ac {

// 档位3：Everything（es.exe）。依赖 Everything 主程序后台运行。
class EverythingEngine : public ISearchEngine {
public:
    explicit EverythingEngine(const Config& cfg) : cfg_(cfg) {}
    std::wstring name() const override { return L"Everything"; }
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
    static bool everythingRunning();
};

} // namespace ac

#endif // AC_EVERYTHING_ENGINE_H
