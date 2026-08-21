#pragma once

#include <string>

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

/** @brief 当前宿主平台（win32 / linux / macosx）。 */
std::string hostPlatformName();

/** @brief 读取环境变量；未设置时返回 def。 */
std::string getEnv(const std::string& name, const std::string& def = "");

/** @brief 设置当前进程的环境变量（不持久化到系统）。 */
void setEnv(const std::string& name, const std::string& value);

/**
 * @brief 从 cwd 向上查找包含 Makefile + CMakeLists.txt 的 EVEngine 检出根目录。
 * @return 根目录绝对路径；找不到时返回空串。
 */
std::string findEngineRoot();

/** @brief 解析 Android SDK 根目录：EVENGINE_ANDROID_SDK / ANDROID_SDK_ROOT /
 * ANDROID_HOME，最后回落到平台默认位置（Windows: %LOCALAPPDATA%/Android/Sdk）。 */
std::string androidSdkRoot();

/** @brief 读取 KEY=VALUE 环境文件；只应用当前进程尚未设置的变量。 */
void applyEnvFile(const std::string& path);

/** @brief 通过系统 shell 执行命令并返回退出码（POSIX 下归一化 WEXITSTATUS）。 */
int runShell(const std::string& cmd);

/** @brief 判断 Android SDK 是否已安装（cmdline-tools 的 sdkmanager 存在）。 */
bool isAndroidSdkInstalled(const std::string& root);

/** @brief 自动下载并安装 Android SDK（cmdline-tools / platform-tools /
 * build-tools / platform / NDK）和 JDK 17，返回退出码。 */
int installAndroidSdk();

}  // namespace eve::cmd::sdk
