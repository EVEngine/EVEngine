#include "ScriptTest.h"

extern const char* i18n_content;
UnitSciptTest(I18nScriptTest, i18n_content);

#define TestScript(func) \
    TEST_CASE_FIXTURE(I18nScriptTest, "I18nScriptTest." #func) { \
        CHECK(vm.callFunc(vm.findFunc(#func), vm).toBool()); \
    }

TestScript(basic)
