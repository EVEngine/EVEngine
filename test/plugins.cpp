#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "plugins/Plugins.h"

#include <string>

using eve::plugins::Plugins;

TEST_CASE("plugins.load.rejectsEmptyPath") {
    Plugins plugins;
    CHECK_THROWS((plugins.load(""), false));
    CHECK_EQ(plugins.isLoaded(""), false);
}

TEST_CASE("plugins.load.rejectsMissingLibrary") {
    Plugins plugins;
    const std::string missing = "evengine-plugin-that-does-not-exist.dll";
    CHECK_THROWS((plugins.load(missing), false));
    CHECK_EQ(plugins.isLoaded(missing), false);
}

TEST_CASE("plugins.load.closesLibraryWithoutEntrypoint") {
    Plugins plugins;
#if defined(_WIN32)
    const std::string library = "kernel32.dll";
#elif defined(__APPLE__)
    const std::string library = "/usr/lib/libSystem.B.dylib";
#else
    const std::string library = "libm.so.6";
#endif
    CHECK_THROWS((plugins.load(library), false));
    CHECK_EQ(plugins.isLoaded(library), false);
    CHECK_EQ(plugins.unload(library), false);
}

TEST_CASE("plugins.unload.unknownPathIsNoop") {
    Plugins plugins;
    CHECK_EQ(plugins.unload("not-loaded"), false);
}
