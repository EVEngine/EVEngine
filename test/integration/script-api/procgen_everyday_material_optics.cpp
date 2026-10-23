#include <simplesquirrel/simplesquirrel.hpp>
#include "procgen/texture/EverydayMaterials.h"
#include "procgen/texture/PbrMaterialScript.h"
#include "zeroerr/unittest.h"
using namespace eve::procgen;

TEST_CASE("procgen.everyday.scriptOpticsMatchNative") {
    Params p;
    p.setSize(8, 8);
    auto result = EverydayMaterials::glass(p);
    REQUIRE(result.ok());
    auto    maps = std::move(result).takeValue();
    ssq::VM vm(1024, ssq::Libs::ALL);
    exposePbrMaterial(vm);
    vm.addFunc("sampleMaterial", [&maps]() -> PbrTextureSet* { return maps.get(); });
    vm.run(vm.compileSource(R"(
        local material = sampleMaterial();
        if (material.getTransmission() < 0.95 || material.getTransmission() > 0.97)
            throw "transmission mismatch";
        if (material.getIor() < 1.49 || material.getIor() > 1.51)
            throw "ior mismatch";
        if (!material.hasAllMaps()) throw "missing maps";
    )"));
    CHECK(maps->getTransmission() == .96f);
    CHECK(maps->getIor() == 1.5f);
}
