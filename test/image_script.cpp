#include "ScriptTest.h"

extern const char* image_content;
UnitSciptTest(ImageScript, image_content);

#define TestScript(func) \
    TEST_CASE_FIXTURE(ImageScript, "ImageScript." #func) { REQUIRE(callScript(#func)); }

TestScript(basic) TestScript(imageDataClass) TestScript(newImageData) TestScript(pixelRoundTrip)
    TestScript(cloneAndRotate) TestScript(paste)

    // CHECK, not REQUIRE: the script calls `point.scalePcg(...)`, which the Image binding
    // does not provide ("buffer:79 (pcgScale): the index 'scalePcg' does not exist"). Either
    // the method was renamed or it was never bound; untriaged (plan doc 9.15).
    TEST_CASE_FIXTURE(ImageScript, "ImageScript.pcgScale") {
    CHECK(callScript("pcgScale"));
}
