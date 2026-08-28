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
