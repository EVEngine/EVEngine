#include "cmdline.h"
#include "scripts.h"
#include "common/Module.h"
#include "common/Runtime.h"
#include "common/config.h"
#include "filesystem/Filesystem.h"
#if !defined(EVENGINE_ANDROID) && !defined(EVENGINE_IOS)
#include "devtools/DevTool.hpp"
#include "devtools/McpServer.hpp"
#endif

#include <simplesquirrel/simplesquirrel.hpp>
#include <CLI11.hpp>
#include <cstdint>
#include <string>
#include <filesystem>

#if defined(EVENGINE_ANDROID)
#include <android/log.h>
#define EVE_ANDROID_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "EVEngine", __VA_ARGS__)
#elif defined(EVENGINE_IOS)
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
            } else
                return cmd.Run(current_path, load_content, debug, dap_port, mcp_port);
        }
        return -1; // not handle
    }
};

CMD_REG(RunArgs);


// create a new project
int Cmdline::Run(std::string path, std::string root, bool debug, int dapPort, int mcpPort) {
    try {
        // Switch to the game directory so load.nut can dofile("config.nut") / "main.nut".
        if (!path.empty() && path != ".") {
            std::error_code ec;
            std::filesystem::current_path(path, ec);
            if (ec) {
                cerr << "Cannot chdir to game path '" << path << "': " << ec.message() << endl;
                EVE_ANDROID_LOGE("Cannot chdir to game path '%s': %s", path.c_str(), ec.message().c_str());
                return 2;
            }
        }

        // Mount game dir for PhysFS so relative watch/read resolve (hot reload).
        {
            auto *fs = eve::filesystem::Filesystem::create();
            if (fs) {
                std::error_code ec;
                auto cwd = std::filesystem::current_path(ec);
                if (!ec) {
                    // setSource only succeeds once; ignore failure if already mounted.
                    fs->setSource(cwd.string());
                }
            }
        }

        Runtime runtime(2048, ssq::Libs::ALL);
        runtime.initialize();
#if !defined(EVENGINE_ANDROID) && !defined(EVENGINE_IOS)
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
        }
        // Name the embedded root so DAP stack frames map to load.nut (not "buffer").
        runtime.runSource(root, "load.nut");
#if !defined(EVENGINE_ANDROID) && !defined(EVENGINE_IOS)
        if (debug) eve::dev::DevTool::instance().detach();
#endif
        return 0;
    } catch (const std::exception& e) {
#if !defined(EVENGINE_ANDROID) && !defined(EVENGINE_IOS)
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
#if !defined(EVENGINE_ANDROID) && !defined(EVENGINE_IOS)
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
