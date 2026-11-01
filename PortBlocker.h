#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace portblocker {

    // ---- 协议枚举（对应 IP 协议号）----
    enum class Protocol : uint8_t {
        TCP = 6,
        UDP = 17,
    };

    // ---- 操作结果 ----
    enum class Result : int32_t {
        Ok = 0,
        InvalidArgument = -1,
        NotElevated = -2,
        EngineOpenFailed = -3,
        TransactionBeginFailed = -4,
        FilterAddFailed = -5,
        TransactionCommitFailed = -6,
        FilterNotFound = -7,
        FilterDeleteFailed = -8,
        AppIdResolutionFailed = -9,
        NoExecutableFound = -10,
        DirectoryNotAccessible = -11,
        PartialSuccess = -12,
        Unknown = -99,
    };

    uint32_t LastWindowsError() noexcept;
    const char* ToString(Result r) noexcept;
    const wchar_t* ToStringW(Result r) noexcept;

    // ---- 全部程序 ----
    Result BlockPort(uint16_t port, Protocol proto) noexcept;
    Result UnblockPort(uint16_t port, Protocol proto) noexcept;

    // ---- 指定程序（完整路径，窄字符 ACP 编码）----
    Result BlockPortForProcess(uint16_t port, Protocol proto,
        const std::string& exePath) noexcept;
    Result UnblockPortForProcess(uint16_t port, Protocol proto,
        const std::string& exePath) noexcept;

    // ---- 按父目录 + 文件名查找 ----
    std::vector<std::string> FindExecutables(const std::string& parentDir,
        const std::string& fileName,
        bool recursive = true,
        uint32_t maxResults = 64);

    Result BlockPortForProcessByName(uint16_t port, Protocol proto,
        const std::string& parentDir,
        const std::string& fileName,
        bool recursive = true,
        uint32_t* outFound = nullptr,
        uint32_t* outApplied = nullptr) noexcept;

    Result UnblockPortForProcessByName(uint16_t port, Protocol proto,
        const std::string& parentDir,
        const std::string& fileName,
        bool recursive = true,
        uint32_t* outFound = nullptr,
        uint32_t* outApplied = nullptr) noexcept;

} // namespace portblocker