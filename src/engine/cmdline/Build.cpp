#include "cmdline/cmdline.h"
#include "cmdline/sdk_tools.h"

#include <CLI11.hpp>
#include <rang.hpp>

#include <filesystem>
#include <iostream>

using std::string;
using std::cerr;
using std::cout;
using std::endl;

namespace eve::cmd {

namespace {

// Reads the positional arguments of the build subcommand: the first positional
// is treated as the platform name when it matches a known platform and no
// -p/--platform was given; otherwise it is the game path (legacy behavior).
bool splitBuildArgs(const std::vector<std::string>& remaining, std::string& platform,
                    std::string& game) {
    game = ".";
    if (remaining.empty()) {
        if (platform.empty()) platform = sdk::hostPlatformName();
        return true;
    }
    if (platform.empty() && sdk::parsePlatform(remaining[0]) != sdk::Platform::Unknown) {
        platform = remaining[0];
        if (remaining.size() > 1) game = remaining[1];
        return remaining.size() <= 2;
    }
    game = remaining[0];
    if (platform.empty()) platform = sdk::hostPlatformName();
    return remaining.size() == 1;
}

}  // namespace

struct BuildArgs : Handler {
    string log_path, platform, output_path, sdk_path;
    bool   release = false, debug = false;

    void setup(CLI::App& app, std::shared_ptr<CLI::Formatter> formatter) override {
        auto subcmd = app.add_subcommand(
            "build", "Build the game for a platform (win32, linux, macosx, android, ios)");
        subcmd->allow_extras()->formatter(formatter);
        subcmd->add_option("-p,--platform", platform, "Build platform (win32, linux, macosx, android, ios)");
        subcmd->add_flag("-r,--release", release, "release build (default)");
        subcmd->add_flag("-d,--debug", debug, "debug build");
        subcmd->add_option("-l,--log", log_path, "log messages into a file");
        subcmd->add_option("-o,--output", output_path, "output folder path");
        subcmd->add_option("--sdk", sdk_path, "EVEngine checkout root (default: auto-detect from cwd or $EVENGINE_SDK)");
    }

    int parse(CLI::App& app, Cmdline& cmd) override {
        auto subcmd = app.get_subcommand("build");
        if (subcmd->parsed()) {
            if (debug && release) {
                cerr << rang::fg::red << "Build type can not be both debug and release" << rang::fg::reset << endl;
                return 1;
            }

            std::string game;
            if (!splitBuildArgs(subcmd->remaining(), platform, game)) {
                cerr << rang::fg::red << "Unknown remaining arguments" << rang::fg::reset << endl;
                return 1;
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
    if (p == Platform::Unknown) {
        cerr << rang::fg::red << "eve build: unknown platform '" << platform
             << "' (supported: win32, linux, macosx, android, ios)" << rang::fg::reset << endl;
        return 2;
    }

    std::string root = sdkRoot.empty() ? getEnv("EVENGINE_SDK") : sdkRoot;
    if (root.empty()) root = findEngineRoot();
    if (root.empty()) {
        cerr << "eve build: cannot locate the EVEngine checkout. Run eve build from the "
                "repository, set $EVENGINE_SDK, or pass --sdk <dir>."
             << endl;
        return 2;
    }
    std::error_code ec;
    if (!fs::is_regular_file(fs::path(root) / "Makefile", ec) ||
        !fs::is_regular_file(fs::path(root) / "CMakeLists.txt", ec)) {
        cerr << rang::fg::red << "eve build: " << root
             << " is not an EVEngine checkout (missing Makefile/CMakeLists.txt)"
             << rang::fg::reset << endl;
        return 2;
    }

#if defined(_WIN32)
    if (p == Platform::Macosx || p == Platform::Ios) {
        cerr << rang::fg::red << "eve build: " << platformName(p)
             << " builds require a macOS host." << rang::fg::reset << endl;
        return 2;
    }
#else
    if (p == Platform::Win32) {
        cerr << rang::fg::red << "eve build: win32 builds require a Windows host."
             << rang::fg::reset << endl;
        return 2;
    }
#endif

    std::string target = "build/" + platformName(p) + (debug ? "-debug" : "");
#if defined(_WIN32)
    // Linux builds on a Windows host go through the Makefile's WSL2 targets.
    if (p == Platform::Linux) target = "wsl/" + std::string(debug ? "linux-debug" : "linux");
#endif

    std::string cmd = "make -C \"" + root + "\" " + target;
    if (p == Platform::Android) {
        const std::string sdk = androidSdkRoot();
        if (!fs::is_directory(sdk, ec)) {
            cerr << rang::fg::red << "Android SDK not found at " << sdk << ". Run `eve get android` "
                 << "first, or set ANDROID_HOME / EVENGINE_ANDROID_SDK."
                 << rang::fg::reset << endl;
            return 2;
        }
        setEnv("ANDROID_HOME", sdk);
        setEnv("ANDROID_SDK_ROOT", sdk);
        applyEnvFile((fs::path(sdk) / "eve-android.env").string());

        std::string game = path;
        if (game.empty() || game == ".") {
            // Running from the engine checkout root falls back to the built-in
            // demo shell; anywhere else the current directory is the game.
            const std::string cwd   = fs::absolute(".").string();
            const std::string engine = findEngineRoot();
            game = (!engine.empty() && engine == cwd) ? std::string("demo") : cwd;
        } else {
            game = fs::absolute(game, ec).string();
            if (ec) {
                cerr << rang::fg::red << "eve build: bad game path '" << path << "'"
                     << rang::fg::reset << endl;
                return 2;
            }
        }
        cmd += " ANDROID_GAME=\"" + game + "\"";
    }

    cout << "eve build: running " << cmd << endl;
    return runShell(cmd);
}

}  // namespace eve::cmd
