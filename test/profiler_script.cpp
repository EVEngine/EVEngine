#include "ScriptTest.h"

extern const char* profiler_content;
UnitSciptTest(ProfilerScriptTest, profiler_content);

TEST_CASE_FIXTURE(ProfilerScriptTest, "ProfilerScriptTest.capture") { REQUIRE(callScript("captureWorks")); }
