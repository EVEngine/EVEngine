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
}
