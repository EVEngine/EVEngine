#include "ScriptTest.h"

extern const char* mouse_content;
UnitSciptTest(Mouse, mouse_content);

#define TestScript(func) TEST_F(Mouse, func) { EXPECT_TRUE(vm.callFunc(vm.findFunc(#func), vm).toBool()); }

TestScript(basic)
