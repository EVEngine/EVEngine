#include "cmdline/sdk_tools.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

#if !defined(_WIN32)
#include <sys/wait.h>
#endif

namespace eve::cmd::sdk {

namespace {

std::string lower(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

}  // namespace

Platform parsePlatform(const std::string& name) {
    const std::string n = lower(name);
    if (n == "android") return Platform::Android;
    if (n == "ios") return Platform::Ios;
    if (n == "linux") return Platform::Linux;
    if (n == "macosx" || n == "macos" || n == "mac" || n == "osx") return Platform::Macosx;
    if (n == "win32" || n == "windows" || n == "win" || n == "win64") return Platform::Win32;
    return Platform::Unknown;
}

std::string platformName(Platform p) {
    switch (p) {
        case Platform::Win32: return "win32";
        case Platform::Linux: return "linux";
        case Platform::Macosx: return "macosx";
        case Platform::Android: return "android";
        case Platform::Ios: return "ios";
        default: return "unknown";
    }
}

std::string hostPlatformName() {
#if defined(_WIN32)
    return "win32";
#elif defined(__APPLE__)
    return "macosx";
#else
    return "linux";
#endif
}

std::string getEnv(const std::string& name, const std::string& def) {
    const char* v = std::getenv(name.c_str());
    return v ? std::string(v) : def;
}

void setEnv(const std::string& name, const std::string& value) {
#if defined(_WIN32)
    _putenv_s(name.c_str(), value.c_str());
#else
    setenv(name.c_str(), value.c_str(), 1);
#endif
}

std::string findEngineRoot() {
    std::error_code ec;
    auto dir = std::filesystem::current_path(ec);
    if (ec) return "";
    for (;;) {
        if (std::filesystem::is_regular_file(dir / "Makefile", ec) &&
            std::filesystem::is_regular_file(dir / "CMakeLists.txt", ec))
            return dir.string();
        const auto parent = dir.parent_path();
        if (parent == dir) return "";
        dir = parent;
    }
}

std::string androidSdkRoot() {
    for (const char* n : {"EVENGINE_ANDROID_SDK", "ANDROID_SDK_ROOT", "ANDROID_HOME"}) {
        const std::string v = getEnv(n);
        if (!v.empty()) return v;
    }
#if defined(_WIN32)
    const std::string local = getEnv("LOCALAPPDATA");
    if (!local.empty()) return local + "/Android/Sdk";
    return getEnv("USERPROFILE") + "/Android/Sdk";
#else
    const std::string home = getEnv("HOME");
    return home.empty() ? std::string("Android/Sdk") : home + "/Android/Sdk";
#endif
}

void applyEnvFile(const std::string& path) {
    std::ifstream in(path);
    if (!in) return;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = line.substr(0, eq);
        if (key.empty()) continue;
        if (getEnv(key).empty()) setEnv(key, line.substr(eq + 1));
    }
}

int runShell(const std::string& cmd) {
    const int rc = std::system(cmd.c_str());
    if (rc == -1) {
        std::cerr << "eve: failed to run: " << cmd << std::endl;
        return 127;
    }
#if defined(_WIN32)
    return rc;
#else
    if (WIFEXITED(rc)) return WEXITSTATUS(rc);
    return 1;
#endif
}

namespace {

std::string exeSuffix() {
#if defined(_WIN32)
    return ".exe";
#else
    return "";
#endif
}

// sdkmanager 在 Windows 上是 .bat 脚本，其它平台是无后缀的 sh 脚本。
std::string sdkmanagerSuffix() {
#if defined(_WIN32)
    return ".bat";
#else
    return "";
#endif
}

// Google 官方 command-line tools 压缩包（按宿主平台）。可用
// EVE_ANDROID_CMDLINE_TOOLS_URL 覆盖（镜像/测试）。
std::string androidCmdlineToolsUrl() {
#if defined(_WIN32)
    return "https://dl.google.com/android/repository/commandlinetools-win-11076708_latest.zip";
#elif defined(__APPLE__)
    return "https://dl.google.com/android/repository/commandlinetools-mac-11076708_latest.zip";
#else
    return "https://dl.google.com/android/repository/commandlinetools-linux-11076708_latest.zip";
#endif
}

// Eclipse Temurin JDK 17 直链（Adoptium API 重定向到最新 17 GA）。
// 可用 EVE_JDK17_URL 覆盖（镜像/测试）。
std::string temurinJdk17Url() {
    const char* os;
    const char* arch;
#if defined(_WIN32)
    os = "windows";
    arch = "x64";
#elif defined(__APPLE__)
    os = "mac";
#if defined(__aarch64__)
    arch = "aarch64";
#else
    arch = "x64";
#endif
#else
    os = "linux";
#if defined(__aarch64__)
    arch = "aarch64";
#else
    arch = "x64";
#endif
#endif
    return std::string("https://api.adoptium.net/v3/binary/latest/17/ga/") + os + "/" + arch +
           "/jdk/hotspot/normal/eclipse";
}

// 与仓库 Makefile / APK 工程匹配的 Android SDK 组件版本。
const char* kAndroidPlatform = "platforms;android-34";
const char* kAndroidBuildTools = "build-tools;34.0.0";
const char* kAndroidNdk = "ndk;26.1.10909125";

bool jdkHomeExists(const std::string& home) {
    std::error_code ec;
    return std::filesystem::is_regular_file(
               std::filesystem::path(home) / "bin" / ("java" + exeSuffix()), ec) ||
           std::filesystem::is_regular_file(
               std::filesystem::path(home) / "bin" / ("javac" + exeSuffix()), ec);
}

// 下载一个压缩包并用系统 tar 解压（Windows 10+ 自带 bsdtar）。
int downloadAndExtract(const std::string& url, const std::string& zipPath,
                       const std::string& extractDir) {
    if (runShell("curl -fL --retry 3 -o \"" + zipPath + "\" \"" + url + "\"") != 0) {
        std::cerr << "eve get: download failed: " << url << std::endl;
        return 3;
    }
    std::error_code ec;
    std::filesystem::remove_all(extractDir, ec);
    std::filesystem::create_directories(extractDir, ec);
    if (runShell("tar -xf \"" + zipPath + "\" -C \"" + extractDir + "\"") != 0) {
        std::cerr << "eve get: failed to extract " << zipPath
                  << " (need a zip-capable tar)" << std::endl;
        return 3;
    }
    std::filesystem::remove(zipPath, ec);
    return 0;
}

// move 失败（跨盘符等）时退化为递归 copy。
bool moveOrCopy(const std::filesystem::path& from, const std::filesystem::path& to) {
    std::error_code ec;
    std::filesystem::remove_all(to, ec);
    std::filesystem::rename(from, to, ec);
    if (!ec) return true;
    ec.clear();
    std::filesystem::copy(from, to,
                          std::filesystem::copy_options::recursive |
                              std::filesystem::copy_options::overwrite_existing,
                          ec);
    return !ec;
}

int installJdk(const std::string& root) {
    const std::string url = getEnv("EVE_JDK17_URL", temurinJdk17Url());
    std::cout << "eve get: downloading JDK 17 (Temurin)...\n";
    const std::string tmp = root + "/.jdk-extract";
    if (downloadAndExtract(url, root + "/jdk17.zip", tmp) != 0) return 3;
    std::error_code ec;
    std::filesystem::path jdkDir;
    for (const auto& entry : std::filesystem::directory_iterator(tmp, ec)) {
        if (entry.is_directory()) {
            jdkDir = entry.path();
            break;
        }
    }
    if (jdkDir.empty() || !moveOrCopy(jdkDir, std::filesystem::path(root) / "jdk17")) {
        std::cerr << "eve get: unexpected JDK archive layout in " << tmp << std::endl;
        return 3;
    }
    std::filesystem::remove_all(tmp, ec);
    return 0;
}

}  // namespace

bool isAndroidSdkInstalled(const std::string& root) {
    std::error_code ec;
    return std::filesystem::is_regular_file(
        std::filesystem::path(root) / "cmdline-tools" / "latest" / "bin" /
            ("sdkmanager" + sdkmanagerSuffix()),
        ec);
}

int installAndroidSdk() {
    const std::string root = androidSdkRoot();
    std::error_code ec;
    std::filesystem::create_directories(root, ec);
    if (ec) {
        std::cerr << "eve get: cannot create SDK directory " << root << ": "
                  << ec.message() << std::endl;
        return 3;
    }

    const std::string sdkManager =
        root + "/cmdline-tools/latest/bin/sdkmanager" + sdkmanagerSuffix();
    if (isAndroidSdkInstalled(root)) {
        std::cout << "eve get: Android SDK already installed at " << root << "\n";
    } else {
        std::cout << "eve get: downloading Android command-line tools...\n";
        const std::string url =
            getEnv("EVE_ANDROID_CMDLINE_TOOLS_URL", androidCmdlineToolsUrl());
        const std::string tmp = root + "/.cmdline-tools-extract";
        if (downloadAndExtract(url, root + "/cmdline-tools.zip", tmp) != 0) return 3;
        if (!std::filesystem::is_directory(std::filesystem::path(tmp) / "cmdline-tools", ec)) {
            std::cerr << "eve get: unexpected command-line tools archive layout in "
                      << tmp << std::endl;
            return 3;
        }
        if (!moveOrCopy(std::filesystem::path(tmp) / "cmdline-tools",
                        std::filesystem::path(root) / "cmdline-tools" / "latest")) {
            std::cerr << "eve get: failed to move cmdline-tools into "
                      << root << "/cmdline-tools/latest" << std::endl;
            return 3;
        }
        std::filesystem::remove_all(tmp, ec);
    }

    // JDK 17 优先复用 $JAVA_HOME；否则下载 Temurin 17 到 <sdk>/jdk17。
    std::string javaHome = getEnv("JAVA_HOME");
    if (javaHome.empty()) {
        javaHome = (std::filesystem::path(root) / "jdk17").string();
        if (!jdkHomeExists(javaHome) && installJdk(root) != 0) return 3;
    } else if (!jdkHomeExists(javaHome)) {
        std::cerr << "eve get: warning: JAVA_HOME=" << javaHome
                  << " does not contain bin/java; make sure it points at a JDK 17+.\n";
    }
    setEnv("JAVA_HOME", javaHome);

    // 接受许可并安装组件（sdkmanager 幂等，可重复执行补齐）。
    const std::string lic = root + "/.eve-licenses.txt";
    {
        std::ofstream f(lic, std::ios::binary | std::ios::trunc);
        for (int i = 0; i < 200; ++i) f << "y\n";
    }
#if defined(_WIN32)
    // cmd /c strips the first/last quote of a command that starts with one, so
    // batch files with spaces in their path need the `call` prefix.
    const std::string sdkCmd = "call \"" + sdkManager + "\"";
#else
    const std::string sdkCmd = "\"" + sdkManager + "\"";
#endif
    std::cout << "eve get: accepting Android SDK licenses...\n";
    if (runShell(sdkCmd + " --licenses < \"" + lic + "\"") != 0) {
        std::cerr << "eve get: failed to accept Android SDK licenses" << std::endl;
        return 3;
    }
    std::cout << "eve get: installing SDK packages (platform-tools, android-34, "
                 "build-tools, NDK)...\n";
    const std::string packages = "\"platform-tools\" \"" + std::string(kAndroidPlatform) +
                                 "\" \"" + std::string(kAndroidBuildTools) + "\" \"" +
                                 std::string(kAndroidNdk) + "\"";
    if (runShell(sdkCmd + " " + packages) != 0) {
        std::cerr << "eve get: failed to install Android SDK packages" << std::endl;
        return 3;
    }
    std::filesystem::remove(lic, ec);

    // 记录环境，供 `eve build android` 在环境变量未设置时使用。
    {
        std::ofstream f(std::filesystem::path(root) / "eve-android.env",
                        std::ios::binary | std::ios::trunc);
        f << "ANDROID_HOME=" << root << "\n"
          << "ANDROID_SDK_ROOT=" << root << "\n"
          << "ANDROID_NDK_ROOT=" << root << "/" << kAndroidNdk << "\n"
          << "JAVA_HOME=" << javaHome << "\n";
    }

    std::cout << "eve get: Android SDK ready at " << root << "\n"
              << "eve get: run `eve build android` to build an APK "
                 "(or `eve build -d android` for a debug APK).\n";
    return 0;
}

}  // namespace eve::cmd::sdk
