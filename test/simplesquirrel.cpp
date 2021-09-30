#include "ScriptTest.h"

extern const char* simplesquirrel_content;
UnitSciptTest(SimpleSquirrelTest, simplesquirrel_content);

TEST_F(SimpleSquirrelTest, ExportClass) {
  EXPECT_TRUE(vm.callFunc(vm.findFunc("exportClass"), vm).toBool());
}

