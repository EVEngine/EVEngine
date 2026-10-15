#include "ScriptTest.h"

extern const char *decal_content;
UnitSciptTest(DecalScriptTest, decal_content);

TEST_CASE_FIXTURE(DecalScriptTest, "DecalScriptTest.basic") {
    CHECK(vm.callFunc(vm.findFunc("basic"), vm).toBool());
}
