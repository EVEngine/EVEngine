#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "tactics/TacticsSettlement.h"

#include <array>

namespace {

eve::SubjectRef subject(std::uint8_t value) {
    std::array<std::uint8_t, 16> bytes{};
    bytes[15] = value;
    return eve::SubjectRef::fromPersistentId(eve::PersistentId(bytes));
}

class TacticalHealthPolicy final : public eve::settlement::ISettlementPolicy {
public:
    explicit TacticalHealthPolicy(double& health, bool failCommit = false)
        : health_(health), failCommit_(failCommit) {}
    eve::Result<void> validate(eve::settlement::SettlementContext&) override { return eve::Result<void>::success(); }
    eve::Result<void> sourceModifiers(eve::settlement::SettlementContext&) override {
        return eve::Result<void>::success();
    }
    eve::Result<void> targetMitigation(eve::settlement::SettlementContext&) override {
        return eve::Result<void>::success();
    }
    eve::Result<void> armorShield(eve::settlement::SettlementContext&) override { return eve::Result<void>::success(); }
    eve::Result<void> clamp(eve::settlement::SettlementContext& context) override {
        return context.setClampMax(health_);
    }
    eve::Result<eve::settlement::PreparedApply> prepareApply(
        const eve::settlement::SettlementContext& context) override {
        const double before = health_;
        const double after  = before - context.magnitude();
        return eve::Result<eve::settlement::PreparedApply>::success(eve::settlement::PreparedApply(
            [this, after]() {
                health_ = after;
                if (failCommit_)
                    return eve::Result<void>::failure(
                        eve::Diagnostic::error(eve::DiagnosticCode::Failed, "injected area commit failure"));
                return eve::Result<void>::success();
            },
            [this, before]() { health_ = before; }));
    }

private:
    double& health_;
    bool    failCommit_ = false;
};

}  // namespace

TEST_CASE("tactics.settlement.committedAbilityUsesSharedRules") {
    eve::settlement::SettlementRule highGround;
    highGround.id                  = "tactics.high-ground";
    highGround.source              = "board:height";
    highGround.stage               = eve::settlement::StageKind::SourceModifiers;
    highGround.operation           = eve::settlement::RuleOperation::Multiply;
    highGround.value               = 1.5;
    highGround.filter.kinds        = {"damage"};
    highGround.filter.requiredTags = {"tactics:ability"};
    highGround.when                = "context.range <= 3 && context.target_hp_ratio < 0.5";
    eve::settlement::SettlementRuleSet rules;
    REQUIRE(rules.configure({highGround}).ok());

    eve::tactics::TacticsSettlementRuntime runtime;
    REQUIRE(runtime.configureSettlementRules(rules).ok());
    eve::tactics::AbilitySettlementRequest request;
    request.ability.actor      = subject(1);
    request.ability.targetUnit = subject(2);
    request.kind               = "damage";
    request.magnitude          = 20.0;
    request.tick               = eve::SimulationTick(7);
    eve::Value::Object context;
    context["range"]            = 2.0;
    context["target_hp_ratio"]  = 0.4;
    request.context             = eve::Value(std::move(context));
    double               health = 100.0;
    TacticalHealthPolicy policy(health);
    auto                 settled = runtime.settle(request, policy);
    REQUIRE(settled.ok());
    CHECK_EQ(settled.value().applied, 30.0);
    CHECK_EQ(health, 70.0);
}

TEST_CASE("tactics.settlement.cellOnlyAbilityUsesExplicitStableTarget") {
    eve::tactics::AbilitySettlementRequest request;
    request.ability.actor      = subject(3);
    request.ability.targetCell = {4, 5, 0};
    request.target             = subject(4);
    request.kind               = "hazard";
    request.magnitude          = 12.0;
    request.tick               = eve::SimulationTick(8);

    double                                    durability = 40.0;
    TacticalHealthPolicy                      policy(durability);
    eve::tactics::TacticsSettlementRuntime    runtime;
    auto                                      canonical = eve::tactics::makeSettlementRequest(request);
    REQUIRE(canonical.ok());
    CHECK_EQ(canonical.value().target, request.target);
    auto                                      settled = runtime.settle(request, policy);
    REQUIRE(settled.ok());
    CHECK_EQ(durability, 28.0);
}

TEST_CASE("tactics.settlement.areaAbilityUsesAtomicBatchAndRollsBackAllTargets") {
    std::array<eve::tactics::AbilitySettlementRequest, 2> requests;
    for (std::size_t index = 0; index < requests.size(); ++index) {
        requests[index].ability.actor      = subject(5);
        requests[index].ability.targetUnit = subject(static_cast<std::uint8_t>(6 + index));
        requests[index].ability.targetCell = {static_cast<int>(index), 1, 0};
        requests[index].kind               = "damage";
        requests[index].magnitude          = 15.0;
        requests[index].tick               = eve::SimulationTick(9);
    }

    double                 firstHealth  = 100.0;
    double                 secondHealth = 100.0;
    TacticalHealthPolicy   first(firstHealth);
    TacticalHealthPolicy   second(secondHealth, true);
    std::array<eve::settlement::ISettlementPolicy*, 2> policies = {&first, &second};
    eve::tactics::TacticsSettlementRuntime runtime;
    auto failed = runtime.settleAtomic(requests, policies);
    CHECK(!failed.ok());
    CHECK_EQ(firstHealth, 100.0);
    CHECK_EQ(secondHealth, 100.0);

    TacticalHealthPolicy secondRetry(secondHealth);
    policies[1] = &secondRetry;
    auto settled = runtime.settleAtomic(requests, policies);
    REQUIRE(settled.ok());
    REQUIRE_EQ(settled.value().size(), 2u);
    CHECK_EQ(firstHealth, 85.0);
    CHECK_EQ(secondHealth, 85.0);
}
