#include "cmdline.h"

#include <CLI11.hpp>
#include <fstream>
#include <filesystem>
#include <rang.hpp>

using namespace std::filesystem;
using namespace std;

namespace eve::cmd {

struct CreateArgs : Handler {
    void setup(CLI::App& app, std::shared_ptr<CLI::Formatter> formatter) override {
        auto create = app.add_subcommand("create", "Create a new game from template");
        create->allow_extras();
    }

    int parse(CLI::App& app, Cmdline& cmd) override {
        auto create = app.get_subcommand("create");
        if (create->parsed()) {
            string name = cmd.get_remaining(create, "mygame");
            int    res  = cmd.Create(".", name);
            if (res == 0) {
                cout << rang::fg::green << "Created " << name << " in current path" << rang::fg::reset << endl;
                cout << "  run it: eve run " << name << endl;
            }
            return res;
        }
        return -1;  // not handle
    }
};

CMD_REG(CreateArgs);


// create a new project
int Cmdline::Create(std::string path, std::string name) {
    if (name.empty()) name = "mygame";
    std::filesystem::path dir = std::filesystem::path(path) / name;
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        cerr << rang::fg::red << "Failed to create " << dir << ": " << ec.message() << rang::fg::reset << endl;
        return 1;
    }

    const std::string config = R"(config = { width=800 height=600 title=")" + name +
                               R"(" hotReload=true };)";
    const std::string main = R"(// EVEngine minimal game template.
// Frame: eve_init (once) -> eve_update(dt) -> eve_render().
// Guard mutable state so hot reload (re-dofile) does not reset it.
if (!("boxX" in getroottable())) boxX <- 0.0;
if (!("boxY" in getroottable())) boxY <- 100.0;
if (!("vx" in getroottable())) vx <- 240.0;

eve_init = function() {
    gfx.setBackgroundColor(0.08, 0.10, 0.20, 1.0);
};

eve_update = function(dt) {
    boxX += vx * dt;
    if (boxX > config.width || boxX < 0.0) vx = -vx;
};

eve_render = function() {
    gfx.clear();
    gfx.drawSolidRect(boxX - 20.0, boxY - 20.0, 40.0, 40.0, 1.3, 0.8, 0.4, 1.0);
};
)";

    auto writeIfMissing = [&](const char* file, const std::string& content) {
        auto p = dir / file;
        if (std::filesystem::exists(p)) {
            cout << "  skip existing " << file << endl;
            return;
        }
        std::ofstream out(p, std::ios::binary);
        out << content;
        out.close();
    };
    writeIfMissing("config.nut", config);
    writeIfMissing("main.nut", main);
    return 0;
}

}  // namespace eve::cmd
