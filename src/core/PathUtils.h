#ifndef AC_PATH_UTILS_H
#define AC_PATH_UTILS_H

#include <string>
#include <vector>

namespace ac {
namespace path {

// 路径归一化：① 正斜杠→反斜杠 ② GetFullPathNameW 解析为绝对路径（消解 .. 和 .）
// ⚠️ 这是项目最关键的工具（堵住"正反斜杠混合导致 SHFileOperation 100% 失败"的致命 bug）。
// 所有路径进入数据结构前必须归一化。
// 失败时（路径非法）返回尽量处理后的原值，不抛异常。
std::wstring normalize(const std::wstring& input);

// 文件是否存在（归一化后用 PathFileExistsW）
bool exists(const std::wstring& path);

// 提取盘符（大写字母，如 "C"）。无盘符返回空。
std::wstring driveOf(const std::wstring& normalizedPath);

// 判断是否 NTFS 卷（给定该卷上任意路径）。用于 MFT 档位降级判断。
bool isNtfs(const std::wstring& pathOnVolume);

// 当前进程是否以管理员权限运行（用于 MFT 档位和 UAC）
bool isElevated();

// 程序所在目录（用 GetModuleFileNameW，替代 QCoreApplication::applicationDirPath）
std::wstring appDir();

// 从路径取文件名（最后一个反斜杠或斜杠之后的部分）
std::wstring fileName(const std::wstring& path);

// 从路径取父目录（去掉最后的文件名和反斜杠；无反斜杠返回空）
std::wstring parentDir(const std::wstring& normalizedPath);

// === 程序目录保护（防止误删游戏/程序资源）===

// 已安装程序：InstallLocation（小写带尾反斜杠，前缀匹配用）+ 注册表 DisplayName
struct InstalledProgram {
    std::wstring dir;
    std::wstring name;
};

// 枚举注册表中所有已安装程序（含国产游戏的自定义安装目录）。
// 首次调用后缓存（注册表不常变）。
const std::vector<InstalledProgram>& installedPrograms();

// 兼容旧接口：只取 InstallLocation 前缀列表。
const std::vector<std::wstring>& installedProgramDirs();

// 判断路径是否以任一前缀开头（大小写不敏感）。prefixes 为小写带尾反斜杠。
bool startsWithAny(const std::wstring& path, const std::vector<std::wstring>& prefixes);

// 判断目录中是否含有 .exe 文件（绿色版游戏的资源包总与游戏 exe 同目录）。
// 一次 FindFirstFileW 调用。
bool dirContainsExe(const std::wstring& dir);

// 应用识别结果：root=软件根目录（用于展示），name=给用户看的显示名
struct AppIdentity {
    std::wstring root;
    std::wstring name;
};

// exe 版本资源显示名：FileDescription → ProductName → 空（正规软件都填了中文显示名）
std::wstring exeDisplayName(const std::wstring& exePath);

// 目录中第一个 exe（优先与目录同名的 exe）。没有返回空。
std::wstring firstExeInDir(const std::wstring& dir);

// 从文件所在目录识别所属软件（弹窗分组标题用）：
// ① 向上逐级找 exe，读其版本资源显示名（最具体，如"网易云音乐"）
// ② 反查注册表：目录在哪个软件的 InstallLocation 下，用其 DisplayName
// ③ 兜底：路径里最后一个有意义的目录段（跳过 data/cache/版本号等）
// 全部失败返回空 name。
AppIdentity findAppIdentity(const std::wstring& fileDir);

// 判断目录是否是项目根（含 .git/package.json/Cargo.toml/go.mod/pom.xml/
// build.gradle/CMakeLists.txt/*.sln 任一标记文件）。
// 源码项目里的压缩包是构建资源或素材，删除会破坏项目。
bool dirContainsProjectMarker(const std::wstring& dir);

}} // namespace ac::path

#endif // AC_PATH_UTILS_H
