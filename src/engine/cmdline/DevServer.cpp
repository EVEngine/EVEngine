#include <CLI11.hpp>
#include <filesystem>
#include <rang.hpp>
#include "cmdline.h"

using namespace std::filesystem;
using namespace std;

namespace eve::cmd {

struct DevServerArgs : Handler {
    void setup(CLI::App& app, std::shared_ptr<CLI::Formatter> formatter) override {
        auto create = app.add_subcommand("dev", "Start a development server for the current game");
        create->allow_extras();
    }

    int parse(CLI::App& app, Cmdline& cmd) override {
        auto create = app.get_subcommand("dev");
        if (create->parsed()) {
            string path = cmd.get_remaining(create, ".");
            int    res  = cmd.DevServer(path);
            return res;
        }
        return -1;  // not handle
    }
};

CMD_REG(DevServerArgs);


int Cmdline::DevServer(std::string path) { return 0; }

}  // namespace eve::cmd
