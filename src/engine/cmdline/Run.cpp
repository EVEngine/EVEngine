#include "cmdline.h"
#include "scripts.h"
#include "common/Module.h"

#include <simplesquirrel/simplesquirrel.hpp>
#include <CLI11.hpp>
#include <string>

using namespace std;

namespace eve::cmd
{

struct RunArgs : Handler {
    string log_path;
    bool no_window = false, debug = false;

    void setup(CLI::App& app, std::shared_ptr<CLI::Formatter> formatter) override {
        auto run = app.add_subcommand("run", "Run game under current path");
        run->allow_extras()->formatter(formatter);
        run->add_flag("--no-window", no_window, "Run script only, no window mode");
        run->add_flag("--debug", debug, "debug mode");
        run->add_option("-l,--log", log_path, "log messages into a file");
    }

    int parse(CLI::App& app, Cmdline& cmd) override {
        auto run = app.get_subcommand("run");
        if (run->parsed() || cmd.getArgc() == 1) {
            string current_path = cmd.get_remaining(run);
            return cmd.Run(current_path);
        }
        return -1; // not handle
    }
};

CMD_REG(RunArgs);


// create a new project
int Cmdline::Run(std::string path) {
    ssq::VM vm(2048, ssq::Libs::ALL);
    ModuleManager::expose(vm);
    ssq::Script script = vm.compileSource(load_content);
    vm.run(script);
    return 0;
}



} // namespace eve
