#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "cmdline/cmdline.h"
#include "cmdline/sdk_tools.h"
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
int runCli(const std::vector<std::string>& args) {
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

std::filesystem::path tempDir(const char* tag) {
    const auto p = std::filesystem::temp_directory_path() / tag;
    std::error_code ec;
    std::filesystem::remove_all(p, ec);
    std::filesystem::create_directories(p, ec);
    return p;
}

void writeFile(const std::filesystem::path& p, const std::string& content) {
    std::filesystem::create_directories(p.parent_path());
    std::ofstream ofs(p, std::ios::binary | std::ios::trunc);
    ofs << content;
}

// Reads the ZIP central directory (EOCD -> central entries) and returns the
// entry names in archive order. Returns {} when the file is not a valid ZIP.
std::vector<std::string> zipEntryNames(const std::filesystem::path& p) {
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

void setEnvVar(const char* name, const std::string& value) {
#if defined(_WIN32)
    _putenv_s(name, value.c_str());
#else
    setenv(name, value.c_str(), 1);
#endif
}

void unsetEnvVar(const char* name) {
#if defined(_WIN32)
    _putenv_s(name, "");
#else
    unsetenv(name);
#endif
}

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
std::filesystem::path fakeSdkRoot(const char* tag, const char* platform) {
    const auto sdk = tempDir(tag);
    writeFile(sdk / "share" / "eve" / "TARGET_PLATFORM", std::string(platform) + "\n");
    writeFile(sdk / "lib" / "libmain.so", "fake so");
    writeFile(sdk / "lib" / "libSDL2.so", "fake so");
    writeFile(sdk / "platform" / "apk" / "template.txt", "apk template");
    writeFile(sdk / "platform" / "game-shell" / "config.nut", "width=800\n");
    return sdk;
}

// A fake gradle distribution whose launcher records the invocation and writes
// the APK output that `eve build android` checks for.
void writeFakeGradle(const std::filesystem::path& gradleHome) {
    const auto bin = gradleHome / "bin";
    std::error_code ec;
    std::filesystem::create_directories(bin, ec);
#if defined(_WIN32)
    writeFile(bin / "gradle.bat",
              "@echo off\r\n"
              "echo FAKE_GRADLE %* > gradle-invoked.txt\r\n"
              "mkdir app\\build\\outputs\\apk\\release 2>nul\r\n"
              "mkdir app\\build\\outputs\\apk\\debug 2>nul\r\n"
              "echo ok > app\\build\\outputs\\apk\\release\\app-release.apk\r\n"
              "echo ok > app\\build\\outputs\\apk\\debug\\app-debug.apk\r\n");
#else
    writeFile(bin / "gradle",
              "#!/bin/sh\n"
              "echo \"FAKE_GRADLE $*\" > gradle-invoked.txt\n"
              "mkdir -p app/build/outputs/apk/release app/build/outputs/apk/debug\n"
              "echo ok > app/build/outputs/apk/release/app-release.apk\n"
              "echo ok > app/build/outputs/apk/debug/app-debug.apk\n");
    std::filesystem::permissions(bin / "gradle",
                                 std::filesystem::perms::owner_all |
                                     std::filesystem::perms::group_read |
                                     std::filesystem::perms::others_read,
                                 ec);
#endif
}

}  // namespace

TEST_CASE("cmdline.helpListsSubcommands") {
    CaptureStreams cap;
    const int      rc = runCli({"eve", "--help"});
    CHECK(rc == 0);
    CHECK(cap.out().find("Subcommands:") != std::string::npos);
    for (const char* sub : {"run", "create", "zip", "package", "dev", "doc", "clean", "test", "build", "get", "mcp"})
        CHECK(cap.out().find(sub) != std::string::npos);
}

TEST_CASE("cmdline.versionPrintsVersion") {
    CaptureStreams cap;
    const int      rc = runCli({"eve", "--version"});
    CHECK(rc == 0);
    CHECK(cap.out().find('v') == 0);
}

TEST_CASE("cmdline.unknownCommandFails") {
    CaptureStreams cap;
    const int      rc = runCli({"eve", "frobnicate"});
    CHECK(rc != 0);
    CHECK(cap.all().find("not expected") != std::string::npos);
}

TEST_CASE("cmdline.createScaffoldsGame") {
    const auto dir = tempDir("eve_ut_cmdline_create");
    PushDir   pd(dir);
    {
        CaptureStreams cap;
        const int      rc = runCli({"eve", "create", "mygame"});
        CHECK(rc == 0);
        CHECK(cap.out().find("Created mygame") != std::string::npos);
    }
    REQUIRE(std::filesystem::is_regular_file(dir / "mygame" / "config.nut"));
    REQUIRE(std::filesystem::is_regular_file(dir / "mygame" / "main.nut"));

    // A second run must not clobber existing files.
    {
        CaptureStreams cap;
        const int      rc = runCli({"eve", "create", "mygame"});
        CHECK(rc == 0);
        CHECK(cap.out().find("skip existing config.nut") != std::string::npos);
    }
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

TEST_CASE("cmdline.cleanDoesNotScaffoldProject") {
    // Regression: the clean handler used to call Create(), so `eve clean`
    // scaffolded a fresh "debug" project in the current directory.
    const auto dir = tempDir("eve_ut_cmdline_clean");
    PushDir   pd(dir);
    CaptureStreams cap;
    const int      rc = runCli({"eve", "clean"});
    CHECK(rc == 0);
    CHECK(cap.out().find("Clean debug build") != std::string::npos);
    CHECK(!std::filesystem::exists(dir / "debug"));
    CHECK(!std::filesystem::exists(dir / "config.nut"));
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
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

TEST_CASE("cmdline.zipDefaultNamesArchiveAfterCwd") {
    // Regression: `eve zip` with no path (default ".") used to write a file
    // literally named "..eve" instead of <current-dir-name>.eve.
    const auto dir = tempDir("eve_ut_cmdline_zip_default");
    PushDir   pd(dir);
    writeFile(dir / "main.nut", "eve_init = function() {}\n");

    CaptureStreams cap;
    const int      rc = runCli({"eve", "zip"});
    CHECK(rc == 0);
    const std::string expected = dir.filename().string() + ".eve";
    CHECK(cap.out().find(expected) != std::string::npos);
    CHECK(!std::filesystem::exists(dir / "..eve"));
    REQUIRE(std::filesystem::is_regular_file(dir / expected));
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

TEST_CASE("cmdline.zipProducesValidArchiveWithoutItself") {
    const auto dir = tempDir("eve_ut_cmdline_zip_entries");
    PushDir   pd(dir);
    writeFile(dir / "main.nut", "eve_init = function() {}\n");
    writeFile(dir / "scripts" / "a.nut", "a <- 1\n");

    CaptureStreams cap;
    const int      rc = runCli({"eve", "zip"});
    CHECK(rc == 0);

    const std::string archive = dir.filename().string() + ".eve";
    REQUIRE(std::filesystem::is_regular_file(dir / archive));

    const auto names = zipEntryNames(dir / archive);
    REQUIRE(names.size() == 2);
    // Directory iteration order is not guaranteed, so compare sorted sets.
    std::vector<std::string> sorted = names;
    std::sort(sorted.begin(), sorted.end());
    CHECK(sorted[0] == "main.nut");
    CHECK(sorted[1] == "scripts/a.nut");
    // The writer must never embed the output archive into itself.
    for (const auto& n : names) CHECK(n != archive);

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

TEST_CASE("cmdline.buildAndroidAssemblesApkFromSdk") {
    const auto sdk   = fakeSdkRoot("eve_ut_cmdline_sdk_android", "android");
    const auto tools = tempDir("eve_ut_cmdline_android_tools");
    writeFakeGradle(tools / "gradle-8.5");
    ScopedEnv sdkEnv("EVENGINE_ANDROID_SDK", tools.string());
    ScopedEnv javaEnv("JAVA_HOME", tools.string());
    ScopedEnv gradleEnv("GRADLE_HOME", (tools / "gradle-8.5").string());

    const auto game = tempDir("eve_ut_cmdline_game");
    writeFile(game / "main.nut", "eve_init = function() {}\n");
    writeFile(game / "scripts" / "a.nut", "a <- 1\n");
    const auto out = tempDir("eve_ut_cmdline_apk_out");

    CaptureStreams cap;
    const int      rc = runCli(
        {"eve", "build", "android", game.string(), "--sdk", sdk.string(), "-o", out.string()});
    CHECK(rc == 0);

    // Game assets + prebuilt libs + sdk.dir landed in the assembled project.
    std::error_code ec;
    CHECK(std::filesystem::is_regular_file(
        out / "apk" / "app" / "src" / "main" / "assets" / "game" / "main.nut", ec));
    CHECK(std::filesystem::is_regular_file(
        out / "apk" / "app" / "src" / "main" / "assets" / "game" / "scripts" / "a.nut", ec));
    CHECK(std::filesystem::is_regular_file(
        out / "apk" / "app" / "src" / "main" / "jniLibs" / "arm64-v8a" / "libmain.so", ec));
    CHECK(std::filesystem::is_regular_file(
        out / "apk" / "app" / "src" / "main" / "jniLibs" / "arm64-v8a" / "libSDL2.so", ec));
    REQUIRE(std::filesystem::is_regular_file(out / "apk" / "local.properties", ec));
    {
        std::ifstream in(out / "apk" / "local.properties");
        std::string   s((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        std::string   expected = tools.string();
        for (char& c : expected)
            if (c == '\\') c = '/';
        CHECK(s.find("sdk.dir=" + expected) != std::string::npos);
    }
    // Fake gradle ran with the release task and the APK path was reported.
    REQUIRE(std::filesystem::is_regular_file(out / "apk" / "gradle-invoked.txt", ec));
    {
        std::ifstream in(out / "apk" / "gradle-invoked.txt");
        std::string   s;
        std::getline(in, s);
        CHECK(s.find("assembleRelease") != std::string::npos);
    }
    CHECK(cap.out().find("app-release.apk") != std::string::npos);

    // Debug variant picks the assembleDebug task.
    CaptureStreams cap2;
    const int      rc2 = runCli({"eve", "build", "-d", "android", game.string(),
                            "--sdk", sdk.string(), "-o", out.string()});
    CHECK(rc2 == 0);
    {
        std::ifstream in(out / "apk" / "gradle-invoked.txt");
        std::string   s;
        std::getline(in, s);
        CHECK(s.find("assembleDebug") != std::string::npos);
    }
    CHECK(cap2.out().find("app-debug.apk") != std::string::npos);

    std::filesystem::remove_all(sdk, ec);
    std::filesystem::remove_all(tools, ec);
    std::filesystem::remove_all(game, ec);
    std::filesystem::remove_all(out, ec);
}

TEST_CASE("cmdline.buildWrongSdkPlatformFails") {
    const auto sdk = fakeSdkRoot("eve_ut_cmdline_sdk_win32", "win32");
    CaptureStreams cap;
    const int      rc = runCli({"eve", "build", "android", "--sdk", sdk.string()});
    CHECK(rc == 2);
    CHECK(cap.all().find("not android") != std::string::npos);
    std::error_code ec;
    std::filesystem::remove_all(sdk, ec);
}

TEST_CASE("cmdline.buildMissingPlatformFails") {
    CaptureStreams cap;
    const int      rc = runCli({"eve", "build"});
    CHECK(rc == 2);
    CHECK(cap.all().find("missing platform") != std::string::npos);
}

TEST_CASE("cmdline.buildUnknownPlatformFails") {
    CaptureStreams cap;
    const int      rc = runCli({"eve", "build", "-p", "nonsense"});
    CHECK(rc == 2);
    CHECK(cap.all().find("unknown platform") != std::string::npos);
}

TEST_CASE("cmdline.buildMissingAndroidSdkFails") {
    const auto sdk = fakeSdkRoot("eve_ut_cmdline_sdk_missing_tools", "android");
    ScopedEnv sdkEnv("EVENGINE_ANDROID_SDK", (sdk / "no-tools").string());
    CaptureStreams cap;
    const int      rc = runCli({"eve", "build", "android", "--sdk", sdk.string()});
    CHECK(rc == 2);
    CHECK(cap.all().find("eve get android") != std::string::npos);
    std::error_code ec;
    std::filesystem::remove_all(sdk, ec);
}

TEST_CASE("cmdline.getAndroidAlreadyInstalled") {
    const auto sdk = tempDir("eve_ut_cmdline_android_installed");
    const auto bin = sdk / "cmdline-tools" / "latest" / "bin";
    std::error_code ec;
    std::filesystem::create_directories(bin, ec);
#if defined(_WIN32)
    std::ofstream(bin / "sdkmanager.bat", std::ios::binary | std::ios::trunc) << "@exit /b 0\r\n";
#else
    std::ofstream(bin / "sdkmanager", std::ios::binary | std::ios::trunc)
        << "#!/bin/sh\nexit 0\n";
    std::filesystem::permissions(bin / "sdkmanager",
                                 std::filesystem::perms::owner_all |
                                     std::filesystem::perms::group_read |
                                     std::filesystem::perms::others_read,
                                 ec);
#endif
    writeFakeGradle(sdk / "gradle-8.5");

    ScopedEnv sdkEnv("EVENGINE_ANDROID_SDK", sdk.string());
    ScopedEnv javaEnv("JAVA_HOME", sdk.string());  // non-empty -> skip JDK download
    CaptureStreams cap;
    const int      rc = runCli({"eve", "get", "android"});
    CHECK(rc == 0);
    CHECK(cap.out().find("already installed") != std::string::npos);
    REQUIRE(std::filesystem::is_regular_file(sdk / "eve-android.env", ec));
    {
        std::ifstream in(sdk / "eve-android.env");
        std::string   s((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        CHECK(s.find("GRADLE_HOME=") != std::string::npos);
        CHECK(s.find("ANDROID_HOME=") != std::string::npos);
    }
    std::filesystem::remove_all(sdk, ec);
}

TEST_CASE("cmdline.getUnsupportedPlatformFails") {
    CaptureStreams cap;
    const int      rc = runCli({"eve", "get", "ios"});
    CHECK(rc == 2);
    CHECK(cap.all().find("not implemented") != std::string::npos);
}
