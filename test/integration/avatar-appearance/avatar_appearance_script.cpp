#include "common/Module.h"
#include "zeroerr/unittest.h"

#include <cmath>
#include <simplesquirrel/simplesquirrel.hpp>

TEST_CASE("avatar.appearance.scriptBoundaryUsesCheckedResults") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        avatarModule <- eve.Avatar();
        appearance <- eve.AvatarAppearance();
        configured <- appearance.configure({schema="eve.appearance.definition",version=1,profile="test.v1",
            parameters=[{id="eye",min=0.0,max=1.0,["default"]=0.0}],drivers=[],slots=[],items=[]}).ok;
        applied <- appearance.apply({parameters={eye=0.7}}).ok;
        saved <- appearance.snapshot();
        rejected <- !appearance.apply({parameters={eye=2.0}}).ok;
        restored <- appearance.restore(saved.value).ok;
        value <- appearance.snapshot().value.parameters.eye;
        graphicsModule <- eve.Graphics();
        sourceMaterial <- graphicsModule.newMaterial();
        targetMaterial <- graphicsModule.newMaterial();
        sourceMaterial.setTint(0.2, 0.3, 0.4, 1.0);
        targetMaterial.setRoughness(0.7);
        copiedPbr <- targetMaterial.copyPbrSurfaceFrom(sourceMaterial).ok;
        targetRoughness <- targetMaterial.getRoughness();
    )"));
    CHECK(vm.find("configured").toBool());
    CHECK(vm.find("applied").toBool());
    CHECK(vm.find("rejected").toBool());
    CHECK(vm.find("restored").toBool());
    CHECK(std::abs(vm.find("value").toFloat() - 0.7f) < 1e-6f);
    CHECK(vm.find("copiedPbr").toBool());
    CHECK(std::abs(vm.find("targetRoughness").toFloat() - 0.7f) < 1e-6f);
}
