#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "cmdline/cmdline.h"
#include "cmdline/sdk_tools.h"
#include "cmdline/zip_writer.h"
#include "common/Module.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <streambuf>
#include <string>
#include <vector>

namespace {

// Runs the cmdline module in-process with the given argv (argv[0] is the
// program name) and returns the exit code. CWD-relative commands operate on
// the current directory, so tests wrap this with PushDir.
[[maybe_unused]] int runCli(const std::vector<std::string>& args) {
    std::vector<char*> argv;
    argv.reserve(args.size());
    for (const auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
    auto* cmd = eve::ModuleManager::requireInstance<eve::cmd::Cmdline>("Cmdline");
    return cmd->runArgs(static_cast<unsigned>(argv.size()), argv.data());
}

// RAII: chdir into `dir` for the lifetime of the object, restoring afterwards
// so tests sharing a process cannot leak the working directory into each other.
class PushDir {
public:
    explicit PushDir(const std::filesystem::path& dir) : old_(std::filesystem::current_path()) {
        std::filesystem::current_path(dir);
    }
    ~PushDir() {
        std::error_code ec;
        std::filesystem::current_path(old_, ec);
    }

private:
    std::filesystem::path old_;
};

// Swaps cout/cerr buffers so the test can inspect what a command printed
// (rang color codes are dropped automatically when the sink is not a TTY).
class CaptureStreams {
public:
    CaptureStreams() {
        cout_old_ = std::cout.rdbuf(cout_buf_.rdbuf());
        cerr_old_ = std::cerr.rdbuf(cerr_buf_.rdbuf());
    }
    ~CaptureStreams() {
        std::cout.rdbuf(cout_old_);
        std::cerr.rdbuf(cerr_old_);
    }

    std::string out() const { return cout_buf_.str(); }
    std::string err() const { return cerr_buf_.str(); }
    std::string all() const { return out() + err(); }

private:
    std::ostringstream cout_buf_, cerr_buf_;
    std::streambuf*    cout_old_ = nullptr;
    std::streambuf*    cerr_old_ = nullptr;
};

[[maybe_unused]] std::filesystem::path tempDir(const char* tag) {
    const auto p = std::filesystem::temp_directory_path() / tag;
    std::error_code ec;
    std::filesystem::remove_all(p, ec);
    std::filesystem::create_directories(p, ec);
    return p;
}

[[maybe_unused]] void writeFile(const std::filesystem::path& p, const std::string& content) {
    std::filesystem::create_directories(p.parent_path());
    std::ofstream ofs(p, std::ios::binary | std::ios::trunc);
    ofs << content;
}

[[maybe_unused]] std::string readTextFile(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

// Reads the ZIP central directory (EOCD -> central entries) and returns the
// entry names in archive order. Returns {} when the file is not a valid ZIP.
[[maybe_unused]] std::vector<std::string> zipEntryNames(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return {};
    const std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (data.size() < 22) return {};

    const size_t eocd = data.rfind("PK\x05\x06");
    if (eocd == std::string::npos || data.size() - eocd < 22) return {};

    const auto le16 = [&](size_t off) -> uint16_t {
        return static_cast<uint16_t>(static_cast<unsigned char>(data[off]) |
                                     (static_cast<unsigned char>(data[off + 1]) << 8));
    };
    const auto le32 = [&](size_t off) -> uint32_t {
        return static_cast<uint32_t>(static_cast<unsigned char>(data[off]) |
                                     (static_cast<unsigned char>(data[off + 1]) << 8) |
                                     (static_cast<unsigned char>(data[off + 2]) << 16) |
                                     (static_cast<unsigned char>(data[off + 3]) << 24));
    };

    const uint32_t cdSize   = le32(eocd + 12);
    const uint32_t cdOffset = le32(eocd + 16);
    if (cdOffset > data.size() || cdSize > data.size() - cdOffset) return {};

    std::vector<std::string> names;
    size_t                   pos = cdOffset;
    const size_t             end = static_cast<size_t>(cdOffset) + cdSize;
    while (pos + 46 <= end && data.compare(pos, 4, "PK\x01\x02") == 0) {
        const size_t nameLen = le16(pos + 28);
        const size_t extraLen = le16(pos + 30);
        const size_t commentLen = le16(pos + 32);
        if (pos + 46 + nameLen + extraLen + commentLen > end) return {};
        names.emplace_back(data.substr(pos + 46, nameLen));
        pos += 46 + nameLen + extraLen + commentLen;
    }
    return names;
}

[[maybe_unused]] void setEnvVar(const char* name, const std::string& value) {
#if defined(_WIN32)
    _putenv_s(name, value.c_str());
#else
    setenv(name, value.c_str(), 1);
#endif
}

[[maybe_unused]] void unsetEnvVar(const char* name) {
#if defined(_WIN32)
    _putenv_s(name, "");
#else
    unsetenv(name);
#endif
}

// RAII env removal; restores the previous value (or leaves it unset) afterwards.
class ScopedUnsetEnv {
public:
    explicit ScopedUnsetEnv(const char* name) : name_(name) {
        const char* old = std::getenv(name);
        if (old) {
            had_ = true;
            old_ = old;
            unsetEnvVar(name);
        }
    }
    ~ScopedUnsetEnv() {
        if (had_) setEnvVar(name_.c_str(), old_);
    }

private:
    std::string name_, old_;
    bool        had_ = false;
};

// RAII env override; restores the previous value (or removes it) afterwards so
// tests sharing a process cannot leak settings into each other.
class ScopedEnv {
public:
    ScopedEnv(const char* name, const std::string& value) : name_(name) {
        const char* old = std::getenv(name);
        if (old) {
            had_ = true;
            old_ = old;
        }
        setEnvVar(name, value);
    }
    ~ScopedEnv() {
        if (had_)
            setEnvVar(name_.c_str(), old_);
        else
            unsetEnvVar(name_.c_str());
    }

private:
    std::string name_, old_;
    bool        had_ = false;
};

// A fake EVEngine SDK install: TARGET_PLATFORM marker, prebuilt lib/, and the
// android APK template (platform/apk) exactly like `make sdk/android` ships.
[[maybe_unused]] std::filesystem::path fakeSdkRoot(const char* tag, const char* platform) {
    const auto sdk = tempDir(tag);
    writeFile(sdk / "share" / "eve" / "TARGET_PLATFORM", std::string(platform) + "\n");
    writeFile(sdk / "lib" / "libmain.so", "fake so");
    writeFile(sdk / "lib" / "libSDL2.so", "fake so");
    writeFile(sdk / "platform" / "apk" / "template.txt", "apk template");
    writeFile(sdk / "platform" / "game-shell" / "config.nut", "width=800\n");
    return sdk;
}

// A fake gradle distribution whose launcher records the invocation and writes
// the APK output that `eve build android` checks for. unsignedOnly mimics a
// release build without a signing config (AGP emits app-release-unsigned.apk).
[[maybe_unused]] void writeFakeGradle(const std::filesystem::path& gradleHome, bool unsignedOnly = false) {
    const auto bin = gradleHome / "bin";
    std::error_code ec;
    std::filesystem::create_directories(bin, ec);
#if defined(_WIN32)
    writeFile(bin / "gradle.bat",
              "@echo off\r\n"
              "echo FAKE_GRADLE %* > gradle-invoked.txt\r\n"
              "mkdir app\\build\\outputs\\apk\\release 2>nul\r\n"
              "mkdir app\\build\\outputs\\apk\\debug 2>nul\r\n"
              "echo ok > app\\build\\outputs\\apk\\release\\" +
                  std::string(unsignedOnly ? "app-release-unsigned.apk" : "app-release.apk") +
                  "\r\n"
              "echo ok > app\\build\\outputs\\apk\\debug\\app-debug.apk\r\n");
#else
    writeFile(bin / "gradle",
              "#!/bin/sh\n"
              "echo \"FAKE_GRADLE $*\" > gradle-invoked.txt\n"
              "mkdir -p app/build/outputs/apk/release app/build/outputs/apk/debug\n"
              "echo ok > app/build/outputs/apk/release/" +
                  std::string(unsignedOnly ? "app-release-unsigned.apk" : "app-release.apk") +
                  "\n"
              "echo ok > app/build/outputs/apk/debug/app-debug.apk\n");
    std::filesystem::permissions(bin / "gradle",
                                 std::filesystem::perms::owner_all |
                                     std::filesystem::perms::group_read |
                                     std::filesystem::perms::others_read,
                                 ec);
#endif
}

// A fake sdkmanager whose only job is to exit 0 for --licenses and package
// installs, so `eve get android` skips the real command-line-tools download.
[[maybe_unused]] void writeFakeSdkmanager(const std::filesystem::path& sdkRoot) {
    const auto bin = sdkRoot / "cmdline-tools" / "latest" / "bin";
    std::error_code ec;
    std::filesystem::create_directories(bin, ec);
#if defined(_WIN32)
    writeFile(bin / "sdkmanager.bat", "@exit /b 0\r\n");
#else
    writeFile(bin / "sdkmanager", "#!/bin/sh\nexit 0\n");
    std::filesystem::permissions(bin / "sdkmanager",
                                 std::filesystem::perms::owner_all |
                                     std::filesystem::perms::group_read |
                                     std::filesystem::perms::others_read,
                                 ec);
#endif
}

// 版本号去前缀 v（share/eve/VERSION 里不带 v）。
[[maybe_unused]] std::string sdkVersionNoV() {
    std::string tag = eve::cmd::sdk::sdkVersionTag();
    return tag.empty() ? std::string("0.0.0") : tag.substr(1);
}

// file:// URL（供 curl 读取本地 fake release）。
[[maybe_unused]] std::string fileUrl(const std::filesystem::path& p) {
    std::string s = p.lexically_normal().string();
    std::replace(s.begin(), s.end(), '\\', '/');
    if (s.empty() || s[0] != '/') s = "/" + s;
    return "file://" + s;
}

// 写一个和官方 zip-sdk.py 布局一致的 fake SDK 包：
// 顶层 eve-sdk/android/...（含标记、lib、platform 模板）。
[[maybe_unused]] void writeFakeEveSdkZip(const std::filesystem::path& zipPath, const std::string& ver) {
    eve::cmdline::ZipWriter zw;
    REQUIRE(zw.open(zipPath.string()));
    const auto add = [&](const std::string& rel, const std::string& data) {
        REQUIRE(zw.addFile(rel, data.data(), data.size()));
    };
    add("eve-sdk/android/share/eve/TARGET_PLATFORM", "android\n");
    add("eve-sdk/android/share/eve/VERSION", ver + "\n");
    add("eve-sdk/android/lib/libmain.so", "fake so");
    add("eve-sdk/android/lib/libSDL2.so", "fake so");
    add("eve-sdk/android/platform/apk/template.txt", "apk template");
    add("eve-sdk/android/platform/game-shell/config.nut", "width=800\n");
    REQUIRE(zw.finish());
}

// 为 fake release 目录写 SHA256SUMS（correct=false 时放一个错误摘要）。
[[maybe_unused]] void writeSha256Sums(const std::filesystem::path& releaseDir,
                     const std::filesystem::path& zipPath, bool correct) {
    const std::string hex =
        correct ? eve::cmd::sdk::fileSha256(zipPath.string()) : std::string(64, '0');
    writeFile(releaseDir / "SHA256SUMS",
              hex + "  " + zipPath.filename().string() + "\n");
}

// 在 EVEngine SDK 安装根预置一个 android SDK（跳过下载路径）。
[[maybe_unused]] std::filesystem::path fakeInstalledEveSdk(const std::filesystem::path& installRoot) {
    const auto sdk = installRoot / "android";
    writeFile(sdk / "share" / "eve" / "TARGET_PLATFORM", "android\n");
    writeFile(sdk / "share" / "eve" / "VERSION", sdkVersionNoV() + "\n");
    writeFile(sdk / "lib" / "libmain.so", "fake so");
    writeFile(sdk / "lib" / "libSDL2.so", "fake so");
    writeFile(sdk / "platform" / "apk" / "template.txt", "apk template");
    writeFile(sdk / "platform" / "game-shell" / "config.nut", "width=800\n");
    return sdk;
}

}  // namespace

TEST_CASE("cmdline.helpListsSubcommands") {
    CaptureStreams cap;
    const int      rc = runCli({"eve", "--help"});
    CHECK(rc == 0);
    CHECK(cap.out().find("Subcommands:") != std::string::npos);
    for (const char* sub : {"run", "create", "zip", "package", "dev", "doc", "clean", "test", "build", "get", "mcp",
                            "language-server"})
        CHECK(cap.out().find(sub) != std::string::npos);
}

TEST_CASE("cmdline.versionPrintsVersion") {
    CaptureStreams cap;
    const int      rc = runCli({"eve", "--version"});
    CHECK(rc == 0);
    CHECK(cap.out().find('v') == 0);
}

TEST_CASE("cmdline.invalidArgumentsReportErrors") {
    struct InvalidArguments {
        const char*              name;
        std::vector<std::string> arguments;
        int                      exitCode;  // -1 means any nonzero code, matching the parser contract.
        const char*              message;
    };
    const InvalidArguments cases[] = {
        {"unknown command", {"eve", "frobnicate"}, -1, "not expected"},
        {"missing platform", {"eve", "build"}, 2, "missing platform"},
        {"unknown platform", {"eve", "build", "-p", "nonsense"}, 2, "unknown platform"},
        {"unsupported platform", {"eve", "get", "ios"}, 2, "not implemented"},
    };
    for (const auto& input : cases) {
        std::cout << "CLI input: " << input.name << '\n';
        int         rc = 0;
        std::string output;
        {
            CaptureStreams cap;
            rc     = runCli(input.arguments);
            output = cap.all();
        }
        REQUIRE(rc != 0);
        if (input.exitCode != -1) REQUIRE_EQ(rc, input.exitCode);
        REQUIRE(output.find(input.message) != std::string::npos);
    }
}


TEST_CASE("cmdline.docPrintsUrlAndSymbol") {
    // Regression: the doc handler looked up the "create" subcommand, so
    // `eve doc` never ran. --no-open keeps the test (and headless/CI calls)
    // from spawning a browser.
    CaptureStreams cap;
    const int      rc = runCli({"eve", "doc", "--no-open", "gfx.drawSolidRect"});
    CHECK(rc == 0);
    CHECK(cap.out().find("EVEngine docs for 'gfx.drawSolidRect'") != std::string::npos);
    CHECK(cap.out().find("https://evengine.github.io/EVEngine/") != std::string::npos);

    CaptureStreams cap2;
    const int      rc2 = runCli({"eve", "doc", "--no-open"});
    CHECK(rc2 == 0);
    CHECK(cap2.out().find("EVEngine docs:") != std::string::npos);
}
