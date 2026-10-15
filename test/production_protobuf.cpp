#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/SnapshotHash.h"
#include "production/Production.h"

#include <cstdint>
#include <limits>
#include <string>
#include <utility>

using eve::production::ProductionRequest;
using eve::production::SchedulerStrategy;
using eve::production::WorkQueue;

TEST_CASE("production.protobuf.typedQueueRoundTripAndAtomicFailure") {
    WorkQueue original;
    REQUIRE(original.setSlotCount("forge", 2).ok());
    REQUIRE(original.setResourceCapacity("forge", "furnace", 3).ok());
    REQUIRE(original.setSchedulerStrategy("forge", SchedulerStrategy::Fifo).ok());
    REQUIRE(original.setTagAvailable("forge", "heated", true).ok());
    auto reference = eve::DefinitionRef::parse("recipe:ingot");
    REQUIRE(reference.ok());
    const eve::definition::DefinitionHandle definition{std::move(reference).takeValue(), eve::Generation(4)};
    REQUIRE(original.setDefinitionAvailable("forge", definition, true).ok());

    ProductionRequest first;
    first.owner    = "forge";
    first.kind     = "smelt";
    first.product  = "ingot";
    first.context  = eve::Value(eve::Value::Object{{"quantity", eve::Value(std::numeric_limits<std::int64_t>::max())}});
    first.duration = eve::Duration::fromNanoseconds(4000000000LL);
    first.definition  = definition;
    first.reservation = eve::Value(eve::Value::Object{{"ore", eve::Value(3)}});
    first.resources.push_back({"furnace", 1});
    first.dependencies.requiredTags.push_back("heated");
    first.dependencies.requiredDefinitions.push_back(definition);
    first.settlementRequired = false;
    auto firstId             = original.enqueue(std::move(first));
    REQUIRE(firstId.ok());

    ProductionRequest second;
    second.owner    = "forge";
    second.kind     = "forge";
    second.product  = "blade";
    second.duration = eve::Duration::fromNanoseconds(2000000000LL);
    second.dependencies.prerequisites.push_back(firstId.value());
    second.settlementRequired = false;
    REQUIRE(original.enqueue(std::move(second)).ok());
    REQUIRE(original.advance({eve::SimulationTick(1), eve::Duration::fromNanoseconds(1000000000LL)}).ok());

    auto binary = original.snapshotBinary();
    REQUIRE(binary.ok());
    WorkQueue restored;
    REQUIRE(restored.restoreBinary(binary.value()).ok());
    auto recaptured = restored.snapshotBinary();
    REQUIRE(recaptured.ok());
    CHECK_EQ(recaptured.value(), binary.value());
    REQUIRE(restored.find(firstId.value()));
    const auto* context = restored.find(firstId.value())->get().context.getIf<eve::Value::Object>();
    REQUIRE(context != nullptr);
    CHECK_EQ(context->at("quantity").asInt(), std::numeric_limits<std::int64_t>::max());
    CHECK_EQ(restored.resourceCapacity("forge", "furnace"), 3);
    CHECK_EQ(restored.schedulerStrategy("forge"), SchedulerStrategy::Fifo);

    auto legacy = original.snapshot();
    REQUIRE(legacy.ok());
    WorkQueue imported;
    REQUIRE(imported.restore(legacy.value()).ok());
    auto upgraded = imported.snapshotBinary();
    REQUIRE(upgraded.ok());
    CHECK_EQ(upgraded.value(), binary.value());

    std::string unknown = binary.value();
    unknown.append("\xA0\x06\x01", 3);
    CHECK(!restored.restoreBinary(unknown).ok());
    CHECK(!restored.restoreBinary("invalid").ok());
    auto afterFailure = restored.snapshotBinary();
    REQUIRE(afterFailure.ok());
    CHECK_EQ(afterFailure.value(), binary.value());

    const auto hash     = eve::snapshotContentHashProvider();
    auto       envelope = original.snapshotEnvelopeBinary(hash);
    REQUIRE(envelope.ok());
    WorkQueue sealed;
    REQUIRE(sealed.restoreSnapshotBinary(envelope.value(), hash).ok());
    auto sealedBinary = sealed.snapshotBinary();
    REQUIRE(sealedBinary.ok());
    CHECK_EQ(sealedBinary.value(), binary.value());
    std::string tampered = envelope.value();
    tampered.back() ^= 1;
    CHECK(!sealed.restoreSnapshotBinary(tampered, hash).ok());
}

TEST_CASE("production.protobuf.settlementAndReservationRelease") {
    WorkQueue         queue;
    ProductionRequest settledRequest;
    settledRequest.owner       = "alchemy";
    settledRequest.kind        = "craft";
    settledRequest.product     = "potion";
    settledRequest.duration    = eve::Duration::fromNanoseconds(1000000000LL);
    settledRequest.reservation = eve::Value(eve::Value::Object{{"inventory", eve::Value("hold-1")}});
    auto settledId             = queue.enqueue(std::move(settledRequest));
    REQUIRE(settledId.ok());
    REQUIRE(queue.advance({eve::SimulationTick(1), eve::Duration::fromNanoseconds(1000000000LL)}).ok());
    eve::production::ProductionSettlementReceipt receipt;
    receipt.settlementId = "receipt-1";
    receipt.payload      = eve::Value(eve::Value::Object{{"quantity", eve::Value(1)}});
    REQUIRE(queue.settle(settledId.value(), std::move(receipt)).ok());

    ProductionRequest cancelledRequest;
    cancelledRequest.owner       = "alchemy";
    cancelledRequest.kind        = "craft";
    cancelledRequest.product     = "elixir";
    cancelledRequest.duration    = eve::Duration::fromNanoseconds(2000000000LL);
    cancelledRequest.reservation = eve::Value(eve::Value::Object{{"inventory", eve::Value("hold-2")}});
    auto cancelledId             = queue.enqueue(std::move(cancelledRequest));
    REQUIRE(cancelledId.ok());
    REQUIRE(queue.cancel(cancelledId.value(), "changed_plan").ok());
    REQUIRE(queue.releaseReservation(cancelledId.value(), "release-2").ok());

    auto binary = queue.snapshotBinary();
    REQUIRE(binary.ok());
    WorkQueue restored;
    REQUIRE(restored.restoreBinary(binary.value()).ok());
    REQUIRE(restored.find(settledId.value()));
    REQUIRE(restored.find(cancelledId.value()));
    CHECK_EQ(restored.find(settledId.value())->get().settlement.settlementId, std::string("receipt-1"));
    CHECK_EQ(restored.find(cancelledId.value())->get().reservationRelease.releaseId, std::string("release-2"));
    auto recaptured = restored.snapshotBinary();
    REQUIRE(recaptured.ok());
    CHECK_EQ(recaptured.value(), binary.value());
}
