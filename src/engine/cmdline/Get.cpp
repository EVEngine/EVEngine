#include "cmdline.h"
#include "common/config.h"

#include <filesystem>
#include <CLI11.hpp>
#include <rang.hpp>

using namespace std::filesystem;
using namespace std;

namespace eve::cmd
{

struct GetArgs : Handler {
    void setup(CLI::App& app, std::shared_ptr<CLI::Formatter> formatter) override {
        auto subcmd = app.add_subcommand("get", "Get a external module from Github");
        subcmd->allow_extras();
    }

    int parse(CLI::App& app, Cmdline& cmd) override {
        auto subcmd = app.get_subcommand("get");
        if (subcmd->parsed()) {
            string name = cmd.get_remaining(subcmd, "");
            int res = cmd.Get(name);
            if (res == 0) cout << rang::fg::green << "Get " << name << rang::fg::reset << endl;
            return res;
        }
        return -1; // not handle
    }
};

CMD_REG(GetArgs);


// create a new project
int Cmdline::Get(std::string url) {
    return 0;
}

} // namespace eve
