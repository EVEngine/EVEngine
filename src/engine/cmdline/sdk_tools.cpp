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

}  // namespace eve::cmd::sdk
