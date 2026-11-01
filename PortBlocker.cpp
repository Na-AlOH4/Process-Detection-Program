#include "PortBlocker.h"

#include <windows.h>
#include <fwpmu.h>

#include <string>
#include <vector>
#include <algorithm>

#pragma comment(lib, "fwpuclnt.lib")
#pragma comment(lib, "rpcrt4.lib")

namespace portblocker {

    namespace {

        thread_local uint32_t g_lastWinError = 0;

        // ---------------- 编码转换 ----------------
        std::wstring ToWide(const std::string& s) {
            if (s.empty()) return {};
            int n = MultiByteToWideChar(CP_ACP, 0, s.c_str(),
                static_cast<int>(s.size()), nullptr, 0);
            if (n <= 0) return {};
            std::wstring w(static_cast<size_t>(n), L'\0');
            MultiByteToWideChar(CP_ACP, 0, s.c_str(),
                static_cast<int>(s.size()), w.data(), n);
            return w;
        }

        std::string ToNarrow(const std::wstring& w) {
            if (w.empty()) return {};
            int n = WideCharToMultiByte(CP_ACP, 0, w.c_str(),
                static_cast<int>(w.size()),
                nullptr, 0, nullptr, nullptr);
            if (n <= 0) return {};
            std::string s(static_cast<size_t>(n), '\0');
            WideCharToMultiByte(CP_ACP, 0, w.c_str(),
                static_cast<int>(w.size()),
                s.data(), n, nullptr, nullptr);
            return s;
        }

        // ---------------- 权限 ----------------
        bool IsElevated() noexcept {
            BOOL elevated = FALSE;
            HANDLE token = nullptr;
            if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
                TOKEN_ELEVATION elev{};
                DWORD sz = sizeof(elev);
                if (GetTokenInformation(token, TokenElevation, &elev, sz, &sz)) {
                    elevated = elev.TokenIsElevated;
                }
                CloseHandle(token);
            }
            return elevated != FALSE;
        }

        // ---------------- 哈希与 GUID ----------------
        // 对宽字符路径做 FNV-1a（与旧版一致，保证规则可被解除）
        uint32_t Fnv1a(const std::wstring& s) noexcept {
            uint32_t h = 2166136261u;
            for (wchar_t c : s) {
                h ^= static_cast<uint32_t>(c & 0xFF);         h *= 16777619u;
                h ^= static_cast<uint32_t>((c >> 8) & 0xFF);  h *= 16777619u;
            }
            return h;
        }

        GUID MakeFilterKey(uint16_t port, uint8_t proto,
            bool inbound, uint32_t appHash) noexcept {
            GUID g{};
            g.Data1 = 0x504F5254;                 // 'PORT'
            g.Data2 = port;
            g.Data3 = static_cast<uint16_t>(
                (static_cast<uint16_t>(proto) << 8) | (inbound ? 1u : 0u));
            g.Data4[0] = static_cast<uint8_t>(appHash & 0xFF);
            g.Data4[1] = static_cast<uint8_t>((appHash >> 8) & 0xFF);
            g.Data4[2] = static_cast<uint8_t>((appHash >> 16) & 0xFF);
            g.Data4[3] = static_cast<uint8_t>((appHash >> 24) & 0xFF);
            g.Data4[4] = 0; g.Data4[5] = 0; g.Data4[6] = 0; g.Data4[7] = 0;
            return g;
        }

        void FillConditions(FWPM_FILTER_CONDITION0* conds, UINT32& count,
            uint16_t port, uint8_t proto,
            FWP_BYTE_BLOB* appId) noexcept {
            conds[0].fieldKey = FWPM_CONDITION_IP_PROTOCOL;
            conds[0].matchType = FWP_MATCH_EQUAL;
            conds[0].conditionValue.type = FWP_UINT8;
            conds[0].conditionValue.uint8 = proto;

            conds[1].fieldKey = FWPM_CONDITION_IP_LOCAL_PORT;
            conds[1].matchType = FWP_MATCH_EQUAL;
            conds[1].conditionValue.type = FWP_UINT16;
            conds[1].conditionValue.uint16 = port;

            count = 2;
            if (appId) {
                conds[2].fieldKey = FWPM_CONDITION_ALE_APP_ID;
                conds[2].matchType = FWP_MATCH_EQUAL;
                conds[2].conditionValue.type = FWP_BYTE_BLOB_TYPE;
                conds[2].conditionValue.byteBlob = appId;
                count = 3;
            }
        }

        // ---------------- 核心：加/删规则 ----------------
        Result ApplyBlock(uint16_t port, uint8_t proto,
            uint32_t appHash, FWP_BYTE_BLOB* appId) noexcept {
            if (!IsElevated()) return Result::NotElevated;

            HANDLE engine = nullptr;
            FWPM_SESSION0 session{};
            session.displayData.name = const_cast<wchar_t*>(L"PortBlocker");
            session.txnWaitTimeoutInMSec = 5000;

            DWORD r = FwpmEngineOpen0(nullptr, RPC_C_AUTHN_WINNT, nullptr, &session, &engine);
            if (r != ERROR_SUCCESS) { g_lastWinError = r; return Result::EngineOpenFailed; }

            r = FwpmTransactionBegin0(engine, 0);
            if (r != ERROR_SUCCESS) {
                g_lastWinError = r;
                FwpmEngineClose0(engine);
                return Result::TransactionBeginFailed;
            }

            struct LayerSpec { const GUID* key; const wchar_t* name; bool inbound; };
            const LayerSpec layers[] = {
                { &FWPM_LAYER_ALE_AUTH_CONNECT_V4,     L"PortBlocker Out V4", false },
                { &FWPM_LAYER_ALE_AUTH_RECV_ACCEPT_V4, L"PortBlocker In  V4", true  },
            };

            for (const auto& L : layers) {
                FWPM_FILTER_CONDITION0 conds[3]{};
                UINT32 condCount = 0;
                FillConditions(conds, condCount, port, proto, appId);

                FWPM_FILTER0 filter{};
                filter.layerKey = *L.key;
                filter.subLayerKey = FWPM_SUBLAYER_UNIVERSAL;
                filter.displayData.name = const_cast<wchar_t*>(L.name);
                filter.displayData.description =
                    const_cast<wchar_t*>(L"Blocked by PortBlocker");
                filter.action.type = FWP_ACTION_BLOCK;
                filter.weight.type = FWP_EMPTY;
                filter.numFilterConditions = condCount;
                filter.filterCondition = conds;
                filter.filterKey = MakeFilterKey(port, proto, L.inbound, appHash);

                UINT64 filterId = 0;
                r = FwpmFilterAdd0(engine, &filter, nullptr, &filterId);
                if (r != ERROR_SUCCESS) {
                    g_lastWinError = r;
                    FwpmTransactionAbort0(engine);
                    FwpmEngineClose0(engine);
                    return Result::FilterAddFailed;
                }
            }

            r = FwpmTransactionCommit0(engine);
            if (r != ERROR_SUCCESS) {
                g_lastWinError = r;
                FwpmTransactionAbort0(engine);
                FwpmEngineClose0(engine);
                return Result::TransactionCommitFailed;
            }
            FwpmEngineClose0(engine);
            return Result::Ok;
        }

        Result ApplyUnblock(uint16_t port, uint8_t proto, uint32_t appHash) noexcept {
            if (!IsElevated()) return Result::NotElevated;

            HANDLE engine = nullptr;
            FWPM_SESSION0 session{};
            session.displayData.name = const_cast<wchar_t*>(L"PortBlocker");
            session.txnWaitTimeoutInMSec = 5000;

            DWORD r = FwpmEngineOpen0(nullptr, RPC_C_AUTHN_WINNT, nullptr, &session, &engine);
            if (r != ERROR_SUCCESS) { g_lastWinError = r; return Result::EngineOpenFailed; }

            r = FwpmTransactionBegin0(engine, 0);
            if (r != ERROR_SUCCESS) {
                g_lastWinError = r;
                FwpmEngineClose0(engine);
                return Result::TransactionBeginFailed;
            }

            bool anyDeleted = false, anyFailed = false;
            for (int i = 0; i < 2; ++i) {
                GUID key = MakeFilterKey(port, proto, /*inbound=*/i == 1, appHash);
                r = FwpmFilterDeleteByKey0(engine, &key);
                if (r == ERROR_SUCCESS) {
                    anyDeleted = true;
                }
                else if (r != FWP_E_FILTER_NOT_FOUND) {
                    g_lastWinError = r;
                    anyFailed = true;
                }
            }

            if (anyFailed) {
                FwpmTransactionAbort0(engine);
                FwpmEngineClose0(engine);
                return Result::FilterDeleteFailed;
            }
            if (!anyDeleted) {
                FwpmTransactionAbort0(engine);
                FwpmEngineClose0(engine);
                return Result::FilterNotFound;
            }

            r = FwpmTransactionCommit0(engine);
            if (r != ERROR_SUCCESS) {
                g_lastWinError = r;
                FwpmTransactionAbort0(engine);
                FwpmEngineClose0(engine);
                return Result::TransactionCommitFailed;
            }
            FwpmEngineClose0(engine);
            return Result::Ok;
        }

        // ---------------- 目录扫描（内部使用宽字符） ----------------
        std::wstring TrimTrailingSlashW(std::wstring p) {
            while (!p.empty() && (p.back() == L'\\' || p.back() == L'/'))
                p.pop_back();
            return p;
        }

        bool WideEqualICase(const std::wstring& a, const std::wstring& b) noexcept {
            if (a.size() != b.size()) return false;
            for (size_t i = 0; i < a.size(); ++i) {
                wchar_t ca = a[i], cb = b[i];
                if (ca >= L'A' && ca <= L'Z') ca = static_cast<wchar_t>(ca + 32);
                if (cb >= L'A' && cb <= L'Z') cb = static_cast<wchar_t>(cb + 32);
                if (ca != cb) return false;
            }
            return true;
        }

        void ScanRecursiveW(const std::wstring& dir,
            const std::wstring& targetName,
            std::vector<std::wstring>& out,
            uint32_t maxResults,
            bool recursive) {
            if (out.size() >= maxResults) return;

            std::wstring pattern = dir + L"\\*";
            WIN32_FIND_DATAW fd{};
            HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
            if (h == INVALID_HANDLE_VALUE) return;

            do {
                if (out.size() >= maxResults) break;

                std::wstring name = fd.cFileName;
                if (name == L"." || name == L"..") continue;

                std::wstring full = dir + L"\\" + name;

                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                    if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;
                    if (recursive)
                        ScanRecursiveW(full, targetName, out, maxResults, recursive);
                }
                else {
                    if (WideEqualICase(name, targetName))
                        out.push_back(full);
                }
            } while (FindNextFileW(h, &fd));

            FindClose(h);
        }

        // 内部宽字符版本（供 Block/UnblockByName 复用，避免二次转换损耗）
        std::vector<std::wstring> FindExecutablesW(const std::wstring& parentDir,
            const std::wstring& fileName,
            bool recursive,
            uint32_t maxResults) {
            std::vector<std::wstring> results;
            if (parentDir.empty() || fileName.empty() || maxResults == 0) return results;

            std::wstring root = TrimTrailingSlashW(parentDir);
            if (root.empty()) return results;

            DWORD attrs = GetFileAttributesW(root.c_str());
            if (attrs == INVALID_FILE_ATTRIBUTES || !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
                g_lastWinError = GetLastError();
                return results;
            }

            ScanRecursiveW(root, fileName, results, maxResults, recursive);
            std::sort(results.begin(), results.end());
            results.erase(std::unique(results.begin(), results.end()), results.end());
            return results;
        }

    } // anonymous namespace

    // ================= 对外实现 =================

    uint32_t LastWindowsError() noexcept { return g_lastWinError; }

    const char* ToString(Result r) noexcept {
        switch (r) {
        case Result::Ok:                      return "成功";
        case Result::InvalidArgument:         return "参数无效";
        case Result::NotElevated:             return "权限不足，需管理员身份运行";
        case Result::EngineOpenFailed:        return "FwpmEngineOpen0 失败";
        case Result::TransactionBeginFailed:  return "FwpmTransactionBegin0 失败";
        case Result::FilterAddFailed:         return "FwpmFilterAdd0 失败（条件字段或层不匹配）";
        case Result::TransactionCommitFailed: return "FwpmTransactionCommit0 失败";
        case Result::FilterNotFound:          return "未找到对应过滤器（未阻止或已被删除）";
        case Result::FilterDeleteFailed:      return "FwpmFilterDeleteByKey0 失败";
        case Result::AppIdResolutionFailed:   return "程序路径解析失败（文件不存在或不可访问）";
        case Result::NoExecutableFound:       return "目录中未找到匹配的程序";
        case Result::DirectoryNotAccessible:  return "父目录不可访问（不存在或权限不足）";
        case Result::PartialSuccess:          return "部分程序处理成功，部分失败";
        case Result::Unknown:                 return "未知错误";
        }
        return "未知结果";
    }

    const wchar_t* ToStringW(Result r) noexcept {
        switch (r) {
        case Result::Ok:                      return L"成功";
        case Result::InvalidArgument:         return L"参数无效";
        case Result::NotElevated:             return L"权限不足，需管理员身份运行";
        case Result::EngineOpenFailed:        return L"FwpmEngineOpen0 失败";
        case Result::TransactionBeginFailed:  return L"FwpmTransactionBegin0 失败";
        case Result::FilterAddFailed:         return L"FwpmFilterAdd0 失败（条件字段或层不匹配）";
        case Result::TransactionCommitFailed: return L"FwpmTransactionCommit0 失败";
        case Result::FilterNotFound:          return L"未找到对应过滤器（未阻止或已被删除）";
        case Result::FilterDeleteFailed:      return L"FwpmFilterDeleteByKey0 失败";
        case Result::AppIdResolutionFailed:   return L"程序路径解析失败（文件不存在或不可访问）";
        case Result::NoExecutableFound:       return L"目录中未找到匹配的程序";
        case Result::DirectoryNotAccessible:  return L"父目录不可访问（不存在或权限不足）";
        case Result::PartialSuccess:          return L"部分程序处理成功，部分失败";
        case Result::Unknown:                 return L"未知错误";
        }
        return L"未知结果";
    }

    // ---------- 全部程序 ----------
    Result BlockPort(uint16_t port, Protocol proto) noexcept {
        g_lastWinError = 0;
        if (port == 0) return Result::InvalidArgument;
        if (proto != Protocol::TCP && proto != Protocol::UDP) return Result::InvalidArgument;
        return ApplyBlock(port, static_cast<uint8_t>(proto), 0, nullptr);
    }

    Result UnblockPort(uint16_t port, Protocol proto) noexcept {
        g_lastWinError = 0;
        if (port == 0) return Result::InvalidArgument;
        if (proto != Protocol::TCP && proto != Protocol::UDP) return Result::InvalidArgument;
        return ApplyUnblock(port, static_cast<uint8_t>(proto), 0);
    }

    // ---------- 指定程序 ----------
    Result BlockPortForProcess(uint16_t port, Protocol proto,
        const std::string& exePath) noexcept {
        g_lastWinError = 0;
        if (port == 0 || exePath.empty()) return Result::InvalidArgument;
        if (proto != Protocol::TCP && proto != Protocol::UDP) return Result::InvalidArgument;

        std::wstring wPath = ToWide(exePath);
        if (wPath.empty()) return Result::InvalidArgument;

        FWP_BYTE_BLOB* blob = nullptr;
        DWORD r = FwpmGetAppIdFromFileName0(wPath.c_str(), &blob);
        if (r != ERROR_SUCCESS || !blob) {
            g_lastWinError = r;
            return Result::AppIdResolutionFailed;
        }

        uint32_t hash = Fnv1a(wPath);
        if (hash == 0) hash = 1;

        Result res = ApplyBlock(port, static_cast<uint8_t>(proto), hash, blob);
        FwpmFreeMemory0(reinterpret_cast<void**>(&blob));
        return res;
    }

    Result UnblockPortForProcess(uint16_t port, Protocol proto,
        const std::string& exePath) noexcept {
        g_lastWinError = 0;
        if (port == 0 || exePath.empty()) return Result::InvalidArgument;
        if (proto != Protocol::TCP && proto != Protocol::UDP) return Result::InvalidArgument;

        std::wstring wPath = ToWide(exePath);
        if (wPath.empty()) return Result::InvalidArgument;

        uint32_t hash = Fnv1a(wPath);
        if (hash == 0) hash = 1;
        return ApplyUnblock(port, static_cast<uint8_t>(proto), hash);
    }

    // ---------- 目录查找（公开版：返回窄字符） ----------
    std::vector<std::string> FindExecutables(const std::string& parentDir,
        const std::string& fileName,
        bool recursive,
        uint32_t maxResults) {
        std::vector<std::string> out;
        if (parentDir.empty() || fileName.empty() || maxResults == 0) return out;

        auto wideHits = FindExecutablesW(ToWide(parentDir),
            ToWide(fileName),
            recursive, maxResults);
        out.reserve(wideHits.size());
        for (const auto& w : wideHits) out.push_back(ToNarrow(w));
        return out;
    }

    // ---------- 按目录 + 文件名阻止 ----------
    Result BlockPortForProcessByName(uint16_t port, Protocol proto,
        const std::string& parentDir,
        const std::string& fileName,
        bool recursive,
        uint32_t* outFound,
        uint32_t* outApplied) noexcept {
        g_lastWinError = 0;
        if (outFound)   *outFound = 0;
        if (outApplied) *outApplied = 0;

        if (port == 0 || parentDir.empty() || fileName.empty())
            return Result::InvalidArgument;
        if (proto != Protocol::TCP && proto != Protocol::UDP)
            return Result::InvalidArgument;

        std::wstring wRoot = TrimTrailingSlashW(ToWide(parentDir));
        DWORD attrs = GetFileAttributesW(wRoot.c_str());
        if (attrs == INVALID_FILE_ATTRIBUTES || !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
            g_lastWinError = GetLastError();
            return Result::DirectoryNotAccessible;
        }

        auto files = FindExecutablesW(wRoot, ToWide(fileName), recursive, 64);
        if (outFound) *outFound = static_cast<uint32_t>(files.size());
        if (files.empty()) return Result::NoExecutableFound;

        uint32_t applied = 0;
        Result lastError = Result::Ok;
        for (const auto& wPath : files) {
            Result r = BlockPortForProcess(port, proto, ToNarrow(wPath));
            if (r == Result::Ok) ++applied;
            else lastError = r;
        }
        if (outApplied) *outApplied = applied;

        if (applied == files.size()) return Result::Ok;
        if (applied == 0)            return lastError;
        return Result::PartialSuccess;
    }

    Result UnblockPortForProcessByName(uint16_t port, Protocol proto,
        const std::string& parentDir,
        const std::string& fileName,
        bool recursive,
        uint32_t* outFound,
        uint32_t* outApplied) noexcept {
        g_lastWinError = 0;
        if (outFound)   *outFound = 0;
        if (outApplied) *outApplied = 0;

        if (port == 0 || parentDir.empty() || fileName.empty())
            return Result::InvalidArgument;
        if (proto != Protocol::TCP && proto != Protocol::UDP)
            return Result::InvalidArgument;

        std::wstring wRoot = TrimTrailingSlashW(ToWide(parentDir));
        DWORD attrs = GetFileAttributesW(wRoot.c_str());
        if (attrs == INVALID_FILE_ATTRIBUTES || !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
            g_lastWinError = GetLastError();
            return Result::DirectoryNotAccessible;
        }

        auto files = FindExecutablesW(wRoot, ToWide(fileName), recursive, 64);
        if (outFound) *outFound = static_cast<uint32_t>(files.size());
        if (files.empty()) return Result::NoExecutableFound;

        uint32_t applied = 0;
        Result lastError = Result::Ok;
        for (const auto& wPath : files) {
            Result r = UnblockPortForProcess(port, proto, ToNarrow(wPath));
            if (r == Result::Ok) ++applied;
            else lastError = r;
        }
        if (outApplied) *outApplied = applied;

        if (applied == files.size()) return Result::Ok;
        if (applied == 0)            return lastError;
        return Result::PartialSuccess;
    }

} // namespace portblocker