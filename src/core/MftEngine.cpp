#include "MftEngine.h"
#include "PathUtils.h"
#include "ArchiveFile.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winioctl.h>
#include <fileapi.h>

#include <unordered_map>
#include <vector>
#include <memory>
#include <string>

namespace ac {

namespace {

// 从 USN_RECORD_V2 取文件名（UTF-16）
std::wstring recordFileName(const USN_RECORD_V2* r) {
    const wchar_t* name = reinterpret_cast<const wchar_t*>(
        reinterpret_cast<const BYTE*>(r) + r->FileNameOffset);
    int len = r->FileNameLength / sizeof(wchar_t);
    return std::wstring(name, len);
}

// FRN → 目录完整路径 映射
using FrnMap = std::unordered_map<uint64_t, std::wstring>;

// 枚举卷上所有记录，对每条调用 cb
template <typename Cb>
void enumUsnRecords(HANDLE hVol, USN maxUsn, const std::atomic<bool>& cancel, Cb cb) {
    DWORDLONG startFrn = 0;
    const DWORD BUF_SIZE = 1 << 20;
    auto buf = std::make_unique<BYTE[]>(BUF_SIZE);

    MFT_ENUM_DATA_V0 med{};
    med.StartFileReferenceNumber = 0;
    med.LowUsn = 0;
    med.HighUsn = maxUsn;

    while (!cancel.load(std::memory_order_relaxed)) {
        med.StartFileReferenceNumber = startFrn;
        DWORD bytesReturned = 0;
        BOOL ok = DeviceIoControl(hVol, FSCTL_ENUM_USN_DATA,
            &med, sizeof(med), buf.get(), BUF_SIZE, &bytesReturned, nullptr);
        if (!ok || bytesReturned == 0) break;

        if (bytesReturned < sizeof(USN)) break;
        USN nextUsn = *reinterpret_cast<USN*>(buf.get());
        startFrn = nextUsn;

        DWORD offset = sizeof(USN);
        while (offset + sizeof(USN_RECORD_V2) <= bytesReturned) {
            const USN_RECORD_V2* r = reinterpret_cast<const USN_RECORD_V2*>(buf.get() + offset);
            if (r->RecordLength == 0) break;
            if (offset + r->RecordLength > bytesReturned) break;
            cb(r);
            offset += r->RecordLength;
        }
        if (bytesReturned < BUF_SIZE) break;
    }
}

} // namespace

bool MftEngine::isAvailable() const {
    return path::isElevated();
}

std::wstring MftEngine::unavailableReason() const {
    if (!path::isElevated()) return L"MFT 搜索需要管理员权限";
    return {};
}

std::pair<bool, std::wstring> MftEngine::run(
    const std::wstring& folder, bool recursive,
    const std::atomic<bool>& cancel,
    const FoundCallback& onFound,
    const ProgressCallback& onProgress) {

    if (!path::isElevated()) {
        return {false, unavailableReason()};
    }

    std::wstring normFolder = path::normalize(folder);
    if (!path::exists(normFolder)) {
        return {false, L"路径不存在：" + normFolder};
    }

    std::wstring drv = path::driveOf(normFolder);
    if (drv.empty()) return {false, L"无法识别盘符"};
    if (!path::isNtfs(normFolder)) {
        return {false, L"MFT 仅支持 NTFS：" + drv};
    }

    std::wstring volPath = L"\\\\.\\" + drv + L":";
    HANDLE hVol = CreateFileW(volPath.c_str(),
        GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hVol == INVALID_HANDLE_VALUE) {
        return {false, L"无法打开卷（需管理员）"};
    }
    struct HandleGuard { HANDLE h; ~HandleGuard() { if (h) CloseHandle(h); } } guard{ hVol };

    USN_JOURNAL_DATA_V0 ujd{};
    DWORD br = 0;
    if (!DeviceIoControl(hVol, FSCTL_QUERY_USN_JOURNAL, nullptr, 0, &ujd, sizeof(ujd), &br, nullptr)) {
        return {false, L"FSCTL_QUERY_USN_JOURNAL 失败"};
    }
    USN maxUsn = ujd.NextUsn;

    std::wstring rootPrefix = normFolder;
    if (!rootPrefix.empty() && rootPrefix.back() != L'\\') rootPrefix.push_back(L'\\');

    // 取根目录 FRN
    uint64_t rootFrn = 0;
    {
        HANDLE hRoot = CreateFileW(normFolder.c_str(),
            GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        if (hRoot != INVALID_HANDLE_VALUE) {
            BY_HANDLE_FILE_INFORMATION info{};
            if (GetFileInformationByHandle(hRoot, &info)) {
                rootFrn = (static_cast<uint64_t>(info.nFileIndexHigh) << 32) | info.nFileIndexLow;
            }
            CloseHandle(hRoot);
        }
    }

    // 第一遍：建目录 FRN→路径 映射（前缀匹配，只收扫描根下）
    FrnMap dirMap;
    dirMap.insert({rootFrn, rootPrefix});
    enumUsnRecords(hVol, maxUsn, cancel, [&](const USN_RECORD_V2* r) {
        if (!(r->FileAttributes & FILE_ATTRIBUTE_DIRECTORY)) return;
        std::wstring name = recordFileName(r);
        // 跳过回收站和系统目录（不加入映射，其下文件自然拼不出路径被排除）
        if (_wcsicmp(name.c_str(), L"$RECYCLE.BIN") == 0) return;
        if (_wcsicmp(name.c_str(), L"System Volume Information") == 0) return;
        auto it = dirMap.find(static_cast<uint64_t>(r->ParentFileReferenceNumber));
        if (it != dirMap.end()) {
            std::wstring full = it->second + name + L"\\";
            if (full.size() >= rootPrefix.size() &&
                _wcsnicmp(full.c_str(), rootPrefix.c_str(), rootPrefix.size()) == 0) {
                dirMap.insert({static_cast<uint64_t>(r->FileReferenceNumber), full});
            }
        }
    });

    // 第二遍：枚举文件，按扩展名过滤，拼路径
    int filesFound = 0;
    enumUsnRecords(hVol, maxUsn, cancel, [&](const USN_RECORD_V2* r) {
        if (r->FileAttributes & FILE_ATTRIBUTE_DIRECTORY) return;
        std::wstring name = recordFileName(r);
        if (!ArchiveFile::isArchiveExt(name)) return;

        if (!recursive && r->ParentFileReferenceNumber != rootFrn) return;

        auto it = dirMap.find(static_cast<uint64_t>(r->ParentFileReferenceNumber));
        if (it == dirMap.end()) return;
        std::wstring full = it->second + name;

        // GetFileAttributesExW 查大小/时间
        WIN32_FILE_ATTRIBUTE_DATA fad{};
        if (!GetFileAttributesExW(full.c_str(), GetFileExInfoStandard, &fad)) return;
        if (fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) return;

        ArchiveFile af;
        af.path = path::normalize(full);
        af.name = name;
        LARGE_INTEGER sz;
        sz.LowPart = fad.nFileSizeLow; sz.HighPart = fad.nFileSizeHigh;
        af.size = sz.QuadPart;
        af.mtimeMs = filetimeToUnixMs(fad.ftLastWriteTime.dwLowDateTime,
                                      fad.ftLastWriteTime.dwHighDateTime);
        onFound(af);
        ++filesFound;
        if (filesFound % 20 == 0) onProgress(0, filesFound);
    });

    onProgress(0, filesFound);
    if (cancel.load()) return {false, L"已取消"};
    return {true, {}};
}

} // namespace ac
