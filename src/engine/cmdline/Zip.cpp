#include "cmdline.h"
#include <filesystem>
#include <CLI11.hpp>
#include <rang.hpp>

using namespace std::filesystem;
using namespace std;

namespace eve::cmd
{

struct ZipArgs : Handler {
    void setup(CLI::App& app, std::shared_ptr<CLI::Formatter> formatter) override {
        auto subcmd = app.add_subcommand("zip", "Zip the game to a package");
        subcmd->allow_extras();
    }

    int parse(CLI::App& app, Cmdline& cmd) override {
        auto subcmd = app.get_subcommand("zip");
        if (subcmd->parsed()) {
            string name = cmd.get_remaining(subcmd, "mygame");
            int res = cmd.Zip(name);
            return res;
        }
        return -1; // not handle
    }
};

CMD_REG(ZipArgs);


int Cmdline::Zip(std::string path) {
    return 0;
}

} // namespace eve
