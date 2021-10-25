#include "cmdline.h"
#include <filesystem>
#include <CLI11.hpp>
#include <rang.hpp>

using namespace std::filesystem;
using namespace std;

namespace eve::cmd
{

struct CreateArgs : Handler {
    void setup(CLI::App& app, std::shared_ptr<CLI::Formatter> formatter) override {
        auto create = app.add_subcommand("create", "Create a new game from template");
        create->allow_extras();
    }

    int parse(CLI::App& app, Cmdline& cmd) override {
        auto create = app.get_subcommand("create");
        if (create->parsed()) {
            string name = cmd.get_remaining(create, "mygame");
            int res = cmd.Create(".", name);
            if (res == 0) cout << rang::fg::green << "Created " << name << " in current path" << rang::fg::reset << endl;
            return res;
        }
        return -1; // not handle
    }
};

CMD_REG(CreateArgs);


// create a new project
int Cmdline::Create(std::string path, std::string name) {
    create_directory(path+"/"+name);
    return 0;
}

} // namespace eve
