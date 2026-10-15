#include "ScriptTest.h"

extern const char *crowd_content;
UnitSciptTest(CrowdScriptTest, crowd_content);

TEST_CASE_FIXTURE(CrowdScriptTest, "CrowdScriptTest.basic") { REQUIRE(callScript("basic")); }

TEST_CASE_FIXTURE(CrowdScriptTest, "CrowdScriptTest.invalidIds") { REQUIRE(callScript("invalidIds")); }
