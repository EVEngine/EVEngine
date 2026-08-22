#include "common/config.h"
#include "common/CrashHandler.h"
#include "cmdline/cmdline.h"
#include <CLI11.hpp>
#include <rang.hpp>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <iostream>
#include <string>
#include <vector>

#if defined(EVENGINE_ANDROID) || defined(EVENGINE_IOS) || defined(EVENGINE_WEBGPU)
#include <SDL2/SDL.h>
#include <SDL2/SDL_main.h>
#endif

#if defined(EVENGINE_IOS)
#include "ios/ios.h"
#endif

#if defined(EVENGINE_WEBGPU)
#include "webgpu/webplatform.h"
#endif

using namespace eve;
using namespace std;

namespace {
// Earliest code we control: runs during static initialization, before main().
const auto gEveStaticInitStart = std::chrono::steady_clock::now();
}  // namespace

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
    // Make script print() / cout diagnostics visible immediately when stdout is
    // redirected (CI logs, pipes): the C runtime fully buffers stdout when it
    // is not a console, so a startup marker from eve_init could otherwise stay
    // in the buffer and be lost when the process is killed after a smoke run.
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::cout.setf(std::ios::unitbuf);

#if defined(EVENGINE_WINDOWS) || defined(_WIN32)
    eve::installCrashHandler();
    // Diagnostic hook: EVE_TEST_CRASH=1 forces an access violation right after
    // startup so the crash handler output can be verified.
    const char *testCrash = std::getenv("EVE_TEST_CRASH");
    if (testCrash && testCrash[0] != '\0' && testCrash[0] != '0') {
        std::fprintf(stderr, "[crash] EVE_TEST_CRASH: forcing an access violation\n");
        *static_cast<volatile int *>(nullptr) = 0;
    }
#endif
    const auto tMain = std::chrono::steady_clock::now();
    std::fprintf(stderr, "[startup] static-init -> main(): %.1f ms\n",
                 std::chrono::duration<double, std::milli>(tMain - gEveStaticInitStart).count());
    std::fprintf(stderr, "[startup] process clock() at main(): %.1f ms\n",
                 (double) std::clock() * 1000.0 / (double) CLOCKS_PER_SEC);
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
#elif defined(EVENGINE_WEBGPU)
    // The browser launches with no CLI args; inject `run <game root>` where
    // the game is the preloaded VFS mount (/game) or the CWD.
    static std::string gamePath;
    static std::vector<char *> injected;
    if (argc <= 1) {
        gamePath = eve::webgpu_platform::getGameDirectory();
        if (gamePath.empty())
            gamePath = ".";
        static char runFlag[] = "run";
        injected = {argv[0], runFlag, gamePath.data()};
        argc = static_cast<int>(injected.size());
        argv = injected.data();
    }
#endif
    return requireModInst(eve::cmd,Cmdline)->runArgs(argc, argv);
}
