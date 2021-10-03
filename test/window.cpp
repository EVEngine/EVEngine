#include "ScriptTest.h"

extern const char* window_content;
UnitSciptTest(WindowTest, window_content);

TEST_F(WindowTest, basicWindow) { EXPECT_TRUE(vm.callFunc(vm.findFunc("basicWindow"), vm).toBool()); }
