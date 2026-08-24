#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/Module.h"

#include <simplesquirrel/simplesquirrel.hpp>

TEST_CASE("procgen.script.reflects_schema_and_generates_from_dynamic_values") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        procgen <- eve.Procgen();
        algorithm <- "cave.cellular";
        params <- procgen.newParams();
        defaultsApplied <- procgen.applyAlgorithmDefaults(algorithm, params);
        paramCount <- procgen.getAlgorithmParamCount(algorithm);
        fieldsComplete <- true;
        fillIndex <- -1;
        for (local i = 0; i < paramCount; ++i) {
            if (procgen.getAlgorithmParamKey(algorithm, i) == "" ||
                procgen.getAlgorithmParamLabel(algorithm, i) == "" ||
                procgen.getAlgorithmParamKind(algorithm, i) == "") fieldsComplete = false;
            if (procgen.getAlgorithmParamKey(algorithm, i) == "fill") fillIndex = i;
        }
        params.setInt("width", 40);
        params.setInt("height", 24);
        params.setInt("seed", 77);
        params.setFloat("fill", 0.38);
        grid <- procgen.generate(algorithm, params);
        width <- grid == null ? 0 : grid.getWidth();
        height <- grid == null ? 0 : grid.getHeight();
        fillKind <- fillIndex < 0 ? "" : procgen.getAlgorithmParamKind(algorithm, fillIndex);
        fillMin <- fillIndex < 0 ? -1.0 : procgen.getAlgorithmParamMinimum(algorithm, fillIndex);
        fillMax <- fillIndex < 0 ? -1.0 : procgen.getAlgorithmParamMaximum(algorithm, fillIndex);
        wfcChoiceCount <- procgen.getAlgorithmParamChoiceCount("wfc.simple", 3);
        wfcFirstChoice <- procgen.getAlgorithmParamChoice("wfc.simple", 3, 0);
        algorithmSchema <- procgen.getAlgorithmSchema(algorithm);
        genericAlgorithmId <- algorithmSchema.getId();
        textureParams <- procgen.newParams();
        textureDefaultsApplied <- procgen.applyTextureRecipeDefaults("tex.rock", textureParams);
        textureSchema <- procgen.getTextureRecipeSchema("tex.rock");
        textureCategory <- textureSchema.getCategory();
        textureScaleFound <- false;
        for (local i = 0; i < textureSchema.getParamCount(); ++i)
            if (textureSchema.getParamKey(i) == "scale") textureScaleFound = true;
        pbrParams <- procgen.newParams();
        pbrParams.setSize(16, 12);
        pbrDefaultsApplied <- procgen.applyPbrRecipeDefaults("pbr.rock", pbrParams);
        pbrSchema <- procgen.getPbrRecipeSchema("pbr.rock");
        pbrMetallicFound <- false;
        pbrNormalFound <- false;
        for (local i = 0; i < pbrSchema.getParamCount(); ++i) {
            local key = pbrSchema.getParamKey(i);
            if (key == "metallic") pbrMetallicFound = true;
            if (key == "normalStrength") pbrNormalFound = true;
        }
        pbrSet <- procgen.generatePbrMaterial("pbr.rock", pbrParams);
        pbrHasAllMaps <- pbrSet != null && pbrSet.hasAllMaps();
        pbrAlbedoWidth <- pbrSet == null ? 0 : pbrSet.getAlbedo().getWidth();
        pbrNormalWidth <- pbrSet == null ? 0 : pbrSet.getNormal().getWidth();
        pbrHeightWidth <- pbrSet == null ? 0 : pbrSet.getHeight().getWidth();
        if (pbrSet != null) pbrSet.destroy();
    )"));

    CHECK(vm.find("defaultsApplied").toBool());
    CHECK_GT(vm.find("paramCount").toInt(), 3);
    CHECK(vm.find("fieldsComplete").toBool());
    CHECK_EQ(vm.find("fillKind").toString(), std::string("float"));
    CHECK_EQ(vm.find("fillMin").toFloat(), 0.f);
    CHECK_EQ(vm.find("fillMax").toFloat(), 1.f);
    CHECK_EQ(vm.find("width").toInt(), 40);
    CHECK_EQ(vm.find("height").toInt(), 24);
    CHECK_EQ(vm.find("wfcChoiceCount").toInt(), 3);
    CHECK_EQ(vm.find("wfcFirstChoice").toString(), std::string("dungeon"));
    CHECK_EQ(vm.find("genericAlgorithmId").toString(), std::string("cave.cellular"));
    CHECK(vm.find("textureDefaultsApplied").toBool());
    CHECK_EQ(vm.find("textureCategory").toString(), std::string("Texture"));
    CHECK(vm.find("textureScaleFound").toBool());
    CHECK(vm.find("pbrDefaultsApplied").toBool());
    CHECK(vm.find("pbrMetallicFound").toBool());
    CHECK(vm.find("pbrNormalFound").toBool());
    CHECK(vm.find("pbrHasAllMaps").toBool());
    CHECK_EQ(vm.find("pbrAlbedoWidth").toInt(), 16);
    CHECK_EQ(vm.find("pbrNormalWidth").toInt(), 16);
    CHECK_EQ(vm.find("pbrHeightWidth").toInt(), 16);
}
