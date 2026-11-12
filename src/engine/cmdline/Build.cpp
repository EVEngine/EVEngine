#include "cmdline/cmdline.h"
#include "cmdline/sdk_tools.h"
#include "common/config.h"

#if defined(EVENGINE_MACOSX)
#include <Poco/Process.h>
#endif
#include <CLI11.hpp>
#include <rang.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>

using std::string;
using std::cerr;
using std::cout;
using std::endl;

namespace eve::cmd {

namespace {

// Reads the positional arguments of the build subcommand: the first positional
// is treated as the platform name when it matches a known platform and no
// -p/--platform was given; otherwise it is the game path.
bool splitBuildArgs(const std::vector<std::string>& remaining, std::string& platform,
                    std::string& game) {
    game = ".";
    if (remaining.empty()) return true;
    if (platform.empty() && sdk::parsePlatform(remaining[0]) != sdk::Platform::Unknown) {
        platform = remaining[0];
        if (remaining.size() > 1) game = remaining[1];
        return remaining.size() <= 2;
    }
    game = remaining[0];
    return remaining.size() == 1;
}

// Apple tooling is shipped with the SDK; argv is passed directly, never through a shell.
int buildApple(const string& game, const string& output, const string& platform, const string& sdkArgument,
               const std::vector<string>& options) {
#if defined(EVENGINE_MACOSX)
    namespace fs        = std::filesystem;
    string       root   = sdk::findSdkRoot(sdkArgument);
    const string target = platform == "macos" ? "macosx" : "ios";
    if (sdkArgument.empty() && sdk::getEnv("EVENGINE_SDK").empty() &&
        (root.empty() || sdk::sdkTargetPlatform(root) != target)) {
        const fs::path installed = fs::path(sdk::eveSdkInstallRoot()) / target;
        if (fs::is_regular_file(installed / "share/eve/TARGET_PLATFORM")) root = installed.string();
    }
    if (root.empty()) {
        cerr << "eve build: Apple SDK not found; pass --sdk <target-sdk>." << endl;
        return 2;
    }
    const fs::path helper = fs::path(root) / "share/eve/tools/apple_package.py";
    if (!fs::is_regular_file(helper)) {
        cerr << "eve build: SDK is missing share/eve/tools/apple_package.py; update the target SDK." << endl;
        return 2;
    }
    const fs::path gameDirectory = fs::weakly_canonical(fs::absolute(game));
    const string   destination =
        output.empty() ? (gameDirectory.parent_path() / (gameDirectory.filename().string() + "-" + platform)).string()
                         : output;
    std::vector<string> args{helper.string(), platform, game, "--sdk", root, "--output", destination};
    args.insert(args.end(), options.begin(), options.end());
    try {
        return Poco::Process::launch("/usr/bin/python3", args).wait();
    } catch (const std::exception& error) {
        cerr << "eve build: cannot launch Apple packaging tools: " << error.what() << endl;
        return 2;
    }
#else
    (void)game;
    (void)output;
    (void)platform;
    (void)sdkArgument;
    (void)options;
    cerr << "eve build: Apple bundle assembly requires macOS and Xcode." << endl;
    return 2;
#endif
}

std::string gradleLauncher(const std::string& gradleHome) {
#if defined(_WIN32)
    return (std::filesystem::path(gradleHome) / "bin" / "gradle.bat").string();
#else
    return (std::filesystem::path(gradleHome) / "bin" / "gradle").string();
#endif
}

// 组装命令：切到 <apk>/ 目录后调用 gradle 的 assembleRelease/assembleDebug。
std::string gradleCommand(const std::string& apkDir, const std::string& gradleHome,
                          bool debug) {
    const std::string launcher = gradleLauncher(gradleHome);
#if defined(_WIN32)
    // `call` 前缀避免 cmd /c 剥掉以引号开头的命令的首尾引号。
    return "cd /d \"" + apkDir + "\" && call \"" + launcher + "\" assemble" +
           std::string(debug ? "Debug" : "Release");
#else
    return "cd '" + apkDir + "' && '" + launcher + "' assemble" +
           std::string(debug ? "Debug" : "Release");
#endif
}

}  // namespace

struct BuildArgs : Handler {
    string log_path, platform, output_path, sdk_path;
    string name, bundle_id, version, identity, profile, device;
    bool   unsigned_app = false, simulator = false;
    bool   release = false, debug = false;

    void setup(CLI::App& app, std::shared_ptr<CLI::Formatter> formatter) override {
        auto subcmd = app.add_subcommand("build", "Build an Android game or an Apple app using the packaged SDK");
        subcmd->allow_extras()->formatter(formatter);
        subcmd->add_option("-p,--platform", platform, "Build platform (android, macos, ios)");
        subcmd->add_flag("-r,--release", release, "release build (default)");
        subcmd->add_flag("-d,--debug", debug, "debug build");
        subcmd->add_option("-l,--log", log_path, "log messages into a file");
        subcmd->add_option("-o,--output", output_path, "output directory for the built game");
        subcmd->add_option("--sdk", sdk_path, "EVEngine SDK root (default: $EVENGINE_SDK or next to the eve binary)");
        subcmd->add_option("--name", name, "Apple app name (default: Game)");
        subcmd->add_option("--bundle-id", bundle_id, "Apple reverse-DNS bundle identifier");
        subcmd->add_option("--version", version, "Apple app version (default: 1.0.0)");
        subcmd->add_option("--sign-identity", identity, "iOS development certificate name or SHA-1 fingerprint");
        subcmd->add_option("--profile", profile, "iOS development provisioning profile");
        subcmd->add_option("--device", device, "Provisioned device UDID; install and launch the iOS app");
        subcmd->add_flag("--unsigned", unsigned_app, "Assemble iOS app only; cannot install on a device");
        subcmd->add_flag("--simulator", simulator, "Use an iOS simulator SDK and ad-hoc signing");
    }

    int parse(CLI::App& app, Cmdline& cmd) override {
        auto subcmd = app.get_subcommand("build");
        if (subcmd->parsed()) {
            if (debug && release) {
                cerr << rang::fg::red << "Build type can not be both debug and release"
                     << rang::fg::reset << endl;
                return 1;
            }

            std::string game;
            if (!splitBuildArgs(subcmd->remaining(), platform, game)) {
                cerr << rang::fg::red << "Unknown remaining arguments" << rang::fg::reset << endl;
                return 1;
            }
            const auto          target = sdk::parsePlatform(platform);
            std::vector<string> appleOptions;
            for (const auto& [flag, value] : std::vector<std::pair<string, string>>{{"--name", name},
                                                                                    {"--bundle-id", bundle_id},
                                                                                    {"--version", version},
                                                                                    {"--sign-identity", identity},
                                                                                    {"--profile", profile},
                                                                                    {"--device", device}}) {
                if (!value.empty()) appleOptions.insert(appleOptions.end(), {flag, value});
            }
            if (unsigned_app) appleOptions.push_back("--unsigned");
            if (simulator) appleOptions.push_back("--simulator");
            if (target == sdk::Platform::Macosx || target == sdk::Platform::Ios) {
                if (debug || release) {
                    cerr << "eve build: Apple apps use the selected SDK configuration; omit -d/-r." << endl;
                    return 2;
                }
                return buildApple(game, output_path, target == sdk::Platform::Macosx ? "macos" : "ios", sdk_path,
                                  appleOptions);
            }
            if (!appleOptions.empty()) {
                cerr << "eve build: Apple app options require macos or ios." << endl;
                return 2;
            }
            return cmd.Build(game, output_path, platform, sdk_path, debug);
        }
        return -1;  // not handle
    }
};

CMD_REG(BuildArgs);

int Cmdline::Build(std::string path, std::string output, std::string platform,
                   std::string sdkRoot, bool debug) {
    using namespace sdk;
    namespace fs = std::filesystem;

    const Platform p = parsePlatform(platform);
    if (platform.empty()) {
        cerr << rang::fg::red << "eve build: missing platform. Pass a platform name, "
             << "e.g. `eve build android`." << rang::fg::reset << endl;
        return 2;
    }
    if (p == Platform::Unknown) {
        cerr << rang::fg::red << "eve build: unknown platform '" << platform << "' (supported: android, macos, ios)"
             << rang::fg::reset << endl;
        return 2;
    }
    if (p == Platform::Macosx || p == Platform::Ios)
        return buildApple(path, output, p == Platform::Macosx ? "macos" : "ios", sdkRoot, {});
    if (p != Platform::Android) {
        cerr << rang::fg::red << "eve build: SDK mode supports android, macos and ios; "
             << "desktop targets are packaged with `eve package`." << rang::fg::reset << endl;
        return 2;
    }

    // 1. Android SDK 工具链目录（`eve get android` 安装）。先读它写的
    // eve-android.env（ANDROID_HOME / JAVA_HOME / GRADLE_HOME / EVENGINE_SDK），
    // 再据此定位 EVEngine SDK。
    const std::string asdk = androidSdkRoot();
    std::error_code  ec;
    if (!fs::is_directory(asdk, ec)) {
        cerr << rang::fg::red << "Android SDK not found at " << asdk << ". Run `eve get android` "
             << "first, or set ANDROID_HOME / EVENGINE_ANDROID_SDK."
             << rang::fg::reset << endl;
        return 2;
    }
    applyEnvFile((fs::path(asdk) / "eve-android.env").string());

    // 2. 定位 EVEngine SDK（无需源码/Makefile/cmake）：--sdk > $EVENGINE_SDK
    // （含 env 文件注入）> eve 可执行文件位置 > `eve get` 安装目录。
    std::string root = findSdkRoot(sdkRoot);
    if (root.empty()) {
        cerr << "eve build: cannot locate the EVEngine SDK. Pass --sdk <dir>, set "
                "$EVENGINE_SDK, or run `eve get android` first."
             << endl;
        return 2;
    }
    const std::string sdkPlat = sdkTargetPlatform(root);
    if (sdkPlat != "android") {
        cerr << rang::fg::red << "eve build: this SDK targets '" << sdkPlat
             << "', not android. Get the android SDK." << rang::fg::reset << endl;
        return 2;
    }

    // 3. JDK / Gradle 由 `eve get android` 安装（env 文件已注入）。
    const std::string javaHome = getEnv("JAVA_HOME");
    const std::string gradleHome = getEnv("GRADLE_HOME");
    if (javaHome.empty() || gradleHome.empty() ||
        !fs::is_regular_file(gradleLauncher(gradleHome), ec)) {
        cerr << rang::fg::red << "Missing JDK/Gradle for Android builds. Run `eve get android` "
             << "to install them (JAVA_HOME / GRADLE_HOME)." << rang::fg::reset << endl;
        return 2;
    }
    setEnv("ANDROID_HOME", asdk);
    setEnv("ANDROID_SDK_ROOT", asdk);

    // 4. 游戏目录：默认当前目录；位于 SDK 根时用自带的 demo 壳。
    std::string game = path;
    if (game.empty() || game == ".") game = fs::absolute(".").string();
    else game = fs::absolute(game, ec).string();
    if (ec || !fs::is_directory(game, ec)) {
        cerr << rang::fg::red << "eve build: bad game path '" << path << "'"
             << rang::fg::reset << endl;
        return 2;
    }
    if (fs::equivalent(game, root, ec) || fs::path(game) == fs::path(root)) {
        const auto demo = fs::path(root) / "platform" / "game-shell";
        if (fs::is_directory(demo, ec)) game = demo.string();
    }

    // 5. 组装 APK 工程到输出目录（模板 + 游戏资源 + 预编译 .so，不动 SDK）。
    const std::string outDir = output.empty()
                                   ? (fs::current_path() / "build" / "eve-android").string()
                                   : fs::absolute(output, ec).string();
    fs::remove_all(outDir, ec);
    fs::create_directories(outDir, ec);
    const std::string apkDir = (fs::path(outDir) / "apk").string();
    // 跳过模板自带的构建产物：官方 SDK 的 platform/apk 在发布流水线里被
    // test-sdk.sh 编译过一次，app/build 下会残留一个 CI 测试 APK（不是用户的
    // 游戏）。copyTree 只按相对路径跳过顶层目录，所以 app/build 要显式列出。
    if (!copyTree((fs::path(root) / "platform" / "apk").string(), apkDir,
                  {"build", ".gradle", "local.properties", "app/build"})) {
        cerr << rang::fg::red << "SDK is missing the android APK template (platform/apk)."
             << rang::fg::reset << endl;
        return 3;
    }
    const std::string assetsGame = (fs::path(apkDir) / "app" / "src" / "main" / "assets" / "game").string();
    if (!copyTreeContents(game, assetsGame)) {
        cerr << rang::fg::red << "Failed to copy game assets." << rang::fg::reset << endl;
        return 3;
    }
    const std::string jniDir = (fs::path(apkDir) / "app" / "src" / "main" / "jniLibs" / "arm64-v8a").string();
    fs::create_directories(jniDir, ec);
    int libs = 0;
    for (const auto& entry : fs::directory_iterator(fs::path(root) / "lib", ec)) {
        if (entry.is_regular_file() && entry.path().extension() == ".so") {
            fs::copy_file(entry.path(), fs::path(jniDir) / entry.path().filename(),
                          fs::copy_options::overwrite_existing, ec);
            ++libs;
        }
    }
    if (libs == 0) {
        cerr << rang::fg::yellow
             << "note: no prebuilt .so found in the SDK lib/ dir; the APK will be "
                "missing native code."
             << rang::fg::reset << endl;
    }
    {
        // 用正斜杠避免 Java properties 的转义问题。
        std::string sdkProp = asdk;
        for (char& c : sdkProp)
            if (c == '\\') c = '/';
        std::ofstream f(fs::path(apkDir) / "local.properties", std::ios::binary | std::ios::trunc);
        f << "sdk.dir=" << sdkProp << "\n";
    }

    // 6. Gradle 组装 APK。
    const std::string cmd = gradleCommand(apkDir, gradleHome, debug);
    cout << "eve build: " << cmd << endl;
    const int rc = runShell(cmd);
    if (rc != 0) {
        cerr << rang::fg::red << "eve build: gradle failed (exit " << rc << ")."
             << rang::fg::reset << endl;
        return 4;
    }

    // 7. 输出 APK 路径（release 未配置签名时 AGP 产出 app-release-unsigned.apk）。
    const std::string variant = debug ? "debug" : "release";
    fs::path apk;
    {
        std::vector<std::string> names = {"app-" + variant + ".apk"};
        if (!debug) names.push_back("app-release-unsigned.apk");
        for (const auto& n : names) {
            const fs::path candidate = fs::path(apkDir) / "app" / "build" / "outputs" / "apk" /
                                       variant / n;
            if (fs::is_regular_file(candidate, ec)) {
                apk = candidate;
                break;
            }
        }
    }
    if (apk.empty()) {
        cerr << rang::fg::yellow
             << "note: gradle finished but no APK found under "
             << (fs::path(apkDir) / "app" / "build" / "outputs" / "apk").string()
             << rang::fg::reset << endl;
        return 0;
    }
    cout << rang::fg::green << "Built APK -> " << rang::fg::reset << apk.string();
    if (apk.filename() == "app-release-unsigned.apk")
        cout << " (unsigned; add a signing config to release properly)";
    cout << endl;
    return 0;
}

}  // namespace eve::cmd
