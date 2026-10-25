// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "default": "script-render",
  "cases": {
    "script-binding": [
      "WindowTest.settingsFields"
    ]
  },
  "module": "common",
  "modules": [
    "common",
    "window"
  ],
  "origins": [
    "graphic.cpp",
    "window.cpp"
  ],
  "generated": [
    "GraphicTest."
  ]
}
*/
// clang-format on

#include "zeroerr/assert.h"

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_graphic)
#include "PrivateTestScope.h"
#include "ScriptTest.h"
extern const char* graphic_content;
// graphic.cpp
namespace test_graphic {
using namespace eve;


UnitSciptTest(GraphicTest, graphic_content);

#define TestScript(func) \
    TEST_CASE_FIXTURE(GraphicTest, "GraphicTest." #func) { REQUIRE(callScript(#func)); }

// clang-format off
TestScript(basic)
TestScript(hasDrawText)
// clang-format on
}  // namespace test_graphic
#undef TestScript
#endif
#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_window)
#include "PrivateTestScope.h"
#include "ScriptTest.h"
extern const char* window_content;
// window.cpp
namespace test_window {
using namespace eve;


UnitSciptTest(WindowTest, window_content);

TEST_CASE_FIXTURE(WindowTest, "WindowTest.basicWindow") { REQUIRE(callScript("basicWindow")); }

TEST_CASE_FIXTURE(WindowTest, "WindowTest.settingsFields") { REQUIRE(callScript("settingsFields")); }
}  // namespace test_window
#endif
