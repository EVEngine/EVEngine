#pragma once
// test/PathBesideSource.h
//
// Shared helper for resolving test asset paths relative to the test source file.
// On desktop the path is dirname(__FILE__); on Android the build-host path baked
// into __FILE__ is replaced with the on-device unpacked tree root so that
// `pathBesideThisSource("assets/...")` resolves to <internal>/evengine_test/assets/...
//
// Deliberately uses its own `eve_test_path` namespace (NOT `eve`): this header is
// included *inside* the anonymous namespaces of several test files, so introducing
// `namespace eve` there would shadow the global ::eve and break every
// `eve::window::...` / `eve::image::...` reference in those files.

#include <string>

#if defined(EVENGINE_ANDROID)
#include <SDL2/SDL.h>
#endif

namespace eve_test_path {

#if defined(EVENGINE_ANDROID)
inline std::string androidTestRoot() {
    static const std::string root = [] {
        const char *internal = SDL_AndroidGetInternalStoragePath();
        return internal ? std::string(internal) + "/evengine_test" : std::string();
    }();
    return root;
}

// Map a build-host source path (__FILE__) onto the on-device test layout.
// All test sources are flat in test/, and the APK unpacks assets/test/* directly
// into the device root (<internal>/evengine_test/*), so the basename is the only
// meaningful part of __FILE__ here.
inline std::string androidSourcePath(const char *file) {
    std::string s = file ? file : "";
    const std::string root = androidTestRoot();
    if (root.empty())
        return s;
    size_t slash = s.find_last_of("/\\");
    std::string rel = (slash == std::string::npos) ? s : s.substr(slash + 1);
    return root + "/" + rel;
}
#endif  // EVENGINE_ANDROID

inline std::string pathBesideSourceImpl(const char *file, const char *relative) {
#if defined(EVENGINE_ANDROID)
    std::string source = androidSourcePath(file);
    size_t slash = source.find_last_of('/');
    std::string dir = (slash == std::string::npos) ? std::string(".") : source.substr(0, slash);
    return dir + "/" + relative;
#else
    std::string here = file ? file : "";
    auto slash = here.find_last_of("/\\");
    std::string dir = (slash == std::string::npos) ? std::string(".") : here.substr(0, slash);
    return dir + "/" + relative;
#endif
}

}  // namespace eve_test_path

#define EVE_DEFINE_PATH_BESIDE_SOURCE()                                            \
    static std::string pathBesideThisSource(const char *filename) {                \
        return eve_test_path::pathBesideSourceImpl(__FILE__, filename);            \
    }
