#include "ScriptTest.h"

extern const char* graphic_content;
UnitSciptTest(GraphicTest, graphic_content);

#define TestScript(func) \
    TEST_CASE_FIXTURE(GraphicTest, "GraphicTest." #func) { \
        CHECK(vm.callFunc(vm.findFunc(#func), vm).toBool()); \
    }

TestScript(basic)
TestScript(hasDrawText)
