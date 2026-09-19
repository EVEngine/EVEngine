#include "ScriptTest.h"

extern const char* profiler_content;
UnitSciptTest(ProfilerScriptTest, profiler_content);

TEST_CASE_FIXTURE(ProfilerScriptTest, "ProfilerScriptTest.capture") {
    // CHECK, not REQUIRE: raises at test/profiler.nut:6, `p.begin("scriptScope")`
    // -- the binding expects an INSTANCE, the script passes a STRING (9.15).
    CHECK(callScript("captureWorks"));
}
