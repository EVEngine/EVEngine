#include "common/BindingContracts.h"
#include "common/Module.h"
#include "common/ScriptCompiler.h"
#include "scene/NodeDesc.h"
#include "scene/Scene.h"
#include "scene/SceneHost.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <simplesquirrel/vm.hpp>

TEST_CASE("Scene.script.visibilityArchitectureMapping") {
    const auto* contract = eve::script::generatedBindingContracts().find("scene/SceneNodeRef.setVisible");
    REQUIRE(contract != nullptr);
    REQUIRE(contract->architecture.has_value());
    CHECK_EQ(contract->cppSymbol, std::string("SceneNodeRef::setVisible"));
    CHECK_EQ(contract->architecture->stateOwner, std::string("eve::scene::SceneHost"));
    CHECK_EQ(contract->architecture->writes.at(0), std::string("eve::scene::SceneNode.visible"));
    CHECK_EQ(contract->architecture->inputHash.size(), size_t(64));
    CHECK(contract->toJson().find("Scene.script.visibilityArchitectureMapping") != std::string::npos);

    ssq::VM vm(2048, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    auto* scene   = eve::scene::Scene::create();
    auto  mounted = scene->mountAs("mapping-proof", eve::scene::node("root", {eve::scene::node("target")}));
    REQUIRE(mounted.ok());
    auto script = vm.compileSource(R"SQ(
        local scene = eve.Scene()
        local ref = scene.getNodeRefAt("mapping-proof", "target")
        mapping_applied <- ref.setVisible(false)
        mapping_visible <- ref.isVisible()
        local missing = scene.getNodeRefAt("mapping-proof", "absent")
        mapping_missing <- missing.setVisible(false)
    )SQ");
    vm.run(script);
    CHECK(vm.get<bool>("mapping_applied"));
    CHECK(!vm.get<bool>("mapping_visible"));
    CHECK(!vm.get<bool>("mapping_missing"));
    auto host = scene->findHost("mapping-proof");
    REQUIRE(host.ok());
    auto target = host.value()->findById("target");
    REQUIRE(target.ok());
    CHECK(!target.value()->visible);
}
