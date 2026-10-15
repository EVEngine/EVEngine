#include "ScriptTest.h"

extern const char* image_content;
UnitSciptTest(ImageScript, image_content);

// One case per line, deliberately, and spelled out rather than via a wrapper macro:
// zeroerr registers each case with getTestSet(type).insert(tc) into a std::set whose
// ordering key is only (file, line) -- TestCase::operator< compares nothing else (see
// external/zeroerr/src/unittest.cpp). Two TEST_CASE* invocations on one source line
// therefore compare equivalent and the second is dropped silently: no error, no
// warning, the case simply never runs. A bare `Wrapper(x)` invocation has no trailing
// semicolon, so clang-format packs several of them onto one line and silently
// re-drops the cases; full definitions keep their own line.
// scripts/check_test_case_lines.py guards this.
TEST_CASE_FIXTURE(ImageScript, "ImageScript.basic") { REQUIRE(callScript("basic")); }

TEST_CASE_FIXTURE(ImageScript, "ImageScript.imageDataClass") { REQUIRE(callScript("imageDataClass")); }

TEST_CASE_FIXTURE(ImageScript, "ImageScript.newImageData") { REQUIRE(callScript("newImageData")); }

TEST_CASE_FIXTURE(ImageScript, "ImageScript.pixelRoundTrip") { REQUIRE(callScript("pixelRoundTrip")); }

TEST_CASE_FIXTURE(ImageScript, "ImageScript.cloneAndRotate") { REQUIRE(callScript("cloneAndRotate")); }

TEST_CASE_FIXTURE(ImageScript, "ImageScript.paste") { REQUIRE(callScript("paste")); }

TEST_CASE_FIXTURE(ImageScript, "ImageScript.pcgScale") { REQUIRE(callScript("pcgScale")); }
