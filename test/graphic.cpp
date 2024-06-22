#include "ScriptTest.h"

extern const char* graphics_content;
UnitSciptTest(GraphicTest, graphics_content);

#define TestScript(func) \
    TEST_F(GraphicTest, func) { EXPECT_TRUE(vm.callFunc(vm.findFunc(#func), vm).toBool()); }

TestScript(basic)