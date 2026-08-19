#include "ScriptTest.h"

extern const char* dialogue_proc_script_content;
UnitSciptTest(DialogueProcScriptTest, dialogue_proc_script_content);

TEST_CASE_FIXTURE(DialogueProcScriptTest, "DialogueProcScriptTest.basicPools") {
    CHECK(vm.callFunc(vm.findFunc("basicPools"), vm).toBool());
}
