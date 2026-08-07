#include "cmdline.h"
#include "scripts.h"
#include "common/Module.h"
#include "common/config.h"
#include "filesystem/Filesystem.h"

#include <simplesquirrel/simplesquirrel.hpp>
#include <CLI11.hpp>
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

    void setup(CLI::App& app, std::shared_ptr<CLI::Formatter> formatter) override {
        auto run = app.add_subcommand("run", "Run game under current path");
        run->allow_extras()->formatter(formatter);
        run->add_flag("--no-window", no_window, "Run script only, no window mode");
        run->add_flag("--debug", debug, "debug mode");
        run->add_option("-l,--log", log_path, "log messages into a file");
        run->add_option("-r,--root", root_path, "give a entry script instead of using the system default one");
    }

    int parse(CLI::App& app, Cmdline& cmd) override {
        auto run = app.get_subcommand("run");
        if (run->parsed() || cmd.getArgc() == 1) {
            std::string current_path = cmd.get_remaining(run);
            if (root_path != "") {
                ifstream ifs(root_path);
                if (!ifs) {
                    cerr << "Cannot open root script: " << root_path << endl;
                    return -1;
                }
                std::string load_root((istreambuf_iterator<char>(ifs)), (istreambuf_iterator<char>()));
                return cmd.Run(current_path, load_root);
            } else
                return cmd.Run(current_path, load_content);
        }
        return -1; // not handle
    }
};

CMD_REG(RunArgs);


// create a new project
int Cmdline::Run(std::string path, std::string root) {
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

        ssq::VM vm(2048, ssq::Libs::ALL);
        ModuleManager::expose(vm);
        // Embedded default demo (src/scripts/demo.nut); load.nut runs it when no main.nut.
        {
            ssq::Table eve(vm.find("eve"));
            eve.set("demoScript", std::string(demo_content ? demo_content : ""));
            eve.set("asyncScript", std::string(async_content ? async_content : ""));
        }
        ssq::Script script = vm.compileSource(root.c_str());
        vm.run(script);
        return 0;
    } catch (const std::exception& e) {
        cerr << "Run failed: " << e.what() << endl;
        EVE_ANDROID_LOGE("Run failed: %s", e.what());
        return 3;
    } catch (...) {
        cerr << "Run failed: unknown exception" << endl;
        EVE_ANDROID_LOGE("Run failed: unknown exception");
        return 3;
    }
}



} // namespace eve
