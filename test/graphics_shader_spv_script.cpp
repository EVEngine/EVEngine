#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
#include "common/Module.h"
#include "filesystem/Filesystem.h"
#include <simplesquirrel/simplesquirrel.hpp>

TEST_CASE("graphics.shaderSpvScript.failuresAreStructuredBeforeUpload") {
    auto* fs = eve::filesystem::Filesystem::create();
    REQUIRE(fs->setIdentity("eve_shader_spv_binding_test", true));
    REQUIRE(fs->setupWriteDirectory());
    auto written = fs->writeTextAtomic("malformed.spv", "This is not a SPIR-V module.");
    REQUIRE(written.ok());
    ssq::VM vm(2048, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        gfx <- eve.Graphics();
        empty <- gfx.loadMeshShaderSpv("", "");
        missing <- gfx.loadMeshShaderSpv("", "absent-shader.spv");
        malformed <- gfx.loadMeshShaderSpv("", "malformed.spv");
        badVertex <- gfx.loadMeshShaderSpv("malformed.spv", "absent-shader.spv");
    )"));
    for (const char* name : {"empty", "missing", "malformed", "badVertex"}) {
        auto result = vm.find(name).toTable();
        CHECK(!result.get<bool>("ok"));
        CHECK(result.get<ssq::Array>("diagnostics").size() > 0);
    }
    REQUIRE(fs->remove("malformed.spv"));
}
