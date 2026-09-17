#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "physics/backend/SimulationBackend.h"
#include "physics/cloth/Cloth.h"
#include "physics/cloth/Cloth3D.h"
#include "physics/rope/Rope3D.h"
#include "physics/softbody/SoftBody3D.h"
#include "fluids/Fluids.h"

#include <cstdint>
#include <memory>
#include <utility>

namespace {

using eve::physics::ISimulationBackend;
using eve::physics::SimulationObservation;
using eve::physics::SimulationSettings;

void checkCpuSatelliteContract(ISimulationBackend& backend) {
    const eve::Duration duration = eve::Duration::fromNanoseconds(16666667);

    SimulationSettings invalidSettings;
    invalidSettings.subStepCount = 0;
    auto invalid = backend.step({eve::SimulationTick{1}, duration}, invalidSettings);
    CHECK(!invalid.ok());
    CHECK_EQ(backend.observation().stepCount, std::uint64_t{0});
    CHECK(backend.observation().lastTick.isZero());

    auto first = backend.step({eve::SimulationTick{1}, duration}, SimulationSettings{});
    REQUIRE(first.ok());
    const SimulationObservation afterFirst = backend.observation();
    CHECK_EQ(afterFirst.stepCount, std::uint64_t{1});
    CHECK(afterFirst.lastTick == eve::SimulationTick{1});
    CHECK_EQ(afterFirst.simulatedDuration, duration);

    auto duplicate = backend.step({eve::SimulationTick{1}, duration}, SimulationSettings{});
    CHECK(!duplicate.ok());
    CHECK_EQ(backend.observation().stepCount, afterFirst.stepCount);
    CHECK(backend.observation().lastTick == afterFirst.lastTick);
    CHECK_EQ(backend.observation().simulatedDuration, afterFirst.simulatedDuration);

    auto second = backend.step({eve::SimulationTick{2}, duration}, SimulationSettings{});
    REQUIRE(second.ok());
    CHECK_EQ(backend.observation().stepCount, std::uint64_t{2});
    CHECK(backend.observation().lastTick == eve::SimulationTick{2});
    CHECK_EQ(backend.observation().simulatedDuration,
             eve::Duration::fromNanoseconds(duration.nanoseconds() * 2));
}

}  // namespace

TEST_CASE("physics.backend.cpuCloth2DConforms") {
    eve::physics::Cloth cloth(3, 3, 0.5f, 0.f, 1.f);
    checkCpuSatelliteContract(cloth);
}

TEST_CASE("physics.backend.cpuCloth3DConforms") {
    eve::physics::Cloth3D cloth(3, 3, 0.5f, 0.f, 1.f, 0.f);
    checkCpuSatelliteContract(cloth);
}

TEST_CASE("physics.backend.cpuRope3DConforms") {
    eve::physics::Rope3D rope(4, 0.f, 1.f, 0.f, 1.5f, 1.f, 0.f);
    checkCpuSatelliteContract(rope);
}

TEST_CASE("physics.backend.cpuSoftBody3DConforms") {
    auto created = eve::physics::SoftBody3D::create(2, 2, 2, 0.5f, 0.f, 1.f, 0.f);
    REQUIRE(created.ok());
    std::unique_ptr<eve::physics::SoftBody3D> body = std::move(created).takeValue();
    checkCpuSatelliteContract(*body);
}

TEST_CASE("physics.backend.cpuSurfaceFluidConforms") {
    eve::fluids::FluidParams params;
    eve::fluids::FluidSimulator fluid(64, params, false);
    checkCpuSatelliteContract(fluid);
}
