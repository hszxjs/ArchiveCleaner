# 项目提示词：用 C++ + Qt 重写「压缩包清理工具」

请用 **C++ + Qt Widgets** 从零开发一个 Windows 桌面应用：「压缩包清理工具 ArchiveCleaner」。

这个程序之前用 Python + customtkinter 实现，遇到大量 GUI 框架层面的顽固问题（窗口黑屏、grab 死锁、文字漂移、打包臃肿）。现在用 C++ + Qt 重写，彻底解决这些问题。下面的提示词包含了完整的功能需求、所有踩过的坑（请务必避开）、以及技术实现要点。

---

## 一、程序功能

扫描指定文件夹下的所有压缩包文件，用复选框让用户勾选其中一部分或全部，然后批量删除（默认送入回收站，可切换永久删除）。删除失败的文件自动检测并提示是哪个进程占用。

### 核心功能
1. **扫描压缩包**：指定文件夹（支持递归子文件夹），识别压缩包文件
2. **列表展示**：卡片式列表，每项显示 文件名/大小/完整路径/修改时间 + 复选框
3. **批量勾选**：全选/全不选/反选
4. **删除**：默认送入回收站（可恢复），可切换永久删除（不可恢复）
5. **删除进度**：进度条 + 当前文件名 + 已用/预计剩余时间 + 成功/失败计数 + 可取消
6. **占用进程检测**：删除失败的文件，检测并提示是哪个进程占用（进程名 + PID）
7. **设置持久化**：配置保存到文件，下次启动自动恢复

### 压缩包扩展名（共14种）
`.zip .rar .7z .tar .gz .bz2 .tgz .xz .iso .cab .z .lz .lzma .tbz2`

---

## 二、搜索引擎（三档，在设置里人工切换）

### 档位1：os.walk 等价（默认，通用）
- 用 Windows API（如 FindFirstFile/FindNextFile 递归）遍历目录
- 兼容所有盘符、所有文件系统
- 扫描时实时显示进度（已扫描目录数、已找到文件数）

### 档位2：Everything（秒搜）
- 调用 voidtools Everything 的命令行工具 **es.exe**
- **es.exe 查询语法（已验证正确，务必用这个）**：
  - 路径以反斜杠 `\` 结尾，空格连接扩展名，整体作为搜索词
  - 扩展名用 `ext:zip;rar;7z`（**分号分隔，单个 ext: 前缀**），不是 `ext:zip | ext:rar`
  - **不要加 `path:` 前缀**（`path:"C:\folder\"` 会导致查不到任何结果）
  - 示例：`es.exe -n -1 "C:\folder\ ext:zip;rar;7z;tar;gz;bz2;tgz;xz;iso;cab;z;lz;lzma;tbz2"`
  - `-n -1` = 不限制结果数量
  - **不要用 `-s` 开关**（它不是 es.exe 的标准开关）
  - es.exe 输出默认按控制台代码页，**含中文路径会乱码**，建议用 `errors` 容错或检测是否支持 `-utf8`
- 前提：Everything 主程序需在后台运行（es.exe 依赖它的索引服务）
- 程序内置「下载最新版 es.exe」功能：从 `https://www.voidtools.com/ES-{版本}.x64.zip` 下载（先解析下载页 `https://www.voidtools.com/zh-cn/downloads/` 拿最新版本号，当前 1.1.0.37），解压出 es.exe

### 档位3：MFT（最快，需管理员+NTFS）
- 直接读 NTFS MFT（主文件表）
- 需要：管理员权限 + NTFS 文件系统
- 用 C++ 可直接调用 DeviceIoControl + FSCTL_ENUM_USN_DATA 或解析 MFT，比 Python 实现可靠得多

---

## 三、界面需求

### 主窗口布局
- 顶部标题栏（程序名 + 设置按钮 + 主题切换）
- 控制区：路径输入框 + 浏览按钮 + 开始扫描按钮 + 选项（包含子文件夹开关、删除方式下拉、搜索引擎指示）
- 列表区：可滚动的文件卡片列表 + 扫描进度条
- 底部：全选/全不选/反选 + 删除选中(N个) 按钮（红色）
- 状态栏：显示扫描数量/选中数量/总大小

### 文件卡片
- 复选框 + 文件类型图标 + 文件名（粗体）+ 完整路径（灰色小字）+ 大小（橙色）+ 修改时间
- 选中时整张卡片高亮（边框+背景色变化）
- 点击卡片任意位置切换勾选
- 删除失败时卡片标红

### 设置弹窗
- 搜索引擎三选一（os.walk / Everything / MFT）
- Everything 选中时显示 es.exe 路径（输入框 + 浏览按钮 + 自动检测按钮 + 下载按钮）
- 删除默认方式下拉
- 外观主题（深/浅/系统）
- **主题色自定义**：预设强调色快选（蓝/绿/红/紫/橙）+ 调色盘任意自定义颜色（QColorDialog）
- 保存按钮固定在底部，永不被挤出

### 删除进度对话框
- 进度条 + "第 3/10 个 · xxx.zip (12.4 MB)"
- "已用 5秒 · 预计剩余 12秒 · 成功2 失败0"
- 取消按钮

---

## 四、关键踩坑经验（务必避开）

### ⚠️ 路径分隔符（致命）
- 用户输入路径可能用正斜杠（如 `B:/2023年`），递归拼接后路径混用正反斜杠（`B:/2023年\xxx.zip`）
- **送回收站的 SHFileOperation 不认这种混合路径，会 100% 失败**
- 修复：所有路径必须归一化为统一分隔符（`PathCanonicalize` 或手动规范化），存入数据结构前统一处理

### ⚠️ 失败后不能卡住（致命）
- 之前对每个删除失败的文件都跑"占用进程检测"，对不存在的路径会卡 60 秒
- **文件不存在时应立即跳过**（不跑进程检测），只有文件确实存在却删不掉时才检测占用

### ⚠️ send2trash / SHFileOperation 死锁（致命）
- 对非法路径调用 SHFileOperation 的 COM 会挂死，线程无法中断，进程变成无法强杀的僵尸
- 修复：① 路径归一化从源头避免非法路径 ② 单文件删除加超时保护 ③ 删除在独立线程，超时后复查文件是否仍存在来修正结果

### ⚠️ UAC 提权（Windows 10/11 规范）
- 程序启动时应弹 UAC 请求管理员权限
- 用户同意 → 以管理员运行（MFT 可用）；拒绝 → 降级普通权限（MFT 不可用，自动用 os.walk）
- **规范做法**：用 manifest 声明 `requireAdministrator`（双击直接弹 UAC），或在运行时用 ShellExecuteW 的 runas 动词
- **防循环**：提权后的子进程要能识别自己已被提权，不重复弹窗（用命令行参数标记，比环境变量可靠——环境变量跨 UAC 边界可能丢失）

### ⚠️ 占用进程检测的正确方法
- 用 **Windows Restart Manager API**（rstrtmgr.dll：RmStartSession / RmRegisterResources / RmGetList / RmEndSession）
- 这是 Windows 资源管理器提示"文件被占用"时用的同一套 API，无需管理员权限，可靠
- **不要用遍历所有进程 open_files 的方法**（受权限限制，易返回空）
- C++ 直接调用要点：
  - RM_PROCESS_INFO 结构体：ProcessId(DWORD) + ProcessStartTime(FILETIME) + strAppName[256] + strServiceShortName[64] + ApplicationType + AppStatus + tsSessionId + bRestartable
  - 文件不存在时直接返回空，不跑检测
  - 返回占用进程的 进程名 + PID

### ⚠️ 跨卷大文件提示
- 送回收站时，Windows 把文件 move 到该盘的 `$Recycle.Bin`：同卷快，跨卷退化为完整数据复制（5GB 文件删半分钟很正常）
- 删除前若检测到大文件（>500MB），提示用户"送回收站可能较慢，是否改用永久删除"

### ⚠️ GUI 注意事项（Python 版的坑，Qt 能避免但要注意）
- 模态对话框（grab_set 等价）必须在窗口**完全显示后**再设置模态，否则事件循环死锁 → 窗口全黑无法关闭。Qt 用 exec() 自然模态，无此问题
- 状态栏文字不要在固定高度的 Label 里塞多行文本，会导致渲染坐标错乱、文字漂移
- 列表项布局用支持删除后自动收缩的布局管理器（Qt 的 QVBoxLayout/QListWidget 天然支持，不会留空白空洞）
- 进度条：能算百分比时用确定模式（QProgressBar setValue），无法预估总量时用不确定模式（setRange(0,0)）并配文字说明"搜索中"

### ⚠️ 扫描重入保护
- 扫描/删除进行中，禁用相关按钮（扫描、删除、浏览），防止用户并发触发导致数据竞争

### ⚠️ 扫描重试时的状态保护
- 搜索引擎失败自动降级到 os.walk 重试时，必须重新设置"忙碌"标志、显示进度条、禁用按钮，否则用户能在重试期间并发触发第二次扫描，导致数据错乱

---

## 五、技术实现要点（C++ + Qt）

### 推荐技术栈
- **语言**：C++17
- **GUI 框架**：Qt 6（Widgets 模块，不要用 QML，工具类应用 Widgets 更稳）
- **编译器**：MSVC（Windows 上 Qt 标配）
- **构建**：CMake 或 qmake
- **链接**：静态链接 Qt，编译成单个 exe（5-15MB）

### 关键 API 映射
| 功能 | Python(旧) | C++/Qt(新) |
|------|-----------|------------|
| 文件删除-回收站 | send2trash | SHFileOperationW |
| 文件删除-永久 | os.remove | DeleteFileW |
| 占用检测 | ctypes+Restart Manager | 直接调 rstrtmgr.dll |
| 目录遍历 | os.walk | FindFirstFileW/FindNextFileW 或 QDir::entryInfoList |
| 调 es.exe | subprocess.Popen | QProcess |
| HTTP下载 | urllib | QNetworkAccessManager |
| UAC提权 | ShellExecuteW | manifest requireAdministrator 或 ShellExecuteW |
| JSON配置 | json | QJsonDocument/QJsonObject |
| ZIP解压 | zipfile | QZipReader(私有) 或第三方 |
| 调色盘 | - | QColorDialog |
| 主题/QSS | - | QSS样式表 + QStyleFactory |

### 线程模型
- 扫描/删除/下载都在 **QThread** 工作线程执行，通过信号槽（signals/slots）更新 UI
- 主线程只做 UI 更新，绝不阻塞
- 取消机制：工作线程检查取消标志（std::atomic<bool>），QThread 可安全终止
- 进程检测也放工作线程，避免阻塞 UI

### 进程检测（C++ 实现 Restart Manager）
```cpp
// 伪代码要点
#include <RestartManager.h>
#pragma comment(lib, "Rstrtmgr.lib")

DWORD sessionHandle = 0;
WCHAR sessionKey[256] = {0};
RmStartSession(&sessionHandle, 0, sessionKey);
PCWSTR files[] = { targetPath };
RmRegisterResources(sessionHandle, 0, NULL, 0, NULL, 1, files, 0, NULL);

UINT pnProcInfoNeeded = 0, pnProcInfo = 0;
DWORD reason = 0;
RmGetList(sessionHandle, &pnProcInfoNeeded, &pnProcInfo, NULL, &reason);
if (pnProcInfoNeeded > 0) {
    std::vector<RM_PROCESS_INFO> infos(pnProcInfoNeeded);
    pnProcInfo = pnProcInfoNeeded;
    RmGetList(sessionHandle, &pnProcInfoNeeded, &pnProcInfo, infos.data(), &reason);
    // 遍历 infos，取 ProcessId 和 strAppName
}
RmEndSession(sessionHandle);
// 文件不存在时直接返回空，不调用上述流程
```

---

## 六、工作目录与交付

- 工作目录：`A:\ArchiveCleaner\`（或在新项目里自建目录，如 `A:\ArchiveCleanerQt\`）
- 最终产物：单个 exe（静态编译）+ 必要的资源文件
- 配置文件：与 exe 同目录的 config.json

---

## 七、开发顺序建议

1. **环境检查**：确认已装 Qt 6 + MSVC（若没装，先指导安装：Qt 在线安装器约 3-4GB）
2. 搭建 Qt 项目骨架（CMakeLists.txt + 主窗口 MainWindow + 基本布局）
3. 实现目录遍历扫描（os.walk 等价档位）+ 文件卡片列表
4. 实现删除（SHFileOperationW 回收站 + DeleteFileW 永久）+ 进度对话框
5. 实现占用进程检测（Restart Manager，放工作线程）
6. 加 Everything 搜索档位（QProcess 调 es.exe + 自动下载 es.exe 功能）
7. 加 MFT 搜索档位（可选，较复杂）
8. 设置弹窗 + 主题色自定义（QColorDialog）+ 配置持久化
9. UAC 提权（manifest）
10. 测试 + 静态编译打包成单 exe

---

## 八、参考：旧版 Python 实现
旧版源码在 `A:\ArchiveCleaner\ArchiveCleaner.py`（Python），可作为功能逻辑参考（但不要照搬其实现，它的 GUI 部分有大量问题）。配置文件在 `A:\ArchiveCleaner\config.json`，删除日志在 `A:\ArchiveCleaner\delete_log.txt`，es.exe 已下载在 `A:\ArchiveCleaner\es.exe`。

---

请基于以上需求，用 C++ + Qt 从零实现这个程序。先检查开发环境（是否已装 Qt + MSVC），然后开始搭建项目。
