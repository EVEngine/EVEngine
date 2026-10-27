#include "physics/Box2DSimulationBackend.h"
#include <Box2D/Box2D.h>
#include "common/Exception.h"
namespace eve::physics {
namespace {
void stepBox2D(void* context, const eve::SimulationStep& step, const SimulationSettings& settings) noexcept {
    auto* world = static_cast<b2World*>(context);
    world->Step(static_cast<float>(step.delta.seconds()), settings.velocityIterations, settings.positionIterations);
}
}  // namespace
namespace detail {
std::unique_ptr<ISimulationBackend> makeBox2DSimulationBackend(b2World* world) {
    if (!world) throw eve::Exception("Physics: cannot create a simulation backend for a null world");
    return makeCallbackSimulationBackend(world, &stepBox2D, SimulationBackendKind::Cpu,
                                         SimulationDeterminism::ToleranceBounded);
}
}  // namespace detail
}  // namespace eve::physics
