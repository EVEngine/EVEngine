#include "cmdline/sdk_tools.h"

#include "common/config.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

#if defined(_WIN32)
#include <windows.h>
#include <process.h>
#else
#include <unistd.h>
#endif

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

int processId() {
#if defined(_WIN32)
    return _getpid();
#else
    return static_cast<int>(getpid());
#endif
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

std::string sdkVersionTag() {
    const std::string override = getEnv("EVE_SDK_TAG");
    if (!override.empty()) return override;
    std::string v = EVENGINE_VERSION;
    const size_t dash = v.find('-');
    if (dash != std::string::npos) v = v.substr(0, dash);
    if (v.empty()) return "";
    if (v[0] != 'v') v = "v" + v;
    return v;
}

std::string eveSdkInstallRoot() {
    const std::string override = getEnv("EVE_SDK_INSTALL_ROOT");
    if (!override.empty()) return override;
#if defined(_WIN32)
    std::string base = getEnv("LOCALAPPDATA");
    if (base.empty()) base = getEnv("USERPROFILE");
    return base.empty() ? std::string("EVEngine/sdk") : base + "/EVEngine/sdk";
#else
    std::string base = getEnv("XDG_DATA_HOME");
    if (base.empty()) {
        const std::string home = getEnv("HOME");
        base = home.empty() ? std::string("") : home + "/.local/share";
    }
    return base.empty() ? std::string("EVEngine/sdk") : base + "/EVEngine/sdk";
#endif
}

std::string eveSdkBaseUrl() {
    const std::string override = getEnv("EVE_SDK_BASE_URL");
    if (!override.empty()) return override;
    return std::string("https://github.com/EVEngine/EVEngine/releases/download/") +
           sdkVersionTag();
}

std::string fileSha256(const std::string& path) {
    std::error_code ec;
    const auto      tmp = std::filesystem::temp_directory_path(ec) / "eve-sha256.txt";
    if (ec) return "";
    const std::string cmd =
#if defined(_WIN32)
        "certutil -hashfile \"" + path + "\" SHA256 > \"" + tmp.string() + "\"";
#else
        "sha256sum \"" + path + "\" > \"" + tmp.string() + "\"";
#endif
    if (runShell(cmd) != 0) return "";
    std::ifstream in(tmp);
    std::string   line, hex;
    while (std::getline(in, line)) {
        std::istringstream iss(line);
        std::string        tok;
        while (iss >> tok) {
            if (tok.size() != 64) continue;
            bool ok = true;
            for (const char c : tok) {
                if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
                      (c >= 'A' && c <= 'F'))) {
                    ok = false;
                    break;
                }
            }
            if (ok) {
                hex = lower(tok);
                break;
            }
        }
        if (!hex.empty()) break;
    }
    std::filesystem::remove(tmp, ec);
    return hex;
}

namespace {

bool isSdkRoot(const std::filesystem::path& dir) {
    std::error_code ec;
    return std::filesystem::is_regular_file(
        std::filesystem::path(dir) / "share" / "eve" / "TARGET_PLATFORM", ec);
}

}  // namespace

std::string findSdkRoot(const std::string& sdkArg) {
    std::error_code ec;
    if (!sdkArg.empty()) {
        const auto p = std::filesystem::absolute(sdkArg, ec);
        if (!ec && std::filesystem::is_directory(p, ec)) return p.string();
        return "";
    }
    const std::string env = getEnv("EVENGINE_SDK");
    if (!env.empty() && std::filesystem::is_directory(env, ec))
        return std::filesystem::absolute(env, ec).string();

    // Running from <sdk>/bin/<runtime> -> root is two directories up.
#if defined(_WIN32)
    wchar_t buf[MAX_PATH + 1] = {0};
    if (GetModuleFileNameW(nullptr, buf, MAX_PATH) != 0) {
        const auto exeDir = std::filesystem::path(buf).parent_path();
        if (isSdkRoot(exeDir.parent_path())) return exeDir.parent_path().string();
    }
#else
    char buf[4096] = {0};
    const ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n > 0) {
        const auto exeDir = std::filesystem::path(std::string(buf, static_cast<size_t>(n))).parent_path();
        if (isSdkRoot(exeDir.parent_path())) return exeDir.parent_path().string();
    }
#endif

    const auto cwd = std::filesystem::current_path(ec);
    if (!ec && isSdkRoot(cwd)) return cwd.string();

    // `eve get <platform>` 安装的 SDK（<installRoot>/<platform>）。优先返回
    // android（build 目前唯一支持的平台），其次任意 SDK 根。
    const auto  installRoot = std::filesystem::path(eveSdkInstallRoot());
    std::string fallback;
    if (std::filesystem::is_directory(installRoot, ec)) {
        std::filesystem::directory_iterator it(installRoot, ec), end;
        for (; it != end; it.increment(ec)) {
            if (ec) {
                ec.clear();
                continue;
            }
            if (!it->is_directory(ec) || !isSdkRoot(it->path())) continue;
            const std::string plat = sdkTargetPlatform(it->path().string());
            if (plat == "android") return it->path().string();
            if (fallback.empty()) fallback = it->path().string();
        }
    }
    return fallback;
}

namespace {

// 从 SHA256SUMS 文本里找 "<hex>  <name>" 行，返回小写 hex；找不到返回空串。
std::string sha256ForName(const std::string& sums, const std::string& name) {
    std::istringstream iss(sums);
    std::string        line;
    while (std::getline(iss, line)) {
        const size_t sp = line.find(' ');
        if (sp == std::string::npos) continue;
        const std::string hex = line.substr(0, sp);
        std::string       rest = line.substr(sp + 1);
        while (!rest.empty() && (rest.front() == ' ' || rest.front() == '\t'))
            rest.erase(rest.begin());
        while (!rest.empty() &&
               (rest.back() == '\r' || rest.back() == ' ' || rest.back() == '\t'))
            rest.pop_back();
        if (rest == name) return lower(hex);
    }
    return "";
}

// 读取 <root>/share/eve/VERSION；文件缺失或为空时返回空串。
std::string sdkVersion(const std::string& root) {
    std::ifstream in(std::filesystem::path(root) / "share" / "eve" / "VERSION");
    std::string   s;
    std::getline(in, s);
    return s;
}

// moveOrCopy 定义在本文件较后位置（下载/解压工具区），这里前向声明。
bool moveOrCopy(const std::filesystem::path& from, const std::filesystem::path& to);

}  // namespace

int installEveSdk(Platform p) {
    namespace fs = std::filesystem;
    if (p == Platform::Unknown) return 3;
    const std::string plat = platformName(p);
    const std::string tag = sdkVersionTag();
    if (tag.empty() || tag[0] != 'v') {
        std::cerr << "eve get: cannot determine the current EVEngine version. "
                     "Set EVE_SDK_TAG to the release tag (e.g. EVE_SDK_TAG=v0.1.0)."
                  << std::endl;
        return 3;
    }
    const std::string expectedVer = tag.substr(1);
    const std::string installRoot = eveSdkInstallRoot();
    const std::string destRoot = (fs::path(installRoot) / plat).string();
    std::error_code  ec;

    // 已安装同版本则直接复用，避免重复下载。
    const std::string currentVer = sdkVersion(destRoot);
    if (!currentVer.empty() && currentVer == expectedVer) {
        std::cout << "eve get: EVEngine " << plat << " SDK already installed at "
                  << destRoot << " (" << tag << ")" << std::endl;
        return 0;
    }

    const std::string base = eveSdkBaseUrl();
    const std::string zipName = "eve-sdk-" + plat + "-" + tag + ".zip";
    const std::string zipUrl = base + "/" + zipName;
    const std::string sumsUrl = base + "/SHA256SUMS";
    const auto work =
        fs::temp_directory_path(ec) / ("eve-get-" + plat + "-" + std::to_string(processId()));
    if (ec) {
        std::cerr << "eve get: no temporary directory available" << std::endl;
        return 3;
    }
    fs::remove_all(work, ec);
    fs::create_directories(work, ec);
    const std::string zipPath = (work / zipName).string();
    const std::string sumsPath = (work / "SHA256SUMS").string();

    std::cout << "eve get: downloading " << zipName << " (" << zipUrl << ")...\n";
    if (runShell("curl -fL --retry 3 -o \"" + zipPath + "\" \"" + zipUrl + "\"") != 0) {
        std::cerr << "eve get: download failed: " << zipUrl << std::endl;
        fs::remove_all(work, ec);
        return 3;
    }
    if (runShell("curl -fL --retry 3 -o \"" + sumsPath + "\" \"" + sumsUrl + "\"") != 0) {
        std::cerr << "eve get: failed to download the checksum file: " << sumsUrl
                  << std::endl;
        fs::remove_all(work, ec);
        return 3;
    }
    {
        std::ifstream in(sumsPath);
        const std::string sums(
            (std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        const std::string expected = sha256ForName(sums, zipName);
        const std::string actual = fileSha256(zipPath);
        if (expected.empty() || actual.empty() || lower(expected) != lower(actual)) {
            std::cerr << "eve get: SHA-256 verification failed for " << zipName
                      << " (expected "
                      << (expected.empty() ? std::string("<missing in SHA256SUMS>") : expected)
                      << ", got " << (actual.empty() ? std::string("<error>") : actual) << ").\n"
                      << "The download may be corrupt; retry, or point EVE_SDK_BASE_URL "
                         "at a mirror."
                      << std::endl;
            fs::remove_all(work, ec);
            return 3;
        }
    }

    const std::string extractDir = (work / "extract").string();
    fs::create_directories(extractDir, ec);
    if (runShell("tar -xf \"" + zipPath + "\" -C \"" + extractDir + "\"") != 0) {
        std::cerr << "eve get: failed to extract " << zipName
                  << " (need a zip-capable tar)" << std::endl;
        fs::remove_all(work, ec);
        return 3;
    }
    const auto srcRoot = fs::path(extractDir) / "eve-sdk" / plat;
    if (!fs::is_directory(srcRoot, ec)) {
        std::cerr << "eve get: unexpected SDK archive layout (expected eve-sdk/"
                  << plat << "/ at the archive root): " << zipName << std::endl;
        fs::remove_all(work, ec);
        return 3;
    }
    {
        const std::string platMarker = sdkTargetPlatform(srcRoot.string());
        const std::string verMarker = sdkVersion(srcRoot.string());
        if (platMarker != plat) {
            std::cerr << "eve get: " << zipName << " targets '" << platMarker
                      << "', not '" << plat << "'." << std::endl;
            fs::remove_all(work, ec);
            return 3;
        }
        if (verMarker != expectedVer) {
            std::cerr << "eve get: " << zipName << " contains version '" << verMarker
                      << "', expected '" << expectedVer << "'." << std::endl;
            fs::remove_all(work, ec);
            return 3;
        }
    }

    fs::create_directories(installRoot, ec);
    if (!moveOrCopy(srcRoot, fs::path(destRoot))) {
        std::cerr << "eve get: failed to install the SDK into " << destRoot
                  << std::endl;
        fs::remove_all(work, ec);
        return 3;
    }
    fs::remove_all(work, ec);
    std::cout << "eve get: EVEngine " << plat << " SDK " << tag << " installed at "
              << destRoot << std::endl;
    return 0;
}

std::string sdkTargetPlatform(const std::string& sdkRoot) {
    std::ifstream in(std::filesystem::path(sdkRoot) / "share" / "eve" / "TARGET_PLATFORM");
    std::string  s;
    std::getline(in, s);
    return s;
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
#if defined(EVENGINE_IOS)
    // iOS forbids system(); the shell-out paths (eve get / eve build) are
    // desktop-host only and never run on the iOS runtime.
    std::cerr << "eve: shell commands are not available on iOS: " << cmd << std::endl;
    return 127;
#else
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

// 与仓库 APK 工程匹配的 Android SDK 组件版本（.so 由 SDK 预编译，无需 NDK）。
const char* kAndroidPlatform = "platforms;android-34";
const char* kAndroidBuildTools = "build-tools;34.0.0";

// APK 模板的 gradle-wrapper.properties 指定的 Gradle 版本。
const char* kGradleVersion = "8.5";

std::string gradleUrl() {
    return std::string("https://services.gradle.org/distributions/gradle-") + kGradleVersion +
           "-bin.zip";
}

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

int installGradle(const std::string& root) {
    const std::string url = getEnv("EVE_GRADLE_URL", gradleUrl());
    std::cout << "eve get: downloading Gradle " << kGradleVersion << "...\n";
    const std::string tmp = root + "/.gradle-extract";
    if (downloadAndExtract(url, root + "/gradle.zip", tmp) != 0) return 3;
    std::error_code ec;
    const auto gradleDir = std::filesystem::path(root) / ("gradle-" + std::string(kGradleVersion));
    if (!moveOrCopy(std::filesystem::path(tmp) / ("gradle-" + std::string(kGradleVersion)),
                    gradleDir)) {
        std::cerr << "eve get: unexpected Gradle archive layout in " << tmp << std::endl;
        return 3;
    }
    std::filesystem::remove_all(tmp, ec);
    return 0;
}

bool gradleInstalled(const std::string& gradleHome) {
    std::error_code ec;
    return std::filesystem::is_regular_file(
               std::filesystem::path(gradleHome) / "bin" / "gradle", ec) ||
           std::filesystem::is_regular_file(
               std::filesystem::path(gradleHome) / "bin" / "gradle.bat", ec);
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
    // EVEngine 官方 android SDK 优先：预编译 native 库 + APK 模板按当前 eve
    // 版本从 GitHub Release 下载（校验 SHA256）。
    if (installEveSdk(Platform::Android) != 0) return 3;

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
                 "build-tools)...\n";
    const std::string packages = "\"platform-tools\" \"" + std::string(kAndroidPlatform) +
                                 "\" \"" + std::string(kAndroidBuildTools) + "\"";
    if (runShell(sdkCmd + " " + packages) != 0) {
        std::cerr << "eve get: failed to install Android SDK packages" << std::endl;
        return 3;
    }
    std::filesystem::remove(lic, ec);

    // Gradle：APK 模板没有 gradlew.bat，直接用发行版启动器，无需额外工具。
    std::string gradleHome = (std::filesystem::path(root) / ("gradle-" + std::string(kGradleVersion))).string();
    if (!gradleInstalled(gradleHome) && installGradle(root) != 0) return 3;

    // 记录环境，供 `eve build android` 在环境变量未设置时使用。
    const std::string eveSdkRoot =
        (std::filesystem::path(eveSdkInstallRoot()) / "android").string();
    {
        std::ofstream f(std::filesystem::path(root) / "eve-android.env",
                        std::ios::binary | std::ios::trunc);
        f << "ANDROID_HOME=" << root << "\n"
          << "ANDROID_SDK_ROOT=" << root << "\n"
          << "JAVA_HOME=" << javaHome << "\n"
          << "GRADLE_HOME=" << gradleHome << "\n"
          << "EVENGINE_SDK=" << eveSdkRoot << "\n";
    }

    std::cout << "eve get: Android SDK ready at " << root << "\n"
              << "eve get: run `eve build android` to build an APK "
                 "(or `eve build -d android` for a debug APK).\n";
    return 0;
}

namespace {

void copyOne(const std::filesystem::path& from, const std::filesystem::path& to,
             const std::vector<std::string>& skipDirs, bool contentsOnly) {
    std::error_code ec;
    if (!contentsOnly) std::filesystem::create_directories(to, ec);
    std::filesystem::recursive_directory_iterator it(
        from, std::filesystem::directory_options::skip_permission_denied, ec);
    std::filesystem::recursive_directory_iterator end;
    for (; it != end; it.increment(ec)) {
        if (ec) {
            ec.clear();
            continue;
        }
        const auto rel = std::filesystem::relative(it->path(), from, ec).lexically_normal();
        if (ec) continue;
        const std::string relStr = rel.generic_string();
        bool              skip   = false;
        for (const auto& d : skipDirs) {
            if (relStr == d || relStr.rfind(d + "/", 0) == 0) {
                skip = true;
                break;
            }
        }
        if (skip) {
            it.disable_recursion_pending();
            continue;
        }
        // 无论是否 contentsOnly，目标都按相对路径落位（contentsOnly 时 to 就是
        // 目标根目录，rel 相对源根目录）。
        const auto dst = to / rel;
        if (it->is_directory()) {
            std::filesystem::create_directories(dst, ec);
        } else if (it->is_regular_file()) {
            std::filesystem::create_directories(dst.parent_path(), ec);
            std::filesystem::copy_file(it->path(), dst,
                                       std::filesystem::copy_options::overwrite_existing, ec);
        }
    }
}

}  // namespace

bool copyTree(const std::string& from, const std::string& to,
              const std::vector<std::string>& skipDirs) {
    std::error_code ec;
    if (!std::filesystem::is_directory(from, ec)) return false;
    copyOne(from, to, skipDirs, /*contentsOnly=*/false);
    return true;
}

bool copyTreeContents(const std::string& from, const std::string& to) {
    std::error_code ec;
    if (!std::filesystem::is_directory(from, ec)) return false;
    std::filesystem::create_directories(to, ec);
    copyOne(from, to, {}, /*contentsOnly=*/true);
    return true;
}

}  // namespace eve::cmd::sdk
