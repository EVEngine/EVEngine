#include "ScriptTest.h"

extern const char *crowd_content;
UnitSciptTest(CrowdScriptTest, crowd_content);

TEST_CASE_FIXTURE(CrowdScriptTest, "CrowdScriptTest.basic") {
    CHECK(vm.callFunc(vm.findFunc("basic"), vm).toBool());
}

TEST_CASE_FIXTURE(CrowdScriptTest, "CrowdScriptTest.invalidIds") {
    CHECK(vm.callFunc(vm.findFunc("invalidIds"), vm).toBool());
}
