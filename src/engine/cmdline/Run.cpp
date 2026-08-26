#include "cmdline.h"
#include "scripts.h"
#include "common/Module.h"
#include "common/Runtime.h"
#include "common/ScriptCompiler.h"
#include "common/config.h"
#include "common/ECS.h"
#include "common/CrashLog.h"
#include "filesystem/Filesystem.h"
#include "filesystem/physfs/FileApi.h"
#include "graphics/Light.h"
#include "graphics/RenderSystem3D.h"
#if !defined(EVENGINE_ANDROID) && !defined(EVENGINE_IOS) && !defined(__EMSCRIPTEN__)
#include "devtools/DevTool.hpp"
#include "devtools/McpServer.hpp"
#endif

#include <simplesquirrel/simplesquirrel.hpp>
#include <CLI11.hpp>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <ctime>
#include <string>
#include <vector>
#include <filesystem>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#include <squirrel.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <sstream>

#include "graphics/Graphics.h"

namespace {
// Global frame-loop state: the root script (load.nut) defines a global
// eve_frame() function; emscripten_set_main_loop drives it per animation frame.
ssq::VM* gFrameVm = nullptr;
ssq::Function* gFrameFunc = nullptr;
// Raw Squirrel handle for the browser playground bridges below. Set right
// after load.nut finishes; Run() never returns (emscripten_set_main_loop with
// simulateInfiniteLoop=1), so it stays valid for the page lifetime.
HSQUIRRELVM gPlaygroundVm = nullptr;

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

// ---------------------------------------------------------------------------
// Playground bridge (browser only).
//
// The shell page (platform/webgpu/shell.html) calls these exported functions
// to push a new main.nut into the running engine and trigger the same soft
// hot-reload path used by the desktop file watcher (load.nut handle_change).
// They run on the browser main thread only, so a single shared result buffer
// is safe.
// ---------------------------------------------------------------------------

constexpr SQInteger kPlaygroundResultCap = 8192;
char gPlaygroundResult[kPlaygroundResultCap];

const char* setPlaygroundResult(const char* text) {
    if (!text) text = "";
    std::snprintf(gPlaygroundResult, kPlaygroundResultCap, "%s", text);
    return gPlaygroundResult;
}

// Write /game/main.nut and soft-reload it (dofile + optional eve_reload hook).
// The reload error path prints through the same channels as the desktop
// watcher, so the page console shows compile/runtime errors.
void playgroundApply(const char* source) {
    if (!gPlaygroundVm || !source) return;
    HSQUIRRELVM vm = gPlaygroundVm;
    const SQInteger top = sq_gettop(vm);
    try {
        std::ofstream out("/game/main.nut", std::ios::binary | std::ios::trunc);
        if (!out) {
            std::fprintf(stderr, "playground: cannot open /game/main.nut for writing\n");
            return;
        }
        out.write(source, static_cast<std::streamsize>(std::strlen(source)));
        out.close();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "playground: write /game/main.nut failed: %s\n", e.what());
        return;
    }
    // Route through load.nut's handle_change("main.nut") so scripts are
    // tracked and re-dofiled exactly like an on-disk change.
    sq_pushroottable(vm);
    sq_pushstring(vm, _SC("handle_change"), -1);
    if (SQ_SUCCEEDED(sq_get(vm, -2))) {          // [root, fn]
        sq_remove(vm, -2);                       // [fn]
        sq_pushroottable(vm);                    // [fn, root] (env/this param)
        sq_pushstring(vm, _SC("main.nut"), -1);
        // UI.cpp callScriptHandler convention: the roottable is the first
        // parameter, so 2 params = [root, "main.nut"].
        sq_call(vm, 2, SQFalse, SQTrue);
    }
    sq_settop(vm, top);
}

// Evaluate one Squirrel snippet in the running VM (REPL). Returns the
// stringified result, or a "compile error:" / "eval error:" message.
const char* playgroundEval(const char* source) {
    if (!gPlaygroundVm) return "engine not ready";
    if (!source) return "";
    HSQUIRRELVM vm = gPlaygroundVm;
    const SQInteger top = sq_gettop(vm);
    char errbuf[640];
    // REPL semantics: expressions like `__pg.frames` should return their value.
    // A bare expression statement yields null in Squirrel, so first try
    // `return (...)`; if that does not compile (e.g. an assignment or a
    // multi-statement block), fall back to executing the raw source.
    const std::string wrapped = std::string("return (") + source + ");";
    const char* evalSource = wrapped.c_str();
    SQInteger evalLen = static_cast<SQInteger>(wrapped.size());
    bool compileOk = SQ_SUCCEEDED(eve::script::ScriptCompiler::compileBuffer(
        vm, evalSource, evalLen, _SC("playground_eval"), SQTrue));
    if (!compileOk) {
        sq_settop(vm, top);
        compileOk = SQ_SUCCEEDED(eve::script::ScriptCompiler::compileBuffer(
            vm, source, static_cast<SQInteger>(std::strlen(source)), _SC("playground_eval"), SQTrue));
    }
    if (!compileOk) {
        const SQChar* msg = nullptr;
        if (sq_gettype(vm, -1) == OT_STRING) sq_getstring(vm, -1, &msg);
        std::snprintf(errbuf, sizeof(errbuf), "compile error: %s", msg ? msg : "unknown");
        sq_settop(vm, top);
        return setPlaygroundResult(errbuf);
    }
    sq_pushroottable(vm);
    if (SQ_FAILED(sq_call(vm, 1, SQTrue, SQTrue))) {
        const SQChar* msg = nullptr;
        if (sq_gettype(vm, -1) == OT_STRING) sq_getstring(vm, -1, &msg);
        std::snprintf(errbuf, sizeof(errbuf), "eval error: %s", msg ? msg : "unknown");
        sq_settop(vm, top);
        return setPlaygroundResult(errbuf);
    }
    if (sq_gettype(vm, -1) == OT_NULL) {
        sq_settop(vm, top);
        return setPlaygroundResult("null");
    }
    if (SQ_FAILED(sq_tostring(vm, -1))) {
        sq_settop(vm, top);
        return setPlaygroundResult("(unprintable result)");
    }
    const SQChar* result = nullptr;
    if (SQ_FAILED(sq_getstring(vm, -1, &result)) || !result) {
        sq_settop(vm, top);
        return setPlaygroundResult("(unprintable result)");
    }
    const char* out = setPlaygroundResult(result);
    sq_settop(vm, top);
    return out;
}

// Read a file from the browser VFS (relative to /game, which is the CWD).
const char* playgroundRead(const char* path) {
    if (!gPlaygroundVm || !path) return "";
    try {
        std::ifstream in(path, std::ios::binary);
        if (!in) return setPlaygroundResult("");
        std::ostringstream ss;
        ss << in.rdbuf();
        const std::string text = ss.str();
        const size_t n = std::min(text.size(), static_cast<size_t>(kPlaygroundResultCap - 1));
        std::memcpy(gPlaygroundResult, text.data(), n);
        gPlaygroundResult[n] = '\0';
        return gPlaygroundResult;
    } catch (...) {
        return setPlaygroundResult("");
    }
}

// Binary-safe VFS read (e.g. PNG screenshots): copies up to dstCap bytes into
// the JS-provided buffer and returns the byte count written.
int playgroundReadBytes(const char* path, void* dst, int dstCap) {
    if (!gPlaygroundVm || !path || !dst || dstCap <= 0) return 0;
    try {
        std::ifstream in(path, std::ios::binary);
        if (!in) return 0;
        std::ostringstream ss;
        ss << in.rdbuf();
        const std::string text = ss.str();
        const int n = std::min<int>(static_cast<int>(text.size()), dstCap);
        std::memcpy(dst, text.data(), n);
        return n;
    } catch (...) {
        return 0;
    }
}

// Queue an async frame readback (PNG). The webgpu backend pumps it from
// present() so we never sleep inside this JS-initiated call (ASYNCIFY).
void playgroundCapture(const char* path) {
    if (!gPlaygroundVm || !path) return;
    auto* gfx = eve::ModuleManager::getInstance<eve::graphics::Graphics>("Graphics");
    if (gfx) gfx->beginFrameReadback(path);
}

int playgroundReadbackStatus() {
    if (!gPlaygroundVm) return 0;
    auto* gfx = eve::ModuleManager::getInstance<eve::graphics::Graphics>("Graphics");
    return gfx ? gfx->frameReadbackStatus() : 0;
}

// Pause/freeze game updates (eve_update) while the render keeps presenting.
// load.nut's eve_frame checks the eve_playground_paused root flag.
void playgroundSetPaused(int paused) {
    if (!gPlaygroundVm) return;
    HSQUIRRELVM vm = gPlaygroundVm;
    const SQInteger top = sq_gettop(vm);
    sq_pushroottable(vm);
    sq_pushstring(vm, _SC("eve_playground_paused"), -1);
    sq_pushbool(vm, paused != 0);
    sq_newslot(vm, -3, SQFalse);
    sq_settop(vm, top);
}

// Destroy the ECS entities demos create (renderables, lights, cameras) and
// drop the playground state table, so applying a *different* demo (or pressing
// "重置状态") starts from a clean scene instead of stacking entities.
void playgroundResetScene() {
    if (!gPlaygroundVm) return;
    HSQUIRRELVM vm = gPlaygroundVm;
    const SQInteger top = sq_gettop(vm);
    try {
        auto *table = ecs::current();
        auto destroyAll = [&](auto *mgr) {
            if (!mgr || !mgr->registy) return;
            auto *reg = dynamic_cast<ecs::IRegistryComponentBuffer *>(mgr->registy);
            if (!reg) return;
            std::vector<ecs::Entity *> entities;
            const uint32_t n = reg->entity_count();
            entities.reserve(n);
            for (uint32_t i = 0; i < n; ++i) {
                if (ecs::Entity *e = reg->entity_at(i)) entities.push_back(e);
            }
            for (ecs::Entity *e : entities) ecs::DestroyEntity(e);
        };
        destroyAll(table->getManager<eve::graphics::Renderable3D>());
        destroyAll(table->getManager<eve::graphics::Light3D>());
        destroyAll(table->getManager<eve::graphics::Camera3D>());

        sq_pushroottable(vm);
        sq_pushstring(vm, _SC("__pg"), -1);
        sq_deleteslot(vm, -2, SQFalse);
        sq_settop(vm, top);
    } catch (...) {
        sq_settop(vm, top);
    }
}
} // namespace

extern "C" {

EMSCRIPTEN_KEEPALIVE void eve_playground_apply(const char* source) { playgroundApply(source); }
EMSCRIPTEN_KEEPALIVE const char* eve_playground_eval(const char* source) {
    return playgroundEval(source);
}
EMSCRIPTEN_KEEPALIVE const char* eve_playground_read(const char* path) {
    return playgroundRead(path);
}
EMSCRIPTEN_KEEPALIVE int eve_playground_read_bytes(const char* path, void* dst, int dstCap) {
    return playgroundReadBytes(path, dst, dstCap);
}
EMSCRIPTEN_KEEPALIVE void eve_playground_capture(const char* path) { playgroundCapture(path); }
EMSCRIPTEN_KEEPALIVE int eve_playground_readback_status() { return playgroundReadbackStatus(); }
EMSCRIPTEN_KEEPALIVE void eve_playground_set_paused(int paused) { playgroundSetPaused(paused); }
EMSCRIPTEN_KEEPALIVE void eve_playground_reset() { playgroundResetScene(); }

} // extern "C"
#endif

#if defined(EVENGINE_ANDROID)
#include <android/log.h>
#define EVE_ANDROID_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "EVEngine", __VA_ARGS__)
#elif defined(EVENGINE_IOS) || defined(__EMSCRIPTEN__)
#include <cstdio>
#define EVE_ANDROID_LOGE(...) do { fprintf(stderr, "EVEngine: "); fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); } while (0)
#else
#define EVE_ANDROID_LOGE(...) ((void)0)
#endif

using namespace std;

namespace eve::cmd
{

struct RunArgs : Handler {
    std::string log_path, root_path, dev_server;
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
        run->add_option("--dev-server", dev_server,
                        "Remote hot-reload dev server URL (eve dev), e.g. http://192.168.1.5:8765");
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
                return cmd.Run(current_path, load_root, debug, dap_port, mcp_port, dev_server);
            } else {
                // One root script for every platform: it binds whatever modules
                // the build contains from eve.moduleList, and leaves the frame
                // driver to eve.hostDrivesFrames.
                return cmd.Run(current_path, load_content, debug, dap_port, mcp_port, dev_server);
            }
        }
        return -1; // not handle
    }
};

CMD_REG(RunArgs);


// create a new project
int Cmdline::Run(std::string path, std::string root, bool debug, int dapPort, int mcpPort,
                 std::string devServer) {
    std::fprintf(stderr, "[startup] Run() begins at process clock %.1f ms\n",
                 (double) std::clock() * 1000.0 / (double) CLOCKS_PER_SEC);
    try {
        // Resolve the game directory. A packaged game ships a game.eve archive next to
        // the executable; we mount it into memory and run without extracting to disk.
        std::string gameDir = path;
        std::string archivePath;
        eve::recordLogEvent("info", "eve run starting: " + (gameDir.empty() ? std::string(".") : gameDir));

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
#if !defined(EVENGINE_ANDROID) && !defined(EVENGINE_IOS) && !defined(__EMSCRIPTEN__)
        if (debug) {
            auto& dt = eve::dev::DevTool::instance();
            dt.attach(runtime, /*sampleLocals=*/true);
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
            // Route script-side milestones into the same crash/error log
            // (eve.log) so a crash shows how far the boot sequence got.
            eve.addFunc("log", [](const std::string& level, const std::string& msg) {
                eve::recordLogEvent(level, msg);
            });
            // Scene-director authoring kit (src/scripts/scene_director.nut). Host
            // games load it via `compilestring(eve.sceneDirectorScript)()`; the
            // MCP tools auto-install it on demand.
            eve.set("sceneDirectorScript", std::string(scene_director_content ? scene_director_content : ""));
            eve.set("devServerArg", devServer);
            const char* bench = std::getenv("EVE_BOOT_BENCH");
            eve.set("bootBench", bench && bench[0] != '\0' && bench[0] != '0');
#if defined(__EMSCRIPTEN__)
            // The browser has no blocking loop; load.nut defines eve_frame and
            // returns, and emscripten_set_main_loop drives it below.
            eve.set("hostDrivesFrames", true);
#else
            eve.set("hostDrivesFrames", false);
#endif
        }
        // The generated slot -> class table load.nut iterates over.
        if (module_list_content && *module_list_content)
            runtime.runSource(module_list_content, "module_list.nut");
        {
            ssq::Table eve = runtime.table("eve");
            eve.set("moduleList", runtime.root().find("eve_modules"));
            eve.set("moduleContract", runtime.root().find("eve_module_contract"));
        }
        // Name the embedded root so DAP stack frames map to load.nut (not "buffer").
        // Route file/dofile/loadfile through PhysFS so a packaged game (mounted in
        // memory) can load its scripts without extracting to disk.
        eve::filesystem::physfs::installScriptFileApi(runtime.vm());
        std::fprintf(stderr, "[startup] load.nut begins at process clock %.1f ms\n",
                     (double) std::clock() * 1000.0 / (double) CLOCKS_PER_SEC);
        runtime.runSource(root, "load.nut");
#if defined(__EMSCRIPTEN__)
        // Instead of a blocking while(running) Squirrel loop (which the browser
        // never composites), drive the global eve_frame() function from an
        // Emscripten requestAnimationFrame main loop. simulateInfiniteLoop=1
        // keeps main() alive; `runtime` stays in scope because Run() never returns.
        gFrameVm = &runtime.vm();
        gFrameFunc = new ssq::Function(runtime.vm().find("eve_frame").toFunction());
        gPlaygroundVm = runtime.handle();
        // Let the shell page enable the editor once the engine VM is live
        // (onRuntimeInitialized fires before main(), so it is not enough).
        EM_ASM({ if (window.eveEngineReady) window.eveEngineReady(); });
        emscripten_set_main_loop(&webgpuFrameTick, 0, /*simulateInfiniteLoop=*/1);
#endif
#if !defined(EVENGINE_ANDROID) && !defined(EVENGINE_IOS) && !defined(__EMSCRIPTEN__)
        if (debug) eve::dev::DevTool::instance().detach();
#endif
        return 0;
    } catch (const std::exception& e) {
        std::string what = e.what() ? e.what() : "unknown exception";
        eve::recordLogEvent("error", "Run failed: " + what);
#if !defined(EVENGINE_ANDROID) && !defined(EVENGINE_IOS) && !defined(EVENGINE_WEBGPU)
        if (debug) {
            auto& dt = eve::dev::DevTool::instance();
            // The VM error hook and the Runtime error-handler sink (wired in
            // DevTool::attach(Runtime&)) already slice script errors; only
            // synthesize a report when none was produced (e.g. a native
            // std::exception with no script involvement).
            const auto* scriptError = dynamic_cast<const eve::ScriptException*>(&e);
            std::string report = dt.lastReport();
            if (!(scriptError && scriptError->reported()) || report.empty())
                report = dt.notifyError(what);
            cerr << report << endl;
            dt.detach();
        } else {
            cerr << "Run failed: " << what << endl;
        }
#else
        cerr << "Run failed: " << what << endl;
#endif
        EVE_ANDROID_LOGE("Run failed: %s", what.c_str());
        return 3;
    } catch (...) {
        eve::recordLogEvent("error", "Run failed: unknown exception");
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
