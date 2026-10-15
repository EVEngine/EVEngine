#include "ScriptTest.h"

extern const char* mouse_content;
UnitSciptTest(Mouse, mouse_content);

#define TestScript(func) \
    TEST_CASE_FIXTURE(Mouse, "Mouse." #func) { REQUIRE(callScript(#func)); }

TestScript(basic)
TestScript(relativeMode)
TestScript(visibility)
