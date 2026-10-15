#include "ScriptTest.h"

extern const char* window_content;
UnitSciptTest(WindowTest, window_content);

TEST_CASE_FIXTURE(WindowTest, "WindowTest.basicWindow") { REQUIRE(callScript("basicWindow")); }

TEST_CASE_FIXTURE(WindowTest, "WindowTest.settingsFields") { REQUIRE(callScript("settingsFields")); }
