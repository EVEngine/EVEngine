#include "cmdline.h"

#include "common/Capability.h"
#include "common/EditorHost.h"
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
#include <squirrel.h>

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>

namespace eve::cmd {
namespace {

/** Route Squirrel print() to stderr so stdio MCP stdout stays JSON-RPC-only. */
void hostPrintFunc(HSQUIRRELVM, const SQChar* s, ...) {
    char buf[4096];
    va_list args;
    va_start(args, s);
    vsnprintf(buf, sizeof(buf), s, args);
    va_end(args);
    fputs(buf, stderr);
}

}  // namespace

struct McpArgs : Handler {
    std::string root_path;
    int         port = 0;

    void setup(CLI::App& app, std::shared_ptr<CLI::Formatter> formatter) override {
        auto mcp = app.add_subcommand(
            "mcp", "Run a headless MCP host for AI agents (stdio by default; --port for TCP)");
        mcp->allow_extras()->formatter(formatter);
        mcp->add_option("--port", port,
                        "Listen on this TCP port instead of stdio (Codex uses stdio)");
        mcp->add_option("-r,--root", root_path, "Project root directory (default: current dir)");
    }

    int parse(CLI::App& app, Cmdline& cmd) override {
        auto mcp = app.get_subcommand("mcp");
        if (mcp->parsed()) {
            std::string current_path = cmd.get_remaining(mcp);
            if (!root_path.empty()) current_path = root_path;
            return cmd.McpHost(current_path, port);
        }
        return -1;
    }
};

CMD_REG(McpArgs);

int Cmdline::McpHost(std::string path, int port) {
#if defined(EVENGINE_ANDROID) || defined(EVENGINE_IOS) || defined(EVENGINE_WEBGPU)
    (void)path;
    (void)port;
    std::cerr << "eve mcp: headless MCP host is desktop-only" << std::endl;
    return 4;
#else
    try {
        std::string gameDir = path;
        {
            std::error_code ec;
            if (path.empty() || path == ".")
                gameDir = std::filesystem::current_path(ec).string();
            else
                gameDir = std::filesystem::absolute(path, ec).string();
            if (ec) gameDir = path;
        }
        if (!gameDir.empty() && gameDir != ".") {
            std::error_code ec;
            std::filesystem::current_path(gameDir, ec);
            if (ec) {
                std::cerr << "Cannot chdir to project path '" << gameDir << "': " << ec.message()
                          << std::endl;
                return 2;
            }
        }

        Runtime runtime(2048, ssq::Libs::ALL);
        runtime.initialize();
        runtime.vm().setPrintFunc(&hostPrintFunc, &hostPrintFunc);

        // Mount the project for PhysFS so script file APIs / editors/ resolve.
        // Optional for the headless host: keep running when the environment
        // cannot initialize PhysFS (sandboxed/CI shells), scripts just lose
        // the PhysFS-backed file API.
        try {
            auto* fs = eve::filesystem::Filesystem::create();
            if (fs) {
                std::error_code ec;
                auto            cwd = std::filesystem::current_path(ec);
                if (!ec) {
                    try {
                        fs->setSource(cwd.string());
                    } catch (...) {
                    }
                }
            }
            eve::filesystem::physfs::installScriptFileApi(runtime.vm());
        } catch (const std::exception& e) {
            std::cerr << "eve mcp: filesystem mount skipped (" << e.what() << ")" << std::endl;
        }

        // DevTools: attach the VM so eve_eval / eve_run_script / snapshot work;
        // expose eve.dev. No statement-level local sampling (host is a service).
        auto& dt = eve::dev::DevTool::instance();
        dt.attach(runtime.vm(), /*sampleLocals=*/false);
        dt.exposeScriptApi(runtime.vm());

        // The headless host never runs load.nut, so registered modules are not
        // instantiated automatically and their capability providers
        // (IEditorHost, IRenderCapture, ISceneQuery, ...) are absent. Mirror
        // load.nut's binding loop so the host exposes the full tool surface.
        eve::ModuleManager::requireAll();

        // Editor host: JSON Views <-> Squirrel ViewModel binding + window/render.
        auto* host = eve::cap::query<eve::IEditorHost>();
        if (host) {
            host->start(runtime.vm(), gameDir, /*allowWindow=*/true);
            host->exposeScriptApi(runtime.vm());
        } else {
            std::cerr << "eve mcp: editor host unavailable (ui module missing)"
                      << std::endl;
            return 3;
        }

        auto& mcp = dt.mcp();
        mcp.setGameRoot(gameDir);

        if (port > 0) {
            const int bound = dt.startMcp(static_cast<uint16_t>(port));
            if (bound <= 0) {
                std::cerr << "Failed to start MCP server on port " << port << std::endl;
                return 3;
            }
            std::cerr << "MCP listening on 127.0.0.1:" << bound
                      << " (newline JSON-RPC; use tools/eve-mcp for Cursor stdio)" << std::endl;
        } else {
            if (!mcp.listenStdio()) {
                std::cerr << "Failed to start stdio MCP transport" << std::endl;
                return 3;
            }
            std::cerr << "MCP stdio transport ready (headless editor host; project=" << gameDir
                      << ")" << std::endl;
        }

        while (!host->exitRequested()) {
            dt.poll();  // drains MCP requests on the main thread
            host->frame();
            if (mcp.transport() == eve::dev::McpServer::Transport::Stdio &&
                mcp.stdinClosed()) {
                std::cerr << "MCP stdio closed; host exiting" << std::endl;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }

        host->stop();
        dt.detach();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "eve mcp failed: " << e.what() << std::endl;
        return 3;
    }
#endif
}

}  // namespace eve::cmd
