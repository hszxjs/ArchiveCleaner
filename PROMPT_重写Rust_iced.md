# 项目提示词：用 Rust + iced 重写「压缩包清理工具」

请用 **Rust + iced** 从零开发一个 Windows 桌面应用：「压缩包清理工具 ArchiveCleaner」。

这个程序之前用 Python + customtkinter 实现，遇到大量 GUI 框架层面的顽固问题（窗口黑屏、grab 死锁、文字漂移、菜单资源耗尽 `No more menus can be allocated`、打包臃肿 9MB+）。现在用 Rust + iced 重写，彻底解决这些问题。

Rust 版的核心优势：**内存安全 + 默认单 exe 分发（cargo 静态链接，无需随附一堆 DLL）+ 现代异步运行时**。下面的提示词包含完整功能需求、所有踩过的坑（务必避开）、搜索引擎四档（含新增的 fdfind）、以及 Rust 特定的技术实现要点。

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

> 这是本程序最核心的差异点，必须四档全部实现。每档都封装成统一的 trait `SearchEngine`，返回 `Vec<ArchiveFile>`，UI 层不关心具体实现，可平滑切换/降级。

### 档位1：Win32 遍历（os.walk 等价，默认通用兜底）
- 用 `windows` crate 的 `FindFirstFileW`/`FindNextFileW` 递归遍历，或直接用 `std::fs::read_dir` 递归
- 兼容所有盘符、所有文件系统，无需任何依赖
- 扫描时实时上报进度（已扫描目录数、已找到文件数）——通过 iced 的 channel/subscription 把进度推到 UI
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
- fd 输出 UTF-8（这点比 es.exe 强，中文路径不乱码）
- 前提：fd.exe 需放到程序同目录或 PATH
- **内置下载功能**：调用 GitHub API `https://api.github.com/repos/sharkdp/fd/releases/latest` 拿最新版本号和下载地址，下载 `fd-vX.Y.Z-x86_64-pc-windows-msvc.zip`，解压出 fd.exe
- 调用方式：`tokio::process::Command`（异步，不阻塞 UI 线程）

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
- 调用方式：`tokio::process::Command`

### 档位4：MFT（最快，需管理员+NTFS）
- 直接读 NTFS MFT（主文件表）或 USN 日志
- 用 `windows` crate 调 `DeviceIoControl` + `FSCTL_ENUM_USN_DATA`（比解析 MFT 文件本身更安全、API 更稳定），遍历卷上所有文件记录，过滤扩展名
- 需要：管理员权限 + NTFS 文件系统（非 NTFS 卷自动降级）
- 用 Rust 调用比 Python 可靠得多（强类型、零成本抽象、无 GC 停顿）

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

### 设置弹窗
- 搜索引擎四选一（Win32 / fdfind / Everything / MFT）
- fdfind 选中时显示 fd.exe 路径（输入框 + 浏览按钮 + 自动检测按钮 + 下载按钮）
- Everything 选中时显示 es.exe 路径（同上）
- 删除默认方式下拉
- 外观主题（深/浅/系统）
- **主题色自定义**：预设强调色快选（蓝/绿/红/紫/橙）+ 调色盘任意自定义颜色（需自己实现或用 `colorpicker` crate / 简易 RGB 滑块，iced 无内置 QColorDialog）
- 保存按钮固定在底部，永不被挤出

### 删除进度对话框
- 进度条 + "第 3/10 个 · xxx.zip (12.4 MB)"
- "已用 5秒 · 预计剩余 12秒 · 成功2 失败0"
- 取消按钮

### iced GUI 特别注意（性能坑）
- **大量文件时必须虚拟化列表**：iced 没有内置虚拟列表，几千个卡片 widget 一次性塞进 scrollable 会严重卡顿甚至 OOM。实现方案二选一：
  - ① 只渲染可视区域的卡片（自己根据 scroll offset 计算可见区间，配合固定行高）
  - ② 用 `iced_lazy`（若版本兼容）或社区虚拟列表方案
  - ③ 最简单：分页，每页 200 条，列表底部"加载更多"
- **中文字体**：iced 在 Windows 上默认字体可能不含 CJK 字形，中文会显示为方框。务必在启动时用 `iced::application` 的字体加载机制嵌入一个中文字体（如思源黑体子集），或显式指定系统字体回退链（"Microsoft YaHei"）
- **DPI 缩放**：iced 对高 DPI 支持尚在完善，测试时确认 150%/200% 缩放下不糊不溢出

---

## 四、关键踩坑经验（务必避开）

### ⚠️ 路径分隔符（致命，已实测复现）
- 用户输入路径可能用正斜杠（如 `B:/2023年`），递归拼接后路径混用正反斜杠（`B:/2023年\xxx.zip`）
- **送回收站的 SHFileOperationW 不认这种混合路径，会 100% 失败**（旧版 delete_log.txt 第8-20行全是这个 bug：`ProcessLookupError: 系统找不到指定的路径`）
- 修复：**所有路径在进入数据结构前必须归一化**为统一反斜杠 + 绝对路径。用 `PathCanonicalizeW` 或 `std::path::Path` 规范化，注意 `std::path` 在 Windows 上会把正斜杠也当分隔符但不会主动统一，需手动 `replace('/', "\\")` 再 canonicalize

### ⚠️ 失败后不能卡住（致命）
- 之前对每个删除失败的文件都跑"占用进程检测"，对不存在的路径会卡数十秒
- **文件不存在时应立即跳过**（不跑进程检测），只有文件确实存在却删不掉时才检测占用
- 用 `PathFileExistsW` 或 `std::fs::metadata` 先判断存在性

### ⚠️ SHFileOperationW / IFileOperation 死锁（致命）
- 对非法路径调用 SHFileOperationW 的 COM 会挂死，线程无法中断，进程变成无法强杀的僵尸
- 修复：① 路径归一化从源头避免非法路径 ② 单文件删除加超时保护（`tokio::time::timeout` 包裹） ③ 删除在异步 task 执行，超时后复查文件是否仍存在来修正结果
- COM 初始化：若用 SHFileOperationW 或 IFileOperation，**调用线程必须先 `CoInitializeEx`**（Rust 用 `windows` crate 的 `CoInitializeEx`），用完 `CoUninitialize`。建议在删除 task 开始时初始化

### ⚠️ UAC 提权（Windows 10/11 规范）
- 程序启动时应弹 UAC 请求管理员权限（MFT 档位需要）
- 用户同意 → 以管理员运行（MFT 可用）；拒绝 → 降级普通权限（MFT 不可用，自动用 Win32 遍历）
- **规范做法**：用 manifest 声明 `requireAdministrator`（双击直接弹 UAC）。Rust 可通过 `cargo-build` 配合 `.cargo/config.toml` 的 linker 参数，或用 `embed-resource`/`winres` crate 嵌入 `.manifest` 文件到 exe 资源中
- **防循环**：提权后的进程要能识别自己已被提权，不重复弹窗（用命令行参数标记 `--elevated`，比环境变量可靠——环境变量跨 UAC 边界可能丢失）。做法：检测到未提权时 `ShellExecuteW` runas 重启自身并带上 `--elevated` 参数
- 提权检测：用 `OpenProcessToken` + `GetTokenInformation(TokenElevation)` 判断当前是否已提权

### ⚠️ 占用进程检测的正确方法
- 用 **Windows Restart Manager API**（rstrtmgr.dll：RmStartSession / RmRegisterResources / RmGetList / RmEndSession）
- 这是 Windows 资源管理器提示"文件被占用"时用的同一套 API，**无需管理员权限，可靠**
- **不要用遍历所有进程 open_files 的方法**（受权限限制，易返回空）
- Rust 调用要点（`windows` crate，feature 需开 `Win32_System_RestartManager`）：
  - `RM_UNIQUE_PROCESS` / `RM_PROCESS_INFO` 结构体：`ProcessId(DWORD)` + `ProcessStartTime(FILETIME)` + `strAppName([u16;256])` + `strServiceShortName([u16;64])` + `ApplicationType` + `AppStatus` + `tsSessionId` + `bRestartable`
  - 文件不存在时直接返回空，**不调用** Restart Manager
  - 返回占用进程的 进程名（从 `strAppName` 取宽字符串）+ PID
  - `RmRegisterResources` 的文件参数是 `PCWSTR` 数组

### ⚠️ 跨卷大文件提示
- 送回收站时，Windows 把文件 move 到该盘的 `$Recycle.Bin`：同卷快，跨卷退化为完整数据复制（5GB 文件删半分钟很正常）
- 删除前若检测到大文件（>500MB），提示用户"送回收站可能较慢，是否改用永久删除"

### ⚠️ GUI 注意事项（Python 版的坑，iced 需注意）
- 模态对话框必须在窗口**完全显示后**再弹出，否则事件循环异常。iced 用 `iced::Task::perform` 或弹出式 dialog widget 处理，避免在初始化阶段阻塞
- 状态栏文字用单行，不要塞多行文本导致布局抖动
- 进度条：能算百分比时用确定模式（`progress_bar` 设 0.0~1.0），无法预估总量时显示"搜索中…"不定态（`progress_bar` 设为不确定样式或显示动画文本）

### ⚠️ 扫描重入保护
- 扫描/删除进行中，禁用相关按钮（扫描、删除、浏览），防止用户并发触发导致数据竞争。用 iced 的 state 标志位控制 widget 的 `disabled` 属性

### ⚠️ 扫描重试时的状态保护
- 搜索引擎失败自动降级到 Win32 重试时，必须重新设置"忙碌"标志、显示进度条、禁用按钮，否则用户能在重试期间并发触发第二次扫描，导致数据错乱

---

## 五、技术实现要点（Rust + iced）

### 推荐技术栈
- **语言**：Rust（edition 2021 或 2024）
- **GUI**：iced 0.12+（Elm 架构：State + Message + update + view）
- **Windows API**：`windows` crate（windows-rs，微软官方维护）
- **异步运行时**：iced 内置 tokio，子进程/超时/并发用 `tokio::process`、`tokio::time`、`tokio::sync`
- **JSON 配置**：`serde` + `serde_json`
- **ZIP 解压**（下载 fd/es.exe 时）：`zip` crate
- **HTTP 下载**：`reqwest`（blocking 或 async）或 `ureq`
- **嵌入 manifest/资源**：`embed-resource` 或 `winres` crate
- **中文字体嵌入**：`iced::font::from_data` + `include_bytes!`

### Cargo.toml 关键依赖（示意）
```toml
[dependencies]
iced = { version = "0.12", features = ["tokio", "debug", "image"] }
tokio = { version = "1", features = ["full"] }
serde = { version = "1", features = ["derive"] }
serde_json = "1"
reqwest = { version = "0.12", features = ["blocking", "json"] }
zip = "2"

[target.'cfg(windows)'.dependencies]
windows = { version = "0.58", features = [
    "Win32_Foundation",
    "Win32_Storage_FileSystem",
    "Win32_UI_Shell",                     # SHFileOperationW, SHFILEOPSTRUCTW
    "Win32_UI_WindowsAndMessaging",
    "Win32_System_RestartManager",        # RmStartSession 等
    "Win32_System_Threading",             # OpenProcessToken, GetTokenInformation
    "Win32_Security",                     # TOKEN_ELEVATION
    "Win32_System_IO",                    # DeviceIoControl
    "Win32_System_Diagnostics_Debug",
]}

[build-dependencies]
embed-resource = "3"
```

### 关键 API 映射
| 功能 | 旧 Python | Rust(新) |
|------|-----------|----------|
| 文件删除-回收站 | send2trash | `SHFileOperationW`（`Win32_UI_Shell`） |
| 文件删除-永久 | os.remove | `std::fs::remove_file` 或 `DeleteFileW` |
| 占用检测 | ctypes+Restart Manager | `windows::Win32::System::RestartManager` |
| 目录遍历 | os.walk | `std::fs::read_dir` 递归 / `FindFirstFileW` |
| 调 es.exe/fd.exe | subprocess.Popen | `tokio::process::Command` |
| HTTP 下载 | urllib | `reqwest` |
| UAC 提权 | ShellExecuteW | manifest requireAdministrator + `ShellExecuteW` runas 重启 |
| JSON 配置 | json | `serde_json` |
| ZIP 解压 | zipfile | `zip` crate |
| 调色盘 | - | 自实现 RGB 滑块 / `colorpicker` crate |
| 主题/QSS | - | `iced::Theme` + 自定义 `Palette` |

### SHFileOperationW 调用要点（Rust）
```rust
use windows::Win32::UI::Shell::*;
use windows::core::PCWSTR;

// 1. 路径归一化为绝对反斜杠路径
// 2. 构造双 \0 结尾的宽字符串缓冲区
let mut files: Vec<u16> = path_wide.iter().copied()
    .chain([0, 0]).collect(); // 两个 \0 结尾
let op = SHFILEOPSTRUCTW {
    hwnd: None,
    wFunc: FO_DELETE,
    pFrom: PCWSTR(files.as_mut_ptr()),
    pTo: PCWSTR::null(),
    fFlags: FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT,
    fAnyOperationsAborted: FALSE,
    hNameMappings: None,
    lpszProgressTitle: PCWSTR::null(),
};
// 调用线程需先 CoInitializeEx
let ret = unsafe { SHFileOperationW(&op) };
// ret == 0 成功
```

### 线程/异步模型
- iced 主循环跑在 UI 线程，**绝不阻塞**
- 扫描/删除/下载/进程检测都作为 `iced::Task`（异步 task）执行，通过 `Message` 把进度/结果回传 UI
- 取消机制：task 内部持有 `CancellationToken`（`tokio_util::sync::CancellationToken`）或 `Arc<AtomicBool>`，定期检查
- 子进程（fd/es）用 `tokio::process::Command`，逐行读 stdout 异步解析，边读边上报

### 单 exe 构建与分发
```bash
# 在 .cargo/config.toml 设置静态链接 CRT（msvc target 默认动态链 VC++ Runtime）
# [build]
# rustflags = ["-C", "target-feature=+crt-static"]

cargo build --release
# 产出 target/release/ArchiveCleaner.exe —— 单文件，约 8-15MB
```
- 默认 release 构建即为单 exe（所有 Rust 依赖静态链接进二进制）
- 唯一可选的外部依赖：VC++ Redistributable（用 `+crt-static` 可消除）
- manifest 通过 `embed-resource` 在 build.rs 嵌入 exe 资源段，实现双击弹 UAC

---

## 六、工作目录与交付

- 工作目录：新建 `A:\ArchiveCleanerRust\`（独立于旧 Python 项目）
- 最终产物：单个 exe（cargo release）+ 可选的 fd.exe / es.exe（首次运行按需自动下载到 exe 同目录）
- 配置文件：与 exe 同目录的 config.json
- 删除日志：与 exe 同目录的 delete_log.txt

---

## 七、开发顺序建议

1. **环境检查**：`cargo --version`、`rustc --version`、确认 msvc target 已装（`rustup target list --installed`）
2. **项目骨架**：`cargo init`，配好 Cargo.toml（iced + windows + tokio + serde），跑通一个空 iced 窗口（含中文标题，确认字体不方框）
3. **主窗口布局**：控制区 + 列表区 + 底部按钮 + 状态栏（先静态占位）
4. **搜索引擎 trait + 档位1（Win32 遍历）**：扫描能出结果，进度能上报，卡片列表能渲染（含虚拟化/分页处理大量项）
5. **删除流程**：SHFileOperationW（回收站）+ remove_file（永久）+ 路径归一化 + 进度对话框 + 取消 + 超时保护
6. **占用进程检测**：Restart Manager，放异步 task
7. **档位2 fdfind**：tokio 调 fd.exe + 解析 + 自动下载
8. **档位3 Everything**：tokio 调 es.exe（`-utf8`）+ 解析 + 自动下载
9. **档位4 MFT**：DeviceIoControl + FSCTL_ENUM_USN_DATA + 管理员检测 + 非 NTFS 降级
10. **设置弹窗** + 主题色自定义 + 配置持久化
11. **UAC 提权**（manifest + 防循环重启）
12. **搜索引擎降级链** + 重入/重试状态保护
13. **测试 + `cargo build --release` 产出单 exe**

---

## 八、参考：旧版实现与已知数据
- 旧版 Python 源码：`A:\ArchiveCleaner\ArchiveCleaner.py`（功能逻辑参考，**不要照搬 GUI 实现**）
- 崩溃日志：`A:\ArchiveCleaner\crash.log`（记录了 tkinter 的 `No more menus can be allocated` 和 grab 死锁，正是要规避的）
- 删除日志：`A:\ArchiveCleaner\delete_log.txt`（第 8-20 行全是路径分隔符混合导致的删除失败，**验证了路径归一化的必要性**）
- 配置示例：`A:\ArchiveCleaner\config.json`
- es.exe 已下载：`A:\ArchiveCleaner\es.exe`
- Everything 主程序已安装：`C:\Program Files\Everything\`

---

请基于以上需求，用 Rust + iced 从零实现这个程序。先检查开发环境（`cargo`/`rustc` 是否就绪、msvc target 是否安装），然后开始搭建项目。
