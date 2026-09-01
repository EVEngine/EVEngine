#pragma once

#include "common/Result.h"

#include <string>
#include <vector>

namespace eve::cmd::sdk {

/** @brief 平台集合（与仓库 Makefile 的 build/<platform> 目标一一对应）。 */
enum class Platform {
    Win32,
    Linux,
    Macosx,
    Android,
    Ios,
    Unknown
};

/** @brief 解析平台名（大小写不敏感，含常用别名：windows/win、mac/macos/osx 等）。 */
Platform parsePlatform(const std::string& name);

/** @brief 平台规范名（用于 Makefile 目标名）。 */
std::string platformName(Platform p);

/** @brief 读取环境变量；未设置时返回 def。 */
std::string getEnv(const std::string& name, const std::string& def = "");

/** @brief 设置当前进程的环境变量（不持久化到系统）。 */
void setEnv(const std::string& name, const std::string& value);

/**
 * @brief 当前 eve 版本对应的发布 tag（v0.1.0-dev → v0.1.0）。
 * 可用 EVE_SDK_TAG 覆盖（dev 构建 / 测试）。解析失败时返回空串。
 */
std::string sdkVersionTag();

/**
 * @brief EVEngine 官方 SDK 安装根目录（Windows: %LOCALAPPDATA%/EVEngine/sdk，
 * POSIX: $XDG_DATA_HOME/EVEngine/sdk），可用 EVE_SDK_INSTALL_ROOT 覆盖。
 * 各平台 SDK 安装在 <root>/<platform> 下。
 */
std::string eveSdkInstallRoot();

/**
 * @brief 官方 SDK 下载基址（GitHub Release 对应 tag），可用 EVE_SDK_BASE_URL
 * 覆盖（镜像 / 测试，如 file://...）。
 */
std::string eveSdkBaseUrl();

/** @brief 计算文件的 SHA-256（小写十六进制）；失败返回空串。 */
std::string fileSha256(const std::string& path);

/**
 * @brief 定位 EVEngine SDK 根目录：--sdk 参数 > $EVENGINE_SDK > 可执行文件
 * 所在位置的 ../（<sdk>/bin/eve）> 当前目录 > `eve get` 安装的 SDK。
 * @return SDK 根目录绝对路径；找不到时返回空串。
 */
std::string findSdkRoot(const std::string& sdkArg);

/** @brief 读取 <sdk>/share/eve/TARGET_PLATFORM（该 SDK 面向的目标平台）。 */
std::string sdkTargetPlatform(const std::string& sdkRoot);

/** @brief 解析 Android SDK 根目录：EVENGINE_ANDROID_SDK / ANDROID_SDK_ROOT /
 * ANDROID_HOME，最后回落到平台默认位置（Windows: %LOCALAPPDATA%/Android/Sdk）。 */
std::string androidSdkRoot();

/** @brief 读取 KEY=VALUE 环境文件；只应用当前进程尚未设置的变量。 */
void applyEnvFile(const std::string& path);

/** @brief 通过系统 shell 执行命令并返回退出码（POSIX 下归一化 WEXITSTATUS）。 */
int runShell(const std::string& cmd);

/** @brief 递归复制目录，跳过指定名称的子目录（如 build/.gradle）。 */
bool copyTree(const std::string& from, const std::string& to,
              const std::vector<std::string>& skipDirs = {});

/** @brief 把 from 目录的内容（不含 from 本身）复制到 to。 */
bool copyTreeContents(const std::string& from, const std::string& to);

/** @brief 判断 Android SDK 是否已安装（cmdline-tools 的 sdkmanager 存在）。 */
bool isAndroidSdkInstalled(const std::string& root);

/**
 * @brief 下载并安装 EVEngine 官方 <platform> SDK（zip 内 eve-sdk/<platform>/，
 * 从 GitHub Release 按当前 eve 版本挑选），校验 SHA256 后落位到
 * <eveSdkInstallRoot()>/<platform>。
 * @param p 要安装的目标平台。
 * @return 成功时为空结果；下载、校验、解压或安装失败时返回结构化诊断。
 * @ownership 安装过程只在内部拥有临时文件；失败不会发布半安装的 SDK。
 * @thread 调用线程；该操作同步执行，不可并发调用同一安装根目录。
 */
[[nodiscard]] eve::Result<void> installEveSdk(Platform p);

/**
 * @brief 自动下载并安装 Android 工具链：EVEngine 官方 android SDK +
 * cmdline-tools / platform-tools / build-tools / platform + JDK 17 + Gradle。
 * @return 成功时为空结果；外部命令、网络、文件或格式失败时返回结构化诊断。
 * @thread 调用线程；该操作同步执行，不可并发调用同一安装根目录。
 */
[[nodiscard]] eve::Result<void> installAndroidSdk();

}  // namespace eve::cmd::sdk
