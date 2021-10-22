#include "common/config.h"
#include "cmdline/cmdline.h"
#include <CLI11.hpp>
#include <rang.hpp>
#include <iostream>

using namespace eve;
using namespace std;

static string get_remaining(CLI::App* sub, string default_path = ".") {
    auto paths = sub->remaining();
    if (paths.size() > 1) {
        cerr << rang::fg::red << "Unknown remaining arguments: " << rang::fg::reset << paths[1] << endl;
        exit(1);
    }
    if (paths.size() == 0) {
        return default_path;
    }
    return paths[0];
}

class MyFormatter : public CLI::Formatter {
public:
    std::string make_usage(const CLI::App *app, std::string name) const override {
        std::string usage = "Usage: ";
        usage += name;
        usage += " [Options] [Root Folder]";
        return usage;
    }
};

int main(int argc, char **argv)
{
    string log_path, output_path, current_path, platform;
    bool version = false, no_window = false, debug = false, release = false;
    int res = 0;

    CLI::App app{"EVEngine - A Modern Game Engine"};
    app.add_flag("-v,--version", version, "Print version string");
    auto formatter = std::make_shared<MyFormatter>();

    auto run = app.add_subcommand("run", "Run game under current path");
    run->allow_extras()->formatter(formatter);
    run->add_flag("--no-window", no_window, "Run script only, no window mode");
    run->add_flag("--debug", debug, "debug mode");
    run->add_option("-l,--log", log_path, "log messages into a file");

    auto build = app.add_subcommand("build", "Build the game under current path");
    build->allow_extras()->formatter(formatter);
    build->add_option("-p,--platform", platform, "Build platform (linux, macosx, win32, win32_uwp, ios, android)");
    build->add_flag("-r,--release", release, "release build (default)");
    build->add_flag("-d,--debug", debug, "debug build");
    build->add_option("-l,--log", log_path, "log messages into a file");

    auto create = app.add_subcommand("create", "Create a new game from template");
    create->allow_extras();


    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError &e) {
        return app.exit(e);
    }

    Cmdline* cmd = Cmdline::create();
    cmd->setArg0(argv[0]);

    if (build->parsed()) {
        if (debug && release) {
            cerr << rang::fg::red << "Build type can not be both debug and release" << rang::fg::reset << endl;
            exit(1);
        }
        if (!debug && !release) { release = true; }

        current_path = get_remaining(run);
        res = cmd->Build(current_path, output_path, platform);
    }

    if (create->parsed()) {
        string name = get_remaining(create, "mygame");
        res = cmd->Create(".", name);
        cout << rang::fg::green << "Created " << name << " in current path" << rang::fg::reset << endl;
    }

    if (run->parsed() || argc == 1) {
        current_path = get_remaining(run);
        res = cmd->Run(current_path);
    }

    if (version) {
        cout << EVENGINE_VERSION << endl;
    }

    return res;
}