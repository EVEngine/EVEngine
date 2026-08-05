#include "android.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_system.h>

#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace eve {
namespace android {

bool directoryExists(const char *path) {
    if (!path || !*path)
        return false;
    struct stat st {};
    if (stat(path, &st) != 0)
        return false;
    return S_ISDIR(st.st_mode) != 0;
}

bool mkdir(const char *path) {
    if (!path || !*path)
        return false;
    if (directoryExists(path))
        return true;

    // Recursively create parents.
    std::string p(path);
    while (!p.empty() && (p.back() == '/' || p.back() == '\\'))
        p.pop_back();
    if (p.empty())
        return false;

    const auto slash = p.find_last_of("/\\");
    if (slash != std::string::npos) {
        const std::string parent = p.substr(0, slash);
        if (!parent.empty() && !directoryExists(parent.c_str()) && !mkdir(parent.c_str()))
            return false;
    }

    if (::mkdir(p.c_str(), 0770) != 0 && errno != EEXIST)
        return false;
    return true;
}

bool createStorageDirectories() {
    const char *internal = SDL_AndroidGetInternalStoragePath();
    if (!internal)
        return false;

    std::string base(internal);
    if (!mkdir(base.c_str()))
        return false;
    if (!mkdir((base + "/save").c_str()))
        return false;
    return true;
}

void setImmersive(bool immersive) {
    // SDL already drives fullscreen via SDL_WINDOW_FULLSCREEN_DESKTOP on Android.
    // Keep a hook for Window::setFullscreen callers.
    (void)immersive;
}

bool checkFusedGame(void **gameLoveIO) {
    if (gameLoveIO)
        *gameLoveIO = nullptr;
    // MVP: game is unpacked to internal storage by EVEngineActivity, not fused in AAsset.
    return false;
}

bool initializeVirtualArchive() {
    return false;
}

void deinitializeVirtualArchive() {}

std::string getSelectedGameFile() {
    // Prefer explicit path from SDL arguments / env set by Activity.
    if (const char *fromEnv = std::getenv("EVENGINE_GAME_PATH")) {
        if (fromEnv[0] != '\0')
            return std::string(fromEnv);
    }
    const char *internal = SDL_AndroidGetInternalStoragePath();
    if (!internal)
        return {};
    return std::string(internal) + "/game";
}

bool loadGameArchiveToMemory(const char * /*filename*/, char **outPtr, size_t *outSize) {
    if (outPtr)
        *outPtr = nullptr;
    if (outSize)
        *outSize = 0;
    return false;
}

void freeGameArchiveMemory(void *ptr) {
    free(ptr);
}

std::string getExecutablePath() {
    char buffer[2048] = {0};
    ssize_t len = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (len > 0)
        return std::string(buffer, static_cast<size_t>(len));
    return {};
}

}  // namespace android
}  // namespace eve
