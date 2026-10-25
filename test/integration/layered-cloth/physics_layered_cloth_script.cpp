#include "ScriptTest.h"
#include "physics/cloth/LayeredCloth3D.h"

extern const char* physics_cloth_content;
class LayeredClothScriptTest : public ScriptTest {
public:
    LayeredClothScriptTest() : ScriptTest(physics_cloth_content) {}
};
TEST_CASE_FIXTURE(LayeredClothScriptTest, "PhysicsLayeredCloth.scriptBindings") { REQUIRE(callScript("layered")); }
