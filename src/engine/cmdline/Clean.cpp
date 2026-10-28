#include "cmdline.h"

#include <CLI11.hpp>
#include <filesystem>
#include <rang.hpp>

using namespace std::filesystem;
using namespace std;

namespace eve::cmd {

struct CleanArgs : Handler {
    void setup(CLI::App& app, std::shared_ptr<CLI::Formatter> formatter) override {
        auto create = app.add_subcommand("clean", "Clean the build directory");
        create->allow_extras();
    }

    int parse(CLI::App& app, Cmdline& cmd) override {
        auto clean = app.get_subcommand("clean");
        if (clean->parsed()) {
            string name = cmd.get_remaining(clean, "debug");
            int    res  = cmd.Clean(name);
            if (res == 0)
                cout << rang::fg::green << "Clean " << name << " build in current path" << rang::fg::reset << endl;
            return res;
        }
        return -1;  // not handle
    }
};

CMD_REG(CleanArgs);


// create a new project
int Cmdline::Clean(std::string path) { return 0; }

}  // namespace eve::cmd
