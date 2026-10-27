// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "default": "script-render",
  "module": "common",
  "modules": [
    "common",
    "window"
  ],
  "origins": [
    "graphic.cpp",
    "window.cpp"
  ],
  "cases": {
    "script-binding": [
      "WindowTest.settingsFields"
    ]
  }
}
*/
// clang-format on

// Related integration contracts share one translation unit to amortize header parsing.

// Contract section: graphic
#include "PrivateTestScope.h"
#include "zeroerr/assert.h"

#include "ScriptTest.h"
extern const char* graphic_content;
// graphic.cpp
namespace test_graphic {
using namespace eve;


class GraphicTest : public ScriptTest {
public:
    GraphicTest() : ScriptTest(graphic_content) {}
};

TEST_CASE_FIXTURE(GraphicTest, "GraphicTest.bindings") {
    auto* const fixture = this;
    SUB_CASE("basic") { REQUIRE(fixture->callScript("basic")); };
    SUB_CASE("hasDrawText") { REQUIRE(fixture->callScript("hasDrawText")); };
}
}  // namespace test_graphic

// Contract section: window
#include "PrivateTestScope.h"

extern const char* window_content;
// window.cpp
namespace test_window {
using namespace eve;


class WindowTest : public ScriptTest {
public:
    WindowTest() : ScriptTest(window_content) {}
};

TEST_CASE_FIXTURE(WindowTest, "WindowTest.basicWindow") { REQUIRE(callScript("basicWindow")); }

TEST_CASE_FIXTURE(WindowTest, "WindowTest.settingsFields") { REQUIRE(callScript("settingsFields")); }
}  // namespace test_window
