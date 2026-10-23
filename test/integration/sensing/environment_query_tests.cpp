#include <cmath>
#include <limits>
#include <stdexcept>
#include "sensing/EnvironmentQuery.h"
#include "sensing/LineOfSightRouter.h"
#include "sensing/TargetingPipeline.h"
#include "simplesquirrel/simplesquirrel.hpp"
#include "zeroerr/unittest.h"

using namespace eve;
using namespace eve::sensing;

namespace {
TargetLocation point(float x, float y = 0) {
    auto value = WorldPoint::world2D(x, y);
    REQUIRE(value.ok());
    return value.value();
}
EnvironmentQueryDefinition definition() {
    EnvironmentQueryDefinition d;
    d.id       = "test.environment";
    d.contexts = {"self"};
    d.trace    = EnvironmentTraceLevel::Detailed;
    EnvironmentOption option;
    option.id = "primary";
    EnvironmentTest test;
    test.id           = "distance";
    test.context      = "self";
    test.purpose      = EnvironmentPurpose::Score;
    test.scoreMaximum = 20;
    option.tests.push_back(test);
    d.options.push_back(option);
    return d;
}
EnvironmentRequest request() {
    EnvironmentRequest r;
    r.contexts = {{"self", {point(0)}}};
    r.input    = {{3, point(3), "third"}, {1, point(1), "first"}, {2, point(2), "second"}};
    return r;
}
Result<EnvironmentQueryResult> run(const EnvironmentQueryDefinition& d, EnvironmentRequest r,
                                   const EnvironmentServices& services = {}) {
    auto plan = EnvironmentQueryPlan::compile(d);
    if (!plan) return Result<EnvironmentQueryResult>::failure(plan.status());
    auto instance = plan.value().instantiate(std::move(r));
    if (!instance) return Result<EnvironmentQueryResult>::failure(instance.status());
    return instance.value().run(services);
}
class Visibility final : public ILineOfSightQuery {
public:
    mutable int               calls    = 0;
    float                     minimumX = 0;
    bool                      fail     = false;
    bool                      throws   = false;
    Result<LineOfSightResult> query(const TargetLocation&, const TargetLocation& to) const override {
        ++calls;
        if (throws) throw std::runtime_error("provider exception");
        if (fail)
            return Result<LineOfSightResult>::failure(Diagnostic::error(DiagnosticCode::Failed, "backend failed"));
        const double x = std::visit([](const auto& p) { return double(p.x()); }, to);
        return Result<LineOfSightResult>::success({x >= minimumX, std::nullopt});
    }
};
EnvironmentTest losTest() {
    EnvironmentTest test;
    test.id      = "los";
    test.context = "self";
    test.metric  = EnvironmentMetricKind::LineOfSight;
    test.purpose = EnvironmentPurpose::Filter;
    test.minimum = test.maximum = 1;
    return test;
}
class CustomMetric final : public IEnvironmentMetric {
public:
    bool                      reorderable = false;
    double                    value       = 1;
    EnvironmentMetricContract contract() const noexcept override {
        return {CoordinateSpace::World2D, EnvironmentCost::Medium, reorderable, false};
    }
    Result<double> measure(const EnvironmentItem&, const TargetLocation&, const EnvironmentServices&) const override {
        return Result<double>::success(value);
    }
};
}  // namespace

TEST_CASE("sensing.environment.compiledPlanOwnsDefinitionAndInstancesOwnContexts") {
    auto d    = definition();
    auto plan = EnvironmentQueryPlan::compile(d);
    REQUIRE(plan.ok());
    auto r        = request();
    auto instance = plan.value().instantiate(r);
    REQUIRE(instance.ok());
    d.options.clear();
    r.contexts[0].locations[0] = point(100);
    auto result                = instance.value().run({});
    REQUIRE(result.ok());
    CHECK_EQ(result.value().ranked.front().item.id, 1u);
    CHECK_EQ(instance.value().trace().state, std::string("completed"));
    auto second = instance.value().run({});
    CHECK(!second.ok());
    auto another = plan.value().instantiate(request());
    REQUIRE(another.ok());
    CHECK(another.value().run({}).ok());
}

TEST_CASE("sensing.environment.validatesTypesContextsAndBudgets") {
    auto d                        = definition();
    d.options[0].tests[0].context = "missing";
    CHECK(!EnvironmentQueryPlan::compile(d).ok());
    d         = definition();
    auto plan = EnvironmentQueryPlan::compile(d);
    REQUIRE(plan.ok());
    auto r = request();
    r.contexts[0].locations.clear();
    CHECK(!plan.value().instantiate(r).ok());
    r                   = request();
    r.input[0].location = GridPoint::grid2D(1, 1);
    CHECK(!plan.value().instantiate(r).ok());
    r             = request();
    r.input[0].id = r.input[1].id;
    CHECK(!plan.value().instantiate(r).ok());
    d.options[0].generator.maxItems = 2;
    CHECK(!run(d, request()).ok());
    d                            = definition();
    d.options[0].tests[0].weight = std::numeric_limits<double>::infinity();
    CHECK(!EnvironmentQueryPlan::compile(d).ok());
}

TEST_CASE("sensing.environment.ringAndGridGenerateLocationsWithoutSubjects") {
    auto d            = definition();
    d.maxResults      = 32;
    auto& generator   = d.options[0].generator;
    generator.kind    = EnvironmentGeneratorKind::Ring;
    generator.context = "self";
    generator.radius  = 4;
    generator.samples = 8;
    auto result       = run(d, request());
    REQUIRE(result.ok());
    REQUIRE(result.value().ranked.size() == 8u);
    for (const auto& candidate : result.value().ranked) {
        CHECK(!candidate.item.subjectKey.has_value());
        const auto p = std::get<WorldPoint>(candidate.item.location);
        CHECK(std::abs(std::hypot(p.x(), p.y()) - 4) < 0.0001);
    }
    generator.kind    = EnvironmentGeneratorKind::Grid;
    generator.radius  = 2;
    generator.spacing = 2;
    auto grid         = run(d, request());
    REQUIRE(grid.ok());
    CHECK_EQ(grid.value().ranked.size(), 9u);
    generator.maxItems = 8;
    CHECK(!run(d, request()).ok());
}

TEST_CASE("sensing.environment.multiContextFilterAndAggregateAreIndependent") {
    auto  d                 = definition();
    auto& test              = d.options[0].tests[0];
    test.purpose            = EnvironmentPurpose::FilterAndScore;
    test.maximum            = 3;
    auto r                  = request();
    r.contexts[0].locations = {point(0), point(5)};
    auto all                = run(d, r);
    REQUIRE(all.ok());
    CHECK_EQ(all.value().ranked.size(), 2u);
    test.filterOp  = EnvironmentFilterOp::Any;
    test.aggregate = EnvironmentAggregate::Maximum;
    auto any       = run(d, r);
    REQUIRE(any.ok());
    CHECK_EQ(any.value().ranked.size(), 3u);
    CHECK_EQ(any.value().ranked.front().item.id, 2u);
}

TEST_CASE("sensing.environment.contributionsNormalizeOnceAndExplainRejection") {
    auto d             = definition();
    d.optimize         = false;
    auto& test         = d.options[0].tests[0];
    test.purpose       = EnvironmentPurpose::FilterAndScore;
    test.maximum       = 2;
    test.normalization = EnvironmentNormalization::ActivePopulation;
    auto plan          = EnvironmentQueryPlan::compile(d);
    REQUIRE(plan.ok());
    auto instance = plan.value().instantiate(request());
    REQUIRE(instance.ok());
    auto result = instance.value().run({});
    REQUIRE(result.ok());
    REQUIRE(result.value().ranked.size() == 2u);
    CHECK_EQ(result.value().ranked[0].score, 1.0);
    CHECK_EQ(result.value().ranked[1].score, 0.0);
    const auto& trace = instance.value().trace();
    REQUIRE(trace.items.size() == 3u);
    CHECK_EQ(trace.items[0].rejectedBy, std::string("distance"));
    CHECK_EQ(trace.items[1].contributions[0].lower, 1.0);
    CHECK_EQ(trace.items[1].contributions[0].upper, 2.0);
    auto json = trace.toValue().toJson();
    REQUIRE(json.ok());
    CHECK(json.value().find("environmentTrace") != std::string::npos);
}

TEST_CASE("sensing.environment.weightsAndReferencePreferenceChangeRanking") {
    auto d               = definition();
    auto other           = d.options[0].tests[0];
    other.id             = "preferredDistance";
    other.preference     = EnvironmentPreference::Reference;
    other.referenceValue = 3;
    other.weight         = 10;
    d.options[0].tests.push_back(other);
    auto result = run(d, request());
    REQUIRE(result.ok());
    CHECK_EQ(result.value().ranked.front().item.id, 3u);
    d.options[0].tests[1].weight = 0;
    auto original                = run(d, request());
    REQUIRE(original.ok());
    CHECK_EQ(original.value().ranked.front().item.id, 1u);
}

TEST_CASE("sensing.environment.optionsOnlyAdvanceOnEmptyNotErrors") {
    auto d                        = definition();
    d.options[0].tests[0].purpose = EnvironmentPurpose::Filter;
    d.options[0].tests[0].maximum = 0;
    auto alternative              = definition().options[0];
    alternative.id                = "alternative";
    d.options.push_back(alternative);
    auto result = run(d, request());
    REQUIRE(result.ok());
    CHECK_EQ(result.value().option, std::string("alternative"));
    d.options[0].tests = {losTest()};
    CHECK(!run(d, request()).ok());
    Visibility provider;
    provider.fail = true;
    CHECK(!run(d, request(), {std::cref(provider)}).ok());
    d.options[0].tests = definition().options[0].tests;
    auto preferred     = run(d, request());
    REQUIRE(preferred.ok());
    CHECK_EQ(preferred.value().option, std::string("primary"));
    auto r = request();
    r.input.clear();
    auto empty = run(d, r);
    REQUIRE(empty.ok());
    CHECK(empty.value().ranked.empty());
}

TEST_CASE("sensing.environment.bestDefersExpensiveFilterAndMatchesExhaustive") {
    auto d = definition();
    d.mode = EnvironmentRunMode::Best;
    d.options[0].tests.insert(d.options[0].tests.begin(), losTest());
    Visibility provider;
    provider.minimumX = 2;
    auto plan         = EnvironmentQueryPlan::compile(d);
    REQUIRE(plan.ok());
    auto instance = plan.value().instantiate(request());
    REQUIRE(instance.ok());
    auto optimized = instance.value().run({std::cref(provider)});
    REQUIRE(optimized.ok());
    CHECK_EQ(optimized.value().ranked.front().item.id, 2u);
    CHECK_EQ(provider.calls, 2);
    CHECK(instance.value().trace().stages.back().deferred);
    provider.calls  = 0;
    d.optimize      = false;
    auto exhaustive = run(d, request(), {std::cref(provider)});
    REQUIRE(exhaustive.ok());
    CHECK_EQ(exhaustive.value().ranked.front().item.id, optimized.value().ranked.front().item.id);
    CHECK_EQ(exhaustive.value().ranked.front().score, optimized.value().ranked.front().score);
    CHECK_EQ(provider.calls, 3);
}

TEST_CASE("sensing.environment.relativeScoresAndCustomBarriersDisableUnsafeDeferral") {
    auto d                              = definition();
    d.mode                              = EnvironmentRunMode::Best;
    d.options[0].tests[0].normalization = EnvironmentNormalization::ActivePopulation;
    d.options[0].tests.push_back(losTest());
    Visibility provider;
    auto       result = run(d, request(), {std::cref(provider)});
    REQUIRE(result.ok());
    CHECK_EQ(provider.calls, 3);
    provider.calls = 0;
    d              = definition();
    d.mode         = EnvironmentRunMode::Best;
    auto metric    = std::make_shared<CustomMetric>();
    auto test      = d.options[0].tests[0];
    test.id        = "barrier";
    test.metric    = EnvironmentMetricKind::Custom;
    test.custom    = metric;
    d.options[0].tests.push_back(test);
    d.options[0].tests.push_back(losTest());
    auto barrier = run(d, request(), {std::cref(provider)});
    REQUIRE(barrier.ok());
    CHECK_EQ(provider.calls, 3);
    metric->value = std::numeric_limits<double>::quiet_NaN();
    CHECK(!run(d, request(), {std::cref(provider)}).ok());
}

TEST_CASE("sensing.environment.traceCapAndTraceOffDoNotChangeResults") {
    auto d          = definition();
    d.maxTraceItems = 1;
    auto plan       = EnvironmentQueryPlan::compile(d);
    REQUIRE(plan.ok());
    auto instance = plan.value().instantiate(request());
    REQUIRE(instance.ok());
    auto detailed = instance.value().run({});
    REQUIRE(detailed.ok());
    CHECK(instance.value().trace().truncated);
    CHECK_EQ(instance.value().trace().items.size(), 1u);
    d.trace    = EnvironmentTraceLevel::None;
    auto quiet = run(d, request());
    REQUIRE(quiet.ok());
    CHECK_EQ(quiet.value().ranked.front().score, detailed.value().ranked.front().score);
}

TEST_CASE("sensing.environment.worldCompositionPublishesTraceAndKeepsLegacyResults") {
    SensingWorld world;
    REQUIRE(world.upsert("enemy", 3, 0, "red", "unit", "").ok());
    QuerySpec spec;
    auto      old = world.query({}, spec);
    REQUIRE(old.ok());
    auto input = world.environmentInput({}, spec);
    REQUIRE(input.ok());
    input.value().contexts = {{"self", {point(0)}}};
    auto plan              = EnvironmentQueryPlan::compile(definition());
    REQUIRE(plan.ok());
    auto result = world.executeEnvironmentQuery(plan.value(), std::move(input).takeValue(), {});
    REQUIRE(result.ok());
    CHECK_EQ(*result.value().ranked.front().item.subjectKey, std::string("enemy"));
    CHECK_EQ(world.resultAt(0)->get().id, std::string("enemy"));
    CHECK(world.debugLastQueryJson().find("environmentTrace") != std::string::npos);
    auto d = definition();
    d.options[0].tests.push_back(losTest());
    auto failing = EnvironmentQueryPlan::compile(d);
    REQUIRE(failing.ok());
    CHECK(!world.executeEnvironmentQuery(failing.value(), request(), {}).ok());
    CHECK(world.debugLastQueryJson().find("\"state\":\"failed\"") != std::string::npos);
}

TEST_CASE("sensing.environment.scriptPositionsUseCanonicalExecutor") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        result <- "fail";
        local world = eve.Sensing().newWorld().value;
        local query = world.queryPositions(0.0, 0.0, 10.0, 0.0, 3.0, 8, 1, false);
        local invalid = world.queryPositions(0.0, 0.0, 10.0, 0.0, -1.0, 8, 1, false);
        local trace = world.debugLastQueryJson();
        if (query.ok && !invalid.ok && trace.ok && query.value.ranked.len() == 1 &&
            query.value.ranked[0].subject == null && query.value.ranked[0].location.space == "world2d") result = "ok";
        eve.result.ignore(world.release(), "test cleanup");
    )"));
    REQUIRE_EQ(vm.find("result").toString(), std::string("ok"));
}

TEST_CASE("sensing.pipeline.collectsBeforeFilteringAndPublishesOnlyOnSuccess") {
    SensingWorld world;
    for (int i = 1; i <= 9; ++i)
        REQUIRE(world.upsert(std::to_string(i), float(i), 0, "red", i == 9 ? "valid" : "", "").ok());
    QuerySpec original;
    REQUIRE(world.query({}, original).ok());
    const auto      before = world.debugLastQueryJson();
    TargetingPreset preset;
    preset.id = "test.finalCount";
    TargetingTaskStep select;
    select.taskId             = "sensing.select.world";
    select.querySpec.maxCount = 1;
    preset.steps.push_back(select);
    TargetingTaskStep filter;
    filter.taskId                 = "sensing.filter.spec";
    filter.querySpec.requiredTags = {"valid"};
    preset.steps.push_back(filter);
    auto pipeline = TargetingPipeline::withBuiltins();
    auto compiled = pipeline.compile(preset);
    REQUIRE(compiled.ok());
    TargetingSourceContext context{&world, {}, 1, 0};
    auto                   result = compiled.value().execute(context);
    REQUIRE(result.ok());
    REQUIRE(result.value().size() == 1u);
    CHECK_EQ(result.value().ranked()[0].id, std::string("9"));
    CHECK_EQ(world.debugLastQueryJson(), before);
    preset.steps[1].querySpec.requiredTags = {"absent"};
    preset.steps[0].querySpec.minCount     = 1;
    CHECK(!pipeline.execute(context, preset).ok());
    CHECK_EQ(world.debugLastQueryJson(), before);
    TargetingTaskStep truncate;
    truncate.taskId = "sensing.sort.truncate";
    preset.steps.insert(preset.steps.begin() + 1, truncate);
    CHECK(!pipeline.compile(preset).ok());
}

TEST_CASE("sensing.environment.spatialInputIdentityAndTiesAreStable") {
    SensingWorld world;
    REQUIRE(world.upsert("z", -1, 0, "", "", "").ok());
    REQUIRE(world.upsert("a", 1, 0, "", "", "").ok());
    QuerySpec spec;
    spec.maxRange = 10;
    auto linear   = world.environmentInput({}, spec);
    REQUIRE(linear.ok());
    REQUIRE(world.setSpatialIndexEnabled(true, 2).ok());
    auto indexed = world.environmentInput({}, spec);
    REQUIRE(indexed.ok());
    REQUIRE(linear.value().input.size() == indexed.value().input.size());
    for (std::size_t i = 0; i < linear.value().input.size(); ++i) {
        CHECK_EQ(linear.value().input[i].id, indexed.value().input[i].id);
        CHECK_EQ(*linear.value().input[i].subjectKey, *indexed.value().input[i].subjectKey);
    }
}

TEST_CASE("sensing.environment.optimizationParityAcrossCandidateSets") {
    Visibility provider;
    auto       d = definition();
    d.mode       = EnvironmentRunMode::Best;
    d.options[0].tests.insert(d.options[0].tests.begin(), losTest());
    for (int minimum = 0; minimum < 12; ++minimum) {
        provider.minimumX = float(minimum);
        EnvironmentRequest r;
        r.contexts = {{"self", {point(0), point(1)}}};
        for (int i = 9; i >= 0; --i) r.input.push_back({std::uint64_t(i), point(float(i)), std::nullopt});
        d.optimize     = true;
        auto optimized = run(d, r, {std::cref(provider)});
        REQUIRE(optimized.ok());
        d.optimize      = false;
        auto exhaustive = run(d, r, {std::cref(provider)});
        REQUIRE(exhaustive.ok());
        REQUIRE(optimized.value().ranked.size() == exhaustive.value().ranked.size());
        if (!optimized.value().ranked.empty()) {
            CHECK_EQ(optimized.value().ranked[0].item.id, exhaustive.value().ranked[0].item.id);
            CHECK_EQ(optimized.value().ranked[0].score, exhaustive.value().ranked[0].score);
        }
    }
}

TEST_CASE("sensing.pipeline.compiledPlanSurvivesRegistryAndPresetReplacement") {
    TargetingPreset preset;
    preset.id = "retained";
    TargetingTaskStep select;
    select.taskId = "sensing.select.world";
    preset.steps  = {select};
    auto plan     = [&]() {
        auto pipeline = TargetingPipeline::withBuiltins();
        auto compiled = pipeline.compile(preset);
        REQUIRE(compiled.ok());
        REQUIRE(pipeline.registerPreset(preset).ok());
        preset.steps[0].querySpec.requiredTags = {"absent"};
        REQUIRE(pipeline.registerPreset(preset).ok());
        return std::move(compiled).takeValue();
    }();
    SensingWorld world;
    REQUIRE(world.upsert("a", 5, 0, "", "", "").ok());
    REQUIRE(world.upsert("z", 1, 0, "", "", "").ok());
    TargetingSourceContext context{&world, {}, 1, 0};
    auto                   result = plan.execute(context);
    REQUIRE(result.ok());
    REQUIRE(result.value().size() == 2u);
    CHECK_EQ(result.value().ranked()[0].id, std::string("z"));
}

TEST_CASE("sensing.environment.world3DAndGridKeepTheirCoordinateTags") {
    auto d     = definition();
    auto r     = request();
    d.space    = CoordinateSpace::Grid3D;
    r.contexts = {{"self", {TargetLocation{GridPoint::grid3D(0, 0, 0)}}}};
    r.input    = {{0, TargetLocation{GridPoint::grid3D(0, 0, 4)}, std::nullopt}};
    auto grid  = run(d, r);
    REQUIRE(grid.ok());
    CHECK(std::get<GridPoint>(grid.value().ranked.front().item.location).space() == CoordinateSpace::Grid3D);
    d.space      = CoordinateSpace::World3D;
    auto point3D = WorldPoint::world3D(1, 2, 7);
    REQUIRE(point3D.ok());
    r.contexts = {{"self", {TargetLocation{point3D.value()}}}};
    r.input.clear();
    d.options[0].generator.kind    = EnvironmentGeneratorKind::Ring;
    d.options[0].generator.context = "self";
    d.options[0].generator.samples = 4;
    auto ring                      = run(d, r);
    REQUIRE(ring.ok());
    REQUIRE(ring.value().ranked.size() == 4u);
    for (const auto& candidate : ring.value().ranked) CHECK_EQ(std::get<WorldPoint>(candidate.item.location).z(), 7.f);
}

TEST_CASE("sensing.pipeline.implicitDistanceOrderPrecedesTruncation") {
    auto            pipeline = TargetingPipeline::withBuiltins();
    TargetingPreset preset;
    preset.id = "truncate-only";
    TargetingTaskStep select;
    select.taskId = "sensing.select.world";
    TargetingTaskStep truncate;
    truncate.taskId        = "sensing.sort.truncate";
    truncate.truncateCount = 1;
    preset.steps           = {select, truncate};
    SensingWorld world;
    REQUIRE(world.upsert("a", 5, 0, "", "", "").ok());
    REQUIRE(world.upsert("z", 1, 0, "", "", "").ok());
    TargetingSourceContext context{&world, {}, 1, 0};
    auto                   result = pipeline.execute(context, preset);
    REQUIRE(result.ok());
    REQUIRE(result.value().size() == 1u);
    CHECK_EQ(result.value().ranked()[0].id, std::string("z"));
}

TEST_CASE("sensing.environment.providerExceptionsBecomeFailedDiagnostics") {
    auto d = definition();
    d.options[0].tests.push_back(losTest());
    auto plan = EnvironmentQueryPlan::compile(d);
    REQUIRE(plan.ok());
    auto instance = plan.value().instantiate(request());
    REQUIRE(instance.ok());
    Visibility provider;
    provider.throws = true;
    auto result     = instance.value().run({std::cref(provider)});
    REQUIRE(!result.ok());
    CHECK_EQ(instance.value().trace().state, std::string("failed"));
    REQUIRE(instance.value().trace().diagnostics.size() == 1u);
    CHECK(instance.value().trace().diagnostics[0].code() == DiagnosticCode::CallbackFailure);
    CHECK(instance.value().trace().toValue().toJson().ok());
}

TEST_CASE("sensing.environment.constantPopulationUsesIdentityTieBreak") {
    auto d                              = definition();
    d.options[0].tests[0].normalization = EnvironmentNormalization::ActivePopulation;
    auto r                              = request();
    for (auto& item : r.input) item.location = point(5);
    auto result = run(d, r);
    REQUIRE(result.ok());
    REQUIRE(result.value().ranked.size() == 3u);
    for (std::size_t i = 0; i < 3; ++i) {
        CHECK_EQ(result.value().ranked[i].item.id, i + 1);
        CHECK_EQ(result.value().ranked[i].score, 0.0);
    }
}

TEST_CASE("sensing.environment.costSortPreservesScoreAccumulationOrder") {
    auto d              = definition();
    auto custom         = std::make_shared<CustomMetric>();
    custom->reorderable = true;
    EnvironmentTest expensiveScore;
    expensiveScore.id      = "expensiveScore";
    expensiveScore.context = "self";
    expensiveScore.metric  = EnvironmentMetricKind::Custom;
    expensiveScore.custom  = custom;
    expensiveScore.purpose = EnvironmentPurpose::Score;
    expensiveScore.weight  = 1e16;
    auto cheapFilter       = d.options[0].tests[0];
    cheapFilter.id         = "cheapFilter";
    cheapFilter.purpose    = EnvironmentPurpose::Filter;
    d.options[0].tests     = {expensiveScore, d.options[0].tests[0], losTest(), cheapFilter};
    auto plan              = EnvironmentQueryPlan::compile(d);
    REQUIRE(plan.ok());
    auto instance = plan.value().instantiate(request());
    REQUIRE(instance.ok());
    Visibility provider;
    auto       optimized = instance.value().run({std::cref(provider)});
    REQUIRE(optimized.ok());
    const auto& stages = instance.value().trace().stages;
    REQUIRE(stages.size() == 5u);
    CHECK_EQ(stages[1].test, std::string("cheapFilter"));
    CHECK_EQ(stages[2].test, std::string("los"));
    CHECK_EQ(stages[3].test, std::string("expensiveScore"));
    CHECK_EQ(stages[4].test, std::string("distance"));
    d.optimize      = false;
    auto exhaustive = run(d, request(), {std::cref(provider)});
    REQUIRE(exhaustive.ok());
    REQUIRE(exhaustive.value().ranked.size() == optimized.value().ranked.size());
    for (std::size_t i = 0; i < optimized.value().ranked.size(); ++i) {
        CHECK_EQ(optimized.value().ranked[i].item.id, exhaustive.value().ranked[i].item.id);
        CHECK_EQ(optimized.value().ranked[i].score, exhaustive.value().ranked[i].score);
    }
}
