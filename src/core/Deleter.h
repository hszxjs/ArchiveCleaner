#ifndef AC_DELETER_H
#define AC_DELETER_H

#include "ArchiveFile.h"
#include "ProcessChecker.h"
#include <string>
#include <vector>
#include <functional>
#include <atomic>

namespace ac {

// 单个文件删除结果
struct DeleteResult {
    std::wstring path;
    bool ok = false;
    std::wstring reason;
    std::vector<LockingProcess> lockers;
};

// 删除日志：追加一行到 delete_log.txt
void logDeleteAction(const std::wstring& path, bool permanent, bool success, const std::wstring& reason = L"");

// 批量删除器
class Deleter {
public:
    using ProgressCallback = std::function<void(int done, int total, const std::wstring& currentPath, int64_t currentSize)>;

    void setPermanent(bool p) { permanent_ = p; }
    void setTimeoutSec(int sec) { timeoutSec_ = sec; }

    std::vector<DeleteResult> run(const std::vector<ArchiveFile>& files,
                                  const std::atomic<bool>& cancel,
                                  const ProgressCallback& onProgress);

private:
    bool permanent_ = false;
    int timeoutSec_ = 120;
    DeleteResult deleteOne(const std::wstring& normPath);
};

} // namespace ac

#endif // AC_DELETER_H
