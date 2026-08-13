#ifndef AC_PROCESS_RUNNER_H
#define AC_PROCESS_RUNNER_H

#include <string>
#include <functional>
#include <atomic>

namespace ac {

// 子进程运行器：启动 exe，逐行读 stdout，支持取消和总超时。
// 替代 Qt 版的 QProcess（去 Qt 化）。供 FdEngine/EverythingEngine 共用，消除两引擎的子进程代码重复。
//
// 设计要点（规避 Win32 子进程的常见坑）：
// - 用匿名管道读 stdout，子进程 stdout/stderr 重定向到管道写端
// - 主循环用 PeekNamedPipe 检查是否有数据（非阻塞），周期性检查 cancel/超时
// - 超时/取消时用 TerminateProcess 杀子进程（Win32 无法强制中断管道读取，只能杀进程）
// - 子进程用 CREATE_NO_WINDOW 避免黑框
struct ProcessResult {
    bool ok = false;           // 进程正常退出（exit code 0 且未超时/取消）
    bool cancelled = false;
    bool timedOut = false;
    int exitCode = -1;
};

class ProcessRunner {
public:
    // 启动 exe 并逐行回调。exePath + args 启动；onLine 处理每行 stdout（UTF-8 或本地编码由调用方决定）。
    // 返回最终结果。
    //   exePath     : 可执行文件完整路径
    //   args        : 命令行参数（已含引号处理）
    //   cancel      : 取消标志
    //   onLine      : 每读到一行 stdout 调用（行不含换行符；原始字节，编码由调用方解析）
    //   onPoll      : 每次轮询周期调用（可上报进度），可选
    //   timeoutMs   : 总超时（0=无限）
    static ProcessResult run(
        const std::wstring& exePath,
        const std::wstring& args,
        const std::atomic<bool>& cancel,
        const std::function<void(const std::string&)>& onLine,
        const std::function<void()>& onPoll = nullptr,
        int timeoutMs = 0);
};

} // namespace ac

#endif // AC_PROCESS_RUNNER_H
