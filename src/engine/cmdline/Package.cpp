#include <CLI11.hpp>
#include <iostream>
#include "cmdline.h"

#if !defined(__EMSCRIPTEN__)
#include <Poco/PipeStream.h>
#include <Poco/Process.h>
#include <Poco/UnicodeConverter.h>
#include <algorithm>
#include <chrono>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <map>
#include <rang.hpp>
#include <set>
#include <sstream>
#include <thread>
#include "Sha256.h"
#include "common/Runtime.h"
#include "common/ScriptCompiler.h"
#include "common/ScriptModule.h"
#include "common/config.h"
#include "zip_writer.h"

#if defined(EVENGINE_WINDOWS) || defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif

using namespace std::filesystem;
#endif

using namespace std;

namespace eve::cmd
{

#if !defined(__EMSCRIPTEN__)
namespace {

// Runtime binary name inside the *target* SDK's bin/. The packaged game runs on
// the SDK's platform, which is not necessarily the host running `eve package`
// (e.g. a Linux host can produce a Windows game folder for the win32 SDK).
std::string targetRuntimeName(const std::string& sdkRoot) {
    std::ifstream in(std::filesystem::path(sdkRoot) / "share" / "eve" / "TARGET_PLATFORM");
    std::string   plat;
    std::getline(in, plat);
    while (!plat.empty() && (plat.back() == '\r' || plat.back() == '\n' || plat.back() == ' '))
        plat.pop_back();
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

struct ScriptPackageManifest {
    std::map<std::string, std::vector<std::string>> graph;
    std::string                                     error;

    std::string serialize() const {
        std::ostringstream out;
        out << "version=1\n";
        for (const auto& [module, dependencies] : graph) {
            out << module;
            for (const std::string& dependency : dependencies) out << '\t' << dependency;
            out << '\n';
        }
        return out.str();
    }
};

bool canonicalGameModule(std::string_view importer, std::string_view specifier, std::string& result,
                         std::string& error) {
    script::ScriptModuleRequest request{std::string(importer), std::string(specifier)};
    if (!script::ScriptModuleResolver::canonicalize(request, result, error)) return false;
    if (result.rfind("game:/", 0) != 0) {
        error = "package cannot statically resolve non-game script module: " + result;
        return false;
    }
    return true;
}

// Compilation imports must use the same snapshot as the archive, never the
// process's current directory or an already-mounted game's filesystem service.
class PackageScriptProvider final : public script::IScriptModuleProvider {
public:
    explicit PackageScriptProvider(path root) : root_(std::move(root)) {}
    script::ScriptModuleStatus resolve(const script::ScriptModuleRequest& request, std::string& uri,
                                       std::string& error) override {
        return canonicalGameModule(request.importerUri, request.specifier, uri, error)
                   ? script::ScriptModuleStatus::Found
                   : script::ScriptModuleStatus::Error;
    }
    script::ScriptModuleStatus load(std::string_view uri, script::ScriptModuleSource& source,
                                    std::string& error) override {
        if (uri.rfind("game:/", 0) != 0) {
            error = "non-game package module";
            return script::ScriptModuleStatus::Error;
        }
        const path file = root_ / std::string(uri.substr(6));
        ifstream   input(file, ios::binary);
        if (!input) {
            error = "Cannot read package module: " + file.string();
            return script::ScriptModuleStatus::Error;
        }
        source.canonicalUri = std::string(uri);
        source.utf8Source.assign(std::istreambuf_iterator<char>(input), {});
        source.debugOrigin = file.string();
        sdk::detail::Sha256 hash;
        hash.update(source.utf8Source.data(), source.utf8Source.size());
        source.contentHash = hash.finish();
        return script::ScriptModuleStatus::Found;
    }

private:
    path root_;
};

ScriptPackageManifest scanScriptPackage(const path& gameDir) {
    ScriptPackageManifest manifest;
    std::error_code       ec;
    for (recursive_directory_iterator it(gameDir, directory_options::none, ec), end; it != end; it.increment(ec)) {
        if (ec) {
            manifest.error = "cannot enumerate scripts: " + ec.message();
            return manifest;
        }
        if (!it->is_regular_file() || it->path().extension() != ".nut") continue;
        std::ifstream input(it->path(), std::ios::binary);
        if (!input) {
            manifest.error = "cannot read script during package scan: " + it->path().string();
            return manifest;
        }
        const std::string source(std::istreambuf_iterator<char>(input), {});
        const std::string relative = std::filesystem::relative(it->path(), gameDir, ec).generic_string();
        if (ec) {
            manifest.error = "cannot canonicalize packaged script: " + it->path().string();
            return manifest;
        }
        const std::string canonical = "game:/" + relative;
        auto&             edges     = manifest.graph[canonical];
        for (const std::string& specifier : script::ScriptCompiler::analyze(source, canonical).imports) {
            std::string dependency;
            if (!canonicalGameModule(canonical, specifier, dependency, manifest.error)) return manifest;
            edges.push_back(std::move(dependency));
        }
        std::sort(edges.begin(), edges.end());
        edges.erase(std::unique(edges.begin(), edges.end()), edges.end());
    }

    if (ec) {
        manifest.error = "cannot enumerate scripts: " + ec.message();
        return manifest;
    }

    for (const auto& [module, dependencies] : manifest.graph) {
        for (const std::string& dependency : dependencies) {
            if (manifest.graph.find(dependency) == manifest.graph.end()) {
                manifest.error = "script module imported by " + module + " is missing from package: " + dependency;
                return manifest;
            }
        }
    }

    enum class Visit { Visiting, Ready };
    std::map<std::string, Visit> visits;
    std::vector<std::string>     stack;
    const auto visit = [&](const auto& self, const std::string& module) -> bool {
        if (const auto found = visits.find(module); found != visits.end()) {
            if (found->second == Visit::Ready) return true;
            manifest.error = "cyclic script import in package";
            for (const std::string& entry : stack) manifest.error += " -> " + entry;
            manifest.error += " -> " + module;
            return false;
        }
        visits[module] = Visit::Visiting;
        stack.push_back(module);
        for (const std::string& dependency : manifest.graph[module]) {
            if (!self(self, dependency)) return false;
        }
        stack.pop_back();
        visits[module] = Visit::Ready;
        return true;
    };
    for (const auto& [module, _] : manifest.graph) {
        if (!visit(visit, module)) return manifest;
    }
    Runtime compiler(512, ssq::Libs::ALL);
    if (compiler.scriptModules().registerProvider(std::make_shared<PackageScriptProvider>(gameDir), 100) == 0)
        throw runtime_error("Cannot register package snapshot provider");
    for (const auto& [module, _] : manifest.graph) {
        ifstream input(gameDir / module.substr(6), ios::binary);
        if (!input) throw runtime_error("Cannot read package script: " + module);
        const string source(std::istreambuf_iterator<char>(input), {});
        if (input.bad()) throw runtime_error("Cannot read complete package script: " + module);
        try {
            compiler.compileSource(source, module);
        } catch (const std::exception& error) {
            manifest.error = module + ": " + error.what();
            return manifest;
        }
    }
    return manifest;
}

// Locate the target SDK root: --sdk, EVENGINE_SDK, or next to the running binary.
std::string resolveSdkRoot(const std::string& sdkArg) {
    if (!sdkArg.empty() && is_directory(sdkArg)) return sdkArg;

    const char* env = getenv("EVENGINE_SDK");
    if (env && is_directory(env)) return env;

    // Running from <sdk>/bin/<runtime> -> root is two directories up.
    path exe;
#if defined(EVENGINE_WINDOWS) || defined(_WIN32)
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
#endif

struct PackageArgs : Handler {
    string sdkPath, outputPath;
    void   setup(CLI::App& app, std::shared_ptr<CLI::Formatter>) override {
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

#if defined(__EMSCRIPTEN__)
int Cmdline::Package(std::string, std::string, std::string) {
    cerr << "eve package requires a native host that can run the target SDK runtime; "
            "desktop packaging is unavailable in the browser.\n";
    return 8;
}
#else
int Cmdline::Package(std::string gamePath, std::string output, std::string sdk) try {
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

    gamePath = canonical(gamePath).string();
    if (!is_regular_file(path(gamePath) / "main.nut")) {
        cerr << "Package requires main.nut\n";
        return 5;
    }
    // Game folder name, trailing separators stripped.
    std::string gameName = gamePath;
    while (!gameName.empty() && (gameName.back() == '/' || gameName.back() == '\\'))
        gameName.pop_back();
    if (gameName.find_first_of("/\\") != string::npos)
        gameName = std::filesystem::path(gameName).filename().string();
    if (gameName.empty()) gameName = "game";

    const path destination    = weakly_canonical(output.empty() ? path(gameName + "-package") : path(output));
    const auto relativeOutput = destination.lexically_relative(gamePath);
    const auto relativeGame   = path(gamePath).lexically_relative(destination);
    if (destination == path(gamePath) || (!relativeOutput.empty() && *relativeOutput.begin() != "..") ||
        (!relativeGame.empty() && *relativeGame.begin() != "..")) {
        cerr << "Package output and source must not contain one another\n";
        return 6;
    }
    create_directories(destination.parent_path());
    const path lock = destination.string() + ".package-lock";
    if (!create_directory(lock)) {
        cerr << "Package destination is locked: " << lock << '\n';
        return 6;
    }
    struct Cleanup {
        path directory;
        ~Cleanup() {
            error_code ignored;
            remove_all(directory, ignored);
        }
    } cleanup{lock};
    path outDir = lock / "candidate";
    create_directory(outDir);
    const path snapshot = lock / "source";
    copy(gamePath, snapshot, copy_options::recursive);
    const ScriptPackageManifest scripts = scanScriptPackage(snapshot);
    if (!scripts.error.empty()) {
        cerr << rang::fg::red << "Script dependency scan failed: " << scripts.error << rang::fg::reset << endl;
        return 5;
    }


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
#if defined(EVENGINE_WINDOWS) || defined(_WIN32)
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
    // 2b. macOS: copy the SDK's lib/ tree (Vulkan loader + MoltenVK + ICD
    // manifest + zlib/PNG dylibs) into <out>/lib. eve carries INSTALL_RPATH
    // "@loader_path/../lib", and platform/macosx bootstrapBundledVulkan points
    // SDL at <out>/lib/libvulkan.1.dylib + <out>/lib/MoltenVK_icd.json, so the
    // packaged game is self-contained next to the eve binary. Copying the tree
    // (not just *.dylib) keeps the ICD manifest and any subdirectories.
    if (exists(sdkLib)) {
        std::ifstream platIn(std::filesystem::path(sdkRoot) / "share" / "eve" / "TARGET_PLATFORM");
        std::string   plat;
        std::getline(platIn, plat);
        while (!plat.empty() && (plat.back() == '\r' || plat.back() == '\n' || plat.back() == ' '))
            plat.pop_back();
        // Only macOS-target packages get the dylib tree; cross-packaged
        // win32/linux games must stay clean.
        if (plat == "macosx")
            copy(sdkLib, outDir / "lib",
                 copy_options::recursive | copy_options::overwrite_existing, ec);
        if (ec) throw filesystem_error("copy SDK libraries", sdkLib, ec);
    }
#endif  // EVENGINE_MACOSX

    // 3. Platform packaging template (win32/linux README etc).
    if (exists(sdkPlat)) {
        for (const auto& entry : directory_iterator(sdkPlat, ec)) {
            copy(entry.path(), outDir / entry.path().filename(),
                 copy_options::recursive | copy_options::overwrite_existing, ec);
            if (ec) throw filesystem_error("copy platform template", entry.path(), ec);
        }
    }

    // 4. Compress the game directory into game.eve inside the package.
    path archive = outDir / "game.eve";
    if (!cmdline::createGameArchive(snapshot, archive, {{".eve/script-modules.manifest", scripts.serialize()}})) {
        cerr << rang::fg::red << "Failed to package game archive." << rang::fg::reset << endl;
        return 4;
    }

    const auto hashFile = [](const path& file) {
        ifstream input(file, ios::binary);
        if (!input) throw runtime_error("Cannot hash " + file.string());
        sdk::detail::Sha256 hash;
        char                buffer[65536];
        while (input.read(buffer, sizeof(buffer)) || input.gcount())
            hash.update(buffer, static_cast<size_t>(input.gcount()));
        if (!input.eof()) throw runtime_error("Cannot read " + file.string());
        return hash.finish();
    };
    const string archiveHash = hashFile(archive);
    const string runtimeHash = hashFile(runtimeDst);
    // Run the exact archive and runtime that will be published, with a bounded
    // acceptance mode. Cross-target runtimes must be executable on this host.
    Poco::Process::Env childEnvironment;
#if defined(EVENGINE_WINDOWS) || defined(_WIN32)
    // Unlike POSIX Poco, CreateProcess replaces the entire environment when a
    // nonempty map is supplied. Preserve PATH, system directories and drivers.
    struct EnvironmentBlock {
        LPWCH value = GetEnvironmentStringsW();
        ~EnvironmentBlock() {
            if (value) FreeEnvironmentStringsW(value);
        }
    } environment;
    if (!environment.value) throw runtime_error("Cannot read child process environment");
    for (const wchar_t* entry = environment.value; *entry; entry += std::wcslen(entry) + 1) {
        string utf8;
        Poco::UnicodeConverter::toUTF8(std::wstring(entry), utf8);
        const auto separator = utf8.find('=', 1);
        if (separator != string::npos) childEnvironment.emplace(utf8.substr(0, separator), utf8.substr(separator + 1));
    }
#endif
    childEnvironment["EVE_PACKAGE_CHECK"] = "1";
    Poco::Pipe pipe;
    auto process = Poco::Process::launch(absolute(runtimeDst).string(), {"run"}, absolute(outDir).string(), nullptr,
                                         &pipe, &pipe, childEnvironment);
    std::string log;
    struct ChildCleanup {
        Poco::ProcessHandle child;
        std::thread         reader;
        ~ChildCleanup() {
            try {
                if (child.tryWait() == -1) {
                    Poco::Process::kill(child.id());
                    child.wait();
                }
            } catch (...) {
            }
            if (reader.joinable()) reader.join();
        }
    } child{process, {}};
    child.reader        = std::thread([&] {
        Poco::PipeInputStream input(pipe);
        char                  buffer[4096];
        while (input.read(buffer, sizeof(buffer)) || input.gcount()) {
            // Keep draining after the cap so a noisy candidate cannot block.
            if (log.size() < 8 * 1024 * 1024) log.append(buffer, static_cast<size_t>(input.gcount()));
        }
    });
    int        exit = -1;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    while ((exit = process.tryWait()) == -1 && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    if (exit == -1) {
        Poco::Process::kill(process.id());
        exit = process.wait();
    }
#if defined(EVENGINE_WINDOWS) || defined(_WIN32)
    // On Windows tryWait reads the exit code before the process object is
    // signaled. Wait for teardown to release the candidate's directory handles.
    exit = process.wait();
#endif
    child.reader.join();
    if (exit != 0 || log.find("[package-check] passed: 30 frames") == std::string::npos) {
        cerr << "Package runtime acceptance failed (exit " << exit << ")\n" << log;
        return 7;
    }
    if (hashFile(archive) != archiveHash || hashFile(runtimeDst) != runtimeHash)
        throw runtime_error("Candidate changed during runtime acceptance");
    sdk::detail::Sha256 buildIdentity;
    const string        identityInput = "eve.package-build/1\n" + archiveHash + "\n" + runtimeHash;
    buildIdentity.update(identityInput.data(), identityInput.size());
    ofstream record(outDir / "build-identity.manifest", ios::binary);
    record << "schema=eve.package-build\nversion=1\n"
           << "build_id=" << buildIdentity.finish() << "\n"
           << "archive_sha256=" << archiveHash << "\n"
           << "runtime_sha256=" << runtimeHash << "\n"
           << "built_at_unix_ms="
           << chrono::duration_cast<chrono::milliseconds>(chrono::system_clock::now().time_since_epoch()).count()
           << "\n"
           << "compiled_scripts=" << scripts.graph.size() << "\n"
           << "acceptance=passed\nframes=30\n";
    record.close();
    if (!record) throw runtime_error("Cannot write build identity");
    ofstream acceptance(outDir / "acceptance.log", ios::binary);
    acceptance << log;
    acceptance.close();
    if (!acceptance) throw runtime_error("Cannot write acceptance log");

    const path previous    = lock / "previous";
    const bool hadPrevious = exists(destination);
    if (hadPrevious) rename(destination, previous);
    rename(outDir, destination, ec);
    if (ec) {
        if (hadPrevious) {
            error_code restoreError;
            rename(previous, destination, restoreError);
            if (restoreError) {
                // Retain recovery evidence if rollback itself fails.
                cleanup.directory.clear();
                cerr << "Previous package retained at " << previous << '\n';
            }
        }
        throw filesystem_error("publish package", destination, ec);
    }
    cout << "Built verified game package -> " << destination.string() << '\n';
    cout << "  run: " << (destination / runtimeName).string() << '\n';
    return 0;
} catch (const std::exception& error) {
    cerr << "Package failed: " << error.what() << '\n';
    return 8;
}

#endif

}  // namespace eve::cmd
