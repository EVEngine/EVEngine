#include "zeroerr/unittest.h"

#include "physics/backend/SimulationBackend.h"

#include <cstdint>
#include <exception>

using namespace eve::physics;

namespace {

eve::SimulationStep at(std::uint64_t tick, std::int64_t nanoseconds = 16666667) {
    return {eve::SimulationTick{tick}, eve::Duration::fromNanoseconds(nanoseconds)};
}

void expectUnchanged(const SimulationObservation& before, const SimulationObservation& after) {
    CHECK_EQ(after.stepCount, before.stepCount);
    CHECK(after.lastTick == before.lastTick);
    CHECK(after.simulatedDuration == before.simulatedDuration);
    CHECK_EQ(after.simulatedSeconds, before.simulatedSeconds);
    CHECK_EQ(after.lastDeltaSeconds, before.lastDeltaSeconds);
}

}  // namespace

TEST_CASE("physics.backend.factory rejects missing solver state and callback") {
    bool rejectedWorld = false;
    try {
        [[maybe_unused]] auto backend = detail::makeBox2DSimulationBackend(nullptr);
    } catch (const std::exception&) {
        rejectedWorld = true;
    }
    REQUIRE(rejectedWorld);

    bool rejectedCallback = false;
    try {
        [[maybe_unused]] auto backend = detail::makeCallbackSimulationBackend(
            nullptr, nullptr, SimulationBackendKind::Cpu, SimulationDeterminism::ToleranceBounded);
    } catch (const std::exception&) {
        rejectedCallback = true;
    }
    CHECK(rejectedCallback);
}

TEST_CASE("physics.backend.step records only completed logical steps") {
    auto backend = detail::makeMockAcceleratorBackend();
    CHECK(backend->kind() == SimulationBackendKind::MockAccelerator);
    CHECK(backend->determinism() == SimulationDeterminism::ToleranceBounded);
    expectUnchanged({}, backend->observation());

    const auto first = at(1);
    REQUIRE(backend->step(first, SimulationSettings{}).ok());
    const auto firstObservation = backend->observation();
    CHECK_EQ(firstObservation.stepCount, std::uint64_t{1});
    CHECK(firstObservation.lastTick == first.tick);
    CHECK(firstObservation.simulatedDuration == first.delta);
    CHECK_EQ(firstObservation.simulatedSeconds, first.delta.seconds());

    const auto second = at(3, 10000000);
    CHECK(backend->step(second, SimulationSettings{}).ok());
    const auto secondObservation = backend->observation();
    CHECK_EQ(secondObservation.stepCount, std::uint64_t{2});
    CHECK(secondObservation.lastTick == second.tick);
    CHECK(secondObservation.simulatedDuration == eve::Duration::fromNanoseconds(26666667));
    CHECK_EQ(secondObservation.lastDeltaSeconds, 0.01f);
    CHECK_EQ(firstObservation.stepCount, std::uint64_t{1});  // Earlier snapshot remains unchanged.
    CHECK(firstObservation.lastTick == first.tick);
}

TEST_CASE("physics.backend.step rejects repeated ticks and invalid settings without advancing") {
    auto backend = detail::makeMockAcceleratorBackend();
    CHECK(backend->step(at(2), SimulationSettings{}).ok());
    const auto accepted = backend->observation();

    REQUIRE(!backend->step(at(2), SimulationSettings{}).ok());
    expectUnchanged(accepted, backend->observation());
    CHECK(!backend->step(at(1), SimulationSettings{}).ok());
    expectUnchanged(accepted, backend->observation());
    CHECK(!backend->step(at(3, -1), SimulationSettings{}).ok());
    expectUnchanged(accepted, backend->observation());
    CHECK(!backend->step(at(3, 50000001), SimulationSettings{}).ok());
    expectUnchanged(accepted, backend->observation());

    SimulationSettings settings;
    settings.subStepCount = 0;
    CHECK(!backend->step(at(3), settings).ok());
    expectUnchanged(accepted, backend->observation());
    settings.subStepCount = 1025;
    CHECK(!backend->step(at(3), settings).ok());
    expectUnchanged(accepted, backend->observation());
}

TEST_CASE("physics.backend.restore installs valid metadata and preserves state on rejection") {
    auto source = detail::makeMockAcceleratorBackend();
    CHECK(source->step(at(4), SimulationSettings{}).ok());
    const auto saved = source->observation();

    auto restored = detail::makeMockAcceleratorBackend();
    REQUIRE(restored->restoreObservation(saved).ok());
    expectUnchanged(saved, restored->observation());
    CHECK(!restored->step(at(4), SimulationSettings{}).ok());
    CHECK(restored->step(at(5), SimulationSettings{}).ok());

    const auto beforeFailure = restored->observation();
    auto       invalid       = beforeFailure;
    invalid.simulatedSeconds += 1.0;
    CHECK(!restored->restoreObservation(invalid).ok());
    expectUnchanged(beforeFailure, restored->observation());
}
