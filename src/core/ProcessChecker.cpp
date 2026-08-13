#include "ProcessChecker.h"
#include "PathUtils.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <RestartManager.h>
#pragma comment(lib, "Rstrtmgr.lib")

#include <vector>

namespace ac {

std::vector<LockingProcess> findLockers(const std::wstring& targetPath) {
    std::vector<LockingProcess> result;

    // ⚠️ 文件不存在直接返回空，不跑 Restart Manager（否则卡数十秒）
    if (!path::exists(targetPath)) {
        return result;
    }

    std::wstring norm = path::normalize(targetPath);

    DWORD sessionHandle = 0;
    wchar_t sessionKey[CCH_RM_SESSION_KEY + 1] = {};
    if (RmStartSession(&sessionHandle, 0, sessionKey) != ERROR_SUCCESS) {
        return result;
    }

    LPCWSTR files[] = { norm.c_str() };
    if (RmRegisterResources(sessionHandle, 1, files, 0, nullptr, 0, nullptr) != ERROR_SUCCESS) {
        RmEndSession(sessionHandle);
        return result;
    }

    // 两阶段 RmGetList
    UINT needed = 0, have = 0;
    DWORD reason = 0;
    DWORD gl = RmGetList(sessionHandle, &needed, &have, nullptr, &reason);

    if (gl == ERROR_SUCCESS && needed == 0) {
        RmEndSession(sessionHandle);
        return result;
    }
    if (gl != ERROR_MORE_DATA) {
        RmEndSession(sessionHandle);
        return result;
    }

    std::vector<RM_PROCESS_INFO> infos(needed);
    have = needed;
    gl = RmGetList(sessionHandle, &needed, &have, infos.data(), &reason);

    if (gl == ERROR_SUCCESS || gl == ERROR_MORE_DATA) {
        for (UINT i = 0; i < have; ++i) {
            DWORD pid = infos[i].Process.dwProcessId;
            if (pid == 0) continue;
            LockingProcess p;
            p.name = infos[i].strAppName;
            p.pid = pid;
            if (p.name.empty()) p.name = L"未知进程";
            // 去重（按 pid）
            bool dup = false;
            for (const auto& e : result) {
                if (e.pid == p.pid) { dup = true; break; }
            }
            if (!dup) result.push_back(p);
        }
    }

    RmEndSession(sessionHandle);
    return result;
}

} // namespace ac
