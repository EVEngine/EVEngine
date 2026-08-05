#include "ScriptTest.h"

extern const char* timer_content;
UnitSciptTest(TimerTest, timer_content);

TEST_CASE_FIXTURE(TimerTest, "TimerTest.basic") {
    CHECK(vm.callFunc(vm.findFunc("basic"), vm).toBool());
}
