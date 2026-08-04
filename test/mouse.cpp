#include "ScriptTest.h"

extern const char* mouse_content;
UnitSciptTest(Mouse, mouse_content);

#define TestScript(func) \
    TEST_CASE_FIXTURE(Mouse, "Mouse." #func) { \
        CHECK(vm.callFunc(vm.findFunc(#func), vm).toBool()); \
    }

TestScript(basic)
