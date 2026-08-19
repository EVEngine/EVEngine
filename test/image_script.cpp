#include "ScriptTest.h"

extern const char* image_content;
UnitSciptTest(ImageScript, image_content);

#define TestScript(func) \
    TEST_CASE_FIXTURE(ImageScript, "ImageScript." #func) { \
        CHECK(vm.callFunc(vm.findFunc(#func), vm).toBool()); \
    }

TestScript(basic)
TestScript(imageDataClass)
TestScript(newImageData)
TestScript(pixelRoundTrip)
TestScript(cloneAndRotate)
TestScript(paste)
