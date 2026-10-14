#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "settlement/SettlementRules.h"

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <utility>

namespace {

eve::SubjectRef subject(std::uint8_t suffix) {
    std::array<std::uint8_t, 16> bytes{};
    bytes[15] = suffix;
    return eve::SubjectRef::fromPersistentId(eve::PersistentId(bytes));
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

}  // namespace

TEST_CASE("settlement.rules.composeRpgTacticsRtsAndCardModifiersDeterministically") {
    eve::settlement::SettlementRuleSet rules;
    auto                               configured = rules.configure({
        rule("rpg.attack-buff", eve::settlement::StageKind::SourceModifiers, eve::settlement::RuleOperation::Multiply,
             1.5, {"damage"}),
        rule("tactics.high-ground", eve::settlement::StageKind::SourceModifiers, eve::settlement::RuleOperation::Add,
             4.0, {"damage"}),
        rule("rts.fortification", eve::settlement::StageKind::TargetMitigation,
             eve::settlement::RuleOperation::ResistPercent, 0.25, {"damage"}),
        rule("card.barrier", eve::settlement::StageKind::ArmorShield, eve::settlement::RuleOperation::AbsorbFlat, 5.0,
             {"damage"}),
    });
    REQUIRE(configured.ok());

    eve::settlement::SettlementPipeline pipeline;
    auto                                installed = rules.install(pipeline);
    REQUIRE(installed.ok());

    double                             health = 100.0;
    HealthPolicy                       policy(health);
    eve::settlement::SettlementRequest request;
    request.source    = subject(1);
    request.target    = subject(2);
    request.kind      = "damage";
    request.magnitude = 20.0;

    auto settled = pipeline.settle(request, policy);
    REQUIRE(settled.ok());
    const auto result = std::move(settled).takeValue();
    CHECK_EQ(result.applied, 20.5);
    CHECK_EQ(result.resisted, 8.5);
    CHECK_EQ(result.absorbed, 5.0);
    CHECK_EQ(result.disposition, eve::settlement::SettlementDisposition::PartiallyApplied);
    CHECK_EQ(health, 79.5);
    CHECK(result.hasStage("zz_rule.rpg.attack-buff"));
    CHECK(result.hasStage("zz_rule.tactics.high-ground"));
    CHECK(result.hasStage("zz_rule.rts.fortification"));
    CHECK(result.hasStage("zz_rule.card.barrier"));
}

TEST_CASE("settlement.decision.immunityIsDistinctFromZeroDamage") {
    auto immunity = rule("target.fire-immunity", eve::settlement::StageKind::Decision,
                         eve::settlement::RuleOperation::Immune, 0.0, {"damage"});
    immunity.filter.requiredTags = {"element:fire"};
    eve::settlement::SettlementRuleSet rules;
    REQUIRE(rules.configure({immunity}).ok());
    eve::settlement::SettlementPipeline pipeline;
    REQUIRE(rules.install(pipeline).ok());

    double                             health = 100.0;
    HealthPolicy                       policy(health);
    eve::settlement::SettlementRequest request;
    request.target    = subject(3);
    request.kind      = "damage";
    request.magnitude = 25.0;
    request.tags      = {"element:fire"};
    request.decisions.push_back(
        {*eve::LogicalId::parse("combat:hit"), 7, 0.2, 0.75, true});

    auto settled = pipeline.settle(request, policy);
    REQUIRE(settled.ok());
    CHECK_EQ(settled.value().applied, 0.0);
    CHECK_EQ(settled.value().disposition, eve::settlement::SettlementDisposition::Immune);
    CHECK_EQ(health, 100.0);
    REQUIRE(settled.value().event.has_value());
    auto payload = eve::Value::fromJson(settled.value().event->payload);
    REQUIRE(payload.ok());
    const auto* disposition = payload.value().find("disposition");
    REQUIRE(disposition != nullptr);
    CHECK_EQ(disposition->asString(), std::string("immune"));
    const auto* decisions = payload.value().find("decisions");
    REQUIRE(decisions != nullptr);
    REQUIRE(decisions->isArray());
    REQUIRE_EQ(decisions->arraySize(), 1u);
    CHECK_EQ(decisions->at(0).find("stream")->asString(), std::string("combat:hit"));
    CHECK_EQ(decisions->at(0).find("sequence")->asInt(), 7);
    CHECK(decisions->at(0).find("accepted")->asBool());
}

TEST_CASE("settlement.decision.immunityRemainsTerminalAcrossLaterNumericRules") {
    auto immunity = rule("target.immunity", eve::settlement::StageKind::Decision,
                         eve::settlement::RuleOperation::Immune, 0.0, {"damage"});
    auto added = rule("source.add", eve::settlement::StageKind::SourceModifiers,
                      eve::settlement::RuleOperation::Add, 40.0, {"damage"});
    auto multiplied = rule("target.multiply", eve::settlement::StageKind::TargetMitigation,
                           eve::settlement::RuleOperation::Multiply, 3.0, {"damage"});
    eve::settlement::SettlementRuleSet rules;
    REQUIRE(rules.configure({immunity, added, multiplied}).ok());
    eve::settlement::SettlementPipeline pipeline;
    REQUIRE(rules.install(pipeline).ok());

    double                             health = 100.0;
    HealthPolicy                       policy(health);
    eve::settlement::SettlementRequest request;
    request.target    = subject(30);
    request.kind      = "damage";
    request.magnitude = 25.0;

    auto settled = pipeline.settle(request, policy);
    REQUIRE(settled.ok());
    CHECK_EQ(settled.value().applied, 0.0);
    CHECK_EQ(settled.value().disposition, eve::settlement::SettlementDisposition::Immune);
    CHECK_EQ(health, 100.0);
}

TEST_CASE("settlement.stages.canonicalPolicyPrecedesMinimumPriorityCustomStage") {
    eve::settlement::SettlementPipeline pipeline;
    REQUIRE(pipeline.addStage(eve::settlement::StageKind::SourceModifiers, "aaa_custom",
                              std::numeric_limits<int>::min(), [](eve::settlement::SettlementContext& context) {
                                  return context.setMagnitude(context.magnitude() + 1.0);
                              }).ok());
    double                             health = 100.0;
    OrderedSourcePolicy                policy(health);
    eve::settlement::SettlementRequest request;
    request.target    = subject(31);
    request.kind      = "damage";
    request.magnitude = 10.0;

    auto settled = pipeline.settle(request, policy);
    REQUIRE(settled.ok());
    CHECK_EQ(settled.value().applied, 21.0);
    CHECK_EQ(health, 79.0);
}

TEST_CASE("settlement.decision.rejectsInvalidRecordedRandomInputBeforeMutation") {
    eve::settlement::SettlementPipeline pipeline;
    double                              health = 100.0;
    HealthPolicy                        policy(health);
    eve::settlement::SettlementRequest request;
    request.target    = subject(27);
    request.kind      = "damage";
    request.magnitude = 25.0;
    request.decisions.push_back({*eve::LogicalId::parse("combat:hit"), 1, 1.5, 0.75, false});

    auto settled = pipeline.settle(request, policy);
    CHECK(!settled.ok());
    CHECK_EQ(health, 100.0);
}

TEST_CASE("settlement.rules.domainPolicyAlwaysRunsBeforeDeclarativeRules") {
    auto multiplier     = rule("negative-priority", eve::settlement::StageKind::SourceModifiers,
                               eve::settlement::RuleOperation::Multiply, 2.0, {"damage"});
    multiplier.priority = std::numeric_limits<int>::min();
    eve::settlement::SettlementRuleSet rules;
    REQUIRE(rules.configure({multiplier}).ok());
    eve::settlement::SettlementPipeline pipeline;
    REQUIRE(rules.install(pipeline).ok());
    double                             health = 100.0;
    HealthPolicy                       policy(health);
    eve::settlement::SettlementRequest request;
    request.target    = subject(4);
    request.kind      = "damage";
    request.magnitude = 10.0;
    auto settled      = pipeline.settle(request, policy);
    REQUIRE(settled.ok());
    CHECK_EQ(settled.value().applied, 20.0);
}

TEST_CASE("settlement.trace.offSummaryAndFullReuseStageResults") {
    eve::settlement::SettlementRuleSet rules;
    REQUIRE(rules
                .configure({rule("trace.add", eve::settlement::StageKind::SourceModifiers,
                                 eve::settlement::RuleOperation::Add, 2.0, {"damage"}),
                            rule("trace.miss", eve::settlement::StageKind::SourceModifiers,
                                 eve::settlement::RuleOperation::Add, 50.0, {"healing"})})
                .ok());
    eve::settlement::SettlementPipeline pipeline;
    REQUIRE(rules.install(pipeline).ok());
    eve::settlement::SettlementRequest request;
    request.target    = subject(34);
    request.kind      = "damage";
    request.magnitude = 10.0;

    double       offHealth = 100.0;
    HealthPolicy offPolicy(offHealth);
    request.trace = eve::settlement::SettlementTraceLevel::Off;
    auto off      = pipeline.settle(request, offPolicy);
    REQUIRE(off.ok());
    CHECK(off.value().stages.empty());
    CHECK_EQ(off.value().ruleEvaluationCount(), 0u);
    CHECK_EQ(off.value().ruleMatchCount(), 0u);
    CHECK_EQ(off.value().applied, 12.0);

    double       summaryHealth = 100.0;
    HealthPolicy summaryPolicy(summaryHealth);
    request.trace = eve::settlement::SettlementTraceLevel::Summary;
    auto summary  = pipeline.settle(request, summaryPolicy);
    REQUIRE(summary.ok());
    REQUIRE(summary.value().hasStage("zz_rule.trace.add"));
    CHECK(summary.value().stage("zz_rule.trace.add")->details.keys().empty());
    CHECK_EQ(summary.value().ruleEvaluationCount(), 2u);
    CHECK_EQ(summary.value().ruleMatchCount(), 1u);

    double       fullHealth = 100.0;
    HealthPolicy fullPolicy(fullHealth);
    request.trace = eve::settlement::SettlementTraceLevel::Full;
    auto full      = pipeline.settle(request, fullPolicy);
    REQUIRE(full.ok());
    REQUIRE(full.value().hasStage("zz_rule.trace.add"));
    CHECK(!full.value().stage("zz_rule.trace.add")->details.keys().empty());
    CHECK_EQ(full.value().ruleEvaluationCount(), 2u);
    CHECK_EQ(full.value().ruleMatchCount(), 1u);
}

TEST_CASE("settlement.rules.stackFiltersAndTransactionalConfiguration") {
    eve::settlement::SettlementRuleSet rules;
    auto poison               = rule("poison.vulnerability", eve::settlement::StageKind::SourceModifiers,
                                     eve::settlement::RuleOperation::Add, 2.0, {"damage"});
    poison.stacks             = 3;
    poison.valuePerExtraStack = 1.5;
    poison.filter.requiredTags.push_back("poison");
    REQUIRE(rules.configure({poison}).ok());

    auto invalid = poison;
    invalid.id.clear();
    auto rejected = rules.configure({invalid});
    CHECK(!rejected.ok());
    CHECK_EQ(rules.size(), 1u);

    eve::settlement::SettlementPipeline pipeline;
    REQUIRE(rules.install(pipeline).ok());
    double                             health = 40.0;
    HealthPolicy                       policy(health, 40.0);
    eve::settlement::SettlementRequest request;
    request.target    = subject(3);
    request.kind      = "damage";
    request.magnitude = 10.0;
    request.tags      = {"poison"};
    auto settled      = pipeline.settle(request, policy);
    REQUIRE(settled.ok());
    CHECK_EQ(settled.value().applied, 15.0);
    CHECK_EQ(health, 25.0);
}

TEST_CASE("settlement.rules.compilesAndEvaluatesTypedConditionsOnce") {
    auto conditional = rule("conditional", eve::settlement::StageKind::SourceModifiers,
                            eve::settlement::RuleOperation::Add, 5.0, {"damage"});
    conditional.when =
        "kind == \"damage\" && magnitude + context.bonus * 2 >= 14 && tick == 7 && "
        "has_tag(\"poison\") && context.enabled && context.mode == \"burst\" && "
        "min(3, 4) == 3 && max(2, 5) == 5 && clamp(10, 0, 5) == 5 && abs(-2) == 2";

    eve::settlement::SettlementRuleSet rules;
    REQUIRE(rules.configure({conditional}).ok());
    eve::settlement::SettlementPipeline pipeline;
    REQUIRE(rules.install(pipeline).ok());

    double                             health = 50.0;
    HealthPolicy                       policy(health, 50.0);
    eve::settlement::SettlementRequest request;
    request.target    = subject(5);
    request.kind      = "damage";
    request.magnitude = 10.0;
    request.tick      = eve::SimulationTick(7);
    request.tags      = {"poison"};
    request.context   = eve::Value(eve::Value::Object{
        {"bonus", eve::Value(2.0)},
        {"enabled", eve::Value(true)},
        {"mode", eve::Value("burst")},
    });

    auto settled = pipeline.settle(request, policy);
    REQUIRE(settled.ok());
    CHECK_EQ(settled.value().applied, 15.0);
    const auto* stage = settled.value().stage("zz_rule.conditional");
    REQUIRE(stage != nullptr);
    const auto* digest = stage->details.find("condition_digest");
    REQUIRE(digest != nullptr);
    CHECK(digest->isString());
    CHECK_EQ(digest->asString().size(), 16u);

    eve::settlement::SettlementRuleSet sameRules;
    REQUIRE(sameRules.configure({conditional}).ok());
    eve::settlement::SettlementPipeline samePipeline;
    REQUIRE(sameRules.install(samePipeline).ok());
    double       sameHealth = 50.0;
    HealthPolicy samePolicy(sameHealth, 50.0);
    auto         sameSettled = samePipeline.settle(request, samePolicy);
    REQUIRE(sameSettled.ok());
    const auto* sameStage = sameSettled.value().stage("zz_rule.conditional");
    REQUIRE(sameStage != nullptr);
    const auto* sameDigest = sameStage->details.find("condition_digest");
    REQUIRE(sameDigest != nullptr);
    CHECK_EQ(sameDigest->asString(), digest->asString());
}

TEST_CASE("settlement.rules.conditionShortCircuitSkipsUnavailableFields") {
    auto conditional = rule("short-circuit", eve::settlement::StageKind::SourceModifiers,
                            eve::settlement::RuleOperation::Add, 100.0, {"damage"});
    conditional.when = "false && context.missing > 0";
    eve::settlement::SettlementRuleSet rules;
    REQUIRE(rules.configure({conditional}).ok());
    eve::settlement::SettlementPipeline pipeline;
    REQUIRE(rules.install(pipeline).ok());
    double                             health = 30.0;
    HealthPolicy                       policy(health, 30.0);
    eve::settlement::SettlementRequest request;
    request.target    = subject(6);
    request.kind      = "damage";
    request.magnitude = 4.0;
    auto settled      = pipeline.settle(request, policy);
    REQUIRE(settled.ok());
    CHECK_EQ(settled.value().applied, 4.0);
    CHECK_EQ(health, 26.0);
    const auto* stage = settled.value().stage("zz_rule.short-circuit");
    REQUIRE(stage != nullptr);
    CHECK_EQ(stage->status, eve::StatusCode::NoOp);
    CHECK(stage->details.isObject());
    CHECK(stage->details.keys().empty());
}

TEST_CASE("settlement.rules.conditionCompilationIsTransactionalAndLocated") {
    auto valid = rule("valid", eve::settlement::StageKind::SourceModifiers, eve::settlement::RuleOperation::Multiply,
                      2.0, {"damage"});
    valid.when = "max(magnitude, 3) == 10";
    eve::settlement::SettlementRuleSet rules;
    REQUIRE(rules.configure({valid}).ok());

    auto invalid  = valid;
    invalid.id    = "invalid";
    invalid.when  = "kind &&\n magnitude";
    auto rejected = rules.configure({invalid});
    REQUIRE(!rejected.ok());
    const auto* diagnostic = rejected.status().primaryDiagnostic();
    REQUIRE(diagnostic != nullptr);
    CHECK_EQ(diagnostic->code(), eve::DiagnosticCode::TypeMismatch);
    CHECK_EQ(diagnostic->path(), "rules[0].when");
    REQUIRE(diagnostic->details().size() >= 4u);
    CHECK_EQ(diagnostic->details()[0].second, "1");
    CHECK_EQ(rules.size(), 1u);

    eve::settlement::SettlementPipeline pipeline;
    REQUIRE(rules.install(pipeline).ok());
    double                             health = 30.0;
    HealthPolicy                       policy(health, 30.0);
    eve::settlement::SettlementRequest request;
    request.target    = subject(7);
    request.kind      = "damage";
    request.magnitude = 10.0;
    auto settled      = pipeline.settle(request, policy);
    REQUIRE(settled.ok());
    CHECK_EQ(settled.value().applied, 20.0);
}

TEST_CASE("settlement.rules.conditionRejectsConstantAndRuntimeDivisionByZero") {
    auto constant =
        rule("constant-zero", eve::settlement::StageKind::SourceModifiers, eve::settlement::RuleOperation::Add, 1.0);
    constant.when = "magnitude / 0 > 1";
    eve::settlement::SettlementRuleSet rules;
    auto                               rejected = rules.configure({constant});
    REQUIRE(!rejected.ok());
    REQUIRE(rejected.status().primaryDiagnostic() != nullptr);
    CHECK_EQ(rejected.status().primaryDiagnostic()->code(), eve::DiagnosticCode::InvalidArgument);

    auto dynamic = constant;
    dynamic.id   = "dynamic-zero";
    dynamic.when = "magnitude / context.divisor > 1";
    REQUIRE(rules.configure({dynamic}).ok());
    eve::settlement::SettlementPipeline pipeline;
    REQUIRE(rules.install(pipeline).ok());
    double                             health = 20.0;
    HealthPolicy                       policy(health, 20.0);
    eve::settlement::SettlementRequest request;
    request.target    = subject(8);
    request.kind      = "damage";
    request.magnitude = 5.0;
    request.context   = eve::Value(eve::Value::Object{{"divisor", eve::Value(0.0)}});
    auto settled      = pipeline.settle(request, policy);
    CHECK(!settled.ok());
    CHECK_EQ(health, 20.0);
}

TEST_CASE("settlement.rules.conditionCompilerEnforcesInputLimits") {
    auto candidate =
        rule("limited", eve::settlement::StageKind::SourceModifiers, eve::settlement::RuleOperation::Add, 1.0);
    eve::settlement::SettlementRuleSet rules;

    candidate.when.assign(4097, 'x');
    CHECK(!rules.configure({candidate}).ok());

    candidate.when.assign(40, '(');
    candidate.when += "true";
    candidate.when.append(40, ')');
    CHECK(!rules.configure({candidate}).ok());

    candidate.when = "true";
    for (int index = 0; index < 300; ++index) candidate.when += " && true";
    CHECK(!rules.configure({candidate}).ok());
}

TEST_CASE("settlement.rules.conditionCompilerRejectsMalformedCorpus") {
    const std::array<std::string, 10> malformed{
        " ",          "(",      "kind = \"damage\"",      "unknown > 0",    "1 && true", "kind < 2",
        "has_tag(1)", "min(1)", "context.a == context.b", "\"unterminated",
    };
    for (std::size_t index = 0; index < malformed.size(); ++index) {
        auto candidate = rule("malformed-" + std::to_string(index), eve::settlement::StageKind::SourceModifiers,
                              eve::settlement::RuleOperation::Add, 1.0);
        candidate.when = malformed[index];
        eve::settlement::SettlementRuleSet rules;
        auto                               configured = rules.configure({candidate});
        CHECK(!configured.ok());
    }
}

TEST_CASE("settlement.rules.conditionCompilerHandlesDeterministicFuzzCorpus") {
    constexpr std::string_view alphabet = "abc01&|!<>=+-*/().,\\\" ";
    std::uint32_t              state    = 0x5eed1234u;
    for (int sample = 0; sample < 256; ++sample) {
        std::string source;
        const auto  length = static_cast<std::size_t>(state % 48u);
        for (std::size_t index = 0; index < length; ++index) {
            state = state * 1664525u + 1013904223u;
            source.push_back(alphabet[state % alphabet.size()]);
        }
        if (source.empty()) source = " ";
        auto candidate =
            rule("fuzz", eve::settlement::StageKind::SourceModifiers, eve::settlement::RuleOperation::Add, 1.0);
        candidate.when = std::move(source);
        eve::settlement::SettlementRuleSet rules;
        auto                               configured = rules.configure({candidate});
        (void)configured.ok();
    }
}

TEST_CASE("settlement.atomic.commitsDistinctResourceChannelsTogether") {
    auto manaDiscount = rule("mana.discount", eve::settlement::StageKind::SourceModifiers,
                             eve::settlement::RuleOperation::Multiply, 0.5, {"spend"});
    manaDiscount.when = "resource == \"mana\"";
    eve::settlement::SettlementRuleSet rules;
    REQUIRE(rules.configure({manaDiscount}).ok());
    eve::settlement::SettlementPipeline pipeline;
    REQUIRE(rules.install(pipeline).ok());
    double                                            health = 100.0;
    double                                            mana   = 40.0;
    ResourcePolicy                                    healthPolicy(health, 100.0);
    ResourcePolicy                                    manaPolicy(mana, 40.0);
    std::array<eve::settlement::SettlementRequest, 2> requests;
    requests[0].target    = subject(20);
    requests[0].kind      = "damage";
    requests[0].resource  = "hp";
    requests[0].magnitude = 25.0;
    requests[1].target    = subject(20);
    requests[1].kind      = "spend";
    requests[1].resource  = "mana";
    requests[1].magnitude = 15.0;
    std::array<eve::settlement::ISettlementPolicy*, 2> policies{&healthPolicy, &manaPolicy};

    auto settled = pipeline.settleAtomic(requests, policies);
    REQUIRE(settled.ok());
    REQUIRE_EQ(settled.value().size(), 2u);
    CHECK_EQ(settled.value()[0].applied, 25.0);
    CHECK_EQ(settled.value()[1].applied, 7.5);
    CHECK_EQ(health, 75.0);
    CHECK_EQ(mana, 32.5);
}

TEST_CASE("settlement.atomic.rollsBackPreparedAndCommittedChannelsOnFailure") {
    eve::settlement::SettlementPipeline pipeline;
    const auto                          makeRequests = [] {
        std::array<eve::settlement::SettlementRequest, 2> requests;
        requests[0].target    = subject(21);
        requests[0].kind      = "damage";
        requests[0].resource  = "hp";
        requests[0].magnitude = 20.0;
        requests[1].target    = subject(21);
        requests[1].kind      = "spend";
        requests[1].resource  = "mana";
        requests[1].magnitude = 10.0;
        return requests;
    };

    double                                             health = 100.0;
    double                                             mana   = 30.0;
    ResourcePolicy                                     healthPolicy(health, 100.0);
    ResourcePolicy                                     rejectedMana(mana, 30.0, true);
    std::array<eve::settlement::ISettlementPolicy*, 2> rejectedPolicies{&healthPolicy, &rejectedMana};
    auto                                               rejectedRequests = makeRequests();
    auto rejected = pipeline.settleAtomic(rejectedRequests, rejectedPolicies);
    CHECK(!rejected.ok());
    CHECK_EQ(health, 100.0);
    CHECK_EQ(mana, 30.0);

    ResourcePolicy                                     failingMana(mana, 30.0, false, true);
    std::array<eve::settlement::ISettlementPolicy*, 2> failingPolicies{&healthPolicy, &failingMana};
    auto                                               failingRequests = makeRequests();
    auto failedCommit = pipeline.settleAtomic(failingRequests, failingPolicies);
    CHECK(!failedCommit.ok());
    CHECK_EQ(health, 100.0);
    CHECK_EQ(mana, 30.0);
}

TEST_CASE("settlement.atomic.chainsSegmentsForOneResourcePolicy") {
    eve::settlement::SettlementPipeline               pipeline;
    double                                            value = 100.0;
    ResourcePolicy                                    policy(value, 100.0);
    std::array<eve::settlement::SettlementRequest, 2> requests;
    for (auto& request : requests) {
        request.target    = subject(22);
        request.kind      = "damage";
        request.resource  = "hp";
        request.magnitude = 10.0;
    }
    std::array<eve::settlement::ISettlementPolicy*, 2> policies{&policy, &policy};
    auto                                               settled = pipeline.settleAtomic(requests, policies);
    REQUIRE(settled.ok());
    REQUIRE_EQ(settled.value().size(), 2u);
    CHECK_EQ(settled.value()[0].applied, 10.0);
    CHECK_EQ(settled.value()[1].applied, 10.0);
    CHECK_EQ(value, 80.0);
}

TEST_CASE("settlement.atomic.rejectsAmbiguousChannelOwnership") {
    eve::settlement::SettlementPipeline               pipeline;
    double                                            firstValue  = 100.0;
    double                                            secondValue = 100.0;
    ResourcePolicy                                    firstPolicy(firstValue, 100.0);
    ResourcePolicy                                    secondPolicy(secondValue, 100.0);
    std::array<eve::settlement::SettlementRequest, 2> requests;
    for (auto& request : requests) {
        request.target    = subject(26);
        request.kind      = "damage";
        request.resource  = "hp";
        request.magnitude = 10.0;
    }
    std::array<eve::settlement::ISettlementPolicy*, 2> policies{&firstPolicy, &secondPolicy};
    auto                                               settled = pipeline.settleAtomic(requests, policies);
    CHECK(!settled.ok());
    CHECK_EQ(firstValue, 100.0);
    CHECK_EQ(secondValue, 100.0);
}

TEST_CASE("settlement.atomic.restoresDomainAndEventLogAfterPartialEventAppend") {
    eve::settlement::SettlementPipeline               pipeline;
    double                                            health = 100.0;
    double                                            mana   = 40.0;
    ResourcePolicy                                    healthPolicy(health, 100.0);
    ResourcePolicy                                    manaPolicy(mana, 40.0);
    std::array<eve::settlement::SettlementRequest, 2> requests;
    requests[0].target    = subject(23);
    requests[0].kind      = "damage";
    requests[0].resource  = "hp";
    requests[0].magnitude = 20.0;
    requests[1].target    = subject(23);
    requests[1].kind      = "spend";
    requests[1].resource  = "mana";
    requests[1].magnitude = 10.0;
    std::array<eve::settlement::ISettlementPolicy*, 2> policies{&healthPolicy, &manaPolicy};

    int                           entropyCalls = 0;
    eve::game_event::GameEventLog events([&](std::span<std::uint8_t> bytes) {
        ++entropyCalls;
        if (entropyCalls == 2) return false;
        std::fill(bytes.begin(), bytes.end(), std::uint8_t{0x2a});
        return true;
    });
    auto                          settled = pipeline.settleAtomic(requests, policies, &events);
    CHECK(!settled.ok());
    CHECK_EQ(events.size(), 0);
    CHECK_EQ(health, 100.0);
    CHECK_EQ(mana, 40.0);
}

TEST_CASE("settlement.independent.preservesPerTargetSuccessAndFailure") {
    eve::settlement::SettlementPipeline               pipeline;
    double                                            firstHealth  = 100.0;
    double                                            secondHealth = 100.0;
    ResourcePolicy                                    firstPolicy(firstHealth, 100.0);
    ResourcePolicy                                    secondPolicy(secondHealth, 100.0, true);
    std::array<eve::settlement::SettlementRequest, 2> requests;
    requests[0].target    = subject(24);
    requests[0].kind      = "damage";
    requests[0].resource  = "hp";
    requests[0].magnitude = 15.0;
    requests[1].target    = subject(25);
    requests[1].kind      = "damage";
    requests[1].resource  = "hp";
    requests[1].magnitude = 20.0;
    std::array<eve::settlement::ISettlementPolicy*, 2> policies{&firstPolicy, &secondPolicy};

    auto settled = pipeline.settleIndependent(requests, policies);
    REQUIRE(settled.ok());
    REQUIRE_EQ(settled.value().size(), 2u);
    CHECK(settled.value()[0].status.isSuccess());
    REQUIRE(settled.value()[0].result.has_value());
    CHECK_EQ(settled.value()[0].result->applied, 15.0);
    CHECK(!settled.value()[1].status.isSuccess());
    CHECK(!settled.value()[1].result.has_value());
    CHECK_EQ(firstHealth, 85.0);
    CHECK_EQ(secondHealth, 100.0);
}

TEST_CASE("settlement.chain.lifestealUsesActuallyAppliedDamage") {
    const auto attacker = subject(30);
    const auto enemy    = subject(31);
    double     attackerHealth = 50.0;
    double     enemyHealth    = 100.0;
    HealthPolicy attackerPolicy(attackerHealth);
    HealthPolicy enemyPolicy(enemyHealth);
    eve::settlement::SettlementPipeline pipeline;
    eve::settlement::SettlementRuleSet  rules;
    REQUIRE(rules
                .configure({rule("vampiric", eve::settlement::StageKind::Trigger,
                                 eve::settlement::RuleOperation::Lifesteal, 0.5, {"damage"})})
                .ok());
    REQUIRE(rules.install(pipeline).ok());
    eve::settlement::SettlementRequest  root;
    root.source    = attacker;
    root.target    = enemy;
    root.kind      = "damage";
    root.resource  = "hp";
    root.magnitude = 20.0;

    eve::settlement::SettlementPipeline::RequestExecutor execute =
        [&](const eve::settlement::SettlementRequest& request) -> eve::Result<eve::settlement::SettlementResult> {
        if (request.target == attacker)
            return pipeline.settle(request, attackerPolicy);
        if (request.target == enemy)
            return pipeline.settle(request, enemyPolicy);
        return eve::Result<eve::settlement::SettlementResult>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::NotFound, "test policy not found"));
    };

    auto settled = pipeline.settleChain(root, execute, 4, 8);
    REQUIRE(settled.ok());
    REQUIRE_EQ(settled.value().size(), 2u);
    REQUIRE(settled.value()[0].result.has_value());
    REQUIRE_EQ(settled.value()[0].result->derived.size(), 1u);
    CHECK_EQ(settled.value()[0].result->applied, 20.0);
    CHECK_EQ(settled.value()[1].result->applied, 10.0);
    CHECK_EQ(enemyHealth, 80.0);
    CHECK_EQ(attackerHealth, 60.0);
}

TEST_CASE("settlement.chain.rejectsReflectReentryWithoutLooping") {
    const auto left  = subject(32);
    const auto right = subject(33);
    double     leftHealth  = 100.0;
    double     rightHealth = 100.0;
    HealthPolicy leftPolicy(leftHealth);
    HealthPolicy rightPolicy(rightHealth);
    eve::settlement::SettlementPipeline pipeline;
    eve::settlement::SettlementRuleSet  rules;
    REQUIRE(rules
                .configure({rule("thorns", eve::settlement::StageKind::Trigger,
                                 eve::settlement::RuleOperation::Reflect, 1.0, {"damage"})})
                .ok());
    REQUIRE(rules.install(pipeline).ok());
    eve::settlement::SettlementRequest  root;
    root.source    = left;
    root.target    = right;
    root.kind      = "damage";
    root.resource  = "hp";
    root.magnitude = 10.0;

    eve::settlement::SettlementPipeline::RequestExecutor execute =
        [&](const eve::settlement::SettlementRequest& request) -> eve::Result<eve::settlement::SettlementResult> {
        return request.target == left ? pipeline.settle(request, leftPolicy) : pipeline.settle(request, rightPolicy);
    };

    auto settled = pipeline.settleChain(root, execute, 8, 16);
    REQUIRE(settled.ok());
    REQUIRE_EQ(settled.value().size(), 3u);
    REQUIRE(settled.value()[0].result.has_value());
    REQUIRE(settled.value()[1].result.has_value());
    CHECK(!settled.value()[2].status.isSuccess());
    CHECK(!settled.value()[2].result.has_value());
    CHECK_EQ(rightHealth, 90.0);
    CHECK_EQ(leftHealth, 90.0);
}

TEST_CASE("settlement.rules.digestUsesSemanticOrderAndNormalizedFilters") {
    auto firstRule = rule("armor", eve::settlement::StageKind::TargetMitigation,
                          eve::settlement::RuleOperation::ResistFlat, 3.0, {"damage", "damage"});
    firstRule.filter.requiredTags = {"physical", "weapon"};
    auto secondRule = rule("power", eve::settlement::StageKind::SourceModifiers,
                           eve::settlement::RuleOperation::Multiply, 1.5, {"damage"});

    eve::settlement::SettlementRuleSet first;
    eve::settlement::SettlementRuleSet reordered;
    REQUIRE(first.configure({firstRule, secondRule}).ok());
    firstRule.filter.kinds        = {"damage"};
    firstRule.filter.requiredTags = {"weapon", "physical", "physical"};
    REQUIRE(reordered.configure({secondRule, firstRule}).ok());

    auto firstDigest     = first.digest(ruleHashProvider());
    auto reorderedDigest = reordered.digest(ruleHashProvider());
    REQUIRE(firstDigest.ok());
    REQUIRE(reorderedDigest.ok());
    CHECK_EQ(firstDigest.value(), reorderedDigest.value());

    auto changedRule  = secondRule;
    changedRule.value = 1.6;
    eve::settlement::SettlementRuleSet changed;
    REQUIRE(changed.configure({firstRule, changedRule}).ok());
    auto changedDigest = changed.digest(ruleHashProvider());
    REQUIRE(changedDigest.ok());
    CHECK(firstDigest.value() != changedDigest.value());
}

TEST_CASE("settlement.rules.digestRequiresExplicitProvider") {
    eve::settlement::SettlementRuleSet rules;
    REQUIRE(rules.configure({}).ok());
    const eve::SnapshotHashProvider missing;
    auto result = rules.digest(missing);
    CHECK(!result.ok());
}

TEST_CASE("settlement.rules.versionedJsonRoundTripsAndRejectsUnknownFieldsTransactionally") {
    auto configuredRule = rule("guard", eve::settlement::StageKind::TargetMitigation,
                               eve::settlement::RuleOperation::ResistPercent, 0.25, {"damage"});
    configuredRule.when = "context.target_hp_ratio < 0.5";
    eve::settlement::SettlementRuleSet original;
    REQUIRE(original.configure({configuredRule}).ok());
    auto json = original.canonicalJson();
    REQUIRE(json.ok());

    eve::settlement::SettlementRuleSet restored;
    REQUIRE(restored.configureJson(json.value()).ok());
    auto inspected = eve::settlement::SettlementRuleSet::fromJson(json.value());
    REQUIRE(inspected.ok());
    CHECK_EQ(inspected.value().size(), 1u);
    auto restoredJson = restored.canonicalJson();
    REQUIRE(restoredJson.ok());
    CHECK_EQ(restoredJson.value(), json.value());

    auto invalid = eve::Value::fromJson(json.value());
    REQUIRE(invalid.ok());
    invalid.value().set("unexpected", true);
    auto invalidJson = invalid.value().toJson();
    REQUIRE(invalidJson.ok());
    auto rejectedInspection = eve::settlement::SettlementRuleSet::fromJson(invalidJson.value());
    CHECK(!rejectedInspection.ok());
    REQUIRE(rejectedInspection.error() != nullptr);
    CHECK_EQ(rejectedInspection.error()->path(), std::string("document"));
    CHECK(!restored.configureJson(invalidJson.value()).ok());
    auto retainedJson = restored.canonicalJson();
    REQUIRE(retainedJson.ok());
    CHECK_EQ(retainedJson.value(), json.value());
}
