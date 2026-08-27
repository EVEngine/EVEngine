#include "action/Action.h"
#include "rpg/SkillAction.h"
#include "weapon/WeaponAction.h"

#include "common/Identity.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace {

using eve::action::ActionDefinition;
using eve::action::ActionExecutionId;
using eve::action::ActionPhase;
using eve::action::ActionRequest;
using eve::action::ActionRuntime;
using eve::action::ActionServices;
using eve::action::IActionConditionEvaluator;
using eve::action::IActionEffectExecutor;
using eve::action::IActionEffectOperation;
using eve::action::IActionResourceProvider;
using eve::action::IActionTargetResolver;
using eve::action::TargetingMode;
using eve::decision::Condition;
using eve::decision::ConditionResult;
using eve::resource::AccountNonce;
using eve::resource::Affordability;
using eve::resource::CostSpec;
using eve::resource::IResourceAccount;
using eve::resource::Receipt;
using eve::resource::ReceiptId;
using eve::resource::ReceiptOperation;
using eve::resource::Reservation;
using eve::resource::ReservationId;

ActionDefinition definition(const char* name) {
    ActionDefinition result;
    const auto id = eve::LogicalId::fromParts("test", name);
    REQUIRE(id.has_value());
    result.id = *id;
    return result;
}

ActionRequest requestFor(const ActionDefinition& definition) {
    ActionRequest result;
    result.actionId = definition.id;
    return result;
}

class TestConditionEvaluator final : public IActionConditionEvaluator {
public:
    bool pass = true;
    mutable int calls = 0;

    eve::Result<ConditionResult> evaluate(const ActionDefinition&, const ActionRequest&) const override {
        ++calls;
        if (pass)
            return eve::Result<ConditionResult>::success(ConditionResult::success());
        return eve::Result<ConditionResult>::success(
            ConditionResult::failed(eve::decision::ConditionReasonCode::TagMissing));
    }
};

class TestTargetResolver final : public IActionTargetResolver {
public:
    bool fail = true;
    mutable int calls = 0;

    eve::Result<eve::sensing::TargetSet> resolve(
        const eve::sensing::TargetingQuery&) const override {
        ++calls;
        if (fail)
            return eve::Result<eve::sensing::TargetSet>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::PreconditionViolation, "test target selection failed"));
        return eve::Result<eve::sensing::TargetSet>::success(eve::sensing::TargetSet{});
    }
};

class TestEffectOperation final : public IActionEffectOperation {
public:
    TestEffectOperation(int& commitCalls, int& rollbackCalls)
        : commitCalls_(commitCalls), rollbackCalls_(rollbackCalls) {}

    void commit() noexcept override { ++commitCalls_; }
    void rollback() noexcept override { ++rollbackCalls_; }

private:
    int& commitCalls_;
    int& rollbackCalls_;
};

class TestEffectExecutor final : public IActionEffectExecutor {
public:
    bool succeed = true;
    int prepareCalls = 0;
    int commitCalls = 0;
    int rollbackCalls = 0;

    eve::Result<std::unique_ptr<IActionEffectOperation>> prepare(
        const ActionDefinition&, const ActionRequest&, const eve::sensing::TargetSet*,
        eve::SimulationTick) override {
        ++prepareCalls;
        if (!succeed)
            return eve::Result<std::unique_ptr<IActionEffectOperation>>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Failed, "test active operation failed"));
        std::unique_ptr<IActionEffectOperation> operation(
            new TestEffectOperation(commitCalls, rollbackCalls));
        return eve::Result<std::unique_ptr<IActionEffectOperation>>::success(std::move(operation));
    }
};

class TestResourceAccount final : public IResourceAccount {
public:
    TestResourceAccount() {
        auto nonce = eve::resource::allocateAccountNonce();
        nonce_ = std::move(nonce).expect("test resource account nonce");
    }

    bool allowReserve = true;
    bool reserveMutated = false;
    bool failCommit = false;
    bool debited = false;
    int reserveCalls = 0;
    mutable int canAffordCalls = 0;
    int rollbackCalls = 0;
    bool active = false;
    Reservation activeReservation;

    eve::Result<Affordability> canAfford(const CostSpec&) const override {
        ++canAffordCalls;
        return eve::Result<Affordability>::success(Affordability{allowReserve, {}});
    }

    eve::Result<Reservation> reserve(const CostSpec& cost) override {
        ++reserveCalls;
        if (!allowReserve)
            return eve::Result<Reservation>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::PreconditionViolation, "test account cannot reserve cost"));
        activeReservation = Reservation{nonce_, nextReservation_, cost};
        nextReservation_ = ReservationId(nextReservation_.value() + 1);
        active = true;
        reserveMutated = true;
        return eve::Result<Reservation>::success(activeReservation,
                                                  eve::Status::success(eve::StatusCode::Applied));
    }

    eve::Result<Receipt> debit(const CostSpec& cost) override {
        return eve::Result<Receipt>::success(
            Receipt{nonce_, nextReceipt_, ReservationId{}, ReceiptOperation::Debit, cost},
            eve::Status::success(eve::StatusCode::Applied));
    }

    eve::Result<Receipt> credit(const CostSpec& cost) override {
        return eve::Result<Receipt>::success(
            Receipt{nonce_, nextReceipt_, ReservationId{}, ReceiptOperation::Credit, cost},
            eve::Status::success(eve::StatusCode::Applied));
    }

    eve::Result<Receipt> commit(const Reservation& reservation) override {
        if (!active || reservation.id != activeReservation.id)
            return eve::Result<Receipt>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::Conflict, "test reservation is not active"));
        if (failCommit)
            return eve::Result<Receipt>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::Failed, "test account commit failed"));
        active = false;
        debited = true;
        return eve::Result<Receipt>::success(
            Receipt{nonce_, nextReceipt_, reservation.id, ReceiptOperation::Debit, reservation.cost},
            eve::Status::success(eve::StatusCode::Applied));
    }

    eve::Result<void> rollback(const Reservation& reservation) override {
        if (!active || reservation.id != activeReservation.id)
            return eve::Result<void>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::Conflict, "test reservation is not active"));
        active = false;
        ++rollbackCalls;
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    }

private:
    AccountNonce nonce_;
    ReservationId nextReservation_{1};
    ReceiptId nextReceipt_{1};
};

class TestResourceProvider final : public IActionResourceProvider {
public:
    explicit TestResourceProvider(TestResourceAccount& account) : currentAccount_(&account) {}

    bool available = true;

    eve::Result<Affordability> canAfford(const ActionDefinition&, const ActionRequest&,
                                         const CostSpec& cost) const override {
        if (!available)
            return eve::Result<Affordability>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::Unsupported, "test resource provider is unavailable"));
        return currentAccount_->canAfford(cost);
    }

    eve::Result<Reservation> reserve(const ActionDefinition&, const ActionRequest&,
                                     const CostSpec& cost) override {
        if (!available)
            return eve::Result<Reservation>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::Unsupported, "test resource provider is unavailable"));
        auto result = currentAccount_->reserve(cost);
        if (!result) return eve::Result<Reservation>::failure(result.status());
        auto credential = std::move(result).takeValue();
        if (!routeNonce_) {
            routeNonce_ = credential.account;
            routeAccount_ = currentAccount_;
        }
        if (switchAfterReserve_ && alternate_) currentAccount_ = alternate_;
        return eve::Result<Reservation>::success(std::move(credential),
                                                  eve::Status::success(eve::StatusCode::Applied));
    }

    eve::Result<Receipt> commit(const Reservation& reservation) override {
        if (!available)
            return eve::Result<Receipt>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::Unsupported, "test resource provider is unavailable"));
        TestResourceAccount* account = routeFor(reservation);
        if (!account)
            return eve::Result<Receipt>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::NotFound, "test provider lost reservation route"));
        return account->commit(reservation);
    }

    eve::Result<void> rollback(const Reservation& reservation) override {
        if (!available)
            return eve::Result<void>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::Unsupported, "test resource provider is unavailable"));
        TestResourceAccount* account = routeFor(reservation);
        if (!account)
            return eve::Result<void>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::NotFound, "test provider lost reservation route"));
        return account->rollback(reservation);
    }

    void switchTo(TestResourceAccount& account) { currentAccount_ = &account; }
    void switchAfterReserveTo(TestResourceAccount& account) {
        alternate_ = &account;
        switchAfterReserve_ = true;
    }
    void unload() { available = false; }

private:
    TestResourceAccount* routeFor(const Reservation& reservation) const {
        if (routeAccount_ && routeNonce_ && reservation.account == *routeNonce_)
            return routeAccount_;
        return nullptr;
    }

    TestResourceAccount* currentAccount_ = nullptr;
    TestResourceAccount* alternate_ = nullptr;
    TestResourceAccount* routeAccount_ = nullptr;
    std::optional<AccountNonce> routeNonce_;
    bool switchAfterReserve_ = false;
};

eve::sensing::SubjectRef testSubject(const char* value) {
    auto id = eve::PersistentId::parse(value);
    REQUIRE(id.has_value());
    return eve::sensing::SubjectRef::fromPersistentId(*id);
}

eve::sensing::WorldPoint testPoint() {
    auto point = eve::sensing::WorldPoint::world2D(0.f, 0.f);
    return std::move(point).expect("test action point");
}

}  // namespace

TEST_CASE("gameplayAction.phasePipelineUsesInjectedSimulationTime") {
    ActionDefinition def = definition("timed");
    def.timing.windup = eve::Duration::fromNanoseconds(1);
    def.timing.active = eve::Duration::fromNanoseconds(2);
    def.timing.recover = eve::Duration::fromNanoseconds(3);
    TestEffectExecutor effects;
    ActionRuntime runtime(ActionServices{nullptr, nullptr, nullptr, &effects});

    auto submitted = runtime.submit(def, requestFor(def));
    REQUIRE(submitted.ok());
    const ActionExecutionId id = std::move(submitted).takeValue();
    CHECK_EQ(static_cast<int>(runtime.find(id)->phase()), static_cast<int>(ActionPhase::Requested));

    auto requested = runtime.advance(id, eve::SimulationTick{1}, eve::Duration::zero());
    REQUIRE(requested.ok());
    CHECK_EQ(static_cast<int>(requested.value().phase), static_cast<int>(ActionPhase::Windup));
    CHECK_EQ(requested.value().transitions.size(), 2u);

    auto active = runtime.advance(id, eve::SimulationTick{2}, eve::Duration::fromNanoseconds(1));
    REQUIRE(active.ok());
    CHECK_EQ(static_cast<int>(active.value().phase), static_cast<int>(ActionPhase::Active));
    CHECK_EQ(effects.prepareCalls, 1);
    CHECK_EQ(effects.commitCalls, 1);

    auto recovering = runtime.advance(id, eve::SimulationTick{3}, eve::Duration::fromNanoseconds(2));
    REQUIRE(recovering.ok());
    CHECK_EQ(static_cast<int>(recovering.value().phase), static_cast<int>(ActionPhase::Recover));

    auto completed = runtime.advance(id, eve::SimulationTick{4}, eve::Duration::fromNanoseconds(3));
    REQUIRE(completed.ok());
    CHECK_EQ(static_cast<int>(completed.value().phase), static_cast<int>(ActionPhase::Completed));
    CHECK_EQ(static_cast<int>(runtime.find(id)->status().code()),
             static_cast<int>(eve::StatusCode::Applied));
}

TEST_CASE("gameplayAction.conditionFailureOwnsFailedTransition") {
    ActionDefinition def = definition("condition");
    def.condition = Condition::hasTag("silenced");
    TestConditionEvaluator conditions;
    conditions.pass = false;
    ActionRuntime runtime(ActionServices{&conditions, nullptr, nullptr, nullptr});

    auto submitted = runtime.submit(def, requestFor(def));
    REQUIRE(submitted.ok());
    const ActionExecutionId id = std::move(submitted).takeValue();
    auto advanced = runtime.advance(id, eve::SimulationTick{1}, eve::Duration::zero());
    CHECK(!advanced.ok());
    CHECK_EQ(conditions.calls, 1);
    const auto* execution = runtime.find(id);
    REQUIRE(execution != nullptr);
    CHECK_EQ(static_cast<int>(execution->phase()), static_cast<int>(ActionPhase::Failed));
    REQUIRE(execution->status().primaryDiagnostic() != nullptr);
    CHECK_EQ(static_cast<int>(execution->status().primaryDiagnostic()->code()),
             static_cast<int>(eve::DiagnosticCode::PreconditionViolation));
}

TEST_CASE("gameplayAction.targetFailureStopsBeforeActive") {
    ActionDefinition def = definition("target");
    def.targetingMode = TargetingMode::Query;
    def.targetingSpec = eve::sensing::TargetingSpec{};
    ActionRequest request = requestFor(def);
    request.targetingQuery = eve::sensing::TargetingQuery{
        testSubject("00000000-0000-7000-8000-000000000001"), testPoint(), *def.targetingSpec};
    TestTargetResolver targeting;
    ActionRuntime runtime(ActionServices{nullptr, &targeting, nullptr, nullptr});

    auto submitted = runtime.submit(def, std::move(request));
    REQUIRE(submitted.ok());
    const ActionExecutionId id = std::move(submitted).takeValue();
    auto advanced = runtime.advance(id, eve::SimulationTick{1}, eve::Duration::zero());
    CHECK(!advanced.ok());
    CHECK_EQ(targeting.calls, 1);
    CHECK_EQ(static_cast<int>(runtime.find(id)->phase()), static_cast<int>(ActionPhase::Failed));
    REQUIRE(runtime.find(id)->status().primaryDiagnostic() != nullptr);
    CHECK_EQ(static_cast<int>(runtime.find(id)->status().primaryDiagnostic()->code()),
             static_cast<int>(eve::DiagnosticCode::PreconditionViolation));
}

TEST_CASE("gameplayAction.costFailureIsAtomicBeforeActive") {
    ActionDefinition def = definition("cost");
    auto cost = CostSpec::single("mana", 5);
    REQUIRE(cost.ok());
    def.cost = std::move(cost).takeValue();
    TestResourceAccount account;
    account.allowReserve = false;
    TestResourceProvider resources(account);
    TestEffectExecutor effects;
    ActionRuntime runtime(ActionServices{nullptr, nullptr, &resources, &effects});

    auto submitted = runtime.submit(def, requestFor(def));
    REQUIRE(submitted.ok());
    const ActionExecutionId id = std::move(submitted).takeValue();
    auto advanced = runtime.advance(id, eve::SimulationTick{1}, eve::Duration::zero());
    CHECK(!advanced.ok());
    CHECK_EQ(account.canAffordCalls, 1);
    CHECK_EQ(account.reserveCalls, 0);
    CHECK(!account.reserveMutated);
    CHECK_EQ(account.rollbackCalls, 0);
    CHECK_EQ(effects.prepareCalls, 0);
    CHECK_EQ(static_cast<int>(runtime.find(id)->phase()), static_cast<int>(ActionPhase::Failed));
}

TEST_CASE("gameplayAction.cancelDuringWindupHasNoCrossFrameReservation") {
    ActionDefinition def = definition("cancel");
    def.timing.windup = eve::Duration::fromNanoseconds(10);
    auto cost = CostSpec::single("mana", 2);
    REQUIRE(cost.ok());
    def.cost = std::move(cost).takeValue();
    TestResourceAccount account;
    TestResourceProvider resources(account);
    ActionRuntime runtime(ActionServices{nullptr, nullptr, &resources, nullptr});

    auto submitted = runtime.submit(def, requestFor(def));
    REQUIRE(submitted.ok());
    const ActionExecutionId id = std::move(submitted).takeValue();
    auto started = runtime.advance(id, eve::SimulationTick{1}, eve::Duration::zero());
    REQUIRE(started.ok());
    CHECK_EQ(static_cast<int>(runtime.find(id)->phase()), static_cast<int>(ActionPhase::Windup));
    CHECK_EQ(account.canAffordCalls, 1);
    CHECK_EQ(account.reserveCalls, 0);
    CHECK(!account.active);

    auto cancelled = runtime.cancel(id, eve::SimulationTick{2});
    REQUIRE(cancelled.ok());
    CHECK_EQ(account.rollbackCalls, 0);
    CHECK(!account.active);
    CHECK_EQ(static_cast<int>(runtime.find(id)->phase()), static_cast<int>(ActionPhase::Cancelled));
    CHECK_EQ(static_cast<int>(runtime.find(id)->status().code()),
             static_cast<int>(eve::StatusCode::Cancelled));
}

TEST_CASE("gameplayAction.effectPrepareFailureDoesNotReserveCost") {
    ActionDefinition def = definition("prepare-failure");
    auto cost = CostSpec::single("mana", 3);
    REQUIRE(cost.ok());
    def.cost = std::move(cost).takeValue();
    def.activeExecutionRequired = true;
    TestResourceAccount account;
    TestResourceProvider resources(account);
    TestEffectExecutor effects;
    effects.succeed = false;
    ActionRuntime runtime(ActionServices{nullptr, nullptr, &resources, &effects});

    auto submitted = runtime.submit(def, requestFor(def));
    REQUIRE(submitted.ok());
    const ActionExecutionId id = std::move(submitted).takeValue();
    auto advanced = runtime.advance(id, eve::SimulationTick{1}, eve::Duration::zero());
    CHECK(!advanced.ok());
    CHECK_EQ(effects.prepareCalls, 1);
    CHECK_EQ(effects.commitCalls, 0);
    CHECK_EQ(effects.rollbackCalls, 0);
    CHECK_EQ(account.canAffordCalls, 1);
    CHECK_EQ(account.reserveCalls, 0);
    CHECK(!account.active);
    CHECK_EQ(static_cast<int>(runtime.find(id)->phase()), static_cast<int>(ActionPhase::Failed));
}

TEST_CASE("gameplayAction.commitFailureCompensatesResourceAndPreparedEffect") {
    ActionDefinition def = definition("commit-failure");
    auto cost = CostSpec::single("mana", 4);
    REQUIRE(cost.ok());
    def.cost = std::move(cost).takeValue();
    def.activeExecutionRequired = true;
    TestResourceAccount account;
    account.failCommit = true;
    TestResourceProvider resources(account);
    TestEffectExecutor effects;
    ActionRuntime runtime(ActionServices{nullptr, nullptr, &resources, &effects});

    auto submitted = runtime.submit(def, requestFor(def));
    REQUIRE(submitted.ok());
    const ActionExecutionId id = std::move(submitted).takeValue();
    auto advanced = runtime.advance(id, eve::SimulationTick{1}, eve::Duration::zero());
    CHECK(!advanced.ok());
    CHECK_EQ(effects.prepareCalls, 1);
    CHECK_EQ(effects.commitCalls, 0);
    CHECK_EQ(effects.rollbackCalls, 1);
    CHECK_EQ(account.reserveCalls, 1);
    CHECK_EQ(account.rollbackCalls, 1);
    CHECK(!account.active);
    CHECK(!account.debited);
    CHECK_EQ(static_cast<int>(runtime.find(id)->phase()), static_cast<int>(ActionPhase::Failed));
}

TEST_CASE("gameplayAction.successCommitsCostBeforeNonfailableEffect") {
    ActionDefinition def = definition("transaction-success");
    auto cost = CostSpec::single("mana", 6);
    REQUIRE(cost.ok());
    def.cost = std::move(cost).takeValue();
    def.activeExecutionRequired = true;
    TestResourceAccount account;
    TestResourceProvider resources(account);
    TestEffectExecutor effects;
    ActionRuntime runtime(ActionServices{nullptr, nullptr, &resources, &effects});

    auto submitted = runtime.submit(def, requestFor(def));
    REQUIRE(submitted.ok());
    const ActionExecutionId id = std::move(submitted).takeValue();
    auto advanced = runtime.advance(id, eve::SimulationTick{1}, eve::Duration::zero());
    REQUIRE(advanced.ok());
    CHECK_EQ(account.reserveCalls, 1);
    CHECK(account.debited);
    CHECK(!account.active);
    CHECK_EQ(effects.prepareCalls, 1);
    CHECK_EQ(effects.commitCalls, 1);
    CHECK_EQ(effects.rollbackCalls, 0);
    CHECK_EQ(static_cast<int>(runtime.find(id)->phase()), static_cast<int>(ActionPhase::Completed));
}

TEST_CASE("gameplayAction.providerRoutesCommitByReservationNonceAfterAccountSwitch") {
    ActionDefinition def = definition("nonce-route");
    auto cost = CostSpec::single("mana", 1);
    REQUIRE(cost.ok());
    def.cost = std::move(cost).takeValue();
    TestResourceAccount original;
    TestResourceAccount replacement;
    TestResourceProvider resources(original);
    resources.switchAfterReserveTo(replacement);
    ActionRuntime runtime(ActionServices{nullptr, nullptr, &resources, nullptr});

    auto submitted = runtime.submit(def, requestFor(def));
    REQUIRE(submitted.ok());
    const ActionExecutionId id = std::move(submitted).takeValue();
    auto advanced = runtime.advance(id, eve::SimulationTick{1}, eve::Duration::zero());
    REQUIRE(advanced.ok());
    CHECK(original.debited);
    CHECK(!original.active);
    CHECK(!replacement.debited);
    CHECK_EQ(replacement.reserveCalls, 0);
    CHECK_EQ(static_cast<int>(runtime.find(id)->phase()), static_cast<int>(ActionPhase::Completed));
}

TEST_CASE("gameplayAction.providerRoutesRollbackByReservationNonceAfterAccountSwitch") {
    ActionDefinition def = definition("nonce-rollback");
    auto cost = CostSpec::single("mana", 1);
    REQUIRE(cost.ok());
    def.cost = std::move(cost).takeValue();
    TestResourceAccount original;
    original.failCommit = true;
    TestResourceAccount replacement;
    TestResourceProvider resources(original);
    resources.switchAfterReserveTo(replacement);
    ActionRuntime runtime(ActionServices{nullptr, nullptr, &resources, nullptr});

    auto submitted = runtime.submit(def, requestFor(def));
    REQUIRE(submitted.ok());
    const ActionExecutionId id = std::move(submitted).takeValue();
    auto advanced = runtime.advance(id, eve::SimulationTick{1}, eve::Duration::zero());
    CHECK(!advanced.ok());
    CHECK(!original.active);
    CHECK_EQ(original.rollbackCalls, 1);
    CHECK(!replacement.active);
    CHECK_EQ(static_cast<int>(runtime.find(id)->phase()), static_cast<int>(ActionPhase::Failed));
}

TEST_CASE("gameplayAction.providerUnavailableBeforeActiveLeavesNoReservation") {
    ActionDefinition def = definition("provider-unloaded");
    auto cost = CostSpec::single("mana", 1);
    REQUIRE(cost.ok());
    def.cost = std::move(cost).takeValue();
    TestResourceAccount account;
    TestResourceProvider resources(account);
    resources.unload();
    ActionRuntime runtime(ActionServices{nullptr, nullptr, &resources, nullptr});

    auto submitted = runtime.submit(def, requestFor(def));
    REQUIRE(submitted.ok());
    const ActionExecutionId id = std::move(submitted).takeValue();
    auto advanced = runtime.advance(id, eve::SimulationTick{1}, eve::Duration::zero());
    CHECK(!advanced.ok());
    CHECK_EQ(account.canAffordCalls, 0);
    CHECK_EQ(account.reserveCalls, 0);
    CHECK(!account.active);
    CHECK_EQ(static_cast<int>(runtime.find(id)->phase()), static_cast<int>(ActionPhase::Failed));
}

TEST_CASE("gameplayAction.skillAndWeaponAdaptersShareTheSamePipelineShape") {
    eve::rpg::SkillDefinition skill;
    skill.id = "fireball";
    skill.castTime = 0.25f;
    skill.cooldown = 4.f;
    skill.grantedEffects.push_back("burning");
    auto skillCost = CostSpec::single("mana", 10);
    REQUIRE(skillCost.ok());
    skill.cost = std::move(skillCost).takeValue();

    eve::weapon::WeaponDefinition weapon;
    weapon.id = "rifle";
    weapon.stages.windupTime = 0.25f;
    weapon.stages.activeTime = 0.05f;
    weapon.stages.recoverTime = 0.4f;
    weapon.resource.kind = eve::weapon::ResourceKind::Ammo;
    eve::weapon::AttackRequest attack;

    auto skillDefinition = eve::rpg::SkillActionAdapter::makeDefinition(skill);
    auto weaponDefinition = eve::weapon::WeaponActionAdapter::makeDefinition(weapon, attack);
    REQUIRE(skillDefinition.ok());
    REQUIRE(weaponDefinition.ok());
    CHECK_EQ(skillDefinition.value().id.namespaceName(), std::string_view("rpg"));
    CHECK_EQ(weaponDefinition.value().id.namespaceName(), std::string_view("weapon"));
    CHECK_EQ(skillDefinition.value().id.name(), std::string_view("skill.fireball"));
    CHECK_EQ(weaponDefinition.value().id.name(), std::string_view("attack.rifle"));
    CHECK(skillDefinition.value().activeExecutionRequired);
    CHECK(weaponDefinition.value().activeExecutionRequired);
    CHECK(skillDefinition.value().cost.has_value());
    CHECK(weaponDefinition.value().cost.has_value());
    CHECK_EQ(skillDefinition.value().timing.windup, weaponDefinition.value().timing.windup);
    CHECK_EQ(static_cast<int>(skillDefinition.value().targetingMode),
             static_cast<int>(TargetingMode::None));
    CHECK_EQ(static_cast<int>(weaponDefinition.value().targetingMode),
             static_cast<int>(TargetingMode::None));

    auto skillRequest = eve::rpg::SkillActionAdapter::makeRequest(skill);
    auto weaponRequest = eve::weapon::WeaponActionAdapter::makeRequest(weapon, attack);
    REQUIRE(skillRequest.ok());
    REQUIRE(weaponRequest.ok());
    CHECK_EQ(skillRequest.value().actionId, skillDefinition.value().id);
    CHECK_EQ(weaponRequest.value().actionId, weaponDefinition.value().id);
    CHECK(weaponRequest.value().parameters.find("pelletCount") !=
          weaponRequest.value().parameters.end());
}
