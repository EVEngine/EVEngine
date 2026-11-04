// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "default": "script-binding",
  "module": "physics_cloth",
  "origins": [
    "physics_layered_cloth.cpp"
  ]
}
*/
// clang-format on

#include "ScriptTest.h"
#include "physics/cloth/LayeredCloth3D.h"

extern const char* physics_cloth_content;
class LayeredClothScriptTest : public ScriptTest {
public:
    LayeredClothScriptTest() : ScriptTest(physics_cloth_content) {}
};
TEST_CASE_FIXTURE(LayeredClothScriptTest, "PhysicsLayeredCloth.scriptBindings") { REQUIRE(callScript("layered")); }
