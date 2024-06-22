#include "ScriptTest.h"

extern const char* graphic_content;
UnitSciptTest(GraphicTest, graphic_content);

#define TestScript(func) \
    TEST_F(GraphicTest, func) { EXPECT_TRUE(vm.callFunc(vm.findFunc(#func), vm).toBool()); }

TestScript(basic)