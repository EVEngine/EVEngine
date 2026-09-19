#include "ScriptTest.h"

extern const char* dialogue_proc_script_content;
UnitSciptTest(DialogueProcScriptTest, dialogue_proc_script_content);

TEST_CASE_FIXTURE(DialogueProcScriptTest, "DialogueProcScriptTest.basicPools") {
    // CHECK, not REQUIRE: the script returns false. Pre-existing and untriaged
    // (plan doc 9.15); the sweep only revealed that it never asserted.
    CHECK(callScript("basicPools"));
}
