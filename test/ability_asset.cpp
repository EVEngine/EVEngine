#include "action/AbilityAsset.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

namespace {

eve::LogicalId id(std::string_view value) {
    auto parsed = eve::LogicalId::parse(value);
    REQUIRE(parsed.has_value());
    return *parsed;
}

eve::action::AbilityDefinition complexAbility() {
    eve::action::AbilityDefinition result;
    result.id = id("ability:fire-cone");
    result.action.id = id("action:fire-cone");
    result.action.timing = {eve::Duration::fromNanoseconds(100'000'000),
                            eve::Duration::fromNanoseconds(200'000'000),
                            eve::Duration::fromNanoseconds(300'000'000)};
    result.action.condition = eve::decision::Condition::all({
        eve::decision::Condition::hasTag("State.Combat.Ready"),
        eve::decision::Condition::compare("mana", eve::decision::CompareOperator::GreaterEqual, 12),
        eve::decision::Condition::policyCall(
            "can_cast", eve::Value::Object{{"school", "fire"}},
            eve::decision::ScriptConditionDeclaration{
                "can_cast", {"attributes.mana"}, eve::decision::DeterminismLevel::TickDeterministic})});
    result.action.targetingMode = eve::action::TargetingMode::Query;
    eve::sensing::TargetingSpec targeting;
    targeting.space = eve::sensing::CoordinateSpace::World2D;
    targeting.domain = eve::sensing::TargetDomain::Enemy;
    targeting.minCount = 1;
    targeting.maxCount = 3;
    targeting.maxRange = 8.f;
    targeting.requiredTags = {"Unit.Alive"};
    targeting.lineOfSight = eve::sensing::LineOfSightMode::Required;
    auto center = eve::sensing::WorldPoint::world2D(2.f, 3.f);
    REQUIRE(center.ok());
    auto area = eve::sensing::WorldArea::circle2D(center.value(), 5.f);
    REQUIRE(area.ok());
    targeting.worldArea = area.value();
    result.action.targetingSpec = targeting;
    auto cost = eve::resource::CostSpec::from({{"mana", 12}, {"stamina", 3}});
    REQUIRE(cost.ok());
    result.action.cost = cost.value();
    result.action.effectIds = {"effect:burn", "effect:impact"};
    result.action.activeExecutionRequired = true;
    result.action.metadata = {{"category", "spell"}, {"rank", 2}};
    eve::action::ActionTimeline timeline;
    timeline.actionId = result.action.id;
    timeline.duration = eve::Duration::fromNanoseconds(600'000'000);
    timeline.metadata = {{"author", "test"}};
    result.action.timeline = std::move(timeline);
    result.cooldown = eve::Duration::fromNanoseconds(1'500'000'000);
    result.instancing = eve::action::AbilityInstancingPolicy::PerExecution;
    result.activationGroup = eve::action::AbilityActivationGroup::ExclusiveReplaceable;
    result.triggers = {{"Event.Combat.Cast", eve::tags::GameplayTagMatch::IncludeDescendants}};
    REQUIRE(result.validate().ok());
    return result;
}

}  // namespace

TEST_CASE("abilityAsset.roundTripsCompleteDefinitionAndRegistersDecodedCandidate") {
    const auto source = complexAbility();
    auto encoded = eve::action::encodeAbilityAsset(source);
    REQUIRE(encoded.ok());
    auto decoded = eve::action::decodeAbilityAsset(encoded.value());
    REQUIRE(decoded.ok());
    CHECK_EQ(decoded.value().id, source.id);
    CHECK_EQ(decoded.value().action.id, source.action.id);
    CHECK_EQ(decoded.value().action.timing.active, source.action.timing.active);
    CHECK_EQ(decoded.value().action.condition.children().size(), 3u);
    REQUIRE(decoded.value().action.condition.children()[2].scriptDeclaration().has_value());
    CHECK_EQ(decoded.value().action.condition.children()[2].scriptDeclaration()->dependencies[0],
             std::string("attributes.mana"));
    REQUIRE(decoded.value().action.targetingSpec.has_value());
    CHECK(decoded.value().action.targetingSpec->worldArea.has_value());
    CHECK_EQ(decoded.value().action.targetingSpec->maxCount, 3u);
    REQUIRE(decoded.value().action.cost.has_value());
    CHECK_EQ(decoded.value().action.cost->size(), 2u);
    CHECK_EQ(decoded.value().action.effectIds, source.action.effectIds);
    REQUIRE(decoded.value().action.timeline.has_value());
    CHECK_EQ(decoded.value().action.timeline->duration, source.action.timeline->duration);
    CHECK_EQ(decoded.value().triggers.size(), 1u);

    eve::action::ActionRuntime actions;
    eve::action::AbilityRuntime abilities(actions);
    REQUIRE(abilities.registerDefinition(std::move(decoded).takeValue()).ok());
}

TEST_CASE("abilityAsset.migratesV1MillisecondsAndRejectsFutureUnknownOrMalformedData") {
    auto source = complexAbility();
    source.action.targetingMode = eve::action::TargetingMode::None;
    source.action.targetingSpec.reset();
    source.action.condition = eve::decision::Condition{};
    source.action.cost.reset();
    source.action.timeline.reset();
    auto encoded = eve::action::encodeAbilityAsset(source);
    REQUIRE(encoded.ok());
    auto legacy = encoded.value();
    auto* root = legacy.getIf<eve::Value::Object>();
    (*root)["schemaVersion"] = 1;
    (*root)["cooldownMs"] = 1500;
    root->erase("cooldownNs");
    auto* action = root->at("action").getIf<eve::Value::Object>();
    auto* timing = action->at("timing").getIf<eve::Value::Object>();
    (*timing)["windupMs"] = 100;
    (*timing)["activeMs"] = 200;
    (*timing)["recoverMs"] = 300;
    timing->erase("windupNs");
    timing->erase("activeNs");
    timing->erase("recoverNs");
    auto migrated = eve::action::decodeAbilityAsset(legacy);
    REQUIRE(migrated.ok());
    CHECK_EQ(migrated.value().cooldown.nanoseconds(), 1'500'000'000);
    CHECK_EQ(migrated.value().action.timing.active.nanoseconds(), 200'000'000);
    auto canonical = eve::action::encodeAbilityAsset(migrated.value());
    REQUIRE(canonical.ok());
    CHECK_EQ(canonical.value().getIf<eve::Value::Object>()->at("schemaVersion").asInt(), 2);

    auto future = encoded.value();
    (*future.getIf<eve::Value::Object>())["schemaVersion"] = 3;
    CHECK(!eve::action::decodeAbilityAsset(future).ok());
    auto unknown = encoded.value();
    (*unknown.getIf<eve::Value::Object>())["surprise"] = true;
    CHECK(!eve::action::decodeAbilityAsset(unknown).ok());
    auto malformed = encoded.value();
    (*malformed.getIf<eve::Value::Object>())["cooldownNs"] = -1;
    CHECK(!eve::action::decodeAbilityAsset(malformed).ok());
    auto ambiguous = encoded.value();
    (*ambiguous.getIf<eve::Value::Object>())["cooldownMs"] = 1500;
    CHECK(!eve::action::decodeAbilityAsset(ambiguous).ok());

    auto fractionalGrid = encoded.value();
    auto* fractionalAction = fractionalGrid.getIf<eve::Value::Object>()->at("action").getIf<eve::Value::Object>();
    (*fractionalAction)["targetingMode"] = "query";
    (*fractionalAction)["targetingSpec"] = eve::Value::Object{
        {"space", "grid2d"},
        {"domain", 1},
        {"minCount", 1},
        {"maxCount", 1},
        {"minRange", 0.0},
        {"maxRange", 3.0},
        {"requiredTags", eve::Value::Array{}},
        {"excludedTags", eve::Value::Array{}},
        {"lineOfSight", false},
        {"gridArea", eve::Value::Object{{"shape", 0},
                                         {"minimum", eve::Value::Array{0.5, 0, 0}},
                                         {"maximum", eve::Value::Array{2, 2, 0}}}}};
    CHECK(!eve::action::decodeAbilityAsset(fractionalGrid).ok());
}
