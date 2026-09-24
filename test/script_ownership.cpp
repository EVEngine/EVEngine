/**
 * @file script_ownership.cpp
 * @brief Contract tests for Value/Owned/Borrowed script object semantics.
 */

#include "authority/Authority.h"
#include "common/SquirrelOwnership.h"
#include "decision/Decision.h"
#include "effects/Effects.h"
#include "orders/CommandQueue.h"
#include "procgen/Procgen.h"
#include "production/Production.h"
#include "statepatch/StatePatch.h"

// A RuntimePin references its producing store by address, so that store must never be
// relocated while a pin can exist. RuntimeObjectRegistry keeps it behind a stable heap
// address and stays movable itself (statepatch::Store move-assigns one).
static_assert(!std::is_move_constructible_v<eve::script::detail::RuntimeSlotStore>);
static_assert(!std::is_move_assignable_v<eve::script::detail::RuntimeSlotStore>);

#include "zeroerr/unittest.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <memory>
#include <string>

TEST_CASE("scriptOwnership.commonSemanticsAndRegistryStaleGeneration") {
    struct Tag {};
    struct Item {
        int value = 7;
    };

    CHECK_EQ(std::string(eve::script::objectSemanticName(eve::script::ObjectSemantic::Value)), std::string("value"));
    CHECK_EQ(std::string(eve::script::objectSemanticName(eve::script::ObjectSemantic::Owned)), std::string("owned"));
    CHECK_EQ(std::string(eve::script::objectSemanticName(eve::script::ObjectSemantic::Borrowed)),
             std::string("borrowed"));

    eve::script::RuntimeObjectRegistry<Item, Tag> registry;
    auto                                          created = registry.emplace(std::make_unique<Item>());
    REQUIRE(created.ok());
    const auto reference = std::move(created).takeValue();
    auto       borrowed  = registry.resolve(reference);
    REQUIRE(borrowed.isBound());
    CHECK_EQ(borrowed->value, 7);
    CHECK(!registry.isStale(reference));

    auto released = registry.erase(reference);
    REQUIRE(released.ok());
    CHECK(registry.isStale(reference));
    CHECK(!registry.resolve(reference).isBound());

    auto replacement = registry.emplace(std::make_unique<Item>());
    REQUIRE(replacement.ok());
    const auto replacementReference = std::move(replacement).takeValue();
    CHECK(replacementReference.handle.index() == reference.handle.index());
    CHECK(replacementReference.handle.generation() != reference.handle.generation());
    CHECK(registry.isStale(reference));
    CHECK(!registry.isStale(replacementReference));

    const auto                         oldEpoch          = registry.ownerEpoch();
    eve::script::RuntimeHandleRef<Tag> unloadedReference = replacementReference;
    registry.clear();
    CHECK(registry.isStale(unloadedReference));
    CHECK(!registry.resolve(unloadedReference).isBound());
    {
        eve::script::RuntimeObjectRegistry<Item, Tag> reloadedRegistry;
        CHECK(reloadedRegistry.ownerEpoch() != oldEpoch);
        CHECK(reloadedRegistry.isStale(unloadedReference));
    }
}

TEST_CASE("scriptOwnership.pinKeepsErasedPayloadAliveUntilRelease") {
    struct Tag {};
    struct Tracked {
        int  value     = 7;
        int* destroyed = nullptr;
        ~Tracked() {
            if (destroyed != nullptr) ++*destroyed;
        }
    };

    int                                              destroyed = 0;
    eve::script::RuntimeObjectRegistry<Tracked, Tag> registry;

    auto item       = std::make_unique<Tracked>();
    item->destroyed = &destroyed;
    auto created    = registry.emplace(std::move(item));
    REQUIRE(created.ok());
    const auto reference = std::move(created).takeValue();

    auto pinned = registry.pin(reference);
    REQUIRE(pinned.ok());
    CHECK(pinned.value().isBound());
    CHECK_EQ(pinned.value().get()->value, 7);
    CHECK(pinned.value().borrow().isBound());

    // Destroy order A: the holder is released after the owner erased the entry.
    auto released = registry.erase(reference);
    REQUIRE(released.ok());
    CHECK(registry.isStale(reference));
    CHECK(!registry.resolve(reference).isBound());
    CHECK_EQ(destroyed, 0);
    CHECK_EQ(pinned.value().get()->value, 7);
    CHECK(!registry.pin(reference).ok());

    // A slot with a live pin is never handed out again, even though the handle is stale.
    auto replacement = registry.emplace(std::make_unique<Tracked>());
    REQUIRE(replacement.ok());
    const auto replacementReference = std::move(replacement).takeValue();
    CHECK(replacementReference.handle.index() != reference.handle.index());

    // Moving the pin transfers the keep-alive; releasing the source is a no-op.
    {
        auto moved = std::move(pinned).takeValue();
        CHECK(moved.isBound());
        CHECK_EQ(moved.get()->value, 7);
        CHECK_EQ(destroyed, 0);
    }
    CHECK_EQ(destroyed, 1);
}

TEST_CASE("scriptOwnership.clearDefersPinnedPayloadDestruction") {
    struct Tag {};
    struct Tracked {
        int* destroyed = nullptr;
        ~Tracked() {
            if (destroyed != nullptr) ++*destroyed;
        }
    };

    int                                              kept    = 0;
    int                                              dropped = 0;
    eve::script::RuntimeObjectRegistry<Tracked, Tag> registry;

    auto pinnedItem       = std::make_unique<Tracked>();
    pinnedItem->destroyed = &kept;
    auto pinnedRef        = std::move(registry.emplace(std::move(pinnedItem))).takeValue();
    auto held             = registry.pin(pinnedRef);
    REQUIRE(held.ok());

    auto droppedItem       = std::make_unique<Tracked>();
    droppedItem->destroyed = &dropped;
    auto droppedRef        = std::move(registry.emplace(std::move(droppedItem))).takeValue();

    registry.clear();

    // Destroy order B: the owner was cleared first, the holder is still alive.
    CHECK_EQ(dropped, 1);
    CHECK_EQ(kept, 0);
    CHECK(registry.isStale(pinnedRef));
    CHECK(registry.isStale(droppedRef));
    CHECK(!registry.resolve(pinnedRef).isBound());

    // Releasing the last pin destroys the object the clear() had kept alive; the
    // move-assignment shape also covers releasing through a moved-from pin.
    held.value() = eve::script::RuntimePin<Tracked, Tag>{};
    CHECK(!held.value().isBound());
    CHECK_EQ(kept, 1);
}

TEST_CASE("scriptOwnership.ordersAndEffectsOwnedBinding") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        result <- "fail";
        local orders = eve.Orders();
        local oq = orders.newQueueOwned();
        local effects = eve.Effects();
        local ec = effects.newContainer();
        if (oq.ok && ec.ok && oq.value.ownership() == "owned" &&
            ec.value.ownership() == "owned" && !oq.value.isStale() &&
            !ec.value.isStale()) {
            local order = oq.value.append("ownership_probe", 1, 0.0);
            local effect = ec.value.apply("subject", "probe", "test", 1,
                                                  0.0, "probe", "refresh");
            local releasedOrder = oq.value.release();
            local releasedEffect = ec.value.release();
            if (order.ok && effect.ok && releasedOrder.ok && releasedEffect.ok &&
                oq.value.isStale() && ec.value.isStale() &&
                oq.value.release().code == "rejected" &&
                ec.value.release().code == "rejected") result = "ok";
        }
    )"));
    CHECK_EQ(vm.find("result").toString(), std::string("ok"));
}

TEST_CASE("scriptOwnership.procgenAndStateModulesOwnedBindings") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        result <- "fail";
        local procgen = eve.Procgen();
        local params = procgen.newParams();
        local grid = procgen.newGrid(2, 3);
        local context = procgen.beginSystem("ownership_probe", 7);
        local production = eve.Production().newWorkQueue();
        local authority = eve.Authority().newStore();
        local statepatch = eve.StatePatch().newStore();
        local decision = eve.Decision().newContext();
        local pointSet = procgen.newPointSet();
        local output = procgen.newOutput();
        local cloud = procgen.newCloudField();
        local batch = statepatch.ok ? statepatch.value.newBatch() : { ok = false };
        if (params.ok && grid.ok && context.ok && production.ok && authority.ok &&
            statepatch.ok && decision.ok && pointSet.ok && output.ok && cloud.ok && batch.ok &&
            params.value.ownerEpoch() > 0 && pointSet.ownerEpoch > 0 && output.ownerEpoch > 0 &&
            cloud.ownerEpoch > 0 && pointSet.value.empty() && output.value.getTarget() == "" &&
            grid.value.getWidth() == 2 && grid.value.getHeight() == 3 &&
            params.value.setSeed(42).ok && params.value.getSeed() == 42 &&
            grid.value.fill(1).ok && grid.value.getCell(1, 2) == 1 &&
            context.value.getName() == "ownership_probe" &&
            production.value.enqueue("probe", "build", "unit", "{}", 1.0, 0).ok &&
            authority.value.grant("actor", "scope", "cap", "probe", 1, 0.0).ok &&
            statepatch.value.revision() == 0 && decision.value.setState("fsm", "idle").ok &&
            decision.value.state("fsm") == "idle" && batch.value.set("actor", "probe", "true").ok) {
            local pReleased = params.value.release();
            local gReleased = grid.value.release();
            local cReleased = context.value.release();
            local qReleased = production.value.release();
            local aReleased = authority.value.release();
            local bReleased = batch.value.release();
            local sReleased = statepatch.value.release();
            local dReleased = decision.value.release();
            if (pReleased.ok && gReleased.ok && cReleased.ok && qReleased.ok &&
                aReleased.ok && bReleased.ok && sReleased.ok && dReleased.ok &&
                params.value.isStale() && grid.value.isStale() &&
                context.value.isStale() && production.value.isStale() &&
                authority.value.isStale() && statepatch.value.isStale() &&
                batch.value.isStale() && decision.value.isStale()) result = "ok";
        }
    )"));
    CHECK_EQ(vm.find("result").toString(), std::string("ok"));
}

TEST_CASE("scriptOwnership.procgenOwnedGridExposesAssetPlacement") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        result <- "fail";
        local procgen = eve.Procgen();
        local paramsResult = procgen.newParams();
        if (paramsResult.ok) {
            local params = paramsResult.value;
            params.setSize(32, 24);
            params.setSeed(23);
            params.setString("assets.light", "sconce_probe");
            local generated = procgen.generate("level.roguelike", params);
            if (generated.ok) {
                local grid = generated.value;
                for (local i = 0; i < grid.getObjectCount(); ++i) {
                    if (grid.getObjectType(i) == "light" &&
                        grid.getObjectAsset(i) == "sconce_probe" &&
                        grid.getObjectRotation(i) >= 0.0 &&
                        (grid.getObjectFlags(i) & 4) != 0) result = "ok";
                }
            }
        }
    )"));
    CHECK_EQ(vm.find("result").toString(), std::string("ok"));
}
