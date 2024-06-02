#include "cmdline/cmdline.h"

#include <CLI11.hpp>
#include <rang.hpp>

#include <iostream>
using std::string;
using std::cerr;
using std::endl;

namespace eve::cmd {

struct BuildArgs : Handler {
    string log_path, platform, output_path;
    bool   release = false, debug = false;

    void setup(CLI::App& app, std::shared_ptr<CLI::Formatter> formatter) override {
        auto subcmd = app.add_subcommand("build", "Build the game under current path");
        subcmd->allow_extras()->formatter(formatter);
        subcmd->add_option("-p,--platform", platform, "Build platform (linux, macosx, win32, win32_uwp, ios, android)");
        subcmd->add_flag("-r,--release", release, "release build (default)");
        subcmd->add_flag("-d,--debug", debug, "debug build");
        subcmd->add_option("-l,--log", log_path, "log messages into a file");
        subcmd->add_option("-o,--output", output_path, "output folder path");
    }

    int parse(CLI::App& app, Cmdline& cmd) override {
        auto subcmd = app.get_subcommand("build");
        if (subcmd->parsed()) {
            if (debug && release) {
                cerr << rang::fg::red << "Build type can not be both debug and release" << rang::fg::reset << endl;
                exit(1);
            }
            if (!debug && !release) {
                release = true;
            }

            string current_path = cmd.get_remaining(subcmd);
            return cmd.Build(current_path, output_path, platform);
        }
        return -1;  // not handle
    }
};

CMD_REG(BuildArgs);


int Cmdline::Build(std::string path, std::string output, std::string platform) { return 0; }

}  // namespace eve::cmd
