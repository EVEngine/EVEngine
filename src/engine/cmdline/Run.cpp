#include "cmdline.h"
#include "scripts.h"
#include "common/Module.h"
#include "common/Runtime.h"
#include "common/config.h"
#include "filesystem/Filesystem.h"
#include "filesystem/physfs/FileApi.h"
#if !defined(EVENGINE_ANDROID) && !defined(EVENGINE_IOS) && !defined(EVENGINE_WEBGPU)
#include "devtools/DevTool.hpp"
#include "devtools/McpServer.hpp"
#endif

#include <simplesquirrel/simplesquirrel.hpp>
#include <CLI11.hpp>
#include <cstdint>
#include <string>
#include <vector>
#include <filesystem>

#if defined(EVENGINE_WEBGPU)
#include <emscripten.h>

namespace {
// Global frame-loop state: the root script (load_web.nut) defines a global
// eve_frame() function; emscripten_set_main_loop drives it per animation frame.
ssq::VM* gFrameVm = nullptr;
ssq::Function* gFrameFunc = nullptr;

void webgpuFrameTick() {
    if (!gFrameVm || !gFrameFunc || gFrameFunc->isEmpty()) return;
    // First frame is presented by the browser, so the shell's loading overlay
    // can be dismissed now (it covered the canvas until this point).
    EM_ASM({ if (window.hideEVELoading) window.hideEVELoading(); });
    bool keep = true;
    try {
        ssq::Object r = gFrameVm->callFunc(*gFrameFunc, *gFrameVm);
        if (r.getType() == ssq::Type::BOOL) keep = r.toBool();
    } catch (const std::exception& e) {
        fprintf(stderr, "EVEngine: eve_frame error: %s\n", e.what());
        keep = false;
    }
    if (!keep) emscripten_cancel_main_loop();
}
} // namespace
#endif

#if defined(EVENGINE_ANDROID)
#include <android/log.h>
#define EVE_ANDROID_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "EVEngine", __VA_ARGS__)
#elif defined(EVENGINE_IOS) || defined(EVENGINE_WEBGPU)
#include <cstdio>
#define EVE_ANDROID_LOGE(...) do { fprintf(stderr, "EVEngine: "); fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); } while (0)
#else
#define EVE_ANDROID_LOGE(...) ((void)0)
#endif

using namespace std;

namespace eve::cmd
{

struct RunArgs : Handler {
    std::string log_path, root_path;
    bool no_window = false, debug = false;
    int dap_port = 0;
    int mcp_port = 0;

    void setup(CLI::App& app, std::shared_ptr<CLI::Formatter> formatter) override {
        auto run = app.add_subcommand("run", "Run game under current path");
        run->allow_extras()->formatter(formatter);
        run->add_flag("--no-window", no_window, "Run script only, no window mode");
        run->add_flag("--debug", debug, "debug mode (slicer + pause/breakpoints/snapshot/MCP)");
        run->add_option("--dap-port", dap_port,
                        "Start Debug Adapter Protocol server on port (implies --debug)");
        run->add_option("--mcp-port", mcp_port,
                        "Start Model Context Protocol server on port for AI agents (implies --debug)");
        run->add_option("-l,--log", log_path, "log messages into a file");
        run->add_option("-r,--root", root_path, "give a entry script instead of using the system default one");
    }

    int parse(CLI::App& app, Cmdline& cmd) override {
        auto run = app.get_subcommand("run");
        if (run->parsed() || cmd.getArgc() == 1) {
            if (dap_port > 0 || mcp_port > 0) debug = true;
            std::string current_path = cmd.get_remaining(run);
            if (root_path != "") {
                ifstream ifs(root_path);
                if (!ifs) {
                    cerr << "Cannot open root script: " << root_path << endl;
                    return -1;
                }
                std::string load_root((istreambuf_iterator<char>(ifs)), (istreambuf_iterator<char>()));
                return cmd.Run(current_path, load_root, debug, dap_port, mcp_port);
            } else {
#ifdef EVENGINE_WEBGPU
                // The Emscripten runtime only wires the trimmed module set.
                return cmd.Run(current_path, load_web_content, debug, dap_port, mcp_port);
#else
                return cmd.Run(current_path, load_content, debug, dap_port, mcp_port);
#endif
            }
        }
        return -1; // not handle
    }
};

CMD_REG(RunArgs);


// create a new project
int Cmdline::Run(std::string path, std::string root, bool debug, int dapPort, int mcpPort) {
    try {
        // Resolve the game directory. A packaged game ships a game.eve archive next to
        // the executable; we mount it into memory and run without extracting to disk.
        std::string gameDir = path;
        std::string archivePath;

        {
            std::error_code ec;
            if (path.empty() || path == ".")
                gameDir = std::filesystem::current_path(ec).string();
            else
                gameDir = std::filesystem::absolute(path, ec).string();
            if (ec) gameDir = path;

            std::filesystem::path gp(gameDir);
            if (std::filesystem::is_regular_file(gp, ec) && gp.extension() == ".eve") {
                // `eve run <path>.eve` — the archive itself is the game.
                archivePath = gp.string();
                gameDir = gp.parent_path().string();
            } else {
                std::filesystem::path bundled = gp / "game.eve";
                if (std::filesystem::is_regular_file(bundled, ec))
                    archivePath = bundled.string();
            }
        }

        // Switch to the game directory so relative plugin/asset paths resolve next to
        // the executable (the packaged game's scripts live in the memory-mounted archive).
        if (!gameDir.empty() && gameDir != ".") {
            std::error_code ec;
            std::filesystem::current_path(gameDir, ec);
            if (ec) {
                cerr << "Cannot chdir to game path '" << gameDir << "': " << ec.message() << endl;
                EVE_ANDROID_LOGE("Cannot chdir to game path '%s': %s", gameDir.c_str(), ec.message().c_str());
                return 2;
            }
        }

        // Mount game source for PhysFS so relative watch/read resolve (hot reload).
        {
            auto *fs = eve::filesystem::Filesystem::create();
            if (fs) {
                if (!archivePath.empty()) {
                    // Read the packaged archive into memory and mount it there.
                    std::ifstream ifs(archivePath, std::ios::binary);
                    if (!ifs) {
                        cerr << "Cannot open game archive: " << archivePath << endl;
                        return 2;
                    }
                    std::vector<char> bytes((std::istreambuf_iterator<char>(ifs)),
                                            std::istreambuf_iterator<char>());
                    if (bytes.empty() || !fs->setSourceFromMemory(bytes.data(), bytes.size())) {
                        cerr << "Failed to mount game archive from memory: " << archivePath << endl;
                        return 2;
                    }
                } else {
                    std::error_code ec;
                    auto cwd = std::filesystem::current_path(ec);
                    if (!ec) {
                        // setSource only succeeds once; ignore failure if already mounted.
                        fs->setSource(cwd.string());
                    }
                }
            }
        }

        Runtime runtime(2048, ssq::Libs::ALL);
        runtime.initialize();
#if !defined(EVENGINE_ANDROID) && !defined(EVENGINE_IOS) && !defined(EVENGINE_WEBGPU)
        if (debug) {
            auto& dt = eve::dev::DevTool::instance();
            dt.attach(runtime.vm(), /*sampleLocals=*/true);
            dt.exposeScriptApi(runtime.vm());
            if (dapPort > 0) {
                const int bound = dt.startDap(static_cast<uint16_t>(dapPort));
                if (bound > 0) {
                    cerr << "DAP listening on 127.0.0.1:" << bound << endl;
                    // Wait for the IDE to finish setBreakpoints / configurationDone
                    // before loading scripts, otherwise early breakpoints are missed
                    // and stack paths are not registered yet.
                    if (!dt.dap().waitUntilConfigured(15000))
                        cerr << "DAP: timed out waiting for client; starting anyway" << endl;
                } else {
                    cerr << "Failed to start DAP server on port " << dapPort << endl;
                }
            }
            if (mcpPort > 0) {
                try {
                    dt.mcp().setGameRoot(std::filesystem::current_path().string());
                } catch (...) {
                }
                const int bound = dt.startMcp(static_cast<uint16_t>(mcpPort));
                if (bound > 0) {
                    cerr << "MCP listening on 127.0.0.1:" << bound
                         << " (newline JSON-RPC; use tools/eve-mcp for Cursor stdio)" << endl;
                } else {
                    cerr << "Failed to start MCP server on port " << mcpPort << endl;
                }
            }
        }
#else
        (void)debug;
        (void)dapPort;
        (void)mcpPort;
#endif
        // Embedded default demo (src/scripts/demo.nut); load.nut runs it when no main.nut.
        {
            ssq::Table eve = runtime.table("eve");
            eve.set("demoScript", std::string(demo_content ? demo_content : ""));
            eve.set("asyncScript", std::string(async_content ? async_content : ""));
            // Scene-director authoring kit (src/scripts/scene_director.nut). Host
            // games load it via `compilestring(eve.sceneDirectorScript)()`; the
            // MCP tools auto-install it on demand.
            eve.set("sceneDirectorScript", std::string(scene_director_content ? scene_director_content : ""));
        }
        // Name the embedded root so DAP stack frames map to load.nut (not "buffer").
        // Route file/dofile/loadfile through PhysFS so a packaged game (mounted in
        // memory) can load its scripts without extracting to disk.
        eve::filesystem::physfs::installScriptFileApi(runtime.vm());
        runtime.runSource(root, "load.nut");
#if defined(EVENGINE_WEBGPU)
        // Instead of a blocking while(running) Squirrel loop (which the browser
        // never composites), drive the global eve_frame() function from an
        // Emscripten requestAnimationFrame main loop. simulateInfiniteLoop=1
        // keeps main() alive; `runtime` stays in scope because Run() never returns.
        gFrameVm = &runtime.vm();
        gFrameFunc = new ssq::Function(runtime.vm().find("eve_frame").toFunction());
        emscripten_set_main_loop(&webgpuFrameTick, 0, /*simulateInfiniteLoop=*/1);
#endif
#if !defined(EVENGINE_ANDROID) && !defined(EVENGINE_IOS) && !defined(EVENGINE_WEBGPU)
        if (debug) eve::dev::DevTool::instance().detach();
#endif
        return 0;
    } catch (const std::exception& e) {
#if !defined(EVENGINE_ANDROID) && !defined(EVENGINE_IOS) && !defined(EVENGINE_WEBGPU)
        if (debug) {
            const std::string report =
                eve::dev::DevTool::instance().notifyError(e.what());
            cerr << report << endl;
            eve::dev::DevTool::instance().detach();
        } else {
            cerr << "Run failed: " << e.what() << endl;
        }
#else
        cerr << "Run failed: " << e.what() << endl;
#endif
        EVE_ANDROID_LOGE("Run failed: %s", e.what());
        return 3;
    } catch (...) {
#if !defined(EVENGINE_ANDROID) && !defined(EVENGINE_IOS) && !defined(EVENGINE_WEBGPU)
        if (debug) {
            const std::string report =
                eve::dev::DevTool::instance().notifyError("unknown exception");
            cerr << report << endl;
            eve::dev::DevTool::instance().detach();
        } else {
            cerr << "Run failed: unknown exception" << endl;
        }
#else
        cerr << "Run failed: unknown exception" << endl;
#endif
        EVE_ANDROID_LOGE("Run failed: unknown exception");
        return 3;
    }
}



} // namespace eve
