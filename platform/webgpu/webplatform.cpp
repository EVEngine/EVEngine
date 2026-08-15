#include "webgpu/webplatform.h"

#include <cstdio>
#include <cstdlib>
#include <string>

#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#endif

namespace eve {
namespace webgpu_platform {

namespace {
const char *envOr(const char *name, const char *fallback) {
    const char *v = std::getenv(name);
    return (v && *v) ? v : fallback;
}
}  // namespace

std::string getGameDirectory() {
#if defined(__EMSCRIPTEN__)
    return "/game";
#else
    return envOr("EVENGINE_WEBGPU_GAME_DIR", ".");
#endif
}

std::string getAppdataDirectory() {
#if defined(__EMSCRIPTEN__)
    return "/persist";
#else
    const char *base = envOr("EVENGINE_WEBGPU_APPDATA", "appdata");
    return std::string(base);
#endif
}

std::string getHomeDirectory() {
#if defined(__EMSCRIPTEN__)
    return "/";
#else
    return ".";
#endif
}

std::string getExecutablePath() {
#if defined(__EMSCRIPTEN__)
    return std::string("eve.html");
#else
    return std::string("eve");
#endif
}

bool openURL(const std::string &url) {
#if defined(__EMSCRIPTEN__)
    // Let the browser open the URL in a new tab.
    char *js = new char[url.size() + 64];
    std::snprintf(js, url.size() + 64, "window.open('%s','_blank')", url.c_str());
    emscripten_run_script(js);
    delete[] js;
    return true;
#else
    (void)url;
    return false;
#endif
}

void vibrate() {
#if defined(__EMSCRIPTEN__)
    emscripten_run_script("if (navigator.vibrate) navigator.vibrate(500);");
#endif
}

bool hasBackgroundMusic() { return false; }

void pumpEvents() {
#if defined(__EMSCRIPTEN__)
    // Non-blocking browser event / callback drain. Works both on the main
    // thread and inside pthread workers (emscripten_sleep below is a busy
    // return to the browser, not a real sleep).
    emscripten_sleep(0);
#endif
}

}  // namespace webgpu_platform
}  // namespace eve
