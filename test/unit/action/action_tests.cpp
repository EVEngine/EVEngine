// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "origins": [
    "action_checkpoint.cpp",
    "ability_asset.cpp",
    "ability_controller.cpp",
    "ability_system.cpp",
    "action_notify_registry.cpp",
    "action_preview_profile.cpp",
    "gameplay_composition_action_preview_profile_tests.cpp"
  ]
}
*/
// clang-format on

#include "zeroerr/assert.h"

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_ability_asset) || \
    defined(EVE_TEST_ORIGIN_ability_controller) || defined(EVE_TEST_ORIGIN_ability_system)
#include "PrivateTestScope.h"
#include "action/AbilityAsset.h"
#include "action/AbilityController.h"
#include "action/AbilityRuntime.h"
#include "common/Diagnostic.h"
#include "zeroerr/unittest.h"

#include <limits>
#include <optional>
#include <string>
#include <utility>

namespace action_ability_contract {
eve::LogicalId id(const char* text) {
    auto parsed = eve::LogicalId::parse(text);
    REQUIRE(parsed.has_value());
    return *parsed;
}

eve::action::AbilityDefinition ability(
    const char* name, eve::action::AbilityInstancingPolicy instancing = eve::action::AbilityInstancingPolicy::PerOwner,
    eve::action::AbilityActivationGroup group = eve::action::AbilityActivationGroup::Independent) {
    eve::action::AbilityDefinition result;
    result.id              = eve::LogicalId::fromParts("ability", name).value();
    result.action.id       = eve::LogicalId::fromParts("action", name).value();
    result.instancing      = instancing;
    result.activationGroup = group;
    return result;
}

// ability_asset.cpp
TEST_CASE("action.abilityAsset.roundTripPreservesIdentityTimingCostAndTriggers") {
    auto source          = ability("fire");
    source.action.timing = {eve::Duration::fromNanoseconds(100), eve::Duration::fromNanoseconds(200),
                            eve::Duration::fromNanoseconds(300)};
    source.cooldown      = eve::Duration::fromNanoseconds(400);
    auto cost            = eve::resource::CostSpec::from({{"mana", 12}});
    REQUIRE(cost.ok());
    source.action.cost      = cost.value();
    source.action.effectIds = {"effect:burn", "effect:impact"};
    source.triggers         = {{"Event.Combat.Cast", eve::tags::GameplayTagMatch::IncludeDescendants}};
    REQUIRE(source.validate().ok());
    auto encoded = eve::action::encodeAbilityAsset(source);
    REQUIRE(encoded.ok());
    auto decoded = eve::action::decodeAbilityAsset(encoded.value());
    REQUIRE(decoded.ok());
    CHECK(decoded.value().id == source.id);
    CHECK(decoded.value().action.id == source.action.id);
    CHECK(decoded.value().action.timing.active == source.action.timing.active);
    CHECK(decoded.value().cooldown == source.cooldown);
    REQUIRE(decoded.value().action.cost.has_value());
    CHECK_EQ(decoded.value().action.cost->size(), std::size_t{1});
    CHECK(decoded.value().action.effectIds == source.action.effectIds);
    REQUIRE_EQ(decoded.value().triggers.size(), std::size_t{1});
    CHECK_EQ(decoded.value().triggers[0].gameplayTag, std::string("Event.Combat.Cast"));
    eve::action::ActionRuntime  actions;
    eve::action::AbilityRuntime runtime(actions);
    CHECK(runtime.registerDefinition(std::move(decoded).takeValue()).ok());
}

TEST_CASE("action.abilityAsset.rejectsFutureVersionMalformedIdentityAndInvalidDuration") {
    auto source  = ability("fire");
    auto encoded = eve::action::encodeAbilityAsset(source);
    REQUIRE(encoded.ok());
    auto future = encoded.value();
    future.set("schemaVersion", 999);
    CHECK(!eve::action::decodeAbilityAsset(future).ok());
    auto malformed = encoded.value();
    malformed.set("action", eve::Value::Object{});
    CHECK(!eve::action::decodeAbilityAsset(malformed).ok());
    source.cooldown = eve::Duration::fromNanoseconds(-1);
    CHECK(!source.validate().ok());
}

eve::action::ActionTimeline archiveTimeline(eve::LogicalId actionId) {
    using namespace eve;
    action::ActionTimeline timeline;
    timeline.actionId        = std::move(actionId);
    timeline.duration        = Duration(600);
    timeline.splitTimestamps = {Duration(100), Duration(300)};
    timeline.metadata        = {{"source", "saved"}};
    action::ActionAnimationSection section;
    section.id           = id("section:body");
    section.animationUri = "asset://saved-clip";
    section.end          = Duration(600);
    section.blendIn      = Duration(10);
    section.blendOut     = Duration(20);
    section.sourceStart  = Duration(30);
    section.sourceEnd    = Duration(630);
    timeline.animationSections.push_back(section);
    action::ActionTrack track;
    track.id    = id("track:effects");
    track.label = "saved track";
    track.notifies.push_back({id("notify:hit"), id("event:hit"), Duration(200), {{"amount", 2}}, true});
    track.states.push_back(
        {id("state:window"), id("event:window"), Duration(100), Duration(400), {{"ready", true}}, true});
    timeline.tracks.push_back(std::move(track));
    return timeline;
}
TEST_CASE("action.definitionArchive.completeFieldsRoundTripWithoutInvokingProviders") {
    using namespace eve;
    for (const auto space : {sensing::CoordinateSpace::World2D, sensing::CoordinateSpace::World3D,
                             sensing::CoordinateSpace::Grid2D, sensing::CoordinateSpace::Grid3D}) {
        action::ActionDefinition source;
        source.id        = id("action:archive");
        source.timing    = {Duration(100), Duration(200), Duration(300)};
        source.condition = condition::Condition::all(
            {condition::Condition::compare("mana", condition::CompareOperator::GreaterEqual, 12),
             condition::Condition::policyCall(
                 "policy.ready", Value::object({{"key", "ready"}}),
                 condition::ScriptConditionDeclaration{
                     "policy.ready", {"state.ready"}, condition::DeterminismLevel::TickDeterministic})});
        source.cost      = resource::CostSpec::from({{"mana", 12}, {"stamina", 3}}).expect("canonical cost");
        source.effectIds = {"effect:burn", "effect:impact"};
        source.activeExecutionRequired = true;
        source.metadata                = {{"seed", "42"}, {"escaped", "line\n\t\""}};
        source.targetingMode           = action::TargetingMode::Query;
        sensing::TargetingSpec spec;
        spec.space        = space;
        spec.domain       = sensing::TargetDomain::Enemy;
        spec.minCount     = 1;
        spec.maxCount     = 2;
        spec.countPolicy  = sensing::CountPolicy::TruncateToMax;
        spec.minRange     = 1.25f;
        spec.requiredTags = {"unit", "visible"};
        spec.excludedTags = {"dead"};
        spec.zone         = sensing::ZoneRef::fromLogicalId(id("zone:camp"));
        spec.lineOfSight  = sensing::LineOfSightMode::Required;
        if (space == sensing::CoordinateSpace::World2D)
            spec.worldArea =
                sensing::WorldArea::circle2D(sensing::WorldPoint::world2D(2, 3).expect("center"), 5).expect("circle");
        if (space == sensing::CoordinateSpace::World3D)
            spec.worldArea = sensing::WorldArea::sphere3D(sensing::WorldPoint::world3D(2, 3, 4).expect("center"), 5)
                                 .expect("sphere");
        if (space == sensing::CoordinateSpace::Grid2D)
            spec.gridArea =
                sensing::GridArea::box2D(sensing::GridPoint::grid2D(-2, -3), sensing::GridPoint::grid2D(4, 5))
                    .expect("grid2d");
        if (space == sensing::CoordinateSpace::Grid3D)
            spec.gridArea =
                sensing::GridArea::box3D(sensing::GridPoint::grid3D(-2, -3, -4), sensing::GridPoint::grid3D(4, 5, 6))
                    .expect("grid3d");
        source.targetingSpec = spec;
        source.timeline      = archiveTimeline(source.id);
        const auto text      = source.encodeState().expect("complete definition").toJson().expect("wire");
        source               = {};
        auto parsed          = action::ActionDefinition::prepareState(Value::fromJson(text).expect("owning wire"))
                          .expect("owning definition");
        REQUIRE_EQ(parsed.encodeState().expect("repeat definition").toJson().expect("repeat wire"), text);
        REQUIRE(parsed.targetingSpec->space == space);
        REQUIRE(parsed.targetingSpec->countPolicy == sensing::CountPolicy::TruncateToMax);
        REQUIRE(parsed.targetingSpec->maxRange == std::numeric_limits<float>::infinity());
        REQUIRE(parsed.condition.kind() == condition::ConditionKind::All);
        REQUIRE_EQ(parsed.condition.children().size(), std::size_t(2));
        REQUIRE_EQ(parsed.cost->size(), std::size_t(2));
        REQUIRE_EQ(parsed.timeline->splitTimestamps.size(), std::size_t(2));
        REQUIRE(parsed.timeline->duration == Duration(600));
        action::ActionRequest request;
        request.actionId  = parsed.id;
        const auto origin = SubjectRef::fromPersistentId(*PersistentId::parse("01900000-0000-7000-8000-000000000901"));
        sensing::TargetLocation location;
        if (space == sensing::CoordinateSpace::World2D) location = sensing::WorldPoint::world2D(0, 0).expect("origin");
        if (space == sensing::CoordinateSpace::World3D)
            location = sensing::WorldPoint::world3D(0, 0, 0).expect("origin");
        if (space == sensing::CoordinateSpace::Grid2D) location = sensing::GridPoint::grid2D(0, 0);
        if (space == sensing::CoordinateSpace::Grid3D) location = sensing::GridPoint::grid3D(0, 0, 0);
        request.targetingQuery = sensing::TargetingQuery{origin, location, *parsed.targetingSpec};
        action::ActionRuntime runtime;
        const auto execution = runtime.submit(std::move(parsed), std::move(request)).expect("real runtime submission");
        REQUIRE(runtime.find(execution)->phase() == action::ActionPhase::Requested);
    }
}
TEST_CASE("action.definitionArchive.malformedTimelineFieldsReturnCheckedFailures") {
    using namespace eve;
    auto source       = ability("timeline-errors").action;
    source.timing     = {Duration(100), Duration(200), Duration(300)};
    source.timeline   = archiveTimeline(source.id);
    const auto value  = source.encodeState().expect("complete nested timeline");
    auto       nested = [](Value& value) -> Value& {
        return value.getIf<Value::Object>()->at("definition").getIf<Value::Object>()->at("timeline");
    };
    for (const auto key : {"looping", "footIk", "animationLayer", "defaultBlendInNs", "defaultBlendOutNs",
                           "blendOutOffsetNs", "rootMotionHorizontal", "rootMotionVertical", "rootMotionRotation"}) {
        auto bad = value;
        nested(bad).getIf<Value::Object>()->at("montage").getIf<Value::Object>()->erase(key);
        REQUIRE(!action::ActionDefinition::prepareState(bad).ok());
    }
    for (const auto key : {"blendOutNs", "blendInCurve", "blendOutCurve"}) {
        auto bad = value;
        nested(bad)
            .getIf<Value::Object>()
            ->at("animationSections")
            .getIf<Value::Array>()
            ->front()
            .getIf<Value::Object>()
            ->erase(key);
        REQUIRE(!action::ActionDefinition::prepareState(bad).ok());
    }
    for (const auto key : {"id", "label", "kind", "muted", "locked"}) {
        auto bad = value;
        nested(bad).getIf<Value::Object>()->at("tracks").getIf<Value::Array>()->front().getIf<Value::Object>()->erase(
            key);
        REQUIRE(!action::ActionDefinition::prepareState(bad).ok());
    }
    auto bad = value;
    nested(bad).getIf<Value::Object>()->at("montage").set("animationLayer", std::int64_t(1) << 40);
    REQUIRE(!action::ActionDefinition::prepareState(bad).ok());
    REQUIRE_EQ(source.encodeState().expect("source unchanged").toJson().expect("wire"),
               value.toJson().expect("original wire"));
}
TEST_CASE("action.definitionArchive.conesAndBoxesPreserveAllGeometryFields") {
    using namespace eve;
    const auto        center = sensing::WorldPoint::world2D(0, 0).expect("apex");
    const std::vector areas{
        sensing::WorldArea::cone2D(center, 3, 4, .35f, 7).expect("cone"),
        sensing::WorldArea::box2D(center, sensing::WorldPoint::world2D(4, 5).expect("corner")).expect("box2d"),
        sensing::WorldArea::box3D(sensing::WorldPoint::world3D(0, 0, 0).expect("minimum"),
                                  sensing::WorldPoint::world3D(4, 5, 6).expect("maximum"))
            .expect("box3d")};
    for (const auto& area : areas) {
        auto source          = ability("geometry").action;
        source.targetingMode = action::TargetingMode::Query;
        sensing::TargetingSpec spec;
        spec.space           = area.space();
        spec.worldArea       = area;
        source.targetingSpec = spec;
        const auto value     = source.encodeState().expect("geometry value");
        auto       parsed =
            action::ActionDefinition::prepareState(Value::fromJson(value.toJson().expect("wire")).expect("input"))
                .expect("geometry admission");
        const auto& restored = *parsed.targetingSpec->worldArea;
        REQUIRE(restored.shape() == area.shape());
        REQUIRE(restored.space() == area.space());
        REQUIRE_EQ(restored.second().x(), area.second().x());
        REQUIRE_EQ(restored.second().z(), area.second().z());
        REQUIRE_EQ(restored.radius(), area.radius());
        REQUIRE_EQ(restored.halfAngle(), area.halfAngle());
        REQUIRE_EQ(parsed.encodeState().expect("repeat geometry").toJson().expect("repeat wire"),
                   value.toJson().expect("wire"));
        if (area.shape() == sensing::WorldArea::Shape::Cone2D) {
            REQUIRE(restored.contains(sensing::WorldPoint::world2D(3, 4).expect("inside")));
            REQUIRE(!restored.contains(sensing::WorldPoint::world2D(-3, -4).expect("outside")));
        }
    }
}
TEST_CASE("action.abilityAsset.versionThreePreservesTruncationAndMigratesHistoricalDefaults") {
    using namespace eve;
    auto source                 = ability("query");
    source.action.targetingMode = action::TargetingMode::Query;
    sensing::TargetingSpec spec;
    spec.minCount               = 1;
    spec.maxCount               = 2;
    spec.countPolicy            = sensing::CountPolicy::TruncateToMax;
    source.action.targetingSpec = spec;
    auto value                  = action::encodeAbilityAsset(source).expect("complete ability");
    REQUIRE_EQ(value.getIf<Value::Object>()->at("schemaVersion").asInt(), 3);
    auto decoded = action::decodeAbilityAsset(value).expect("new ability reader");
    REQUIRE(decoded.action.targetingSpec->countPolicy == sensing::CountPolicy::TruncateToMax);
    sensing::SensingCandidateProvider provider;
    struct Reset {
        sensing::SensingCandidateProvider& provider;
        ~Reset() { cap::revoke<sensing::ISensingCandidateProvider>(&provider); }
    } reset{provider};
    cap::provide<sensing::ISensingCandidateProvider>(&provider);
    const auto owner = *PersistentId::parse("01900000-0000-7000-8000-000000000902");
    for (int n = 1; n <= 3; ++n)
        provider
            .upsert({SubjectRef::fromPersistentId(owner.child(std::to_string(n))),
                     sensing::WorldPoint::world2D(float(n), 0).expect("candidate"),
                     sensing::TargetDomain::Enemy,
                     {},
                     {}})
            .expect("candidate registration");
    sensing::TargetingQuery    query{SubjectRef::fromPersistentId(owner),
                                  sensing::WorldPoint::world2D(0, 0).expect("origin"), *decoded.action.targetingSpec};
    sensing::TargetingResolver resolver;
    REQUIRE_EQ(resolver.resolve(query).expect("saved truncation behavior").subjects().size(), std::size_t(2));
    auto legacy = value;
    legacy.set("schemaVersion", 2);
    legacy.getIf<Value::Object>()
        ->at("action")
        .getIf<Value::Object>()
        ->at("targetingSpec")
        .getIf<Value::Object>()
        ->erase("countPolicy");
    auto migrated = action::decodeAbilityAsset(legacy).expect("historical nanosecond asset");
    REQUIRE(migrated.action.targetingSpec->countPolicy == sensing::CountPolicy::FailIfOutOfRange);
    query.spec = *migrated.action.targetingSpec;
    REQUIRE(!resolver.resolve(query).ok());
    legacy.set("schemaVersion", 1);
    legacy.getIf<Value::Object>()->erase("cooldownNs");
    legacy.set("cooldownMs", 4);
    auto& timing =
        *legacy.getIf<Value::Object>()->at("action").getIf<Value::Object>()->at("timing").getIf<Value::Object>();
    timing.clear();
    timing.emplace("windupMs", 1);
    timing.emplace("activeMs", 2);
    timing.emplace("recoverMs", 3);
    migrated = action::decodeAbilityAsset(legacy).expect("historical millisecond asset");
    REQUIRE(migrated.action.timing.active == Duration(2000000));
    REQUIRE(migrated.cooldown == Duration(4000000));
    REQUIRE_EQ(action::encodeAbilityAsset(migrated)
                   .expect("migrated current writer")
                   .getIf<Value::Object>()
                   ->at("schemaVersion")
                   .asInt(),
               3);
}
TEST_CASE("action.definitionArchive.rejectsUnknownFieldsEnumsLossyGeometryAndNonserializableMetadata") {
    using namespace eve;
    auto source          = ability("invalid").action;
    source.targetingMode = action::TargetingMode::Query;
    sensing::TargetingSpec spec;
    spec.countPolicy = sensing::CountPolicy::TruncateToMax;
    spec.worldArea =
        sensing::WorldArea::circle2D(sensing::WorldPoint::world2D(0, 0).expect("center"), 5).expect("circle");
    source.targetingSpec = spec;
    const auto valid     = source.encodeState().expect("valid definition");
    auto       reject    = [&](Value candidate) { REQUIRE(!action::ActionDefinition::prepareState(candidate).ok()); };
    auto       value     = valid;
    value.set("unknown", 1);
    reject(value);
    value = valid;
    value.set("version", 2);
    reject(value);
    value = valid;
    value.getIf<Value::Object>()->at("definition").set("unknown", 1);
    reject(value);
    value = valid;
    value.getIf<Value::Object>()->at("definition").getIf<Value::Object>()->erase("timeline");
    reject(value);
    value = valid;
    value.getIf<Value::Object>()
        ->at("definition")
        .getIf<Value::Object>()
        ->at("targetingSpec")
        .set("countPolicy", "unknown");
    reject(value);
    value = valid;
    value.getIf<Value::Object>()->at("definition").getIf<Value::Object>()->at("targetingSpec").set("maxRange", 1e300);
    reject(value);
    value      = valid;
    auto& area = value.getIf<Value::Object>()
                     ->at("definition")
                     .getIf<Value::Object>()
                     ->at("targetingSpec")
                     .getIf<Value::Object>()
                     ->at("worldArea");
    (*area.getIf<Value::Object>()->at("first").getIf<Value::Array>())[2] = 1;
    reject(value);
    value = valid;
    value.getIf<Value::Object>()
        ->at("definition")
        .getIf<Value::Object>()
        ->at("metadata")
        .set("bad", std::numeric_limits<double>::quiet_NaN());
    reject(value);
    source.metadata = {{"bad", std::numeric_limits<double>::infinity()}};
    REQUIRE(!source.encodeState().ok());
    source.metadata.clear();
    source.targetingMode = static_cast<action::TargetingMode>(99);
    REQUIRE(!source.validate().ok());
    source.targetingMode              = action::TargetingMode::Query;
    source.targetingSpec->countPolicy = static_cast<sensing::CountPolicy>(99);
    REQUIRE(!source.validate().ok());
}

// ability_controller.cpp
struct ControllerFixture {
    eve::action::ActionRuntime            actions;
    eve::action::AbilityRuntime           abilities{actions};
    eve::action::AbilityControllerRuntime controller{abilities};
    eve::action::AbilityDefinition        definition = ability("strike");
    eve::action::AbilityGrantId           grant;

    ControllerFixture() {
        REQUIRE(abilities.registerDefinition(definition).ok());
        auto created = abilities.grant("fighter", definition.id);
        REQUIRE(created.ok());
        grant = created.value();
    }

    eve::action::AbilityIntent intent() const {
        eve::action::AbilityIntent value;
        value.grantId          = grant;
        value.request.actionId = definition.action.id;
        return value;
    }
};

class ScriptedSource final : public eve::action::IAbilityIntentSource {
public:
    eve::Result<std::optional<eve::action::AbilityIntent>> nextIntent(eve::SimulationTick tick) override {
        seen = tick;
        if (fail)
            return eve::Result<std::optional<eve::action::AbilityIntent>>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::PreconditionViolation, "source failed"));
        auto next = pending;
        pending.reset();
        return eve::Result<std::optional<eve::action::AbilityIntent>>::success(std::move(next));
    }
    bool                                      fail = false;
    eve::SimulationTick                       seen;
    std::optional<eve::action::AbilityIntent> pending;
};

TEST_CASE("action.controller.playerQueueConsumesOneIntentWithInjectedTick") {
    ControllerFixture                     f;
    eve::action::PlayerAbilityIntentQueue queue;
    auto                                  empty = f.controller.processNext(queue, eve::SimulationTick(3));
    REQUIRE(empty.ok());
    CHECK(!empty.value().has_value());
    REQUIRE(queue.enqueue(f.intent()).ok());
    CHECK_EQ(queue.pendingCount(), std::size_t{1});
    auto activated = f.controller.processNext(queue, eve::SimulationTick(17));
    REQUIRE(activated.ok());
    REQUIRE(activated.value().has_value());
    CHECK_EQ(queue.pendingCount(), std::size_t{0});
    auto* execution = f.actions.find(activated.value()->executionId);
    REQUIRE(execution != nullptr);
    CHECK(execution->request().requestedTick == eve::SimulationTick(17));
}

TEST_CASE("action.controller.sourceFailureAndRejectedIntentDoNotMutateAbilityState") {
    ControllerFixture f;
    ScriptedSource    source;
    source.pending = f.intent();
    source.fail    = true;
    CHECK(!f.controller.processNext(source, eve::SimulationTick(1)).ok());
    CHECK(source.pending.has_value());
    CHECK(f.abilities.activeActivations().empty());
    source.fail                      = false;
    source.pending->request.actionId = id("action:wrong");
    CHECK(!f.controller.processNext(source, eve::SimulationTick(2)).ok());
    CHECK(!source.pending.has_value());
    CHECK(f.abilities.activeActivations().empty());
}

// ability_system.cpp
TEST_CASE("action.ability.grantsPerOwnerIdentityAndDeterministicCooldown") {
    eve::action::ActionRuntime  actions;
    eve::action::AbilityRuntime runtime(actions);
    auto                        definition = ability("dash");
    definition.cooldown                    = eve::Duration::fromNanoseconds(100);
    REQUIRE(runtime.registerDefinition(definition).ok());
    CHECK(!runtime.registerDefinition(definition).ok());
    auto grant = runtime.grant("fighter", definition.id);
    REQUIRE(grant.ok());
    auto first = runtime.activate(grant.value(), eve::SimulationTick(1));
    REQUIRE(first.ok());
    CHECK(!first.value().instanceId.isZero());
    CHECK(!runtime.activate(grant.value(), eve::SimulationTick(2)).ok());
    REQUIRE(runtime.advanceCooldowns(eve::Duration::fromNanoseconds(99)).ok());
    CHECK(!runtime.activate(grant.value(), eve::SimulationTick(3)).ok());
    REQUIRE(runtime.advanceCooldowns(eve::Duration::fromNanoseconds(1)).ok());
    CHECK(!runtime.activate(grant.value(), eve::SimulationTick(4)).ok());
    REQUIRE(runtime.cancel(first.value().id, eve::SimulationTick(5)).ok());
    auto second = runtime.activate(grant.value(), eve::SimulationTick(6));
    REQUIRE(second.ok());
    CHECK(second.value().instanceId == first.value().instanceId);
    CHECK(!runtime.advanceCooldowns(eve::Duration::fromNanoseconds(-1)).ok());
}

TEST_CASE("action.ability.hotReloadKeepsActiveSnapshotAndGrant") {
    eve::action::ActionRuntime  actions;
    eve::action::AbilityRuntime runtime(actions);
    auto                        original = ability("strike", eve::action::AbilityInstancingPolicy::PerExecution);
    original.action.timing.active        = eve::Duration::fromNanoseconds(100);
    REQUIRE(runtime.registerDefinition(original).ok());
    auto grant = runtime.grant("fighter", original.id);
    REQUIRE(grant.ok());
    auto first = runtime.activate(grant.value(), eve::SimulationTick(1));
    REQUIRE(first.ok());
    auto replacement                 = original;
    replacement.action.id            = id("action:strike-v2");
    replacement.action.timing.active = eve::Duration::fromNanoseconds(250);
    REQUIRE(runtime.replaceDefinition(replacement).ok());
    CHECK(actions.find(first.value().executionId)->definition().id == original.action.id);
    REQUIRE(runtime.cancel(first.value().id, eve::SimulationTick(2)).ok());
    auto second = runtime.activate(grant.value(), eve::SimulationTick(3));
    REQUIRE(second.ok());
    CHECK(actions.find(second.value().executionId)->definition().id == replacement.action.id);
    auto incompatible       = replacement;
    incompatible.instancing = eve::action::AbilityInstancingPolicy::PerOwner;
    CHECK(!runtime.replaceDefinition(incompatible).ok());
}

TEST_CASE("action.ability.exclusiveReplacementBlockingAndRevokeLifecycle") {
    eve::action::ActionRuntime  actions;
    eve::action::AbilityRuntime runtime(actions);
    auto                        light = ability("light", eve::action::AbilityInstancingPolicy::PerExecution,
                                                eve::action::AbilityActivationGroup::ExclusiveReplaceable);
    auto                        heavy = ability("heavy", eve::action::AbilityInstancingPolicy::PerExecution,
                                                eve::action::AbilityActivationGroup::ExclusiveBlocking);
    REQUIRE(runtime.registerDefinition(light).ok());
    REQUIRE(runtime.registerDefinition(heavy).ok());
    auto lightGrant = runtime.grant("fighter", light.id);
    auto heavyGrant = runtime.grant("fighter", heavy.id);
    REQUIRE(lightGrant.ok());
    REQUIRE(heavyGrant.ok());
    auto first = runtime.activate(lightGrant.value(), eve::SimulationTick(1));
    REQUIRE(first.ok());
    CHECK(!runtime.revoke(lightGrant.value()).ok());
    auto blocker = runtime.activate(heavyGrant.value(), eve::SimulationTick(2));
    REQUIRE(blocker.ok());
    CHECK(actions.find(first.value().executionId)->phase() == eve::action::ActionPhase::Cancelled);
    CHECK(!runtime.activate(lightGrant.value(), eve::SimulationTick(3)).ok());
    REQUIRE(runtime.cancel(blocker.value().id, eve::SimulationTick(4)).ok());
    REQUIRE(runtime.revoke(heavyGrant.value()).ok());
}

TEST_CASE("action.ability.eventTriggersAndStepReportUseCanonicalState") {
    eve::action::ActionRuntime  actions;
    eve::action::AbilityRuntime runtime(actions);
    auto                        definition = ability("react");
    definition.action.timing.active        = eve::Duration::fromNanoseconds(200);
    definition.triggers                    = {{"Event.Combat.Hit", eve::tags::GameplayTagMatch::IncludeDescendants}};
    REQUIRE(runtime.registerDefinition(definition).ok());
    auto grant = runtime.grant("fighter", definition.id);
    REQUIRE(grant.ok());
    auto matched = runtime.matchingGrants("fighter", "Event.Combat.Hit.Heavy");
    REQUIRE_EQ(matched.size(), std::size_t{1});
    CHECK(matched[0] == grant.value());
    CHECK(runtime.matchingGrants("other", "Event.Combat.Hit.Heavy").empty());
    auto activation = runtime.activate(grant.value(), eve::SimulationTick(1));
    REQUIRE(activation.ok());
    auto step = runtime.step(eve::SimulationTick(2), eve::Duration::fromNanoseconds(100));
    REQUIRE(step.ok());
    REQUIRE_EQ(step.value().advances.size(), std::size_t{1});
    CHECK(step.value().advances[0].activationId == activation.value().id);
    CHECK_EQ(step.value().activeCount, std::size_t{1});
    auto end = runtime.step(eve::SimulationTick(3), eve::Duration::fromNanoseconds(100));
    REQUIRE(end.ok());
    CHECK_EQ(end.value().completedCount, std::size_t{1});
    CHECK(runtime.activeActivations().empty());
}

TEST_CASE("action.ability.rejectsDuplicateStepOnSameTick") {
    eve::action::ActionRuntime  actions;
    eve::action::AbilityRuntime runtime(actions);
    auto                        definition = ability("once");
    definition.action.timing.active        = eve::Duration::fromNanoseconds(200);
    definition.cooldown                    = eve::Duration::fromNanoseconds(50);
    REQUIRE(runtime.registerDefinition(definition).ok());
    auto grant = runtime.grant("fighter", definition.id);
    REQUIRE(grant.ok());
    REQUIRE(runtime.activate(grant.value(), eve::SimulationTick(1)).ok());
    REQUIRE(runtime.step(eve::SimulationTick(2), eve::Duration::fromNanoseconds(50)).ok());
    auto grantState = runtime.findGrant(grant.value());
    REQUIRE(grantState.ok());
    const auto cooledOnce = grantState.value().cooldownRemaining;
    auto       duplicate  = runtime.step(eve::SimulationTick(2), eve::Duration::fromNanoseconds(50));
    REQUIRE(!duplicate.ok());
    CHECK_EQ(duplicate.status().code(), eve::StatusCode::Conflict);
    grantState = runtime.findGrant(grant.value());
    REQUIRE(grantState.ok());
    CHECK(grantState.value().cooldownRemaining == cooledOnce);
    CHECK(!runtime.step(eve::SimulationTick(1), eve::Duration::fromNanoseconds(50)).ok());
    REQUIRE(runtime.step(eve::SimulationTick(3), eve::Duration::fromNanoseconds(50)).ok());
}
}  // namespace action_ability_contract
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_action_notify_registry)
#include "PrivateTestScope.h"
#include "action/ActionNotifyRegistry.h"
#include "zeroerr/unittest.h"

#include <memory>
#include <string>

// action_notify_registry.cpp
namespace action_notify_contract {
eve::LogicalId id(const char* text) {
    auto parsed = eve::LogicalId::parse(text);
    REQUIRE(parsed.has_value());
    return *parsed;
}

class RecordingHandler final : public eve::action::IActionNotifyHandler {
public:
    eve::Result<void> handle(const eve::action::ActionTimelineEvent& event,
                             const eve::action::ActionNotifyContext&) override {
        ++calls;
        lastType = event.type.format();
        return eve::Result<void>::success();
    }
    int         calls = 0;
    std::string lastType;
};

TEST_CASE("action.notify.builtinCatalogHasStableShapeAndRequiredFields") {
    auto built = eve::action::ActionNotifyRegistry::withBuiltins();
    REQUIRE(built.ok());
    const auto descriptors = built.value().descriptors();
    REQUIRE_EQ(descriptors.size(), std::size_t{18});
    CHECK_EQ(descriptors.front().type, std::string("collision:ignore-window"));
    CHECK_EQ(descriptors.back().type, std::string("presentation:vfx-state"));
    auto damage = built.value().descriptor("combat:damage");
    REQUIRE(damage.ok());
    CHECK(damage.value().shape == eve::action::ActionNotifyShape::Instant);
    CHECK_EQ(damage.value().requiredPayloadFields.size(), std::size_t{2});
    auto hitbox = built.value().descriptor("combat:hitbox-window");
    REQUIRE(hitbox.ok());
    CHECK(hitbox.value().shape == eve::action::ActionNotifyShape::State);
}

TEST_CASE("action.notify.registrationRejectsInvalidAndDuplicateDescriptors") {
    eve::action::ActionNotifyRegistry registry;
    CHECK(!registry.registerDescriptor({"", "empty", "test", eve::action::ActionNotifyShape::Instant, {}}).ok());
    REQUIRE(
        registry
            .registerDescriptor({"test:flash", "Flash", "test", eve::action::ActionNotifyShape::Instant, {"strength"}})
            .ok());
    CHECK(
        !registry
             .registerDescriptor({"test:flash", "Flash", "test", eve::action::ActionNotifyShape::Instant, {"strength"}})
             .ok());
    CHECK(!registry.descriptor("test:missing").ok());
}

TEST_CASE("action.notify.handlerOwnershipAndDispatchAreExplicit") {
    eve::action::ActionNotifyRegistry registry;
    REQUIRE(
        registry
            .registerDescriptor({"test:flash", "Flash", "test", eve::action::ActionNotifyShape::Instant, {"strength"}})
            .ok());
    auto handler = std::make_shared<RecordingHandler>();
    REQUIRE(registry.registerHandler("test:flash", handler).ok());
    CHECK(registry.hasHandler("test:flash"));
    eve::action::ActionTimelineEvent event{
        eve::action::ActionTimelineEventKind::Notify, id("test:track"),   id("test:notify"), id("test:flash"),
        eve::Duration::fromNanoseconds(10),           {{"strength", 1.0}}};
    CHECK(registry.validate(event).ok());
    eve::action::ActionNotifyContext context;
    CHECK(registry.dispatch(event, context).ok());
    CHECK_EQ(handler->calls, 1);
    CHECK_EQ(handler->lastType, std::string("test:flash"));
    REQUIRE(registry.unregisterHandler("test:flash").ok());
    CHECK(!registry.hasHandler("test:flash"));
}
}  // namespace action_notify_contract
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_action_preview_profile) || \
    defined(EVE_TEST_ORIGIN_gameplay_composition_action_preview_profile_tests)
#include <map>
#include "action/editor/ActionPreviewProfile.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

namespace test_action_preview_profile_contracts {
namespace {

eve::action::ActionTimeline previewTimeline() {
    eve::action::ActionTimeline timeline;
    timeline.actionId = *eve::LogicalId::parse("combat:preview-test");
    timeline.duration = eve::Duration::fromSeconds(2.0).takeValue();
    eve::action::ActionAnimationSection first;
    first.id           = *eve::LogicalId::parse("animation:first");
    first.animationUri = "asset://animations/first";
    first.end          = eve::Duration::fromSeconds(1.0).takeValue();
    eve::action::ActionAnimationSection second;
    second.id                  = *eve::LogicalId::parse("animation:second");
    second.animationUri        = "asset://animations/second";
    second.start               = first.end;
    second.end                 = timeline.duration;
    timeline.animationSections = {first, second};
    return timeline;
}

eve::editor::ActionPreviewSkeleton skeleton(std::string uri, std::string root = "Root", std::string child = "Spine") {
    return {std::move(uri), {{std::move(root), -1}, {std::move(child), 0}}};
}

class PreviewAssets final : public eve::editor::IActionPreviewAssetProvider {
public:
    std::map<std::string, eve::editor::ActionPreviewSkeleton> models;
    std::map<std::string, eve::editor::ActionPreviewSkeleton> animations;
    bool                                                      acceptMask = true;

    eve::Result<eve::editor::ActionPreviewSkeleton> modelSkeleton(std::string_view uri) override {
        auto found = models.find(std::string(uri));
        if (found == models.end())
            return eve::Result<eve::editor::ActionPreviewSkeleton>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::NotFound, "preview model was not found", "previewModelUri"));
        return eve::Result<eve::editor::ActionPreviewSkeleton>::success(found->second);
    }

    eve::Result<eve::editor::ActionPreviewSkeleton> animationSkeleton(std::string_view uri) override {
        auto found = animations.find(std::string(uri));
        if (found == animations.end())
            return eve::Result<eve::editor::ActionPreviewSkeleton>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::NotFound, "animation was not found", "animationUri"));
        return eve::Result<eve::editor::ActionPreviewSkeleton>::success(found->second);
    }

    eve::Result<void> validateBoneMask(std::string_view, const eve::editor::ActionPreviewSkeleton&) override {
        if (!acceptMask)
            return eve::Result<void>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Conflict, "mask targets another skeleton", "boneMaskUri"));
        return eve::Result<void>::success();
    }
};

eve::editor::ActionPreviewProfile previewProfile() {
    eve::editor::ActionPreviewProfile profile;
    profile.actionAsset         = eve::editor::AssetGuid("asset-preview-test");
    profile.previewModelUri     = "asset://models/hero";
    profile.baseMotionUri       = "asset://animations/idle";
    profile.animationProfileUri = "asset://animation-profiles/humanoid";
    profile.boneMaskUri         = "asset://animation-masks/upper-body";
    profile.showSkeleton        = true;
    profile.showJointNames      = true;
    profile.shading             = eve::editor::ActionPreviewShading::Unlit;
    return profile;
}

}  // namespace

TEST_CASE("actionPreviewProfile.versionedSidecarRoundTripsAndRejectsUnknownVersion") {
    auto profile = previewProfile();
    auto encoded = profile.toValue();
    REQUIRE(encoded.ok());
    auto decoded = eve::editor::ActionPreviewProfile::fromValue(encoded.value());
    REQUIRE(decoded.ok());
    CHECK(decoded.value() == profile);

    eve::Value unknown = encoded.value();
    unknown.set("schemaVersion", static_cast<std::int64_t>(2));
    auto rejected = eve::editor::ActionPreviewProfile::fromValue(unknown);
    CHECK(!rejected.ok());
}

TEST_CASE("actionPreviewProfile.compatibilityIsExplicitForExactRetargetedAndUnavailableProviders") {
    auto          timeline = previewTimeline();
    auto          profile  = previewProfile();
    PreviewAssets assets;
    assets.models.emplace(profile.previewModelUri, skeleton(profile.previewModelUri));
    assets.animations.emplace("asset://animations/first", skeleton("asset://animations/first"));
    assets.animations.emplace("asset://animations/second", skeleton("asset://animations/second", "root", "spine"));

    eve::editor::ActionPreviewCompatibilityAnalyzer analyzer;
    auto                                            report = analyzer.analyze(timeline, profile, &assets);
    REQUIRE(report.ok());
    REQUIRE(report.value().sections.size() == 2);
    CHECK(report.value().sections[0].compatibility == eve::editor::ActionPreviewCompatibility::Compatible);
    CHECK(report.value().sections[1].compatibility == eve::editor::ActionPreviewCompatibility::Retargetable);
    CHECK(report.value().canPreview());

    auto unsupported = analyzer.analyze(timeline, profile, nullptr);
    REQUIRE(unsupported.ok());
    CHECK(!unsupported.value().canPreview());
    CHECK(unsupported.value().sections[0].compatibility == eve::editor::ActionPreviewCompatibility::Unsupported);

    assets.acceptMask = false;
    auto badMask      = analyzer.analyze(timeline, profile, &assets);
    REQUIRE(badMask.ok());
    CHECK(!badMask.value().canPreview());
}
}  // namespace test_action_preview_profile_contracts
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_action_checkpoint)
// action_checkpoint.cpp
#include <array>
#include <functional>
#include "action/Action.h"
#include "action/ActionCapture.h"
#include "economy/EconomyLedgerResourceAccount.h"
#include "zeroerr/unittest.h"
namespace test_action_checkpoint {
using namespace eve;
using namespace eve::action;
class Endpoint final : public ecs::Entity {
public:
    ENTITY(Endpoint, ecs::Entity)
    void release() override { ecs::DestroyEntity(this); }
};
PersistentId     epoch() { return *PersistentId::parse("01900000-0000-7000-8000-000000000a31"); }
SubjectRef       subject(const char* name) { return SubjectRef::fromPersistentId(epoch().child(name)); }
ActionDefinition definition(const char* name) {
    ActionDefinition value;
    value.id     = *LogicalId::fromParts("checkpoint", name);
    value.timing = {Duration(10), Duration(50), Duration(20)};
    return value;
}
ActionRequest request(const ActionDefinition& value) {
    ActionRequest result;
    result.actionId = value.id;
    return result;
}
struct Effect final : transaction::ITransactionParticipant {
    int                   commits = 0;
    std::function<void()> onPrepare;
    Result<void>          prepare(const transaction::TransactionContext&) override {
        if (onPrepare) onPrepare();
        return Result<void>::success();
    }
    Result<void> commit(const transaction::TransactionContext&) override {
        ++commits;
        return Result<void>::success();
    }
    Result<void> rollback(const transaction::TransactionContext&) override { return Result<void>::success(); }
    Result<void> compensate(const transaction::TransactionContext&) override {
        --commits;
        return Result<void>::success();
    }
};
struct AccountPort final : IActionResourceProvider {
    resource::IResourceAccount& account;
    bool                        failCommit   = false;
    bool                        failRollback = false;
    explicit AccountPort(resource::IResourceAccount& value) : account(value) {}
    Result<resource::Affordability> canAfford(const ActionDefinition&, const ActionRequest&,
                                              const resource::CostSpec& cost) const override {
        return account.canAfford(cost);
    }
    Result<resource::Reservation> reserve(const ActionDefinition&, const ActionRequest&,
                                          const resource::CostSpec& cost) override {
        return account.reserve(cost);
    }
    Result<resource::Receipt> commit(const resource::Reservation& reservation) override {
        if (failCommit)
            return Result<resource::Receipt>::failure(
                Diagnostic::error(DiagnosticCode::Failed, "injected commit failure", "account.commit"));
        return account.commit(reservation);
    }
    Result<void> rollback(const resource::Reservation& reservation) override {
        if (failRollback)
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::Failed, "injected rollback failure", "account.rollback"));
        return account.rollback(reservation);
    }
};
// Deterministic test digest only; no cryptographic security claim.
Result<ContentId> archiveDigest(std::string_view text) {
    std::uint64_t value = 14695981039346656037ULL;
    for (unsigned char byte : text) value = (value ^ byte) * 1099511628211ULL;
    ContentId::Bytes bytes{};
    for (auto& byte : bytes) {
        value = value * 6364136223846793005ULL + 1;
        byte  = std::uint8_t(value >> 32);
    }
    return Result<ContentId>::success(ContentId(bytes));
}
ActionCapture wireRoundTrip(const ActionCapture& source) {
    const auto envelope = source.seal(Revision(7), SimulationTick(100), archiveDigest).expect("seal complete actions");
    const auto wire     = serializeSnapshotEnvelope(envelope).expect("owning file bytes");
    auto       parsed =
        ActionCapture::parse(parseSnapshotEnvelope(wire, archiveDigest).expect("read file"), epoch(), archiveDigest)
            .expect("admit actions");
    REQUIRE_EQ(serializeSnapshotEnvelope(parsed.seal(Revision(7), SimulationTick(100), archiveDigest).expect("reseal"))
                   .expect("repeat file"),
               wire);
    return parsed;
}
TEST_CASE("action.checkpoint.archiveAllPhasesStatusParametersAndSourceRetirement") {
    std::optional<ActionCapture> frozen;
    std::weak_ptr<ecs::Table>    retired;
    {
        auto world = std::make_shared<ecs::Table>();
        retired    = world;
        ActionRuntime runtime;
        const auto    def    = definition("phases");
        auto          req    = request(def);
        req.parameters       = {{"saved", Value::Array{true, 7, "payload"}}};
        req.sourceSubject    = subject("producer");
        req.transactionId    = "saved-operation";
        const auto requested = runtime.submit(def, req).expect("requested");
        const auto windup    = runtime.submit(def, req).expect("windup");
        runtime.advance(windup, SimulationTick(1), Duration(5)).expect("windup step");
        const auto active = runtime.submit(def, req).expect("active");
        runtime.advance(active, SimulationTick(1), Duration(20)).expect("active step");
        const auto recover = runtime.submit(def, req).expect("recover");
        runtime.advance(recover, SimulationTick(1), Duration(65)).expect("recover step");
        const auto done = runtime.submit(def, req).expect("completed");
        runtime.advance(done, SimulationTick(1), Duration(80)).expect("finish");
        const auto cancelled = runtime.submit(def, req).expect("cancelled");
        runtime.advance(cancelled, SimulationTick(1), Duration(20)).expect("before cancel");
        runtime.cancel(cancelled, SimulationTick(2)).expect("cancel");
        auto bad          = def;
        bad.condition     = condition::Condition::policyCall("missing.provider", Value::object({}));
        const auto failed = runtime.submit(bad, req).expect("failed");
        REQUIRE(!runtime.advance(failed, SimulationTick(1), Duration(5)).ok());
        frozen.emplace(wireRoundTrip(runtime.capture(epoch(), world, {}).expect("capture all phases")));
        REQUIRE(frozen->executions()[0].execution->phase() == ActionPhase::Requested);
        REQUIRE(frozen->executions()[1].execution->phase() == ActionPhase::Windup);
        REQUIRE(frozen->executions()[2].execution->phase() == ActionPhase::Active);
        REQUIRE(frozen->executions()[3].execution->phase() == ActionPhase::Recover);
        REQUIRE(frozen->executions()[4].execution->phase() == ActionPhase::Completed);
        REQUIRE(frozen->executions()[5].execution->phase() == ActionPhase::Cancelled);
        REQUIRE(frozen->executions()[6].execution->phase() == ActionPhase::Failed);
        REQUIRE_EQ(runtime.find(requested)->totalElapsed().nanoseconds(), 0);
    }
    REQUIRE(retired.expired());
    auto          world = std::make_shared<ecs::Table>();
    ActionRuntime restored;
    auto          plan = PreparedActionRestore::prepare(*frozen, world, {}).expect("source-free preparation");
    restored.restore(std::move(plan)).expect("atomic publish");
    REQUIRE_EQ(restored.executionCount(), std::size_t(7));
    for (const auto& row : frozen->executions()) {
        auto* e = restored.find(row.execution->id());
        REQUIRE(e != nullptr);
        REQUIRE(e->status().describe() == row.execution->status().describe());
        REQUIRE(e->request().sourceSubject == subject("producer"));
        REQUIRE_EQ(e->request().transactionId, "saved-operation");
        REQUIRE_EQ(Value(e->request().parameters).toJson().expect("parameters"),
                   Value(row.execution->request().parameters).toJson().expect("saved parameters"));
    }
    REQUIRE(restored.advance(ActionExecutionId(3), SimulationTick(3), Duration(60)).expect("resume committed").phase ==
            ActionPhase::Completed);
    auto recaptured = wireRoundTrip(restored.capture(epoch(), world, {}).expect("capture restored facts"));
    REQUIRE_EQ(recaptured.executions().size(), std::size_t(7));
}
TEST_CASE("action.checkpoint.archiveQueryPreservesResolvedSelectionAndGeometryWithoutRediscovery") {
    struct Resolver final : IActionTargetResolver {
        mutable int                calls = 0;
        Result<sensing::TargetSet> resolve(const sensing::TargetingQuery&) const override {
            ++calls;
            sensing::TargetSet set;
            set.addSubject(subject("enemy")).expect("target");
            set.setPrimary(subject("enemy")).expect("primary");
            set.setPoint(sensing::WorldPoint::world2D(3, 4).expect("point")).expect("projection");
            set.setArea(sensing::WorldArea::cone2D(sensing::WorldPoint::world2D(0, 0).expect("origin"), 3, 4, .5f, 9)
                            .expect("cone"))
                .expect("area");
            return Result<sensing::TargetSet>::success(std::move(set));
        }
    } resolver;
    ActionServices services;
    services.targeting  = &resolver;
    auto          world = std::make_shared<ecs::Table>();
    ActionRuntime original(services);
    auto          def = definition("query");
    def.targetingMode = TargetingMode::Query;
    sensing::TargetingSpec spec;
    spec.maxCount     = 2;
    spec.countPolicy  = sensing::CountPolicy::TruncateToMax;
    def.targetingSpec = spec;
    auto req          = request(def);
    req.targetingQuery =
        sensing::TargetingQuery{subject("origin"), sensing::WorldPoint::world2D(1, 2).expect("origin"), spec};
    auto id = original.submit(def, req).expect("query action");
    original.advance(id, SimulationTick(1), Duration(5)).expect("discover once");
    auto          frozen = wireRoundTrip(original.capture(epoch(), world, {}).expect("resolved facts"));
    ActionRuntime restored;
    restored.restore(PreparedActionRestore::prepare(frozen, world, {}).expect("candidate"))
        .expect("no discovery provider needed");
    auto* set = restored.find(id)->resolvedTargets();
    REQUIRE(set != nullptr);
    REQUIRE(set->primary() == subject("enemy"));
    REQUIRE(set->worldArea()->shape() == sensing::WorldArea::Shape::Cone2D);
    REQUIRE_EQ(set->worldArea()->radius(), 9.f);
    REQUIRE(std::get<sensing::WorldPoint>(*set->point()).x() == 3);
    restored.advance(id, SimulationTick(2), Duration(75)).expect("finish saved selection");
    REQUIRE_EQ(resolver.calls, 1);
}
TEST_CASE("action.checkpoint.archiveRejectsLostTimelineMarkerAndForgedCommittedAudits") {
    auto           world = std::make_shared<ecs::Table>();
    Effect         effect;
    ActionServices services;
    services.transactionEffect = &effect;
    ActionRuntime  runtime(services);
    auto           def = definition("audit-guards");
    ActionTimeline timeline;
    timeline.actionId        = def.id;
    timeline.duration        = Duration(80);
    timeline.splitTimestamps = {Duration(40)};
    ActionTrack track;
    track.id = *LogicalId::parse("track:audit");
    track.notifies.push_back(
        {*LogicalId::parse("notify:start"), *LogicalId::parse("event:start"), Duration(0), {}, true});
    timeline.tracks.push_back(track);
    def.timeline  = timeline;
    const auto id = runtime.submit(def, request(def)).expect("submit");
    runtime.setSectionTimingOverride(id, 1, Duration(80)).expect("override");
    runtime.advance(id, SimulationTick(1), Duration(30)).expect("commit effect");
    auto       capture = runtime.capture(epoch(), world, {}).expect("freeze");
    const auto base    = capture.seal(Revision(4), SimulationTick(1), archiveDigest).expect("seal");
    auto       mutate  = [&](auto edit) {
        auto  bad = base;
        auto& row =
            *bad.payload.getIf<Value::Object>()->at("executions").getIf<Value::Array>()->front().getIf<Value::Array>();
        edit(row);
        bad = makeSnapshotEnvelope(bad.type, bad.schema, bad.schemaVersion, bad.instanceId, bad.revision, bad.tick,
                                          bad.payload, archiveDigest)
                  .expect("rehash");
        REQUIRE(!ActionCapture::parse(std::move(bad), epoch(), archiveDigest).ok());
    };
    mutate([](Value::Array& row) { row[14] = false; });
    mutate([](Value::Array& row) { row[13] = false; });
    mutate([](Value::Array& row) { row[12].getIf<Value::Array>()->at(4) = 3; });
    mutate([](Value::Array& row) { row[12].getIf<Value::Array>()->at(7) = "100"; });
    mutate([](Value::Array& row) { row[12].getIf<Value::Array>()->at(0) = "invalid"; });
    mutate([](Value::Array& row) { row[15].getIf<Value::Array>()->front().getIf<Value::Array>()->at(3) = 81; });
    mutate([](Value::Array& row) { row[15].getIf<Value::Array>()->front().getIf<Value::Array>()->at(2) = 0; });
    mutate([](Value::Array& row) {
        auto& list = *row[15].getIf<Value::Array>();
        list.push_back(list.front());
    });
    REQUIRE_EQ(effect.commits, 1);
    REQUIRE(runtime.find(id)->totalElapsed() == Duration(30));
    auto          parsed = wireRoundTrip(capture);
    ActionRuntime restored;
    restored.restore(PreparedActionRestore::prepare(parsed, world, {}).expect("prepare")).expect("publish");
    // Re-capture after binding no future effect provider: pipeline describes future dispatch, not past receipts.
    auto again = wireRoundTrip(restored.capture(epoch(), world, {}).expect("capture restored transaction"));
    REQUIRE(again.executions()[0].execution->transactionReceipt() != nullptr);
    REQUIRE(restored.advance(id, SimulationTick(2), Duration(100)).expect("finish").timelineEvents.empty());
    REQUIRE_EQ(effect.commits, 1);
}
TEST_CASE("action.checkpoint.archiveQueryLocationsAndAreaProjectionsRetainAllCoordinateSpaces") {
    struct Resolver final : IActionTargetResolver {
        Result<sensing::TargetSet> resolve(const sensing::TargetingQuery& q) const override {
            sensing::TargetSet set;
            set.addSubject(q.origin).expect("subject");
            set.setPrimary(q.origin).expect("primary");
            set.setPoint(q.originLocation).expect("point");
            if (q.spec.worldArea) set.setArea(*q.spec.worldArea).expect("world area");
            if (q.spec.gridArea) set.setArea(*q.spec.gridArea).expect("grid area");
            return Result<sensing::TargetSet>::success(std::move(set));
        }
    } resolver;
    for (auto space : {sensing::CoordinateSpace::World2D, sensing::CoordinateSpace::World3D,
                       sensing::CoordinateSpace::Grid2D, sensing::CoordinateSpace::Grid3D}) {
        auto           world = std::make_shared<ecs::Table>();
        ActionServices services;
        services.targeting = &resolver;
        ActionRuntime source(services);
        auto          def = definition("spaces");
        def.targetingMode = TargetingMode::Query;
        sensing::TargetingSpec spec;
        spec.space = space;
        sensing::TargetLocation point;
        if (space == sensing::CoordinateSpace::World2D) {
            point          = sensing::WorldPoint::world2D(1.25f, -2.5f).expect("world2d");
            spec.worldArea = sensing::WorldArea::circle2D(std::get<sensing::WorldPoint>(point), 5).expect("circle");
        }
        if (space == sensing::CoordinateSpace::World3D) {
            point          = sensing::WorldPoint::world3D(1.25f, -2.5f, 3.75f).expect("world3d");
            spec.worldArea = sensing::WorldArea::sphere3D(std::get<sensing::WorldPoint>(point), 5).expect("sphere");
        }
        if (space == sensing::CoordinateSpace::Grid2D) {
            point         = sensing::GridPoint::grid2D(-2147483647, 2147483647);
            spec.gridArea = sensing::GridArea::box2D(sensing::GridPoint::grid2D(-2147483647, -2147483647),
                                                     sensing::GridPoint::grid2D(2147483647, 2147483647))
                                .expect("grid2d");
        }
        if (space == sensing::CoordinateSpace::Grid3D) {
            point         = sensing::GridPoint::grid3D(-2147483647, 2147483647, -100);
            spec.gridArea = sensing::GridArea::box3D(sensing::GridPoint::grid3D(-2147483647, -2147483647, -2147483647),
                                                     sensing::GridPoint::grid3D(2147483647, 2147483647, 2147483647))
                                .expect("grid3d");
        }
        def.targetingSpec  = spec;
        auto req           = request(def);
        req.targetingQuery = sensing::TargetingQuery{subject("origin"), point, spec};
        const auto id      = source.submit(def, req).expect("submit");
        source.advance(id, SimulationTick(1), Duration(5)).expect("resolve");
        auto          frozen = wireRoundTrip(source.capture(epoch(), world, {}).expect("freeze"));
        ActionRuntime target;
        target.restore(PreparedActionRestore::prepare(frozen, world, {}).expect("prepare")).expect("publish");
        REQUIRE(target.find(id)->request().targetingQuery->originLocation == point);
        REQUIRE(target.find(id)->resolvedTargets()->point() == point);
        REQUIRE(std::visit([](auto p) { return p.space(); }, *target.find(id)->resolvedTargets()->point()) == space);
        REQUIRE(target.find(id)->resolvedTargets()->worldArea().has_value() == spec.worldArea.has_value());
        REQUIRE(target.find(id)->resolvedTargets()->gridArea().has_value() == spec.gridArea.has_value());
        target.advance(id, SimulationTick(2), Duration(75)).expect("continue saved target");
    }
}
TEST_CASE("action.checkpoint.archiveRejectsForgedFieldsProgressReferencesAndBoundaries") {
    auto          world = std::make_shared<ecs::Table>();
    ActionRuntime original;
    const auto    def      = definition("invalid-wire");
    const auto    id       = original.submit(def, request(def)).expect("original");
    auto          captured = original.capture(epoch(), world, {}).expect("freeze");
    const auto    base     = captured.seal(Revision(4), SimulationTick(8), archiveDigest).expect("seal");
    auto          read     = [&](SnapshotEnvelope candidate) {
        candidate =
            makeSnapshotEnvelope(candidate.type, candidate.schema, candidate.schemaVersion, candidate.instanceId,
                                              candidate.revision, candidate.tick, candidate.payload, archiveDigest)
                .expect("rehash forged input");
        return ActionCapture::parse(std::move(candidate), epoch(), archiveDigest);
    };
    auto mutate = [&](auto edit) {
        auto candidate = base;
        edit(candidate.payload);
        REQUIRE(!read(std::move(candidate)).ok());
    };
    auto row = [](Value& payload) -> Value::Array& {
        return *payload.getIf<Value::Object>()->at("executions").getIf<Value::Array>()->front().getIf<Value::Array>();
    };
    mutate([](Value& p) { p.set("unknown", true); });
    mutate([](Value& p) { p.set("next", "01"); });
    mutate([](Value& p) { p.set("next", "1"); });
    mutate([](Value& p) { p.set("pipeline", 9); });
    mutate([&](Value& p) { row(p).push_back(false); });
    mutate([&](Value& p) { row(p)[0] = "0"; });
    mutate([&](Value& p) { row(p)[5] = 1; });  // No idle checkpoint exists inside validation.
    mutate([&](Value& p) { row(p)[5] = 3; });  // Requested cannot forge committed Active.
    mutate([&](Value& p) { row(p)[6] = 1; });
    mutate([&](Value& p) { row(p)[13] = true; });
    mutate([&](Value& p) { row(p)[14] = true; });
    mutate([&](Value& p) { row(p)[3] = Value::Array{subject("mismatch").format(), 0}; });
    mutate([&](Value& p) { row(p)[4] = Value::Array{Value::Array{subject("unselected").format(), 0}}; });
    mutate([&](Value& p) { row(p)[15] = Value::Array{Value::Array{"0", 10, 0, 0}}; });
    mutate([&](Value& p) { row(p)[9] = Value::Array{99, Value::Array{}}; });
    mutate([&](Value& p) {
        auto& rows = *p.getIf<Value::Object>()->at("executions").getIf<Value::Array>();
        rows.push_back(rows.front());
    });
    auto bad          = base;
    bad.schemaVersion = SchemaVersion(2);
    REQUIRE(!read(bad).ok());
    bad      = base;
    bad.type = "other";
    REQUIRE(!read(bad).ok());
    bad            = base;
    bad.instanceId = epoch().child("other");
    REQUIRE(!read(bad).ok());
    bad = base;
    bad.payload.set("next", "10");
    REQUIRE(!ActionCapture::parse(bad, epoch(), archiveDigest).ok());
    SnapshotHashProvider unavailable = [](std::string_view) {
        return Result<ContentId>::failure(Diagnostic::error(DiagnosticCode::Unsupported, "digest unavailable"));
    };
    REQUIRE(!captured.seal(Revision(4), SimulationTick(8), unavailable).ok());
    REQUIRE(!captured.seal({}, SimulationTick(8), archiveDigest).ok());
    REQUIRE(original.find(id)->phase() == ActionPhase::Requested);
    REQUIRE_EQ(original.executionCount(), std::size_t(1));
    REQUIRE_EQ(serializeSnapshotEnvelope(captured.seal(Revision(4), SimulationTick(8), archiveDigest).expect("retry"))
                   .expect("wire"),
               serializeSnapshotEnvelope(base).expect("original wire"));
}
TEST_CASE("action.checkpoint.resumePreservesCommittedTransactionTimelineAndIdWatermarkWithoutReplay") {
    auto                   world = std::make_shared<ecs::Table>();
    economy::EconomyLedger ledger;
    ledger.credit("checkpoint-coin", 100);
    economy::EconomyLedgerResourceAccount account(ledger);
    Effect                                effect;
    ActionServices                        services;
    services.transactionEffect  = &effect;
    services.transactionAccount = &account;
    ActionRuntime original(services);
    auto          def           = definition("timeline");
    def.cost                    = resource::CostSpec::from({{"checkpoint-coin", 7}}).expect("real cost");
    def.activeExecutionRequired = true;
    ActionTimeline timeline;
    timeline.actionId = def.id;
    timeline.duration = Duration(80);
    timeline.splitTimestamps.push_back(Duration(40));
    timeline.animationSections.push_back(
        {*LogicalId::parse("section:first"), "memory://first", Duration(0), Duration(40), Duration(0)});
    timeline.animationSections.push_back(
        {*LogicalId::parse("section:second"), "memory://second", Duration(40), Duration(80), Duration(0)});
    ActionTrack track;
    track.id   = *LogicalId::parse("track:events");
    track.kind = ActionTrackKind::Gameplay;
    track.notifies.push_back(
        {*LogicalId::parse("notify:start"), *LogicalId::parse("event:start"), Duration(0), {}, true});
    timeline.tracks.push_back(track);
    def.timeline  = timeline;
    const auto id = original.submit(def, request(def)).expect("real action");
    original.setSectionTimingOverride(id, 1, Duration(80)).expect("actual physical override");
    auto firstAdvance = original.advance(id, SimulationTick(1), Duration(30)).expect("real active commit");
    REQUIRE_EQ(firstAdvance.timelineEvents.size(), std::size_t(1));
    REQUIRE_EQ(ledger.get("checkpoint-coin"), 93);
    REQUIRE_EQ(effect.commits, 1);
    auto frozen = wireRoundTrip(original.capture(epoch(), world, {}).expect("freeze actual phase and cursor"));
    REQUIRE(frozen.executions()[0].execution->activeExecuted());
    REQUIRE(frozen.executions()[0].execution->transactionReceipt() != nullptr);
    REQUIRE(frozen.executions()[0].execution->sectionTimingOverride(1) == Duration(80));
    auto          plan = PreparedActionRestore::prepare(frozen, world, {}).expect("prepare detached action");
    ActionRuntime restored;  // Already committed Active needs no future effect/resource port.
    restored.restore(std::move(plan)).expect("publish without resubmit");
    REQUIRE(!restored.restore(std::move(plan)).ok());
    const auto expected = original.advance(id, SimulationTick(2), Duration(20)).expect("original next cursor");
    const auto actual   = restored.advance(id, SimulationTick(2), Duration(20)).expect("restored next cursor");
    REQUIRE(actual.phase == expected.phase);
    REQUIRE(actual.totalElapsed == expected.totalElapsed);
    REQUIRE(actual.phaseElapsed == expected.phaseElapsed);
    REQUIRE_EQ(actual.timelineEvents.size(), expected.timelineEvents.size());
    REQUIRE(restored.sectionProgress(id, 1).expect("restored progress") ==
            original.sectionProgress(id, 1).expect("original progress"));
    REQUIRE_EQ(effect.commits, 1);
    REQUIRE_EQ(ledger.get("checkpoint-coin"), 93);
    REQUIRE_EQ(frozen.executions()[0].execution->totalElapsed().nanoseconds(), std::int64_t(30));
    const auto next = restored.submit(def, request(def)).expect("no id reuse");
    REQUIRE(next == frozen.nextExecutionId());
}
TEST_CASE("action.checkpoint.futureProviderAbsencePreservesCandidateAndExistingRuntime") {
    auto                   world = std::make_shared<ecs::Table>();
    economy::EconomyLedger ledger;
    ledger.credit("checkpoint-coin", 100);
    economy::EconomyLedgerResourceAccount account(ledger);
    Effect                                effect;
    ActionServices                        services;
    services.transactionEffect  = &effect;
    services.transactionAccount = &account;
    ActionRuntime original(services);
    auto          def           = definition("pending-cost");
    def.cost                    = resource::CostSpec::from({{"checkpoint-coin", 7}}).expect("actual cost");
    def.activeExecutionRequired = true;
    const auto id               = original.submit(def, request(def)).expect("actual requested action");
    original.advance(id, SimulationTick(1), Duration(5)).expect("actual windup");
    auto          frozen = original.capture(epoch(), world, {}).expect("before Active");
    auto          plan   = PreparedActionRestore::prepare(frozen, world, {}).expect("candidate");
    ActionRuntime missing;
    auto          dummy = definition("existing");
    const auto    old   = missing.submit(dummy, request(dummy)).expect("existing execution");
    REQUIRE(!missing.restore(std::move(plan)).ok());
    REQUIRE(missing.find(old)->definitionId() == dummy.id);
    REQUIRE_EQ(ledger.get("checkpoint-coin"), 100);
    ActionRuntime available(services);
    available.restore(std::move(plan)).expect("same candidate with providers");
    available.advance(id, SimulationTick(2), Duration(10)).expect("one future actual commit");
    REQUIRE_EQ(effect.commits, 1);
    REQUIRE_EQ(ledger.get("checkpoint-coin"), 93);
}
TEST_CASE("action.checkpoint.rebindsLiveReferencesAndNeverAdoptsRetiredTargetsAfterSourceWorldDestruction") {
    std::optional<ActionCapture> frozen;
    std::weak_ptr<ecs::Table>    priorWorld;
    ActionExecutionId            id;
    {
        auto world = std::make_shared<ecs::Table>();
        priorWorld = world;
        ecs::ScopedTable scope(*world);
        auto*            source       = Endpoint::create();
        auto*            target       = Endpoint::create();
        const auto       sourceHandle = ecs::handle_of(source), targetHandle = ecs::handle_of(target);
        ActionRuntime    original;
        auto             def = definition("explicit");
        def.targetingMode    = TargetingMode::Explicit;
        auto req             = request(def);
        req.source           = sourceHandle;
        req.targetEntities   = {targetHandle, targetHandle};
        id                   = original.submit(def, req).expect("native explicit request");
        const std::array bindings{ActionEntityBinding{sourceHandle, subject("source")},
                                  ActionEntityBinding{targetHandle, subject("target")}};
        REQUIRE(!original.capture(epoch(), world, {}).ok());
        auto alive = original.capture(epoch(), world, bindings).expect("live references");
        REQUIRE(alive.executions()[0].source->presence == ActionEntityPresence::Present);
        ecs::DestroyEntity(target);
        frozen.emplace(wireRoundTrip(
            original.capture(epoch(), world, bindings).expect("historical identity with retired target")));
        REQUIRE(frozen->executions()[0].targets[0].presence == ActionEntityPresence::Retired);
        REQUIRE(!original.find(id)->request().sourceSubject.has_value());
        REQUIRE(frozen->executions()[0].execution->request().sourceSubject == subject("source"));
        REQUIRE(!frozen->executions()[0].execution->request().source.has_value());
        REQUIRE(frozen->executions()[0].execution->request().targetEntities.empty());
    }
    REQUIRE(priorWorld.expired());
    auto             world = std::make_shared<ecs::Table>();
    ecs::ScopedTable scope(*world);
    auto*            source      = Endpoint::create();
    auto*            replacement = Endpoint::create();
    const std::array bindings{ActionEntityBinding{ecs::handle_of(source), subject("source")},
                              ActionEntityBinding{ecs::handle_of(replacement), subject("target")}};
    auto             plan = PreparedActionRestore::prepare(*frozen, world, bindings).expect("fresh world rebind");
    ActionRuntime    restored;
    restored.restore(std::move(plan)).expect("publish rebound requests");
    REQUIRE(ecs::try_get(*restored.find(id)->request().source) == source);
    REQUIRE(restored.find(id)->request().targetEntities[0].table == nullptr);
    REQUIRE_EQ(restored.find(id)->request().targetEntities.size(), std::size_t(2));
    auto moved = std::move(*frozen);
    REQUIRE(frozen->saveEpoch().isNil());
    REQUIRE(frozen->executions().empty());
    REQUIRE(moved.saveEpoch() == epoch());
}
TEST_CASE("action.checkpoint.serviceCallbackRejectsCaptureAndRestoreUntilTheRealCommitFinishes") {
    auto           world = std::make_shared<ecs::Table>();
    ActionRuntime  empty;
    auto           capturedEmpty = empty.capture(epoch(), world, {}).expect("empty capture");
    auto           plan = PreparedActionRestore::prepare(capturedEmpty, world, {}).expect("empty restore candidate");
    Effect         effect;
    ActionServices services;
    services.transactionEffect = &effect;
    ActionRuntime original(services);
    bool          captureRejected = false, restoreRejected = false;
    effect.onPrepare = [&] {
        captureRejected = !original.capture(epoch(), world, {}).ok();
        restoreRejected = !original.restore(std::move(plan)).ok();
    };
    auto def                    = definition("callback");
    def.activeExecutionRequired = true;
    const auto id               = original.submit(def, request(def)).expect("actual action");
    original.advance(id, SimulationTick(1), Duration(15)).expect("real service callback and commit");
    REQUIRE(captureRejected);
    REQUIRE(restoreRejected);
    REQUIRE_EQ(effect.commits, 1);
    REQUIRE(original.capture(epoch(), world, {}).ok());
    original.restore(std::move(plan)).expect("rejected candidate was not consumed");
    REQUIRE_EQ(original.executionCount(), std::size_t(0));
}
TEST_CASE("action.checkpoint.changedIncarnationsViewsAndRetiredTargetWorldRejectWithoutPublishing") {
    auto          world = std::make_shared<ecs::Table>();
    ActionRuntime original;
    auto          def = definition("source");
    ActionRequest req = request(def);
    {
        ecs::ScopedTable scope(*world);
        req.source = ecs::handle_of(Endpoint::create());
    }
    req.sourceSubject = subject("source");
    const auto id     = original.submit(def, req).expect("actual source");
    REQUIRE(!original.capture({}, world, {}).ok());
    {
        auto view = ecs::View<Endpoint>(*world);
        REQUIRE(!original.capture(epoch(), world, {}).ok());
    }
    auto              frozen = wireRoundTrip(original.capture(epoch(), world, {}).expect("producer stable identity"));
    auto              target = std::make_shared<ecs::Table>();
    ecs::EntityHandle handle;
    {
        ecs::ScopedTable scope(*target);
        handle = ecs::handle_of(Endpoint::create());
    }
    const std::array bindings{ActionEntityBinding{handle, subject("source")}};
    auto             plan = PreparedActionRestore::prepare(frozen, target, bindings).expect("target candidate");
    {
        ecs::ScopedTable scope(*target);
        ecs::DestroyEntity(ecs::try_get(handle));
    }
    ActionRuntime restored;
    REQUIRE(!restored.restore(std::move(plan)).ok());
    REQUIRE_EQ(restored.executionCount(), std::size_t(0));
    std::weak_ptr<ecs::Table> weak = target;
    target.reset();
    REQUIRE(weak.expired());
    REQUIRE(!restored.restore(std::move(plan)).ok());
    REQUIRE(original.find(id) != nullptr);
}
TEST_CASE("action.checkpoint.resourceReceiptIsRetainedAsAuditAndNeverDebitedAgain") {
    auto                   world = std::make_shared<ecs::Table>();
    economy::EconomyLedger ledger;
    ledger.credit("checkpoint-coin", 100);
    economy::EconomyLedgerResourceAccount account(ledger);
    AccountPort                           port(account);
    ActionServices                        services;
    services.resources = &port;
    ActionRuntime original(services);
    auto          def = definition("resource-audit");
    def.cost          = resource::CostSpec::from({{"checkpoint-coin", 7}}).expect("real cost");
    const auto id     = original.submit(def, request(def)).expect("actual request");
    original.advance(id, SimulationTick(1), Duration(20)).expect("real resource debit");
    const auto receipt = original.find(id)->resourceReceipt()->get();
    auto       frozen  = wireRoundTrip(original.capture(epoch(), world, {}).expect("actual audit"));
    REQUIRE(frozen.executions()[0].execution->resourceReceipt()->get().id == receipt.id);
    auto          plan = PreparedActionRestore::prepare(frozen, world, {}).expect("audit restore");
    ActionRuntime restored;
    restored.restore(std::move(plan)).expect("no account needed after debit");
    restored.advance(id, SimulationTick(2), Duration(100)).expect("finish without resource replay");
    REQUIRE_EQ(ledger.get("checkpoint-coin"), 93);
    REQUIRE(restored.find(id)->hasCommittedCost());
    REQUIRE(restored.find(id)->resourceReceipt()->get().account == receipt.account);
}
TEST_CASE("action.checkpoint.futureDispatchModeMismatchKeepsRestoreCandidateReusable") {
    auto           world = std::make_shared<ecs::Table>();
    ActionRuntime  original;
    auto           def    = definition("future-dispatch");
    const auto     id     = original.submit(def, request(def)).expect("queued action");
    auto           frozen = original.capture(epoch(), world, {}).expect("queued freeze");
    auto           plan   = PreparedActionRestore::prepare(frozen, world, {}).expect("queued candidate");
    Effect         effect;
    ActionServices services;
    services.transactionEffect = &effect;
    ActionRuntime incompatible(services);
    REQUIRE(!incompatible.restore(std::move(plan)).ok());
    REQUIRE_EQ(incompatible.executionCount(), std::size_t(0));
    REQUIRE_EQ(effect.commits, 0);
    ActionRuntime compatible;
    compatible.restore(std::move(plan)).expect("same candidate on original dispatch mode");
    compatible.advance(id, SimulationTick(1), Duration(80)).expect("finish queued action");
    REQUIRE_EQ(effect.commits, 0);
}
TEST_CASE("action.checkpoint.escapingProviderExceptionFaultsRuntimeUntilExplicitRecovery") {
    struct ThrowingEffect final : IActionEffectExecutor {
        int                                             calls = 0;
        Result<std::unique_ptr<IActionEffectOperation>> prepare(const ActionDefinition&, const ActionRequest&,
                                                                const sensing::TargetSet*, SimulationTick) override {
            ++calls;
            throw std::runtime_error("provider failed after entering Active");
        }
    } effect;
    auto           world = std::make_shared<ecs::Table>();
    ActionServices services;
    services.effects = &effect;
    ActionRuntime runtime(services);
    auto          def           = definition("throwing-provider");
    def.activeExecutionRequired = true;
    const auto id               = runtime.submit(def, request(def)).expect("queued actual provider");
    bool       escaped          = false;
    try {
        runtime.advance(id, SimulationTick(1), Duration(20)).expect("provider throws");
    } catch (const std::runtime_error&) {
        escaped = true;
    }
    REQUIRE(escaped);
    REQUIRE(runtime.isFaulted());
    REQUIRE(!runtime.capture(epoch(), world, {}).ok());
    REQUIRE(!runtime.advance(id, SimulationTick(1), Duration(20)).ok());
    REQUIRE(!runtime.submit(def, request(def)).ok());
    REQUIRE_EQ(effect.calls, 1);
    runtime.clear();
    REQUIRE(!runtime.isFaulted());
    REQUIRE(runtime.capture(epoch(), world, {}).ok());
}
TEST_CASE("action.checkpoint.failedResourceRollbackRetainsBothDiagnosticsAndRejectsFreeze") {
    auto                   world = std::make_shared<ecs::Table>();
    economy::EconomyLedger ledger;
    ledger.credit("checkpoint-coin", 100);
    economy::EconomyLedgerResourceAccount account(ledger);
    AccountPort                           port(account);
    port.failCommit   = true;
    port.failRollback = true;
    ActionServices services;
    services.resources = &port;
    ActionRuntime runtime(services);
    auto          def   = definition("unresolved-reservation");
    def.cost            = resource::CostSpec::from({{"checkpoint-coin", 7}}).expect("real cost");
    const auto id       = runtime.submit(def, request(def)).expect("actual request");
    auto       advanced = runtime.advance(id, SimulationTick(1), Duration(20));
    REQUIRE(!advanced.ok());
    REQUIRE(runtime.isFaulted());
    REQUIRE(runtime.find(id)->phase() == ActionPhase::Failed);
    REQUIRE_EQ(runtime.find(id)->status().diagnostics().size(), std::size_t(2));
    REQUIRE(!runtime.capture(epoch(), world, {}).ok());
    REQUIRE(!runtime.advance(id, SimulationTick(2), Duration(20)).ok());
    REQUIRE_EQ(ledger.get("checkpoint-coin"), 100);
}
}  // namespace test_action_checkpoint
#endif
