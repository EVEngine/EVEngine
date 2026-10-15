#include "ScriptTest.h"
#include "physics/cloth/Cloth3D.h"
#include "physics/cloth/ClothModel.h"

#include <array>
#include <cmath>

extern const char* physics_cloth_content;
UnitSciptTest(PhysicsClothScriptTest, physics_cloth_content);

TEST_CASE_FIXTURE(PhysicsClothScriptTest, "PhysicsCloth.scriptBindings") {
    CHECK(vm.callFunc(vm.findFunc("basic"), vm).toBool());
}
