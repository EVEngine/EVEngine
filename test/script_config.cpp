#include "common/Runtime.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

using namespace eve;

namespace {

std::string loadStartupThroughModuleValidation() {
    const std::filesystem::path path = std::filesystem::path(EVENGINE_SOURCE_DIR) / "src" / "scripts" / "load.nut";
    std::ifstream               input(path, std::ios::binary);
    std::string                 source(std::istreambuf_iterator<char>(input), {});
    const std::string           marker = "validate_project_modules();";
    const size_t                end    = source.find(marker);
    if (end == std::string::npos) return {};
    source.resize(end + marker.size());
    return source;
}

void configureRuntime(Runtime& runtime) {
    runtime.runSource("eve <- { moduleList = [] }\n", "config-test-bootstrap.nut");
    runtime.runSource(loadStartupThroughModuleValidation(), "load.nut");
}

}  // namespace

TEST_CASE("scriptConfig.acceptsAbsentModuleRequirements") {
    Runtime runtime(1024, ssq::Libs::ALL);
    configureRuntime(runtime);
    CHECK(runtime.handle() != nullptr);
}

TEST_CASE("scriptConfig.rejectsMissingRequiredModule") {
    Runtime runtime(1024, ssq::Libs::ALL);
    configureRuntime(runtime);
    bool rejected = false;
    try {
        runtime.runSource("config.modules <- [\"missing_system\"]\nvalidate_project_modules()\n",
                          "config-validation-test.nut");
    } catch (const ScriptException& error) {
        rejected = std::string(error.what()).find("required module is missing: missing_system") != std::string::npos;
    }
    CHECK(rejected);
}

TEST_CASE("scriptConfig.validatesOptionalModuleDeclarationShape") {
    Runtime runtime(1024, ssq::Libs::ALL);
    configureRuntime(runtime);
    bool rejected = false;
    try {
        runtime.runSource("config.optionalModules <- \"audio\"\nvalidate_project_modules()\n",
                          "config-validation-test.nut");
    } catch (const ScriptException& error) {
        rejected = std::string(error.what()).find("config.optionalModules must be an array") != std::string::npos;
    }
    CHECK(rejected);
}
