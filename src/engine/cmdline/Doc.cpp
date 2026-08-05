#include "cmdline.h"
#include "common/config.h"

#include <CLI11.hpp>
#include <filesystem>
#include <rang.hpp>

using namespace std::filesystem;
using namespace std;

namespace eve::cmd {

struct DocArgs : Handler {
    void setup(CLI::App& app, std::shared_ptr<CLI::Formatter> formatter) override {
        auto create = app.add_subcommand("doc", "Query the online documentation for a class/function");
        create->allow_extras();
    }

    int parse(CLI::App& app, Cmdline& cmd) override {
        auto create = app.get_subcommand("create");
        if (create->parsed()) {
            string name = cmd.get_remaining(create, "");
            int    res  = cmd.Doc(name);
            return res;
        }
        return -1;  // not handle
    }
};

CMD_REG(DocArgs);


// using system command to open a web page

#ifdef _WIN32
#define OPEN_WEB_PAGE_CMD "start "
#elif defined(__APPLE__)
#define OPEN_WEB_PAGE_CMD "open "
#elif defined(__unix__)
#define OPEN_WEB_PAGE_CMD "xdg-open "
#endif

static inline int openWebPage(string url) {
    string cmd = OPEN_WEB_PAGE_CMD + url;
    return system(cmd.c_str());
}

// create a new project
int Cmdline::Doc(std::string name) {
    string version = EVENGINE_VERSION;
    return openWebPage("https://eve-devs.github.io/docs/" + version + name);
}

}  // namespace eve::cmd
