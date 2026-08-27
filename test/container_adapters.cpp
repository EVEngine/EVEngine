#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "building/BuildingDef.h"
#include "building/ContainerAdapters.h"
#include "building/PlacementSystem.h"
#include "building/PlacementWorld.h"
#include "common/Container.h"
#include "game_event/GameEvent.h"
#include "vehicle/ContainerAdapters.h"

#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace {

using eve::container::Capacity;
using eve::container::ContainerId;
using eve::container::MembershipId;
using eve::container::SlotIndex;

void registerTestBuilding(const char* id) {
    eve::building::BuildingRegistry::clear();
    eve::building::PlacementSystem::ensureBuiltins();
    eve::building::BuildingDefinition definition;
    definition.id = id;
    definition.footprintW = 1;
    definition.footprintH = 1;
    eve::building::BuildingRegistry::registerBuilding(definition);
}

}  // namespace

TEST_CASE("container.adaptersShareParityAndCanonicalLifecycleEnvelope") {
    using namespace eve::container;

    static_assert(!std::is_convertible_v<Coordinate<ScreenSpace>, Coordinate<World2DSpace>>);
    static_assert(!std::is_convertible_v<Coordinate<World2DSpace>, Coordinate<World3DSpace>>);
    static_assert(!std::is_convertible_v<Coordinate<GridSpace>, Coordinate<ScreenSpace>>);

    auto zone = Zone<World2DSpace>::create(
        ContainerId("zone:world2d"), Rectangle<World2DSpace>{{0.f, 0.f}, 10.f, 10.f},
        Capacity::fixed(3));
    REQUIRE(zone.ok());
    CHECK_EQ(zone.value().id().value(), std::string("zone:world2d"));
    CHECK(zone.value().contains(Coordinate<World2DSpace>{5.f, 5.f}));

    ecs::Table ecsWorld;
    ecs::ScopedTable guard(ecsWorld);
    auto* vehicle = eve::vehicle::VehicleEntity::create();
    vehicle->identity()->id = "vehicle:test";
    vehicle->seats()->list.resize(2);

    eve::vehicle::VehicleSeatContainerAdapter seats(
        ContainerId("vehicle:seat:test"), vehicle, AcceptedCondition{});
    registerTestBuilding("garrison.test");
    eve::building::PlacementWorld placement(4, 4, 1.f);
    placement.setId("world:test");
    const int buildingId = placement.placeAt("garrison.test", 0, 0);
    REQUIRE(buildingId > 0);
    eve::building::BuildingGarrisonContainerAdapter garrison(
        ContainerId("building:garrison:test"), &placement, buildingId, Capacity::fixed(3));

    CHECK_EQ(static_cast<int>(seats.descriptor().ordering),
             static_cast<int>(Ordering::ExplicitSlots));
    CHECK_EQ(static_cast<int>(garrison.descriptor().ordering),
             static_cast<int>(Ordering::Insertion));
    CHECK_EQ(seats.descriptor().capacity.value(), std::size_t(2));
    CHECK_EQ(garrison.descriptor().capacity.value(), std::size_t(3));

    std::vector<std::string> eventTypes;
    auto sink = [&](const eve::game_event::GameEvent& event) {
        CHECK(!event.eventId.isNil());
        CHECK_EQ(event.schemaId.format(), std::string("container:event"));
        CHECK_EQ(event.schemaVersion.value(), 1u);
        CHECK_EQ(event.source, std::string("vehicle:seat:test"));
        CHECK_EQ(event.tick, eve::SimulationTick(17));
        eventTypes.push_back(event.type);
    };

    auto entered = seats.enter(SlotIndex(0), 7, sink, eve::SimulationTick(17));
    REQUIRE(entered.ok());
    CHECK_EQ(eventTypes.size(), std::size_t(2));
    CHECK_EQ(eventTypes[0], std::string("container.accepted"));
    CHECK_EQ(eventTypes[1], std::string("container.enter"));

    const auto revisionBeforeReject = seats.revision();
    auto rejected = seats.enter(SlotIndex(0), 8, sink, eve::SimulationTick(17));
    CHECK(!rejected.ok());
    CHECK_EQ(eventTypes.back(), std::string("container.rejected"));
    CHECK_EQ(seats.revision(), revisionBeforeReject);
    REQUIRE(seats.snapshot().ok());
    CHECK_EQ(seats.snapshot().value().entries.size(), std::size_t(1));

    auto callbackFailure = seats.enter(
        SlotIndex(1), 8,
        [](const eve::game_event::GameEvent&) { throw std::runtime_error("observer"); },
        eve::SimulationTick(18));
    REQUIRE(callbackFailure.ok());
    CHECK_EQ(static_cast<int>(callbackFailure.code()),
             static_cast<int>(eve::StatusCode::Applied));
    CHECK(callbackFailure.status().hasDiagnostics());
    CHECK(vehicle->seats()->list[1].occupied);

    const auto oldGarrison = garrison.snapshot();
    REQUIRE(oldGarrison.ok());
    auto garrisoned = garrison.enter("unit:one", "rts.unit", {"infantry"}, {},
                                     eve::SimulationTick(19));
    REQUIRE(garrisoned.ok());
    CHECK_EQ(garrison.snapshot().value().entries.size(), std::size_t(1));
    auto stalePrepare = garrison.prepare(oldGarrison.value(), oldGarrison.value());
    CHECK(!stalePrepare.ok());
    CHECK_EQ(garrison.snapshot().value().entries.size(), std::size_t(1));

    std::vector<std::string> garrisonEventTypes;
    auto garrisonSink = [&](const eve::game_event::GameEvent& event) {
        CHECK_EQ(event.schemaId.format(), std::string("container:event"));
        CHECK_EQ(event.source, std::string("building:garrison:test"));
        CHECK_EQ(event.subject, std::string("unit:one"));
        CHECK_EQ(event.tick, eve::SimulationTick(20));
        garrisonEventTypes.push_back(event.type);
    };
    auto exited = garrison.exit(MembershipId("unit:one"), garrisonSink,
                                 eve::SimulationTick(20));
    REQUIRE(exited.ok());
    CHECK_EQ(garrisonEventTypes.size(), std::size_t(2));
    CHECK_EQ(garrisonEventTypes[0], std::string("container.accepted"));
    CHECK_EQ(garrisonEventTypes[1], std::string("container.exit"));

    // Cross-adapter transfer uses only common ContainerObject facts; neither
    // adapter needs to include the other's module or take ownership of it.
    auto movedToGarrison = eve::container::TransferService::transfer(
        TransferRequest{&seats, &garrison, MembershipId("vehicle:occupant:7"), SlotIndex(0),
                        std::nullopt, seats.revision(), garrison.revision()});
    REQUIRE(movedToGarrison.ok());
    CHECK(!vehicle->seats()->list[0].occupied);
    CHECK_EQ(garrison.snapshot().value().entries.size(), std::size_t(2));

    auto movedBack = eve::container::TransferService::transfer(
        TransferRequest{&garrison, &seats, MembershipId("vehicle:occupant:7"), std::nullopt,
                        SlotIndex(0), garrison.revision(), seats.revision()});
    REQUIRE(movedBack.ok());
    CHECK(vehicle->seats()->list[0].occupied);
    CHECK_EQ(vehicle->seats()->list[0].occupant, 7);

    eve::building::BuildingRegistry::clear();
}

TEST_CASE("container.adaptersFailureInjectionLeavesMembershipUnchanged") {
    registerTestBuilding("garrison.failure");
    eve::building::PlacementWorld placement(2, 2, 1.f);
    const int buildingId = placement.placeAt("garrison.failure", 0, 0);
    REQUIRE(buildingId > 0);
    eve::building::BuildingGarrisonContainerAdapter garrison(
        ContainerId("building:garrison:failure"), &placement, buildingId, Capacity::fixed(1));
    REQUIRE(garrison.enter("unit:one").ok());
    const auto before = garrison.snapshot();
    REQUIRE(before.ok());

    auto full = garrison.enter("unit:two");
    CHECK(!full.ok());
    CHECK_EQ(garrison.revision(), before.value().revision);
    auto after = garrison.snapshot();
    REQUIRE(after.ok());
    CHECK_EQ(after.value().entries.size(), std::size_t(1));
    CHECK_EQ(after.value().entries.front().membership.object.value(), std::string("unit:one"));

    eve::building::BuildingRegistry::clear();
}
