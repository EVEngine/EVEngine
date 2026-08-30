#include "cmdline.h"
#include "cmdline/sdk_tools.h"

#include <CLI11.hpp>
#include <rang.hpp>

#include <iostream>

using namespace std;

namespace eve::cmd {

struct GetArgs : Handler {
    void setup(CLI::App& app, std::shared_ptr<CLI::Formatter> formatter) override {
        auto subcmd = app.add_subcommand(
            "get",
            "Download and install a platform SDK matching the current eve version "
            "(e.g. `eve get android`)");
        subcmd->allow_extras();
    }

    int parse(CLI::App& app, Cmdline& cmd) override {
        auto subcmd = app.get_subcommand("get");
        if (subcmd->parsed()) {
            string name = cmd.get_remaining(subcmd, "");
            int    res  = cmd.Get(name);
            if (res == 0) cout << rang::fg::green << "Get " << name << rang::fg::reset << endl;
            return res;
        }
        return -1;  // not handle
    }
};

CMD_REG(GetArgs);

int Cmdline::Get(std::string name) {
    using namespace sdk;

    const Platform p = parsePlatform(name);
    if (name.empty() || p == Platform::Unknown) {
        cerr << rang::fg::red << "eve get: unknown SDK '" << name
             << "' (supported: android)" << rang::fg::reset << endl;
        return 2;
    }
    if (p != Platform::Android) {
        cerr << rang::fg::red << "eve get: automatic SDK install is not implemented for '"
             << platformName(p) << "' yet (supported: android)" << rang::fg::reset << endl;
        return 2;
    }
    auto result = installAndroidSdk();
    if (result.ok()) return 0;
    cerr << rang::fg::red << "eve get: " << result.status().describe() << rang::fg::reset << endl;
    return 3;
}

}  // namespace eve::cmd
