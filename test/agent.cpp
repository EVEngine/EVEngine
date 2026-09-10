#include "agent/Agent.h"
#include "agent/Codec.h"
#include "common/Capability.h"
#include "zeroerr/unittest.h"

#include <cmath>
#include <limits>

using namespace eve::agent;

TEST_CASE("agent.gpuUnavailableAndTrainingFailure") {
    struct DeviceFailure final : IGpuPolicyBackend {
        bool              ready         = false;
        int               trainingCalls = 0;
        std::string       name() const override { return "injected-device"; }
        eve::Result<void> available() const override {
            if (ready) return eve::Result<void>::success();
            return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::Unsupported, "no device"));
        }
        eve::Result<std::vector<double>> evaluate(const Policy& p, const Observation& o) override {
            return infer(p, o);
        }
        eve::Result<Policy> train(const Policy&, const Observation&, std::uint32_t, double) override {
            ++trainingCalls;
            return eve::Result<Policy>::failure(eve::Diagnostic::error(eve::DiagnosticCode::Cancelled, "device lost"));
        }
    } provider;
    struct Environment final : IEnvironment {
        int                      resets = 0;
        eve::Result<Observation> reset(std::uint64_t) override {
            ++resets;
            Observation o;
            o.features     = {0};
            o.legalActions = {0, 1};
            return eve::Result<Observation>::success(std::move(o));
        }
        eve::Result<Observation> step(std::uint32_t, double) override {
            Observation o;
            o.features = {1};
            o.outcome  = Outcome::Success;
            return eve::Result<Observation>::success(std::move(o));
        }
    } environment;
    Config config;
    config.backend     = Backend::Gpu;
    config.population  = 1;
    config.elites      = 1;
    config.generations = 1;
    REQUIRE(!run(config, environment).ok());
    REQUIRE_EQ(environment.resets, 0);
    struct Registration {
        IGpuPolicyBackend* provider;
        ~Registration() { eve::cap::revoke<IGpuPolicyBackend>(provider); }
    } registration{&provider};
    eve::cap::provide<IGpuPolicyBackend>(&provider);
    REQUIRE(!run(config, environment).ok());
    REQUIRE_EQ(environment.resets, 0);
    provider.ready = true;
    auto result    = run(config, environment);
    REQUIRE(!result.ok());
    REQUIRE(result.status().code() == eve::StatusCode::Cancelled);
    REQUIRE_EQ(provider.trainingCalls, 1);
}

namespace {
class Domain final : public IEnvironment {
public:
    int                      resets       = 0;
    int                      tick         = 0;
    bool                     failReset    = false;
    bool                     failStep     = false;
    bool                     invalidState = false;
    bool                     drift        = false;
    eve::Result<Observation> reset(std::uint64_t) override {
        ++resets;
        tick = 0;
        if (failReset)
            return eve::Result<Observation>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Unsupported, "reset unavailable"));
        return eve::Result<Observation>::success(state(0));
    }
    eve::Result<Observation> step(std::uint32_t action, double) override {
        if (failStep)
            return eve::Result<Observation>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Cancelled, "cancelled"));
        ++tick;
        auto o = state(action == 1 ? 1 : 0);
        if (tick == 4 && action == 1) {
            o.outcome = Outcome::Failure;
            o.finding = "injected.invariant";
        }
        return eve::Result<Observation>::success(std::move(o));
    }

private:
    Observation state(double reward) const {
        Observation o;
        o.features = {float(tick) / 4};
        if (invalidState) o.features[0] = std::numeric_limits<float>::quiet_NaN();
        if (drift) o.features[0] += 0.1f;
        o.legalActions = {0, 1};
        o.reward       = reward;
        o.coverage     = {std::to_string(tick)};
        return o;
    }
};

Config smallConfig() {
    Config c;
    c.horizon     = 4;
    c.population  = 16;
    c.elites      = 4;
    c.generations = 3;
    return c;
}
}  // namespace

TEST_CASE("agent.findingsReplayAndDeterminism") {
    Domain a, b;
    auto   first  = run(smallConfig(), a);
    auto   second = run(smallConfig(), b);
    REQUIRE(first.ok());
    REQUIRE(second.ok());
    REQUIRE(first.value().failures > 0);
    REQUIRE_EQ(first.value().findings.size(), 1u);
    REQUIRE_EQ(first.value().episodes, 48u);
    REQUIRE_EQ(first.value().bestScore, second.value().bestScore);
    REQUIRE(first.value().policy.weights == second.value().policy.weights);
    REQUIRE(replay(first.value().findings.front(), b).ok());
    b.drift        = true;
    auto divergent = replay(first.value().findings.front(), b);
    REQUIRE(!divergent.ok());
    REQUIRE(divergent.status().code() == eve::StatusCode::Conflict);
}

TEST_CASE("agent.invalidBeforeMutationAndErrorPropagation") {
    Domain environment;
    auto   c = smallConfig();
    c.dt     = std::numeric_limits<double>::quiet_NaN();
    REQUIRE(!run(c, environment).ok());
    REQUIRE_EQ(environment.resets, 0);
    c                     = smallConfig();
    environment.failReset = true;
    auto missing          = run(c, environment);
    REQUIRE(!missing.ok());
    REQUIRE(missing.status().code() == eve::StatusCode::Unsupported);
    environment.failReset = false;
    environment.failStep  = true;
    auto cancelled        = run(c, environment);
    REQUIRE(!cancelled.ok());
    REQUIRE(cancelled.status().code() == eve::StatusCode::Cancelled);
    environment.failStep     = false;
    environment.invalidState = true;
    REQUIRE(!run(c, environment).ok());
}

TEST_CASE("agent.policyTrainingMaskAndImport") {
    Domain environment;
    auto   c      = smallConfig();
    auto   result = run(c, environment);
    REQUIRE(result.ok());
    REQUIRE(result.value().trainingSamples > 0);
    Observation state;
    state.features     = {0};
    state.legalActions = {1};
    auto masked        = infer(result.value().policy, state);
    REQUIRE(masked.ok());
    REQUIRE_EQ(masked.value()[0], 0.0);
    REQUIRE_EQ(masked.value()[1], 1.0);
    auto policy          = result.value().policy;
    policy.schemaVersion = 2;
    REQUIRE(!infer(policy, state).ok());
    policy            = result.value().policy;
    policy.weights[0] = std::numeric_limits<double>::infinity();
    REQUIRE(!infer(policy, state).ok());
    auto badTrace          = result.value().best;
    badTrace.schemaVersion = 2;
    const auto resets      = environment.resets;
    REQUIRE(!replay(badTrace, environment).ok());
    REQUIRE_EQ(environment.resets, resets);
    badTrace                 = result.value().best;
    badTrace.steps[0].action = 55;
    REQUIRE(!replay(badTrace, environment).ok());
    REQUIRE_EQ(environment.resets, resets);
}

TEST_CASE("agent.randomBaselineAndBoundedFindings") {
    Domain environment;
    auto   c      = smallConfig();
    c.strategy    = Strategy::Random;
    c.maxFindings = 0;
    auto result   = run(c, environment);
    REQUIRE(result.ok());
    REQUIRE_EQ(result.value().trainingSamples, 0u);
    REQUIRE(result.value().failures > 0);
    REQUIRE(result.value().findings.empty());
    REQUIRE_EQ(result.value().episodes, 48u);
    REQUIRE_EQ(result.value().steps, 192u);
}

TEST_CASE("agent.learningImprovesRewardedAction") {
    Domain environment;
    auto   c         = smallConfig();
    c.generations    = 8;
    c.trainingEpochs = 12;
    c.failureWeight  = 0;
    c.coverageWeight = 0;
    c.strategy       = Strategy::Random;
    auto baseline    = run(c, environment);
    REQUIRE(baseline.ok());
    c.strategy   = Strategy::EvolutionLearning;
    auto trained = run(c, environment);
    REQUIRE(trained.ok());
    auto observation = environment.reset(c.environmentSeed);
    REQUIRE(observation.ok());
    auto before = infer(baseline.value().policy, observation.value());
    auto after  = infer(trained.value().policy, observation.value());
    REQUIRE(before.ok());
    REQUIRE(after.ok());
    REQUIRE(after.value()[1] > before.value()[1] + 0.2);
    REQUIRE(after.value()[1] > 0.8);
}

TEST_CASE("agent.codecRoundTripAndUnknownFields") {
    Domain environment;
    auto   c          = smallConfig();
    c.environmentSeed = std::numeric_limits<std::uint64_t>::max();
    auto result       = run(c, environment);
    REQUIRE(result.ok());
    auto json = encodePolicy(result.value().policy).toJson();
    REQUIRE(json.ok());
    auto value = eve::Value::fromJson(json.value());
    REQUIRE(value.ok());
    auto policy = decodePolicy(value.value());
    REQUIRE(policy.ok());
    REQUIRE(policy.value().weights == result.value().policy.weights);
    auto traceJson = encodeTrace(result.value().best).toJson();
    REQUIRE(traceJson.ok());
    auto traceValue = eve::Value::fromJson(traceJson.value());
    REQUIRE(traceValue.ok());
    auto trace = decodeTrace(traceValue.value());
    REQUIRE(trace.ok());
    REQUIRE_EQ(trace.value().environmentSeed, c.environmentSeed);
    REQUIRE(replay(trace.value(), environment).ok());
    value.value().getIf<eve::Value::Object>()->emplace("unknown", 1);
    REQUIRE(!decodePolicy(value.value()).ok());
    REQUIRE(!decodeConfig(eve::Value::object({{"population", 1.5}})).ok());
    REQUIRE(!decodeConfig(eve::Value::object({{"strategy", "typo"}})).ok());
}
