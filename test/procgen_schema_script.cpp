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
        paramsResult <- procgen.newParams();
        paramsResultOk <- paramsResult.ok;
        params <- paramsResult.ok ? paramsResult.value : null;
        defaultsResult <- params == null ? { ok = false } :
            procgen.applyAlgorithmDefaults(algorithm, params);
        defaultsApplied <- defaultsResult.ok;
        paramCount <- procgen.getAlgorithmParamCount(algorithm);
        fieldsComplete <- true;
        fillIndex <- -1;
        for (local i = 0; i < paramCount; ++i) {
            if (procgen.getAlgorithmParamKey(algorithm, i) == "" ||
                procgen.getAlgorithmParamLabel(algorithm, i) == "" ||
                procgen.getAlgorithmParamKind(algorithm, i) == "") fieldsComplete = false;
            if (procgen.getAlgorithmParamKey(algorithm, i) == "fill") fillIndex = i;
        }
        widthSet <- params == null ? { ok = false } : params.setInt("width", 40);
        heightSet <- params == null ? { ok = false } : params.setInt("height", 24);
        seedSet <- params == null ? { ok = false } : params.setInt("seed", 77);
        fillSet <- params == null ? { ok = false } : params.setFloat("fill", 0.38);
        widthSetOk <- widthSet.ok;
        heightSetOk <- heightSet.ok;
        seedSetOk <- seedSet.ok;
        fillSetOk <- fillSet.ok;
        gridResult <- params == null ? { ok = false, hasValue = false } :
            procgen.generate(algorithm, params);
        gridOk <- gridResult.ok && gridResult.hasValue &&
            gridResult.value.ownership() == "owned" && !gridResult.value.isStale();
        width <- params == null ? 0 : params.getWidth();
        height <- params == null ? 0 : params.getHeight();
        fillKind <- fillIndex < 0 ? "" : procgen.getAlgorithmParamKind(algorithm, fillIndex);
        fillMin <- fillIndex < 0 ? -1.0 : procgen.getAlgorithmParamMinimum(algorithm, fillIndex);
        fillMax <- fillIndex < 0 ? -1.0 : procgen.getAlgorithmParamMaximum(algorithm, fillIndex);
        wfcChoiceCount <- procgen.getAlgorithmParamChoiceCount("wfc.simple", 3);
        wfcFirstChoice <- procgen.getAlgorithmParamChoice("wfc.simple", 3, 0);
        algorithmSchemaResult <- procgen.getAlgorithmSchema(algorithm);
        algorithmSchemaOk <- algorithmSchemaResult.ok && algorithmSchemaResult.hasValue;
        algorithmSchema <- algorithmSchemaOk ? algorithmSchemaResult.value : null;
        genericAlgorithmId <- algorithmSchema == null ? "" : algorithmSchema.getId();
        textureParamsResult <- procgen.newParams();
        textureParamsResultOk <- textureParamsResult.ok;
        textureParams <- textureParamsResult.ok ? textureParamsResult.value : null;
        textureDefaultsResult <- textureParams == null ? { ok = false } :
            procgen.applyTextureRecipeDefaults("tex.rock", textureParams);
        textureDefaultsApplied <- textureDefaultsResult.ok;
        textureSchemaResult <- procgen.getTextureRecipeSchema("tex.rock");
        textureSchemaOk <- textureSchemaResult.ok && textureSchemaResult.hasValue;
        textureSchema <- textureSchemaOk ? textureSchemaResult.value : null;
        textureCategory <- textureSchema == null ? "" : textureSchema.getCategory();
        textureScaleFound <- false;
        for (local i = 0; textureSchema != null && i < textureSchema.getParamCount(); ++i)
            if (textureSchema.getParamKey(i) == "scale") textureScaleFound = true;
        pbrParamsResult <- procgen.newParams();
        pbrParamsResultOk <- pbrParamsResult.ok;
        pbrParams <- pbrParamsResult.ok ? pbrParamsResult.value : null;
        sizeSet <- pbrParams == null ? { ok = false } : pbrParams.setSize(16, 12);
        pbrDefaultsResult <- pbrParams == null ? { ok = false } :
            procgen.applyPbrRecipeDefaults("pbr.rock", pbrParams);
        pbrDefaultsApplied <- pbrDefaultsResult.ok && sizeSet.ok;
        pbrSchemaResult <- procgen.getPbrRecipeSchema("pbr.rock");
        pbrSchemaOk <- pbrSchemaResult.ok && pbrSchemaResult.hasValue;
        pbrSchema <- pbrSchemaOk ? pbrSchemaResult.value : null;
        pbrMetallicFound <- false;
        pbrNormalFound <- false;
        for (local i = 0; pbrSchema != null && i < pbrSchema.getParamCount(); ++i) {
            local key = pbrSchema.getParamKey(i);
            if (key == "metallic") pbrMetallicFound = true;
            if (key == "normalStrength") pbrNormalFound = true;
        }
        pbrSetResult <- pbrParams == null ? { ok = false, hasValue = false } :
            procgen.generatePbrMaterial("pbr.rock", pbrParams);
        pbrResultOk <- pbrSetResult.ok && pbrSetResult.hasValue &&
            pbrSetResult.value.ownership() == "owned" && !pbrSetResult.value.isStale();
        meshParamsResult <- procgen.newParams();
        meshParamsResultOk <- meshParamsResult.ok;
        meshParams <- meshParamsResult.ok ? meshParamsResult.value : null;
        meshDefaultsResult <- meshParams == null ? { ok = false } :
            procgen.applyMeshRecipeDefaults("mesh.fence", meshParams);
        meshDefaultsApplied <- meshDefaultsResult.ok;
        meshSchemaResult <- procgen.getMeshRecipeSchema("mesh.fence");
        meshSchemaOk <- meshSchemaResult.ok && meshSchemaResult.hasValue;
        meshSchema <- meshSchemaOk ? meshSchemaResult.value : null;
        meshSegmentsFound <- false;
        for (local i = 0; meshSchema != null && i < meshSchema.getParamCount(); ++i)
            if (meshSchema.getParamKey(i) == "segments") meshSegmentsFound = true;
        meshBuildResult <- meshParams == null ? { ok = false, hasValue = false } :
            procgen.buildMesh("mesh.fence", meshParams);
        meshBuildOk <- meshBuildResult.ok && meshBuildResult.hasValue &&
            meshBuildResult.value.ownership() == "owned" && !meshBuildResult.value.isStale();
    )"));

    CHECK(vm.find("paramsResultOk").toBool());
    CHECK(vm.find("defaultsApplied").toBool());
    CHECK(vm.find("widthSetOk").toBool());
    CHECK(vm.find("heightSetOk").toBool());
    CHECK(vm.find("seedSetOk").toBool());
    CHECK(vm.find("fillSetOk").toBool());
    CHECK(vm.find("gridOk").toBool());
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
    CHECK(vm.find("textureParamsResultOk").toBool());
    CHECK(vm.find("textureDefaultsApplied").toBool());
    CHECK_EQ(vm.find("textureCategory").toString(), std::string("Texture"));
    CHECK(vm.find("textureScaleFound").toBool());
    CHECK(vm.find("pbrParamsResultOk").toBool());
    CHECK(vm.find("pbrDefaultsApplied").toBool());
    CHECK(vm.find("pbrMetallicFound").toBool());
    CHECK(vm.find("pbrNormalFound").toBool());
    CHECK(vm.find("pbrResultOk").toBool());
    CHECK(vm.find("meshParamsResultOk").toBool());
    CHECK(vm.find("meshDefaultsApplied").toBool());
    CHECK(vm.find("meshSegmentsFound").toBool());
    CHECK(vm.find("meshBuildOk").toBool());
}
