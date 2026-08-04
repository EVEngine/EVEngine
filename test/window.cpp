#include "ScriptTest.h"

extern const char* window_content;
UnitSciptTest(WindowTest, window_content);

TEST_CASE_FIXTURE(WindowTest, "WindowTest.basicWindow") {
    CHECK(vm.callFunc(vm.findFunc("basicWindow"), vm).toBool());
}
