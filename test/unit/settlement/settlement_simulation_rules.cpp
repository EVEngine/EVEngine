#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include "settlement/Settlement.h"
#include "settlement/SettlementRules.h"
#include "zeroerr/unittest.h"

namespace test_settlement_contract {
eve::SnapshotHashProvider testHashProvider() {
    return [](std::string_view input) -> eve::Result<eve::ContentId> {
        eve::ContentId::Bytes bytes{};
        std::uint64_t         hash = 14695981039346656037ull;
        for (const unsigned char byte : input) {
            hash ^= byte;
            hash *= 1099511628211ull;
        }
        for (std::size_t index = 0; index < bytes.size(); ++index)
            bytes[index] = static_cast<std::uint8_t>(hash >> ((index % 8) * 8));
        return eve::Result<eve::ContentId>::success(eve::ContentId(bytes));
    };
}

eve::settlement::SettlementResult sampleResult() {
    eve::settlement::SettlementResult result;
    result.requested   = 20.0;
    result.applied     = 15.0;
    result.resisted    = 5.0;
    result.disposition = eve::settlement::SettlementDisposition::PartiallyApplied;
    result.tick        = eve::SimulationTick(42);
    result.stages.push_back({eve::settlement::StageKind::TargetMitigation, "armor", eve::StatusCode::Applied, 20.0,
                             15.0, eve::Value::Object{{"rule", "heavy_armor"}}});
    eve::game_event::GameEvent event;
    event.type     = "settlement.damage";
    event.source   = "attacker";
    event.subject  = "target";
    event.tick     = result.tick;
    event.payload  = "{\"applied\":15}";
    event.eventId  = eve::EventId::parse("018f0f4e-6b3c-7ab1-8def-0123456789ab").value();
    event.sequence = eve::EventSequence(7);
    result.event   = event;
    return result;
}

eve::ContentId contentId(std::uint8_t suffix) {
    eve::ContentId::Bytes bytes{};
    bytes[15] = suffix;
    return eve::ContentId(bytes);
}

eve::SubjectRef subject(std::uint8_t suffix) {
    eve::PersistentId::Bytes bytes{};
    bytes[15] = suffix;
    return eve::SubjectRef::fromPersistentId(eve::PersistentId(bytes));
}

eve::settlement::SettlementRequest sampleRequest() {
    eve::settlement::SettlementRequest request;
    request.source    = subject(1);
    request.target    = subject(2);
    request.kind      = "damage";
    request.resource  = "hp";
    request.magnitude = 20.0;
    request.tags      = {"physical", "melee"};
    request.context   = eve::Value::Object{{"distance", 1.5}};
    request.tick      = eve::SimulationTick(42);
    request.decisions.push_back({*eve::LogicalId::parse("rng:combat"), 4, 0.2, 0.5, true});
    return request;
}

class HealthPolicy : public eve::settlement::ISettlementPolicy {
public:
    explicit HealthPolicy(double& health, double maximum = 100.0) : health_(health), maximum_(maximum) {}

    eve::Result<void> validate(eve::settlement::SettlementContext&) override {
        return health_ >= 0.0 && health_ <= maximum_
                   ? eve::Result<void>::success()
                   : eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvariantViolation,
                                                                       "health is outside bounds", "health"));
    }
    eve::Result<void> sourceModifiers(eve::settlement::SettlementContext&) override {
        return eve::Result<void>::success();
    }
    eve::Result<void> targetMitigation(eve::settlement::SettlementContext&) override {
        return eve::Result<void>::success();
    }
    eve::Result<void> armorShield(eve::settlement::SettlementContext&) override { return eve::Result<void>::success(); }
    eve::Result<void> clamp(eve::settlement::SettlementContext& context) override {
        return context.setClampMax(context.request().kind == "heal" ? maximum_ - health_ : health_);
    }
    eve::Result<eve::settlement::PreparedApply> prepareApply(
        const eve::settlement::SettlementContext& context) override {
        const double before = health_;
        const double after =
            context.request().kind == "heal" ? before + context.magnitude() : before - context.magnitude();
        return eve::Result<eve::settlement::PreparedApply>::success(eve::settlement::PreparedApply(
            [this, after]() {
                health_ = after;
                return eve::Result<void>::success();
            },
            [this, before]() { health_ = before; }));
    }

private:
    double& health_;
    double  maximum_;
};

class ResourcePolicy final : public eve::settlement::ISettlementPolicy {
public:
    ResourcePolicy(double& value, double maximum, bool failPrepare = false, bool failCommit = false)
        : value_(value), maximum_(maximum), failPrepare_(failPrepare), failCommit_(failCommit) {}

    eve::Result<void> validate(eve::settlement::SettlementContext&) override {
        if (failPrepare_)
            return eve::Result<void>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Conflict, "injected resource prepare failure"));
        return eve::Result<void>::success();
    }
    eve::Result<void> sourceModifiers(eve::settlement::SettlementContext&) override {
        return eve::Result<void>::success();
    }
    eve::Result<void> targetMitigation(eve::settlement::SettlementContext&) override {
        return eve::Result<void>::success();
    }
    eve::Result<void> armorShield(eve::settlement::SettlementContext&) override { return eve::Result<void>::success(); }
    eve::Result<void> clamp(eve::settlement::SettlementContext& context) override {
        return context.setClampMax(context.request().kind == "gain" ? maximum_ - value_ : value_);
    }
    eve::Result<eve::settlement::PreparedApply> prepareApply(
        const eve::settlement::SettlementContext& context) override {
        const double before = value_;
        const double after =
            context.request().kind == "gain" ? before + context.magnitude() : before - context.magnitude();
        return eve::Result<eve::settlement::PreparedApply>::success(eve::settlement::PreparedApply(
            [this, after]() {
                if (failCommit_)
                    return eve::Result<void>::failure(
                        eve::Diagnostic::error(eve::DiagnosticCode::Failed, "injected resource commit failure"));
                value_ = after;
                return eve::Result<void>::success();
            },
            [this, before]() { value_ = before; }));
    }

private:
    double& value_;
    double  maximum_;
    bool    failPrepare_;
    bool    failCommit_;
};

class OrderedSourcePolicy final : public HealthPolicy {
public:
    explicit OrderedSourcePolicy(double& health) : HealthPolicy(health) {}

    eve::Result<void> sourceModifiers(eve::settlement::SettlementContext& context) override {
        return context.setMagnitude(context.magnitude() * 2.0);
    }
};

eve::settlement::SettlementRule rule(std::string id, eve::settlement::StageKind stage,
                                     eve::settlement::RuleOperation operation, double value,
                                     std::vector<std::string> kinds = {}) {
    eve::settlement::SettlementRule result;
    result.id           = std::move(id);
    result.source       = "effect:test";
    result.stage        = stage;
    result.operation    = operation;
    result.value        = value;
    result.filter.kinds = std::move(kinds);
    return result;
}

eve::SnapshotHashProvider ruleHashProvider() {
    return [](std::string_view input) -> eve::Result<eve::ContentId> {
        eve::ContentId::Bytes bytes{};
        std::uint64_t         hash = 14695981039346656037ull;
        for (const unsigned char byte : input) {
            hash ^= byte;
            hash *= 1099511628211ull;
        }
        for (std::size_t index = 0; index < bytes.size(); ++index)
            bytes[index] = static_cast<std::uint8_t>(hash >> ((index % 8) * 8));
        return eve::Result<eve::ContentId>::success(eve::ContentId(bytes));
    };
}

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_settlement_replay)
TEST_CASE("settlement replay digest: transport event identity is ignored but payload differences are located") {
    auto expected          = sampleResult();
    auto actual            = expected;
    actual.event->eventId  = eve::EventId::parse("018f0f4e-6b3c-7ab1-8def-0123456789ac").value();
    actual.event->sequence = eve::EventSequence(99);
    auto same              = eve::settlement::verifySettlementResult(expected, actual);
    REQUIRE(same.ok());
    CHECK(same.value().empty());
    actual.applied           = 14.0;
    actual.stages[0].details = eve::Value::Object{{"rule", "light_armor"}};
    auto changed             = eve::settlement::verifySettlementResult(expected, actual);
    REQUIRE(changed.ok());
    REQUIRE_EQ(changed.value().size(), 2u);
    CHECK_EQ(changed.value()[0], std::string("applied"));
    CHECK_EQ(changed.value()[1], std::string("stages[0].details"));
    auto firstDigest  = eve::settlement::settlementResultDigest(expected, testHashProvider());
    auto secondDigest = eve::settlement::settlementResultDigest(actual, testHashProvider());
    REQUIRE(firstDigest.ok());
    REQUIRE(secondDigest.ok());
    CHECK(firstDigest.value() != secondDigest.value());
}

TEST_CASE("settlement replay record: request round trip and tampering is rejected") {
    auto request = sampleRequest();
    auto result  = sampleResult();
    auto record  = eve::settlement::createSettlementReplayRecord(request, contentId(7), result, testHashProvider());
    REQUIRE(record.ok());
    auto decoded = eve::settlement::settlementReplayRequest(record.value());
    REQUIRE(decoded.ok());
    CHECK_EQ(decoded.value().source, request.source);
    CHECK_EQ(decoded.value().target, request.target);
    CHECK_EQ(decoded.value().magnitude, 20.0);
    REQUIRE_EQ(decoded.value().decisions.size(), 1u);
    CHECK_EQ(decoded.value().decisions[0].sequence, 4u);

    auto changed              = result;
    changed.stages[0].details = eve::Value::Object{{"rule", "different"}};
    auto differences =
        eve::settlement::verifySettlementReplayRecord(record.value(), contentId(8), changed, testHashProvider());
    REQUIRE(differences.ok());
    REQUIRE_EQ(differences.value().size(), 2u);
    CHECK_EQ(differences.value()[0], std::string("ruleDigest"));
    CHECK_EQ(differences.value()[1], std::string("result.payload.stages[0].details.rule"));
}

TEST_CASE("settlement replay decode: invalid version and fields have located errors") {
    auto record = eve::settlement::createSettlementReplayRecord(sampleRequest(), contentId(7), sampleResult(),
                                                                testHashProvider());
    REQUIRE(record.ok());
    const std::string serialized = record.value();
    SUB_CASE("future version is rejected") {
        auto parsed = eve::Value::fromJson(serialized);
        REQUIRE(parsed.ok());
        auto value = parsed.value();
        value.set("version", 2);
        auto json = value.toJson();
        REQUIRE(json.ok());
        CHECK(!eve::settlement::settlementReplayRequest(json.value()).ok());
    };
    SUB_CASE("unknown root field is rejected") {
        auto parsed = eve::Value::fromJson(serialized);
        REQUIRE(parsed.ok());
        auto value = parsed.value();
        value.set("unexpected", true);
        auto json = value.toJson();
        REQUIRE(json.ok());
        CHECK(!eve::settlement::settlementReplayRequest(json.value()).ok());
    };
    SUB_CASE("negative magnitude identifies request field") {
        auto parsed = eve::Value::fromJson(serialized);
        REQUIRE(parsed.ok());
        auto value = parsed.value();
        value.find("request")->set("magnitude", -1.0);
        auto json = value.toJson();
        REQUIRE(json.ok());
        auto rejected = eve::settlement::settlementReplayRequest(json.value());
        REQUIRE(!rejected.ok());
        REQUIRE(rejected.error() != nullptr);
        CHECK_EQ(rejected.error()->path(), std::string("record.request.magnitude"));
    };
}

#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_settlement_rules)
TEST_CASE("settlement SettlementRuleSet configure/install: ordered modifiers produce the final applied amount") {
    eve::settlement::SettlementRuleSet rules;
    REQUIRE(rules
                .configure({rule("attack-buff", eve::settlement::StageKind::SourceModifiers,
                                 eve::settlement::RuleOperation::Multiply, 1.5, {"damage"}),
                            rule("high-ground", eve::settlement::StageKind::SourceModifiers,
                                 eve::settlement::RuleOperation::Add, 4.0, {"damage"}),
                            rule("fortification", eve::settlement::StageKind::TargetMitigation,
                                 eve::settlement::RuleOperation::ResistPercent, 0.25, {"damage"}),
                            rule("barrier", eve::settlement::StageKind::ArmorShield,
                                 eve::settlement::RuleOperation::AbsorbFlat, 5.0, {"damage"})})
                .ok());
    eve::settlement::SettlementPipeline pipeline;
    REQUIRE(rules.install(pipeline).ok());
    double                             health = 100.0;
    HealthPolicy                       policy(health);
    eve::settlement::SettlementRequest request;
    request.target    = subject(2);
    request.kind      = "damage";
    request.magnitude = 20.0;
    auto settled      = pipeline.settle(request, policy);
    REQUIRE(settled.ok());
    CHECK_EQ(settled.value().applied, 20.5);
    CHECK_EQ(settled.value().resisted, 8.5);
    CHECK_EQ(settled.value().absorbed, 5.0);
    CHECK_EQ(health, 79.5);
}

TEST_CASE("settlement SettlementPipeline decision: immunity is terminal and invalid randomness leaves state intact") {
    auto immune = rule("fire-immunity", eve::settlement::StageKind::Decision, eve::settlement::RuleOperation::Immune,
                       0.0, {"damage"});
    immune.filter.requiredTags = {"element:fire"};
    auto bonus = rule("bonus", eve::settlement::StageKind::SourceModifiers, eve::settlement::RuleOperation::Add, 40.0,
                      {"damage"});
    eve::settlement::SettlementRuleSet rules;
    REQUIRE(rules.configure({immune, bonus}).ok());
    eve::settlement::SettlementPipeline pipeline;
    REQUIRE(rules.install(pipeline).ok());
    double                             health = 100.0;
    HealthPolicy                       policy(health);
    eve::settlement::SettlementRequest request;
    request.target    = subject(3);
    request.kind      = "damage";
    request.tags      = {"element:fire"};
    request.magnitude = 25.0;
    auto settled      = pipeline.settle(request, policy);
    REQUIRE(settled.ok());
    CHECK_EQ(settled.value().disposition, eve::settlement::SettlementDisposition::Immune);
    CHECK_EQ(settled.value().applied, 0.0);
    CHECK_EQ(health, 100.0);
    request.decisions.push_back({*eve::LogicalId::parse("combat:hit"), 1, 1.5, 0.75, false});
    CHECK(!pipeline.settle(request, policy).ok());
    CHECK_EQ(health, 100.0);
}

TEST_CASE("settlement rule condition: typed expression gates rule and compile errors keep old rules") {
    auto conditional = rule("conditional", eve::settlement::StageKind::SourceModifiers,
                            eve::settlement::RuleOperation::Add, 5.0, {"damage"});
    conditional.when = "kind == \"damage\" && magnitude + context.bonus * 2 >= 14 && has_tag(\"poison\")";
    eve::settlement::SettlementRuleSet rules;
    REQUIRE(rules.configure({conditional}).ok());
    auto invalid  = conditional;
    invalid.id    = "invalid";
    invalid.when  = "kind && magnitude";
    auto rejected = rules.configure({invalid});
    REQUIRE(!rejected.ok());
    CHECK_EQ(rules.size(), 1u);
    eve::settlement::SettlementPipeline pipeline;
    REQUIRE(rules.install(pipeline).ok());
    double                             health = 50.0;
    HealthPolicy                       policy(health, 50.0);
    eve::settlement::SettlementRequest request;
    request.target    = subject(5);
    request.kind      = "damage";
    request.magnitude = 10.0;
    request.tags      = {"poison"};
    request.context   = eve::Value::Object{{"bonus", eve::Value(2.0)}};
    auto settled      = pipeline.settle(request, policy);
    REQUIRE(settled.ok());
    CHECK_EQ(settled.value().applied, 15.0);
    CHECK_EQ(health, 35.0);
}

TEST_CASE("settlement rule condition: constant and runtime division by zero reject before mutation") {
    auto candidate = rule("division", eve::settlement::StageKind::SourceModifiers, eve::settlement::RuleOperation::Add,
                          1.0, {"damage"});
    eve::settlement::SettlementRuleSet rules;
    candidate.when = "magnitude / 0 > 1";
    CHECK(!rules.configure({candidate}).ok());
    candidate.when = "magnitude / context.divisor > 1";
    REQUIRE(rules.configure({candidate}).ok());
    eve::settlement::SettlementPipeline pipeline;
    REQUIRE(rules.install(pipeline).ok());
    double                             health = 20.0;
    HealthPolicy                       policy(health, 20.0);
    eve::settlement::SettlementRequest request;
    request.target    = subject(8);
    request.kind      = "damage";
    request.magnitude = 5.0;
    request.context   = eve::Value::Object{{"divisor", eve::Value(0.0)}};
    CHECK(!pipeline.settle(request, policy).ok());
    CHECK_EQ(health, 20.0);
}

TEST_CASE("settlement settleAtomic: distinct resources commit together and later failure restores both") {
    auto requests = [] {
        std::array<eve::settlement::SettlementRequest, 2> entries;
        entries[0].target    = subject(20);
        entries[0].kind      = "damage";
        entries[0].resource  = "hp";
        entries[0].magnitude = 25.0;
        entries[1].target    = subject(20);
        entries[1].kind      = "spend";
        entries[1].resource  = "mana";
        entries[1].magnitude = 15.0;
        return entries;
    };
    SUB_CASE("both channels commit") {
        eve::settlement::SettlementPipeline                pipeline;
        double                                             health = 100.0, mana = 40.0;
        ResourcePolicy                                     hp(health, 100.0), mp(mana, 40.0);
        std::array<eve::settlement::ISettlementPolicy*, 2> policies{&hp, &mp};
        auto                                               entries = requests();
        auto                                               result  = pipeline.settleAtomic(entries, policies);
        REQUIRE(result.ok());
        REQUIRE_EQ(result.value().size(), 2u);
        CHECK_EQ(health, 75.0);
        CHECK_EQ(mana, 25.0);
    };
    SUB_CASE("second commit fails and first channel rolls back") {
        eve::settlement::SettlementPipeline                pipeline;
        double                                             health = 100.0, mana = 40.0;
        ResourcePolicy                                     hp(health, 100.0), mp(mana, 40.0, false, true);
        std::array<eve::settlement::ISettlementPolicy*, 2> policies{&hp, &mp};
        auto                                               entries = requests();
        CHECK(!pipeline.settleAtomic(entries, policies).ok());
        CHECK_EQ(health, 100.0);
        CHECK_EQ(mana, 40.0);
    };
}

TEST_CASE("settlement settleIndependent: one failed target does not suppress another success") {
    eve::settlement::SettlementPipeline               pipeline;
    double                                            first = 100.0, second = 100.0;
    ResourcePolicy                                    good(first, 100.0), bad(second, 100.0, true);
    std::array<eve::settlement::SettlementRequest, 2> requests;
    requests[0].target    = subject(24);
    requests[0].kind      = "damage";
    requests[0].resource  = "hp";
    requests[0].magnitude = 15.0;
    requests[1]           = requests[0];
    requests[1].target    = subject(25);
    std::array<eve::settlement::ISettlementPolicy*, 2> policies{&good, &bad};
    auto                                               result = pipeline.settleIndependent(requests, policies);
    REQUIRE(result.ok());
    REQUIRE_EQ(result.value().size(), 2u);
    CHECK(result.value()[0].status.isSuccess());
    CHECK(!result.value()[1].status.isSuccess());
    CHECK_EQ(first, 85.0);
    CHECK_EQ(second, 100.0);
}

TEST_CASE(
    "settlement RuleSet canonicalJson/digest: serialization is stable and invalid input does not replace "
    "configuration") {
    auto armor = rule("armor", eve::settlement::StageKind::TargetMitigation, eve::settlement::RuleOperation::ResistFlat,
                      3.0, {"damage", "damage"});
    armor.filter.requiredTags = {"physical", "weapon"};
    eve::settlement::SettlementRuleSet rules;
    REQUIRE(rules.configure({armor}).ok());
    auto json = rules.canonicalJson();
    REQUIRE(json.ok());
    auto restored = eve::settlement::SettlementRuleSet::fromJson(json.value());
    REQUIRE(restored.ok());
    auto canonical = restored.value().canonicalJson();
    REQUIRE(canonical.ok());
    CHECK_EQ(canonical.value(), json.value());
    auto digest = rules.digest(ruleHashProvider());
    REQUIRE(digest.ok());
    auto same = restored.value().digest(ruleHashProvider());
    REQUIRE(same.ok());
    CHECK_EQ(same.value(), digest.value());
    auto invalid = eve::Value::fromJson(json.value());
    REQUIRE(invalid.ok());
    invalid.value().set("unexpected", true);
    auto invalidJson = invalid.value().toJson();
    REQUIRE(invalidJson.ok());
    CHECK(!rules.configureJson(invalidJson.value()).ok());
    CHECK_EQ(rules.size(), 1u);
}
#endif
}  // namespace test_settlement_contract
