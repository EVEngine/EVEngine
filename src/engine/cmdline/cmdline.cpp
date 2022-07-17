#include "cmdline/cmdline.h"
#include "common/config.h"
#include <simplesquirrel/simplesquirrel.hpp>
#include <CLI11.hpp>
#include <iostream>
#include <rang.hpp>
using namespace std;
namespace eve::cmd
{

Module_IMPL(Cmdline, new Cmdline());

std::vector<std::function<Handler*()>>& Cmdline::handers() {
    static std::vector<std::function<Handler*()>> cmds = {};
    return cmds;
}

void Cmdline::registerCmd(std::function<Handler*()> handler) {
    handers().push_back(handler);
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


Cmdline::Cmdline() {}

void Cmdline::expose(ssq::Table& table) {
    auto cls = table.addClass(name, Cmdline::create, false);
    expose(cls);
}

void Cmdline::expose(ssq::Class& cls) {
    cls.addFunc("getName", &Cmdline::getName);
}

int Cmdline::runArgs(unsigned argc, char** argv) {
    CLI::App app{"EVEngine - A Modern Game Engine"};
    bool version;
    app.add_flag("-v,--version", version, "Print version string");
    auto formatter = std::make_shared<MyFormatter>();

    std::vector<Handler*> handles;
    for (auto cmd : handers()) {
        auto* p = cmd();
        handles.push_back(p);
        p->setup(app, formatter);
    }

    try {
        this->argc = argc;
        this->argv = argv;
        app.parse(argc, argv);
    } catch (const CLI::ParseError &e) {
        for (auto* h : handles) delete h;
        int res = app.exit(e);
        return res;
    }

    int res = 0;
    for (auto* h : handles) {
        if (int r = h->parse(app, *this); r != -1) {
            res = r; break;
        }
    }
    if (version) cout << EVENGINE_VERSION << endl;

    for (auto* h : handles) delete h;
    return res;
}


std::string Cmdline::get_remaining(CLI::App* sub, std::string default_path) {
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


} // namespace eve



