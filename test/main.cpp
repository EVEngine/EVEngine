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
#elif defined(EVENGINE_IOS_TEST_APP)
#include <SDL2/SDL.h>
#include <SDL2/SDL_main.h>
#include "ios_test.h"

#include <unistd.h>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <streambuf>
#include <string>
#include <vector>

namespace {

// Route C++ iostream output to unified logging (os_log) so `log stream` can
// show the zeroerr console reporter from a device (stdout/stderr are not
// attached to anything when the app is launched from the home screen).
class OSLogBuf : public std::streambuf {
public:
    OSLogBuf() { setp(buffer_, buffer_ + sizeof(buffer_) - 1); }
    ~OSLogBuf() override { sync(); }

protected:
    int overflow(int ch) override {
        if (ch != traits_type::eof()) {
            *pptr() = static_cast<char>(ch);
            pbump(1);
        }
        return sync() == 0 ? traits_type::not_eof(ch) : traits_type::eof();
    }

    int sync() override {
        if (pptr() == pbase()) return 0;
        *pptr() = '\0';
        eve::ios_test::logLine(pbase());
        setp(buffer_, buffer_ + sizeof(buffer_) - 1);
        return 0;
    }

private:
    char buffer_[1024];
};

// Root of the staged test tree (Caches/evengine_test), unpacked by
// test/ios_test.mm from the app bundle's test/ + examples/ resources. The
// runner chdirs there so EVENGINE_SOURCE_DIR="." / EVENGINE_TEST_BINARY_DIR="."
// resolve on-device, exactly like the Android test app.
std::string testRoot() { return eve::ios_test::stagedTestRoot(); }

}  // namespace
#endif  // EVENGINE_ANDROID / EVENGINE_IOS_TEST_APP

// Signature must match the `extern "C" int SDL_main(int, char*[])` declared by
// SDL_main.h so the symbol stays unmangled (Android: SDLActivity JNI dlsym;
// iOS: SDL's UIKit main calls it). A `const char**` parameter would become a
// C++ overload and be mangled.
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
#elif defined(EVENGINE_IOS_TEST_APP)
    static OSLogBuf osLogBuf;
    std::cout.rdbuf(&osLogBuf);
    std::cerr.rdbuf(&osLogBuf);
    std::clog.rdbuf(&osLogBuf);
    std::cout.setf(std::ios::unitbuf);
    std::cerr.setf(std::ios::unitbuf);
    // The suite can run for minutes; keep the screen on so iOS does not
    // suspend/force-kill the foreground app when the screen auto-locks.
    eve::ios_test::keepAwake();

    const std::string root = testRoot();
    if (root.empty()) {
        eve::ios_test::logLine("no writable test root");
    } else if (chdir(root.c_str()) != 0) {
        eve::ios_test::logLine(("chdir(" + root + ") failed: " + std::strerror(errno)).c_str());
    } else {
        eve::ios_test::logLine(("test root: " + root).c_str());
    }

    // SDL's UIKit main already ran SDL_SetMainReady(); init the subsystems the
    // tests use (window/graphics tests open a real SDL_WINDOW_VULKAN surface).
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_TIMER | SDL_INIT_AUDIO) != 0) {
        eve::ios_test::logLine(("SDL_Init failed: " + std::string(SDL_GetError())).c_str());
    }
    eve::ios_test::logLine("unit_test ios starting");

    // Inject --testcase=<filter> from the launch arguments / UserDefaults
    // (mirrors the evengine.test.filter intent extra on Android). A file
    // filter (-evengine.test.file <basename>) takes precedence so the suite
    // can be run one test file per process (see run/ios-test-all-debug).
    const std::string fileFilter = eve::ios_test::launchFileFilter();
    const std::string filter     = eve::ios_test::launchFilter();
    static std::string filterArg;
    if (!fileFilter.empty()) {
        // TestCase.file stores the compiled __FILE__ path; regex_match needs a
        // full match, so anchor the basename at the end of any path.
        filterArg = "--file=.*" + fileFilter;
        static std::vector<char *> injected;
        injected = {argc > 0 ? argv[0] : const_cast<char *>("eve"), filterArg.data()};
        argc     = static_cast<int>(injected.size());
        argv     = injected.data();
    } else if (!filter.empty()) {
        filterArg = "--testcase=" + filter;
        static std::vector<char *> injected;
        injected = {argc > 0 ? argv[0] : const_cast<char *>("eve"), filterArg.data()};
        argc     = static_cast<int>(injected.size());
        argv     = injected.data();
    }
#endif  // EVENGINE_ANDROID / EVENGINE_IOS_TEST_APP

    const int result = zeroerr::UnitTest().parseArgs(argc, const_cast<const char **>(argv)).run();
#if defined(EVENGINE_IOS_TEST_APP)
    // SDL's UIKit main keeps the app resident after SDL_main returns; a test
    // runner should terminate once the suite is done so per-file launches
    // (--console) return and a driver can move on to the next file.
    eve::ios_test::logLine("unit_test ios done");
    std::exit(result);
#else
    return result;
#endif
}
