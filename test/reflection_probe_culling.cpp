#include "common/ECS.h"
#include "graphics/ReflectionProbeCapture.h"
#include "graphics/ReflectionProbeRegistry.h"
#include "graphics/RenderSystem3D.h"
#include <zeroerr/unittest.h>
#include <limits>
using namespace eve::graphics;
TEST_CASE("graphics.reflectionProbe.pcgDistanceCulling"){
    ReflectionProbeRegistry registry;
    ReflectionProbeCapture nearProbe(nullptr),farProbe(nullptr);
    nearProbe.configure(3.f,0.f,0.f); farProbe.configure(30.f,0.f,0.f);
    registry.add(&nearProbe); registry.add(&farProbe);
    auto* camera=Camera3D::createCamera(); camera->setEye(0.f,0.f,0.f); camera->setTarget(0.f,0.f,-1.f);
    REQUIRE(registry.setMaxRenderDistance(10.f).ok());
    registry.setDistanceCullingEnabled(true);
    registry.updateCamera(camera);
    CHECK_EQ(registry.getLastDistanceCulledCount(),1);
    camera->setEye(25.f,0.f,0.f); camera->setTarget(25.f,0.f,-1.f);
    registry.updateCamera(camera);
    CHECK_EQ(registry.getLastDistanceCulledCount(),1);
    CHECK(!registry.setMaxRenderDistance(0.f).ok());
    CHECK(!registry.setMaxRenderDistance(std::numeric_limits<float>::infinity()).ok());
    CHECK_EQ(registry.getMaxRenderDistance(),10.f);
    registry.setDistanceCullingEnabled(false); registry.updateCamera(camera);
    CHECK_EQ(registry.getLastDistanceCulledCount(),0);
    ecs::DestroyEntity(camera);
}
