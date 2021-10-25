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
    return requireModInst(eve::cmd,Cmdline)->runArgs(argc, argv);
}