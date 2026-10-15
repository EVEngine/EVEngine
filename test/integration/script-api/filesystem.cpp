#include "ScriptTest.h"

extern const char* filesystem_content;
UnitSciptTest(FilesystemTest, filesystem_content);

#define TestScript(func) \
    TEST_CASE_FIXTURE(FilesystemTest, "FilesystemTest." #func) { \
        CHECK(vm.callFunc(vm.findFunc(#func), vm).toBool()); \
    }

TestScript(basic)
TestScript(getPaths)
TestScript(readDir)
TestScript(textRoundTrip)
TestScript(atomicTextRoundTrip)
// TestScript(readFile)
// TestScript(writeFile)
