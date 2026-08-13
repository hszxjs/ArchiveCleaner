#ifndef AC_MFT_ENGINE_H
#define AC_MFT_ENGINE_H

#include "ISearchEngine.h"
#include "Config.h"

namespace ac {

// 档位4：MFT（FSCTL_ENUM_USN_DATA）。需管理员 + NTFS。
class MftEngine : public ISearchEngine {
public:
    explicit MftEngine(const Config& cfg) : cfg_(cfg) {}
    std::wstring name() const override { return L"MFT (最快)"; }
    bool isAvailable() const override;
    std::wstring unavailableReason() const override;
    std::pair<bool, std::wstring> run(
        const std::wstring& folder, bool recursive,
        const std::atomic<bool>& cancel,
        const FoundCallback& onFound,
        const ProgressCallback& onProgress) override;
private:
    const Config& cfg_;
};

} // namespace ac

#endif // AC_MFT_ENGINE_H
