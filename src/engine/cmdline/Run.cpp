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
    ssq::VM vm(2048, ssq::Libs::ALL);
    ModuleManager::expose(vm);
    ssq::Script script = vm.compileSource(root.c_str());
    vm.run(script);
    return 0;
}



} // namespace eve
