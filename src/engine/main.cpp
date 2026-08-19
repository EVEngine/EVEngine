#include "common/config.h"
#include "cmdline/cmdline.h"
#include <CLI11.hpp>
#include <rang.hpp>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <iostream>
#include <string>
#include <vector>

#if defined(EVENGINE_WINDOWS) || defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#include <backward.hpp>

// backward.hpp pulls in <imagehlp.h> with its own packing; declaring the one
// DbgHelp entry point we call directly avoids including dbghelp.h again.
extern "C" BOOL WINAPI SymInitialize(HANDLE hProcess, PCSTR UserSearchPath,
                                     BOOL fInvadeProcess);
#endif

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

#if defined(EVENGINE_WINDOWS) || defined(_WIN32)
namespace {

// Unhandled-exception filter: print the exception code and a symbolized stack
// trace (backward-cpp / DbgHelp), then let the OS terminate as usual.
LONG WINAPI eveCrashHandler(EXCEPTION_POINTERS *ep) {
    // DbgHelp must be initialized before StackWalk64, otherwise the walk
    // produces garbage frames. Ignore the "already initialized" failure.
    SymInitialize(GetCurrentProcess(), nullptr, TRUE);
    std::fprintf(stderr, "\n[crash] code=0x%08lX at %p\n",
                 ep->ExceptionRecord->ExceptionCode,
                 ep->ExceptionRecord->ExceptionAddress);
    try {
        backward::StackTrace st;
        // Walk from the handler's own frame: the exception dispatch ran on the
        // crashing thread's stack, so the crash site is still in the chain.
        // Walking from ep->ContextRecord produced garbage frames (0xCC) with
        // DbgHelp StackWalk64 on this setup.
        st.load_here(64);
        backward::Printer p;
        p.snippet = false;
        p.color_mode = backward::ColorMode::never;
        p.print(st, stderr);
    } catch (...) {
        std::fprintf(stderr, "[crash] backtrace unavailable\n");
    }
    std::fflush(stderr);
    return EXCEPTION_CONTINUE_SEARCH;
}

}  // namespace
#endif

namespace {
// Earliest code we control: runs during static initialization, before main().
const auto gEveStaticInitStart = std::chrono::steady_clock::now();
}  // namespace

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
#if defined(EVENGINE_WINDOWS) || defined(_WIN32)
    SetUnhandledExceptionFilter(&eveCrashHandler);
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
