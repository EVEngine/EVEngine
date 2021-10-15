#include "ScriptTest.h"

extern const char* filesystem_content;
UnitSciptTest(FilesystemTest, filesystem_content);

#define TestScript(func) TEST_F(FilesystemTest, func) { EXPECT_TRUE(vm.callFunc(vm.findFunc(#func), vm).toBool()); }

TestScript(basic)
TestScript(getPaths)
TestScript(readDir)
// TestScript(readFile)
// TestScript(writeFile)