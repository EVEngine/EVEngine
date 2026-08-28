#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/Runtime.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

TEST_CASE("climbing.playground.mainScriptCompilesThroughEveScriptFrontend") {
    const std::filesystem::path sourceRoot = std::filesystem::path(__FILE__).parent_path().parent_path();
    const std::filesystem::path scriptPath = sourceRoot / "examples" / "climbing-playground" / "main.nut";
    std::ifstream               input(scriptPath, std::ios::binary);
    REQUIRE(input.is_open());
    std::ostringstream source;
    source << input.rdbuf();
    CHECK(source.str().find("math.sin") == std::string::npos);

    eve::Runtime runtime(2048, ssq::Libs::ALL);
    bool         compiled = true;
    try {
        runtime.runSource("climbing_playground_sine_probe <- sin(0.0);", "climbing-playground-sine-probe.nut");
        runtime.compileSource(source.str(), "examples/climbing-playground/main.nut");
    } catch (...) {
        compiled = false;
    }
    CHECK(compiled);
}
