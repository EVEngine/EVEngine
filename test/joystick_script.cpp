#include "ScriptTest.h"

extern const char* joystick_content;
UnitSciptTest(JoystickScript, joystick_content);

#define TestScript(func) \
    TEST_CASE_FIXTURE(JoystickScript, "JoystickScript." #func) { \
        CHECK(vm.callFunc(vm.findFunc(#func), vm).toBool()); \
    }

TestScript(basic)
TestScript(padClass)
TestScript(query)
TestScript(axes)
