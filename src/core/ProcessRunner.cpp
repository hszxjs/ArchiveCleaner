#include "ProcessRunner.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <vector>
#include <string>
#include <chrono>

namespace ac {

ProcessResult ProcessRunner::run(
    const std::wstring& exePath,
    const std::wstring& args,
    const std::atomic<bool>& cancel,
    const std::function<void(const std::string&)>& onLine,
    const std::function<void()>& onPoll,
    int timeoutMs) {

    ProcessResult result{};

    // 创建匿名管道（子进程 stdout/stderr → 管道写端 → 父进程读端）
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE childStdoutRead = nullptr, childStdoutWrite = nullptr;
    if (!CreatePipe(&childStdoutRead, &childStdoutWrite, &sa, 0)) {
        return result;
    }
    // 父进程的读端不可被子进程继承
    SetHandleInformation(childStdoutRead, HANDLE_FLAG_INHERIT, 0);

    // 构造完整命令行：exePath + " " + args（exePath 带空格时加引号）
    std::wstring cmdLine = L"\"" + exePath + L"\"" + L" " + args;

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = childStdoutWrite;
    si.hStdError  = childStdoutWrite;  // stderr 合并到 stdout
    si.hStdInput  = nullptr;
    PROCESS_INFORMATION pi{};

    // CREATE_NO_WINDOW 避免黑框
    BOOL ok = CreateProcessW(
        nullptr,
        const_cast<LPWSTR>(cmdLine.c_str()),
        nullptr, nullptr,
        TRUE,  // 继承句柄
        CREATE_NO_WINDOW,
        nullptr, nullptr,
        &si, &pi);
    if (!ok) {
        CloseHandle(childStdoutRead);
        CloseHandle(childStdoutWrite);
        return result;
    }
    // 父进程不需要管道写端
    CloseHandle(childStdoutWrite);
    childStdoutWrite = nullptr;

    auto startTime = std::chrono::steady_clock::now();
    std::string leftover;  // 跨缓冲的半行

    // 主循环：PeekNamedPipe 非阻塞检查，周期性 cancel/timeout
    while (true) {
        if (cancel.load(std::memory_order_relaxed)) {
            result.cancelled = true;
            break;
        }
        if (timeoutMs > 0) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - startTime).count();
            if (elapsed >= timeoutMs) {
                result.timedOut = true;
                break;
            }
        }
        if (onPoll) onPoll();

        // 检查管道里有多少字节可读
        DWORD avail = 0;
        if (!PeekNamedPipe(childStdoutRead, nullptr, 0, nullptr, &avail, nullptr)) {
            // 管道断了（子进程结束）
            break;
        }
        if (avail == 0) {
            // 没数据：检查子进程是否已退出
            DWORD exitCode = 0;
            if (GetExitCodeProcess(pi.hProcess, &exitCode) && exitCode != STILL_ACTIVE) {
                result.exitCode = static_cast<int>(exitCode);
                result.ok = !result.cancelled && !result.timedOut && exitCode == 0;
                break;
            }
            Sleep(50);  // 避免空转
            continue;
        }

        // 读出可用的字节
        DWORD toRead = avail;
        std::vector<char> buf(toRead);
        DWORD got = 0;
        if (!ReadFile(childStdoutRead, buf.data(), toRead, &got, nullptr) || got == 0) {
            break;
        }

        // 按行切分（保留跨缓冲的半行）
        leftover.append(buf.data(), got);
        size_t start = 0;
        while (true) {
            size_t nl = leftover.find('\n', start);
            if (nl == std::string::npos) break;
            std::string line = leftover.substr(start, nl - start);
            // 去掉行尾 \r
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (onLine) onLine(line);
            start = nl + 1;
        }
        leftover = leftover.substr(start);  // 剩余半行留到下次
    }

    // 取消/超时：杀子进程
    if (result.cancelled || result.timedOut) {
        TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, 2000);
    }

    // 处理残留的最后一行（无换行符结尾的）
    if (!leftover.empty() && !result.cancelled && !result.timedOut) {
        if (!leftover.empty() && leftover.back() == '\r') leftover.pop_back();
        if (onLine) onLine(leftover);
    }

    CloseHandle(childStdoutRead);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    return result;
}

} // namespace ac
