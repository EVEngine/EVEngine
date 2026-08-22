#include "cmdline.h"
#include "common/config.h"

#include <CLI11.hpp>
#include <filesystem>
#include <rang.hpp>

#if defined(EVENGINE_IOS)
#include "ios/ios.h"
#endif

using namespace std::filesystem;
using namespace std;

namespace eve::cmd {

struct DocArgs : Handler {
    bool no_open = false;

    void setup(CLI::App& app, std::shared_ptr<CLI::Formatter> formatter) override {
        auto doc = app.add_subcommand("doc", "Query the online documentation for a class/function");
        doc->allow_extras();
        doc->add_flag("--no-open", no_open,
                      "Print the documentation URL without opening a browser (headless/CI)");
    }

    int parse(CLI::App& app, Cmdline& cmd) override {
        auto doc = app.get_subcommand("doc");
        if (doc->parsed()) {
            string name = cmd.get_remaining(doc, "");
            int    res  = cmd.Doc(name, no_open);
            return res;
        }
        return -1;  // not handle
    }
};

CMD_REG(DocArgs);


// using system command to open a web page

#ifdef _WIN32
#define OPEN_WEB_PAGE_CMD "start "
#elif defined(__APPLE__) && !defined(EVENGINE_IOS)
#define OPEN_WEB_PAGE_CMD "open "
#elif defined(__unix__) && !defined(EVENGINE_ANDROID)
#define OPEN_WEB_PAGE_CMD "xdg-open "
#endif

static inline int openWebPage(string url) {
#if defined(EVENGINE_IOS)
    return eve::ios::openURL(url) ? 0 : -1;
#elif defined(OPEN_WEB_PAGE_CMD)
    string cmd = OPEN_WEB_PAGE_CMD + url;
    return system(cmd.c_str());
#else
    (void)url;
    return -1;
#endif
}

int Cmdline::Doc(std::string symbol, bool noOpen) {
    // The user/API manual lives on the organization GitHub Pages site.
    // Doxygen has no stable per-symbol URL, so open the manual root and echo
    // the requested symbol (useful for headless/agent invocations).
    string url = "https://evengine.github.io/EVEngine/";
    if (!symbol.empty())
        cout << "EVEngine docs for '" << symbol << "': " << url << "\n";
    else
        cout << "EVEngine docs: " << url << "\n";
    if (noOpen) return 0;
    return openWebPage(url);
}

}  // namespace eve::cmd
