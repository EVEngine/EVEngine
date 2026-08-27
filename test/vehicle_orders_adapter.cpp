#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "vehicle/VehicleOrderQueueAdapter.h"

#include <utility>

using eve::vehicle::VehicleOrder;
using eve::vehicle::VehicleOrderQueueAdapter;
using eve::vehicle::VehicleOrderType;

namespace {

VehicleOrder moveOrder() {
    VehicleOrder order;
    order.type = VehicleOrderType::Move;
    order.x    = 10.f;
    order.y    = 20.f;
    return order;
}

VehicleOrder attackOrder() {
    VehicleOrder order;
    order.type     = VehicleOrderType::Attack;
    order.x        = 30.f;
    order.y        = 40.f;
    order.targetId = 7;
    return order;
}

std::string takeId(eve::Result<std::string> result) {
    REQUIRE(result.hasValue());
    return std::move(result).takeValue();
}

}  // namespace

TEST_CASE("vehicle.orders.adapterUsesGenericPriorityAndLifecycle") {
    VehicleOrderQueueAdapter adapter;
    const std::string        moveId   = takeId(adapter.append(moveOrder(), 1));
    const std::string        attackId = takeId(adapter.append(attackOrder(), 5));

    REQUIRE(!moveId.empty());
    REQUIRE(!attackId.empty());
    REQUIRE(adapter.current() != nullptr);
    CHECK_EQ(adapter.current()->type, VehicleOrderType::Move);
    CHECK_EQ(adapter.activeOrQueuedCount(), 2);

    CHECK(adapter.completeCurrent().ok());
    REQUIRE(adapter.current() != nullptr);
    CHECK_EQ(adapter.current()->type, VehicleOrderType::Attack);
    CHECK_EQ(adapter.activeOrQueuedCount(), 1);
}

TEST_CASE("vehicle.orders.adapterTimeoutActivatesNextDomainOrder") {
    VehicleOrderQueueAdapter adapter;
    const std::string        timedId = takeId(adapter.append(moveOrder(), 0, 0.25));
    const std::string        nextId  = takeId(adapter.append(attackOrder()));

    REQUIRE(!timedId.empty());
    REQUIRE(!nextId.empty());
    adapter.update(0.25);
    REQUIRE(adapter.current() != nullptr);
    CHECK_EQ(adapter.current()->type, VehicleOrderType::Attack);
    CHECK_EQ(adapter.activeOrQueuedCount(), 1);
    CHECK(adapter.cancel(nextId, "test_cleanup").ok());
    CHECK(adapter.current() == nullptr);
    CHECK_EQ(adapter.activeOrQueuedCount(), 0);
}

TEST_CASE("vehicle.orders.adapterInterruptHonoursGenericPriority") {
    VehicleOrderQueueAdapter adapter;
    const std::string        activeId = takeId(adapter.append(moveOrder(), 20));
    REQUIRE(!activeId.empty());

    CHECK(!adapter.interrupt(attackOrder(), 19).hasValue());
    REQUIRE(adapter.current() != nullptr);
    CHECK_EQ(adapter.current()->type, VehicleOrderType::Move);

    const std::string urgentId = takeId(adapter.interrupt(attackOrder(), 20));
    REQUIRE(!urgentId.empty());
    REQUIRE(adapter.current() != nullptr);
    CHECK_EQ(adapter.current()->type, VehicleOrderType::Attack);
    CHECK_EQ(adapter.activeOrQueuedCount(), 1);
}

TEST_CASE("vehicle.orders.adapterReplaceRefreshesCompatibilityProjection") {
    eve::vehicle::VehicleEntity::Orders legacy;
    VehicleOrderQueueAdapter            adapter;

    const std::string moveId        = takeId(adapter.append(moveOrder()));
    const std::string firstAttackId = takeId(adapter.append(attackOrder()));
    REQUIRE(!moveId.empty());
    REQUIRE(!firstAttackId.empty());
    adapter.syncCompatibility(legacy);
    REQUIRE(legacy.current == 0);
    CHECK_EQ(legacy.queue.size(), size_t{2});

    const std::string replacementId = takeId(adapter.replace(attackOrder()));
    REQUIRE(!replacementId.empty());
    adapter.syncCompatibility(legacy);
    REQUIRE(legacy.current == 0);
    CHECK_EQ(legacy.queue.size(), size_t{1});
    CHECK_EQ(legacy.queue.front().type, VehicleOrderType::Attack);
}

TEST_CASE("vehicle.orders.adapterCopyKeepsLifecycleAndPayloadIndependent") {
    VehicleOrderQueueAdapter original;
    const std::string        moveId   = takeId(original.append(moveOrder(), 3));
    const std::string        attackId = takeId(original.append(attackOrder(), 1));
    REQUIRE(!moveId.empty());
    REQUIRE(!attackId.empty());

    VehicleOrderQueueAdapter copy(original);
    REQUIRE(copy.current() != nullptr);
    CHECK_EQ(copy.current()->type, VehicleOrderType::Move);
    CHECK_EQ(copy.current()->x, 10.f);
    CHECK_EQ(copy.activeOrQueuedCount(), 2);

    CHECK(original.cancel(moveId, "original_only").ok());
    REQUIRE(original.current() != nullptr);
    CHECK_EQ(original.current()->type, VehicleOrderType::Attack);
    REQUIRE(copy.current() != nullptr);
    CHECK_EQ(copy.current()->type, VehicleOrderType::Move);

    CHECK(copy.completeCurrent().ok());
    REQUIRE(copy.current() != nullptr);
    CHECK_EQ(copy.current()->type, VehicleOrderType::Attack);
    CHECK(copy.cancel(attackId, "copy_only").ok());
    CHECK(copy.current() == nullptr);
    REQUIRE(original.current() != nullptr);
    CHECK_EQ(original.current()->type, VehicleOrderType::Attack);

    const std::string originalNext = takeId(original.append(moveOrder()));
    const std::string copyNext     = takeId(copy.append(moveOrder()));
    CHECK_EQ(originalNext, copyNext);
}

TEST_CASE("vehicle.orders.componentCopyClonesAdapterNotLegacyProjection") {
    eve::vehicle::VehicleEntity::Orders source;
    const std::string                   sourceId = takeId(source.adapter->append(attackOrder(), 4, 2.0));
    REQUIRE(!sourceId.empty());
    source.adapter->syncCompatibility(source);

    eve::vehicle::VehicleEntity::Orders copy(source);
    REQUIRE(copy.adapter->current() != nullptr);
    CHECK_EQ(copy.adapter->current()->targetId, 7);
    CHECK_EQ(copy.adapter->activeOrQueuedCount(), 1);

    CHECK(source.adapter->cancel(sourceId, "source_only").ok());
    CHECK(source.adapter->current() == nullptr);
    REQUIRE(copy.adapter->current() != nullptr);
    CHECK_EQ(copy.adapter->current()->targetId, 7);

    eve::vehicle::VehicleEntity::Orders assigned;
    assigned = copy;
    REQUIRE(assigned.adapter->current() != nullptr);
    CHECK_EQ(assigned.adapter->current()->targetId, 7);
    CHECK(assigned.adapter->cancel(sourceId, "assigned_only").ok());
    CHECK(copy.adapter->current() != nullptr);
}
