#include "zeroerr/unittest.h"

#if defined(EVENGINE_ANDROID)
#include <SDL2/SDL.h>
#include <SDL2/SDL_main.h>
#include <android/log.h>

#include <cerrno>
#include <cstring>
#include <iostream>
#include <string>
#include <streambuf>
#include <unistd.h>

namespace {

// Route C++ iostream output to Android logcat so `adb logcat` can show the
// zeroerr console reporter (stdout/stderr are not attached to anything on Android).
class LogcatBuf : public std::streambuf {
public:
    LogcatBuf() { setp(buffer_, buffer_ + sizeof(buffer_) - 1); }
    ~LogcatBuf() override { sync(); }

protected:
    int overflow(int ch) override {
        if (ch != traits_type::eof()) {
            *pptr() = static_cast<char>(ch);
            pbump(1);
        }
        return sync() == 0 ? traits_type::not_eof(ch) : traits_type::eof();
    }

    int sync() override {
        if (pptr() == pbase())
            return 0;
        *pptr() = '\0';
        __android_log_print(ANDROID_LOG_INFO, "EVEngineTest", "%s", pbase());
        setp(buffer_, buffer_ + sizeof(buffer_) - 1);
        return 0;
    }

private:
    char buffer_[1024];
};

// Root of the unpacked test tree: <internal storage>/evengine_test. The Java
// activity unpacks the APK's assets/test + assets/examples here before SDL_main runs.
std::string testRoot() {
    const char *internal = SDL_AndroidGetInternalStoragePath();
    if (!internal)
        return {};
    return std::string(internal) + "/evengine_test";
}

}  // namespace
#endif  // EVENGINE_ANDROID

// Signature must match the `extern "C" int SDL_main(int, char*[])` declared by
// SDL_main.h so the symbol stays unmangled ("SDL_main" for the SDLActivity JNI
// dlsym). A `const char**` parameter would become a C++ overload and be mangled.
int main(int argc, char **argv) {
#if defined(EVENGINE_ANDROID)
    static LogcatBuf logcatBuf;
    std::cout.rdbuf(&logcatBuf);
    std::cerr.rdbuf(&logcatBuf);
    std::clog.rdbuf(&logcatBuf);
    std::cout.setf(std::ios::unitbuf);
    std::cerr.setf(std::ios::unitbuf);

    const std::string root = testRoot();
    if (root.empty()) {
        __android_log_print(ANDROID_LOG_ERROR, "EVEngineTest", "no internal storage path");
    } else if (chdir(root.c_str()) != 0) {
        __android_log_print(ANDROID_LOG_ERROR, "EVEngineTest", "chdir(%s) failed: %s",
                            root.c_str(), strerror(errno));
    } else {
        __android_log_print(ANDROID_LOG_INFO, "EVEngineTest", "test root: %s", root.c_str());
    }

    // SDLActivity already ran SDL_SetMainReady(); init the subsystems the tests use.
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_TIMER | SDL_INIT_AUDIO) != 0) {
        __android_log_print(ANDROID_LOG_ERROR, "EVEngineTest", "SDL_Init failed: %s",
                            SDL_GetError());
    }
    __android_log_print(ANDROID_LOG_INFO, "EVEngineTest", "unit_test android starting");
#endif  // EVENGINE_ANDROID

    return zeroerr::UnitTest().parseArgs(argc, const_cast<const char **>(argv)).run();
}
