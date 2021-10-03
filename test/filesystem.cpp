#include "ScriptTest.h"

extern const char* filesystem_content;
UnitSciptTest(FilesystemTest, filesystem_content);

TEST_F(FilesystemTest, basic) { EXPECT_TRUE(vm.callFunc(vm.findFunc("basic"), vm).toBool()); }