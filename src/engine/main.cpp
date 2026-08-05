#include "common/config.h"
#include "cmdline/cmdline.h"
#include <CLI11.hpp>
#include <rang.hpp>
#include <iostream>
#include <string>
#include <vector>

#if defined(EVENGINE_ANDROID) || defined(EVENGINE_IOS)
#include <SDL2/SDL.h>
#include <SDL2/SDL_main.h>
#endif

#if defined(EVENGINE_IOS)
#include "ios/ios.h"
#endif

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
#if defined(EVENGINE_IOS)
    // UIKit launches with no CLI args; inject `run <bundle game dir>` like Android Activity.
    // Qualify eve::ios to avoid ambiguity with std::ios after using-directives.
    static std::string gamePath;
    static std::vector<char *> injected;
    if (argc <= 1) {
        gamePath = eve::ios::getGameDirectory();
        if (gamePath.empty())
            gamePath = ".";
        static char runFlag[] = "run";
        injected = {argv[0], runFlag, gamePath.data()};
        argc = static_cast<int>(injected.size());
        argv = injected.data();
    }
    eve::ios::initAudioSessionInterruptionHandler();
#endif
    return requireModInst(eve::cmd,Cmdline)->runArgs(argc, argv);
}
