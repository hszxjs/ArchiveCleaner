# 项目提示词：用 C++ + Qt 重写「压缩包清理工具」(v2，含 fdfind)

> **v2 更新说明**：本文档基于原 `PROMPT_重写C++Qt.md` 升级，主要变化：① 搜索引擎从**三档扩展为四档**（新增 fdfind 档位）；② 锁定已确认的技术决策（MSVC 工具链 / 动态库+windeployqt 打包 / MFT 全实现 / 项目目录 `A:\ArchiveCleanerQt`）；③ 补充旧版 delete_log.txt 实测验证的路径 bug 证据。原文件保留作为历史参考。

请用 **C++ + Qt Widgets** 从零开发一个 Windows 桌面应用：「压缩包清理工具 ArchiveCleaner」。

这个程序之前用 Python + customtkinter 实现，遇到大量 GUI 框架层面的顽固问题（窗口黑屏、grab 死锁、文字漂移、菜单资源耗尽 `No more menus can be allocated`、打包臃肿 9MB+）。现在用 C++ + Qt 重写，彻底解决这些问题。

---

## 一、程序功能

扫描指定文件夹下的所有压缩包文件，用复选框让用户勾选其中一部分或全部，然后批量删除（默认送入回收站，可切换永久删除）。删除失败的文件自动检测并提示是哪个进程占用。

### 核心功能
1. **扫描压缩包**：指定文件夹（支持递归子文件夹），识别压缩包文件
2. **列表展示**：卡片式列表，每项显示 文件名/大小/完整路径/修改时间 + 复选框
3. **批量勾选**：全选/全不选/反选
4. **删除**：默认送回收站（可恢复），可切换永久删除（不可恢复）
5. **删除进度**：进度条 + 当前文件名 + 已用/预计剩余时间 + 成功/失败计数 + 可取消
6. **占用进程检测**：删除失败的文件，检测并提示是哪个进程占用（进程名 + PID）
7. **设置持久化**：配置保存到文件，下次启动自动恢复

### 压缩包扩展名（共14种）
`.zip .rar .7z .tar .gz .bz2 .tgz .xz .iso .cab .z .lz .lzma .tbz2`

---

## 二、搜索引擎（四档，在设置里人工切换）

> 这是本程序最核心的差异点，必须四档全部实现。每档都封装成统一的抽象基类 `ISearchEngine`，返回 `std::vector<ArchiveFile>`，UI 层不关心具体实现，可平滑切换/降级。

### 档位1：Win32 遍历（os.walk 等价，默认通用兜底）
- 用 Windows API（`FindFirstFileW`/`FindNextFileW` 递归）或 `QDir::entryInfoList(QDir::Files | QDir::NoDotAndDotDot, QDir::NoSymLinks)` 递归
- 兼容所有盘符、所有文件系统，无需任何依赖
- 扫描时实时显示进度（已扫描目录数、已找到文件数）——通过 Qt 信号槽把工作线程进度推到 UI
- 慢，但永远可用，是其他三档失败时的降级目标

### 档位2：fdfind（新增，快速无依赖）
- 调用 sharkdp/fd 的命令行工具 **fd.exe**（Rust 写的高性能并行遍历工具，比 os.walk 快一个数量级，**无需索引服务、无需管理员权限**）
- **fd.exe 查询语法（务必用这个）**：
  ```
  fd.exe -e zip -e rar -e 7z -e tar -e gz -e bz2 -e tgz -e xz -e iso -e cab -e z -e lz -e lzma -e tbz2 --absolute --no-ignore --hidden "" "C:\folder"
  ```
  - `-e <ext>`：按扩展名过滤，**每个扩展名单独一个 `-e`**（fd 不支持逗号分隔）。扩展名不带点
  - `--absolute`：输出绝对路径（fd 默认输出相对路径，不利于后续删除）
  - **`--no-ignore`**：必须加！否则 fd 会尊重 `.gitignore`/`.fdignore`，漏扫被忽略的压缩包
  - **`--hidden`**：必须加！否则 fd 跳过隐藏文件
  - 空字符串 `""` 作为 pattern：匹配所有文件名（不要省略 pattern 参数，行为随版本变化）
  - 最后一个参数是搜索根目录
  - **不要加 `--full-path`**（那是按路径内容匹配，不是按扩展名）
- fd 输出 UTF-8（这点比 es.exe 强，中文路径不乱码），但 `QProcess` 读 stdout 时用 `setProcessChannelMode(QProcess::MergedChannels)` + `readAllStandardOutput()`，按行解析，用 `QString::fromUtf8`
- 前提：fd.exe 需放到程序同目录或 PATH
- **内置下载功能**：用 `QNetworkAccessManager` 调 GitHub API `https://api.github.com/repos/sharkdp/fd/releases/latest` 拿最新版本号和下载地址，下载 `fd-vX.Y.Z-x86_64-pc-windows-msvc.zip`，用 `QZipReader`（Qt 私有头 `<private/qzipwriter_p.h>`，或第三方 minizip）解压出 fd.exe
- 调用方式：`QProcess`（异步，配合信号槽 `readyReadStandardOutput`，不阻塞 UI 线程）

### 档位3：Everything（秒搜）
- 调用 voidtools Everything 的命令行工具 **es.exe**（不要链接 Everything 的 C SDK，子进程方式更解耦、不背 DLL 版本/位数问题）
- **es.exe 查询语法（已验证正确，务必用这个）**：
  - 路径以反斜杠 `\` 结尾，空格连接扩展名，整体作为搜索词
  - 扩展名用 `ext:zip;rar;7z`（**分号分隔，单个 ext: 前缀**），不是 `ext:zip | ext:rar`
  - **不要加 `path:` 前缀**（`path:"C:\folder\"` 会导致查不到任何结果）
  - 示例：`es.exe -n -1 "C:\folder\ ext:zip;rar;7z;tar;gz;bz2;tgz;xz;iso;cab;z;lz;lzma;tbz2"`
  - `-n -1` = 不限制结果数量
  - **不要用 `-s` 开关**（它不是 es.exe 的标准开关）
  - es.exe 输出默认按控制台代码页，**含中文路径会乱码**，必须用 `-utf8` 开关（较新版 es 支持）或对输出做 UTF-8 容错解码
- 前提：Everything 主程序需在后台运行（es.exe 依赖它的索引服务）
- **内置下载功能**：从 `https://www.voidtools.com/ES-{版本}.x64.zip` 下载（先解析下载页 `https://www.voidtools.com/zh-cn/downloads/` 拿最新版本号，当前 1.1.0.37），解压出 es.exe
- 调用方式：`QProcess`

### 档位4：MFT（最快，需管理员+NTFS）
- 直接读 NTFS MFT（主文件表）或 USN 日志
- 用 C++ 直接调 `DeviceIoControl` + `FSCTL_ENUM_USN_DATA`（比解析 MFT 文件本身更安全、API 更稳定），遍历卷上所有文件记录，过滤扩展名
- 需要：管理员权限 + NTFS 文件系统（非 NTFS 卷自动降级）
- 用 C++ 调用比 Python 可靠得多（强类型、无 GC 停顿、可精细控制缓冲区）

### 搜索引擎降级链（重要）
用户选档位 N，若 N 不可用（缺 fd.exe / Everything 未运行 / 非管理员），**自动降级到档位1（Win32 遍历）并提示用户**。降级重试时必须重新进入"忙碌"状态、显示进度条、禁用按钮，防止用户在重试期间并发触发第二次扫描导致数据错乱。

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
- **实现建议**：用 `QListWidget` + 自定义 `QWidget`（`setItemWidget`），或 `QListView` + `QStyledItemDelegate`（自绘，性能更好，几千项无压力——QListWidget 默认虚拟化）

### 设置弹窗
- 搜索引擎四选一（Win32 / fdfind / Everything / MFT）
- fdfind 选中时显示 fd.exe 路径（输入框 + 浏览按钮 + 自动检测按钮 + 下载按钮）
- Everything 选中时显示 es.exe 路径（同上）
- 删除默认方式下拉
- 外观主题（深/浅/系统）
- **主题色自定义**：预设强调色快选（蓝/绿/红/紫/橙）+ 调色盘任意自定义颜色（`QColorDialog`）
- 保存按钮固定在底部，永不被挤出（用 `QVBoxLayout`，保存按钮区放在主布局外层底部）

### 删除进度对话框
- 进度条 + "第 3/10 个 · xxx.zip (12.4 MB)"
- "已用 5秒 · 预计剩余 12秒 · 成功2 失败0"
- 取消按钮

### Qt GUI 注意事项（Python 版的坑，Qt 天然规避但要注意）
- 模态对话框（旧版 grab_set 死锁）→ Qt 用 `dialog->exec()` 自然模态，无死锁问题
- 列表项布局用 `QVBoxLayout`/`QListWidget`，删除后自动收缩，不留空白空洞
- 进度条：能算百分比时 `QProgressBar::setValue`，无法预估总量时 `setRange(0,0)` 不确定模式 + 文字"搜索中"

---

## 四、关键踩坑经验（务必避开）

### ⚠️ 路径分隔符（致命，已实测复现）
- 用户输入路径可能用正斜杠（如 `B:/2023年`），递归拼接后路径混用正反斜杠（`B:/2023年\xxx.zip`）
- **送回收站的 SHFileOperationW 不认这种混合路径，会 100% 失败**（旧版 `delete_log.txt` 第 8-20 行全是这个 bug，日志原文：`ProcessLookupError: [Errno 3] 系统找不到指定的路径: '\\\\?\\B:/2023年\\...'`）
- 修复：**所有路径在进入数据结构前必须归一化**为统一反斜杠 + 绝对路径。用 `PathCanonicalizeW` 或手动 `replace('/', "\\")` 后再 `QDir::cleanPath` / `GetFullPathNameW`

### ⚠️ 失败后不能卡住（致命）
- 之前对每个删除失败的文件都跑"占用进程检测"，对不存在的路径会卡 60 秒
- **文件不存在时应立即跳过**（不跑进程检测），只有文件确实存在却删不掉时才检测占用
- 用 `PathFileExistsW` 或 `QFileInfo::exists()` 先判断存在性

### ⚠️ SHFileOperation 死锁（致命）
- 对非法路径调用 SHFileOperation 的 COM 会挂死，线程无法中断，进程变成无法强杀的僵尸
- 修复：① 路径归一化从源头避免非法路径 ② 单文件删除加超时保护（`QTimer` 或 `std::future` + `wait_for`） ③ 删除在独立 `QThread`，超时后复查文件是否仍存在来修正结果
- COM 初始化：调用 SHFileOperationW 的线程必须先 `CoInitializeEx(NULL, COINIT_APARTMENTTHREADED)`，用完 `CoUninitialize()`。建议在删除线程的 `run()` 开头初始化

### ⚠️ UAC 提权（Windows 10/11 规范）
- 程序启动时应弹 UAC 请求管理员权限（MFT 档位需要）
- 用户同意 → 以管理员运行（MFT 可用）；拒绝 → 降级普通权限（MFT 不可用，自动用 Win32 遍历）
- **规范做法**：用 manifest 声明 `requireAdministrator`（双击直接弹 UAC）。CMake 配 `.manifest` 文件，用 `MT` 工具或 `set_target_properties(... WIN32_EXECUTABLE ...)` + mt.exe 嵌入资源
- **防循环**：提权后的进程要能识别自己已被提权，不重复弹窗（用命令行参数 `--elevated` 标记，比环境变量可靠——环境变量跨 UAC 边界可能丢失）。做法：检测到未提权时 `ShellExecuteW` 的 `runas` 动词重启自身并带上 `--elevated` 参数
- 提权检测：`OpenProcessToken` + `GetTokenInformation(TokenElevation)`

### ⚠️ 占用进程检测的正确方法
- 用 **Windows Restart Manager API**（rstrtmgr.dll：RmStartSession / RmRegisterResources / RmGetList / RmEndSession）
- 这是 Windows 资源管理器提示"文件被占用"时用的同一套 API，**无需管理员权限，可靠**
- **不要用遍历所有进程 open_files 的方法**（受权限限制，易返回空）
- C++ 调用要点：
  ```cpp
  #include <RestartManager.h>
  #pragma comment(lib, "Rstrtmgr.lib")
  ```
  - `RM_PROCESS_INFO` 结构体：`ProcessId(DWORD)` + `ProcessStartTime(FILETIME)` + `strAppName[256]` + `strServiceShortName[64]` + `ApplicationType` + `AppStatus` + `tsSessionId` + `bRestartable`
  - 文件不存在时直接返回空，**不调用**上述流程
  - 返回占用进程的 进程名（从 `strAppName` 取宽字符串）+ PID
  - 伪代码：
    ```cpp
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
    ```

### ⚠️ 跨卷大文件提示
- 送回收站时，Windows 把文件 move 到该盘的 `$Recycle.Bin`：同卷快，跨卷退化为完整数据复制（5GB 文件删半分钟很正常）
- 删除前若检测到大文件（>500MB），提示用户"送回收站可能较慢，是否改用永久删除"

### ⚠️ 扫描重入保护
- 扫描/删除进行中，禁用相关按钮（扫描、删除、浏览），防止用户并发触发导致数据竞争。用 `QPushButton::setEnabled(false)` 控制

### ⚠️ 扫描重试时的状态保护
- 搜索引擎失败自动降级到 Win32 重试时，必须重新设置"忙碌"标志、显示进度条、禁用按钮，否则用户能在重试期间并发触发第二次扫描，导致数据错乱

---

## 五、技术实现要点（C++ + Qt）

### 推荐技术栈（已锁定决策）
- **语言**：C++17
- **GUI 框架**：Qt 6（Widgets 模块，不要用 QML，工具类应用 Widgets 更稳）
- **编译器**：MSVC（与 Qt 官方预编译包匹配，Windows 标配）
- **构建**：CMake（`CMAKE_PREFIX_PATH` 指向 Qt 安装目录）
- **链接/交付**：**动态库 + `windeployqt` 打包成文件夹**（官方预编译 Qt，构建快；产物约 30-60MB 可直接运行文件夹）。不做静态编译（自编静态 Qt 耗时数小时，性价比低）

### 关键 API 映射
| 功能 | Python(旧) | C++/Qt(新) |
|------|-----------|------------|
| 文件删除-回收站 | send2trash | `SHFileOperationW` |
| 文件删除-永久 | os.remove | `DeleteFileW` 或 `QFile::remove` |
| 占用检测 | ctypes+Restart Manager | `RestartManager.h` + `Rstrtmgr.lib` |
| 目录遍历 | os.walk | `FindFirstFileW`/`FindNextFileW` 或 `QDir::entryInfoList` |
| 调 fd.exe / es.exe | subprocess.Popen | `QProcess` |
| HTTP 下载 | urllib | `QNetworkAccessManager` |
| UAC 提权 | ShellExecuteW | manifest `requireAdministrator` 或 `ShellExecuteW` runas |
| JSON 配置 | json | `QJsonDocument`/`QJsonObject` |
| ZIP 解压 | zipfile | `QZipReader`（私有 `<private/qzipreader_p.h>`）或 minizip |
| 调色盘 | - | `QColorDialog` |
| 主题/QSS | - | QSS 样式表 + `QStyleFactory` |

### 线程模型
- 扫描/删除/下载/进程检测都在 **`QThread`** 工作线程执行，通过信号槽（`signals/slots`，默认 `Qt::QueuedConnection` 跨线程）更新 UI
- 主线程只做 UI 更新，**绝不阻塞**
- 取消机制：工作线程检查 `std::atomic<bool> m_cancel`，`requestInterruption()` + 自定义取消标志
- 进程检测也放工作线程，避免阻塞 UI
- `QProcess` 调 fd/es 时，在专门的工作对象里用 `readyReadStandardOutput` 信号逐行解析

### 项目结构建议
```
A:\ArchiveCleanerQt\
├── CMakeLists.txt
├── src\
│   ├── main.cpp                  # 入口 + UAC 提权检测 + manifest 嵌入
│   ├── MainWindow.h/.cpp         # 主窗口
│   ├── SettingsDialog.h/.cpp     # 设置弹窗
│   ├── DeleteProgressDialog.h/.cpp
│   ├── core\
│   │   ├── ArchiveFile.h         # 数据结构（路径/大小/时间/选中态）
│   │   ├── ISearchEngine.h       # 搜索引擎抽象接口
│   │   ├── WalkEngine.h/.cpp     # 档位1 Win32 遍历
│   │   ├── FdEngine.h/.cpp       # 档位2 fdfind
│   │   ├── EverythingEngine.h/.cpp # 档位3 es.exe
│   │   ├── MftEngine.h/.cpp      # 档位4 MFT/USN
│   │   ├── Deleter.h/.cpp        # 删除逻辑（回收站/永久）+ 超时
│   │   ├── ProcessChecker.h/.cpp # Restart Manager 占用检测
│   │   ├── PathUtils.h/.cpp      # 路径归一化（致命坑，独立模块）
│   │   └── Config.h/.cpp         # JSON 配置读写
│   ├── network\
│   │   └── ToolDownloader.h/.cpp # fd.exe / es.exe 自动下载
│   └── ui\
│       └── ArchiveCardWidget.h/.cpp # 文件卡片 widget
├── resources\
│   ├── app.manifest             # requireAdministrator
│   ├── app.rc                   # 资源文件（图标 + manifest）
│   ├── style.qss                # 主题样式
│   └── icons\
└── config.json                  # 运行时生成
```

---

## 六、工作目录与交付

- 工作目录：`A:\ArchiveCleanerQt\`（新建独立目录，与旧 Python 项目分开）
- 最终产物：`windeployqt` 打包的可运行文件夹（exe + Qt DLL + 插件，约 30-60MB）
- 配置文件：与 exe 同目录的 config.json
- 删除日志：与 exe 同目录的 delete_log.txt
- 可选外部工具：fd.exe / es.exe（首次运行按需自动下载到 exe 同目录）

### config.json 字段（沿用旧版结构并扩展）
```json
{
  "search_engine": "walk",          // walk / fdfind / everything / mft
  "fd_path": ".\\fd.exe",
  "es_path": ".\\es.exe",
  "appearance": "系统",              // 系统/浅色/深色
  "accent_color": "#2563EB",        // 新增：主题强调色
  "delete_mode": "送入回收站（可恢复）",  // 送入回收站（可恢复）/ 永久删除（不可恢复）
  "include_subfolders": true,
  "last_scan_path": ""              // 新增：记住上次扫描路径
}
```

---

## 七、开发顺序建议

1. **环境检查**：确认 Qt 6（MSVC 组件）+ Visual Studio Build Tools（MSVC）+ CMake 已安装。若未装，指导安装（Qt 在线安装器约 3-4GB，选 `MSVC 2022 64-bit` 组件；VS Build Tools 约 3-6GB）
2. **项目骨架**：`CMakeLists.txt` + `main.cpp` + `MainWindow` 基本布局（控制区/列表区/底部/状态栏静态占位），跑通编译运行
3. **路径归一化模块**（`PathUtils`）——**第一个写，因为后面所有功能都依赖它**，必须把致命坑从源头堵住
4. **搜索引擎抽象 + 档位1（Win32 遍历）**：扫描出结果，进度上报，卡片列表渲染
5. **删除流程**：SHFileOperationW（回收站）+ DeleteFileW（永久）+ 进度对话框 + 取消 + 超时保护 + COM 初始化
6. **占用进程检测**：Restart Manager，放工作线程
7. **档位2 fdfind**：QProcess 调 fd.exe + 解析 + 自动下载
8. **档位3 Everything**：QProcess 调 es.exe（`-utf8`）+ 解析 + 自动下载
9. **档位4 MFT**：DeviceIoControl + FSCTL_ENUM_USN_DATA + 管理员检测 + 非 NTFS 降级
10. **设置弹窗** + 主题色自定义（QColorDialog）+ 配置持久化
11. **UAC 提权**（manifest + 防循环重启）
12. **搜索引擎降级链** + 重入/重试状态保护
13. **`windeployqt` 打包测试**

---

## 八、参考：旧版实现与已知数据
- 旧版 Python 源码：`A:\ArchiveCleaner\ArchiveCleaner.py`（功能逻辑参考，**不要照搬 GUI 实现**）
- 崩溃日志：`A:\ArchiveCleaner\crash.log`（记录了 tkinter 的 `No more menus can be allocated` 和 grab 死锁，正是要规避的）
- 删除日志：`A:\ArchiveCleaner\delete_log.txt`（第 8-20 行全是路径分隔符混合导致的删除失败，**验证了路径归一化的必要性**）
- 配置示例：`A:\ArchiveCleaner\config.json`
- es.exe 已下载：`A:\ArchiveCleaner\es.exe`
- Everything 主程序已安装：`C:\Program Files\Everything\`

---

请基于以上需求，用 C++ + Qt 从零实现这个程序。先检查开发环境（是否已装 Qt 6 MSVC + VS Build Tools + CMake），然后开始搭建项目。
