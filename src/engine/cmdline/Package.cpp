#include "cmdline.h"
#include "zip_writer.h"
#include "common/config.h"
#include <filesystem>
#include <fstream>
#include <CLI11.hpp>
#include <rang.hpp>

#ifdef EVENGINE_WINDOWS
#include <windows.h>
#else
#include <unistd.h>
#endif

using namespace std::filesystem;
using namespace std;

namespace eve::cmd
{

namespace {

// Runtime binary name inside the *target* SDK's bin/. The packaged game runs on
// the SDK's platform, which is not necessarily the host running `eve package`
// (e.g. a Linux host can produce a Windows game folder for the win32 SDK).
std::string targetRuntimeName(const std::string& sdkRoot) {
    std::ifstream in(std::filesystem::path(sdkRoot) / "share" / "eve" / "TARGET_PLATFORM");
    std::string   plat;
    std::getline(in, plat);
    return plat == "win32" ? "eve.exe" : "eve";
}

bool copyFileIf(const path& src, const path& dst) {
    error_code ec;
    if (!exists(src, ec)) return false;
    create_directories(dst.parent_path(), ec);
    copy_file(src, dst, copy_options::overwrite_existing, ec);
    return !ec;
}

// Best-effort copy of a DLL from any of the candidate source directories.
bool copyFirst(const std::string& dll, const std::vector<path>& candidates, const path& dstDir) {
    for (const auto& dir : candidates) {
        path p = dir / dll;
        if (copyFileIf(p, dstDir / dll)) return true;
    }
    return false;
}

// Locate the target SDK root: --sdk, EVENGINE_SDK, or next to the running binary.
std::string resolveSdkRoot(const std::string& sdkArg) {
    if (!sdkArg.empty() && is_directory(sdkArg)) return sdkArg;

    const char* env = getenv("EVENGINE_SDK");
    if (env && is_directory(env)) return env;

    // Running from <sdk>/bin/<runtime> -> root is two directories up.
    path exe;
#ifdef EVENGINE_WINDOWS
    wchar_t buf[MAX_PATH + 1] = {0};
    if (GetModuleFileNameW(nullptr, buf, MAX_PATH) != 0) exe = path(buf);
#else
    char buf[4096] = {0};
    const ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n > 0) exe = path(std::string(buf, static_cast<size_t>(n)));
#endif
    if (!exe.empty()) {
        path root = exe.parent_path().parent_path();
        if (exists(root / "share" / "eve" / "TARGET_PLATFORM")) return root.string();
    }
    return "";
}

}  // namespace

struct PackageArgs : Handler {
    string sdkPath, outputPath;
    void setup(CLI::App& app, std::shared_ptr<CLI::Formatter> formatter) override {
        auto subcmd = app.add_subcommand("package", "Package a game into a runnable game folder.");
        subcmd->allow_extras();
        subcmd->add_option("-o,--output", outputPath, "output folder path");
        subcmd->add_option("--sdk", sdkPath, "target SDK root (default: auto-detect / $EVENGINE_SDK)");
    }

    int parse(CLI::App& app, Cmdline& cmd) override {
        auto subcmd = app.get_subcommand("package");
        if (subcmd->parsed()) {
            string game = cmd.get_remaining(subcmd, ".");
            int res = cmd.Package(game, outputPath, sdkPath);
            return res;
        }
        return -1; // not handle
    }
};

CMD_REG(PackageArgs);

int Cmdline::Package(std::string gamePath, std::string output, std::string sdk) {
    error_code ec;
    if (!is_directory(gamePath, ec)) {
        cerr << rang::fg::red << "Not a directory: " << rang::fg::reset << gamePath << endl;
        return 1;
    }

    std::string sdkRoot = resolveSdkRoot(sdk);
    if (sdkRoot.empty()) {
        cerr << rang::fg::red
             << "Cannot locate the target SDK. Pass --sdk <dir>, set $EVENGINE_SDK, "
                "or run from <sdk>/bin/<runtime>."
             << rang::fg::reset << endl;
        return 2;
    }

    // Game folder name, trailing separators stripped.
    std::string gameName = gamePath;
    while (!gameName.empty() && (gameName.back() == '/' || gameName.back() == '\\'))
        gameName.pop_back();
    if (gameName.find_first_of("/\\") != string::npos)
        gameName = std::filesystem::path(gameName).filename().string();
    if (gameName.empty()) gameName = "game";

    path outDir = output.empty() ? path(gameName + "-package") : path(output);
    create_directories(outDir, ec);

    path sdkBin  = path(sdkRoot) / "bin";
    path sdkLib  = path(sdkRoot) / "lib";
    path sdkPlat = path(sdkRoot) / "platform";

    const std::string runtimeName = targetRuntimeName(sdkRoot);

    // 1. Runtime executable.
    path runtimeSrc = sdkBin / runtimeName;
    path runtimeDst = outDir / runtimeName;
    if (!copyFileIf(runtimeSrc, runtimeDst)) {
        cerr << rang::fg::red << "SDK missing " << runtimeSrc.string() << rang::fg::reset << endl;
        return 3;
    }

    // 2. Windows runtime DLLs the packaged game needs (vulkan loader + VC CRT).
    // win32 SDKs bundle them in bin/ so ANY host can produce a self-contained
    // package; on a Windows host we also fall back to the local VS redist for
    // older SDKs that predate bundling.
    if (runtimeName == "eve.exe") {
        std::vector<path> crtCandidates;
        if (exists(sdkBin)) crtCandidates.push_back(sdkBin);
        if (exists(sdkLib)) crtCandidates.push_back(sdkLib);
#ifdef EVENGINE_WINDOWS
        // VC redist (version-independent glob): both release CRT and debug CRT.
        for (const auto& msroot : {"C:/Program Files/Microsoft Visual Studio"}) {
            path base = path(msroot);
            if (exists(base / "18")) base = base / "18";
            for (const auto& entry : directory_iterator(base, ec)) {
                if (!entry.is_directory()) continue;
                path redist = entry.path() / "Community" / "VC" / "Redist" / "MSVC";
                if (!exists(redist)) continue;
                for (const auto& ver : directory_iterator(redist, ec)) {
                    for (const auto& crtDir : {"x64", "debug_nonredist/x64"}) {
                        path crt = ver.path() / crtDir;
                        if (!exists(crt)) continue;
                        for (const auto& sub : directory_iterator(crt, ec)) {
                            if (!sub.is_directory()) continue;
                            const std::string n = sub.path().filename().string();
                            if (n.find("Microsoft.VC") == 0 &&
                                (n.find(".CRT") != string::npos ||
                                 n.find("DebugCRT") != string::npos))
                                crtCandidates.push_back(sub.path());
                        }
                    }
                }
            }
        }
        if (exists(path("C:/Windows/System32")))
            crtCandidates.push_back(path("C:/Windows/System32"));
#endif

        // eve.exe links the VC runtime dynamically; bundle both release and
        // debug sets so the package is self-contained regardless of build type.
        for (const auto& dll : {"vulkan-1.dll", "msvcp140.dll", "msvcp140_1.dll",
                                "msvcp140_2.dll", "vcruntime140.dll",
                                "vcruntime140_1.dll", "concrt140.dll",
                                "msvcp140d.dll", "vcruntime140d.dll",
                                "vcruntime140_1d.dll", "ucrtbased.dll",
                                "concrt140d.dll"}) {
            if (!copyFirst(dll, crtCandidates, outDir))
                cerr << rang::fg::yellow
                     << "note: runtime DLL not found (may be system-installed): "
                     << dll << rang::fg::reset << endl;
        }
    }

#ifdef EVENGINE_MACOSX
    // 2b. macOS: bundle the runtime dylibs (Vulkan loader, zlib, ...) that the
    // executable links from the SDK's lib/ directory, so the packaged game is
    // self-contained next to the eve binary.
    if (exists(sdkLib)) {
        for (const auto& entry : directory_iterator(sdkLib, ec)) {
            if (entry.is_regular_file() && entry.path().extension() == ".dylib")
                copyFileIf(entry.path(), outDir / entry.path().filename());
        }
        ec.clear();
    }
#endif  // EVENGINE_MACOSX

    // 3. Platform packaging template (win32/linux README etc).
    if (exists(sdkPlat)) {
        for (const auto& entry : directory_iterator(sdkPlat, ec)) {
            copy(entry.path(), outDir / entry.path().filename(),
                 copy_options::recursive | copy_options::overwrite_existing, ec);
            ec.clear();
        }
    }

    // 4. Compress the game directory into game.eve inside the package.
    path archive = outDir / "game.eve";
    if (!cmdline::createGameArchive(gamePath, archive)) {
        cerr << rang::fg::red << "Failed to package game archive." << rang::fg::reset << endl;
        return 4;
    }

    cout << rang::fg::green << "Built game package -> " << rang::fg::reset << outDir.string() << endl;
    cout << "  run: " << runtimeDst.string() << endl;
    return 0;
}

}  // namespace eve::cmd

