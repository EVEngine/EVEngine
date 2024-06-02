#include "cmdline.h"
#include <filesystem>
#include <CLI11.hpp>
#include <rang.hpp>

using namespace std::filesystem;
using namespace std;

namespace eve::cmd
{

struct TestArgs : Handler {
    void setup(CLI::App& app, std::shared_ptr<CLI::Formatter> formatter) override {
        auto subcmd = app.add_subcommand("test", "Run unit testing");
        subcmd->allow_extras();
    }

    int parse(CLI::App& app, Cmdline& cmd) override {
        auto subcmd = app.get_subcommand("test");
        if (subcmd->parsed()) {
            string name = cmd.get_remaining(subcmd, "");
            int res = cmd.Test(name);
            return res;
        }
        return -1; // not handle
    }
};

CMD_REG(TestArgs);


// create a new project
int Cmdline::Test(std::string path) {
    return 0;
}

} // namespace eve
