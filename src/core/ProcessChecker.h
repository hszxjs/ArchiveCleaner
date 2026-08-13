#ifndef AC_PROCESS_CHECKER_H
#define AC_PROCESS_CHECKER_H

#include <string>
#include <vector>
#include <cstdint>

namespace ac {

// 占用进程：进程名 + PID
struct LockingProcess {
    std::wstring name;
    uint32_t pid = 0;
};

// 占用进程检测：Windows Restart Manager API。
// ⚠️ 调用前必须确认文件存在（否则卡数十秒）。
std::vector<LockingProcess> findLockers(const std::wstring& targetPath);

} // namespace ac

#endif // AC_PROCESS_CHECKER_H
