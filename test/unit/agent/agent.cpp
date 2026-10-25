// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "origins": [
    "agent.cpp"
  ]
}
*/
// clang-format on

#include "agent/Agent.h"
#include "agent/Codec.h"
#include "common/Capability.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <limits>
#include <string>
#include <utility>

using namespace eve::agent;

namespace {
Config budget() {
    Config value;
    value.horizon     = 4;
    value.population  = 16;
    value.elites      = 4;
    value.generations = 3;
    return value;
}

class Domain final : public IEnvironment {
public:
    int           resets         = 0;
    int           steps          = 0;
    bool          failReset      = false;
    bool          failStep       = false;
    bool          invalidFeature = false;
    bool          drift          = false;
    std::uint64_t seed           = 0;

    eve::Result<Observation> reset(std::uint64_t nextSeed) override {
        ++resets;
        seed  = nextSeed;
        steps = 0;
        if (failReset)
            return eve::Result<Observation>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Unsupported, "reset unavailable"));
        return eve::Result<Observation>::success(observation(0));
    }

    eve::Result<Observation> step(std::uint32_t action, double) override {
        if (failStep)
            return eve::Result<Observation>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Cancelled, "step unavailable"));
        ++steps;
        auto value = observation(action == 1 ? 1 : 0);
        if (steps == 4 && action == 1) {
            value.outcome = Outcome::Failure;
            value.finding = "injected.invariant";
        }
        return eve::Result<Observation>::success(std::move(value));
    }

private:
    Observation observation(double reward) const {
        Observation value;
        value.features = {float(steps) / 4};
        if (invalidFeature) value.features[0] = std::numeric_limits<float>::quiet_NaN();
        if (drift) value.features[0] += .1f;
        value.legalActions = {0, 1};
        value.reward       = reward;
        value.coverage     = {std::to_string(steps)};
        return value;
    }
};

struct GpuFailure final : IGpuPolicyBackend {
    bool              ready      = false;
    int               trainCalls = 0;
    std::string       name() const override { return "injected-gpu"; }
    eve::Result<void> available() const override {
        if (ready) return eve::Result<void>::success();
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::Unsupported, "no device"));
    }
    eve::Result<std::vector<double>> evaluate(const Policy& policy, const Observation& state) override {
        return infer(policy, state);
    }
    eve::Result<Policy> train(const Policy&, const Observation&, std::uint32_t, double) override {
        ++trainCalls;
        return eve::Result<Policy>::failure(eve::Diagnostic::error(eve::DiagnosticCode::Cancelled, "device lost"));
    }
};
}  // namespace

TEST_CASE("Agent search adapter and budget contract") {
    SUB_CASE("agent contract: invalid search budget rejects before touching the environment") {
        Domain domain;
        auto   config = budget();
        for (double invalid :
             {0., -1., std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
            config.dt = invalid;
            REQUIRE(!run(config, domain).ok());
            REQUIRE_EQ(domain.resets, 0);
        }
        config        = budget();
        config.elites = config.population + 1;
        REQUIRE(!run(config, domain).ok());
        REQUIRE_EQ(domain.resets, 0);
    };

    SUB_CASE("agent contract: environment failures propagate as structured failures") {
        Domain domain;
        domain.failReset  = true;
        auto resetFailure = run(budget(), domain);
        REQUIRE(!resetFailure.ok());
        REQUIRE(resetFailure.status().code() == eve::StatusCode::Unsupported);
        domain.failReset = false;
        domain.failStep  = true;
        auto stepFailure = run(budget(), domain);
        REQUIRE(!stepFailure.ok());
        REQUIRE(stepFailure.status().code() == eve::StatusCode::Cancelled);
        domain.failStep       = false;
        domain.invalidFeature = true;
        REQUIRE(!run(budget(), domain).ok());
    };

    SUB_CASE("agent contract: deterministic search publishes budgets coverage failures and replay evidence") {
        Domain left;
        Domain right;
        auto   first  = run(budget(), left);
        auto   second = run(budget(), right);
        REQUIRE(first.ok());
        REQUIRE(second.ok());
        REQUIRE_EQ(first.value().episodes, 48u);
        REQUIRE_EQ(first.value().steps, 192u);
        REQUIRE(first.value().failures > 0);
        REQUIRE_EQ(first.value().findings.size(), 1u);
        REQUIRE(first.value().trainingSamples > 0);
        REQUIRE_EQ(first.value().bestScore, second.value().bestScore);
        REQUIRE(first.value().policy.weights == second.value().policy.weights);
        REQUIRE(first.value().coverage == second.value().coverage);
        REQUIRE(replay(first.value().findings.front(), right).ok());
        right.drift    = true;
        auto divergent = replay(first.value().findings.front(), right);
        REQUIRE(!divergent.ok());
        REQUIRE(divergent.status().code() == eve::StatusCode::Conflict);
    };

    SUB_CASE("agent contract: random baseline has equal rollout budget without training or retained findings") {
        Domain domain;
        auto   config      = budget();
        config.strategy    = Strategy::Random;
        config.maxFindings = 0;
        auto report        = run(config, domain);
        REQUIRE(report.ok());
        REQUIRE_EQ(report.value().episodes, 48u);
        REQUIRE_EQ(report.value().steps, 192u);
        REQUIRE_EQ(report.value().trainingSamples, 0u);
        REQUIRE(report.value().failures > 0);
        REQUIRE(report.value().findings.empty());
        REQUIRE_EQ(report.value().backend, "uniform-random");
    };
}

TEST_CASE("Agent inference and replay contract") {
    SUB_CASE("agent contract: inference validates policy and legal action mask") {
        Domain domain;
        auto   report = run(budget(), domain);
        REQUIRE(report.ok());
        Observation state;
        state.features     = {0};
        state.legalActions = {1};
        auto probabilities = infer(report.value().policy, state);
        REQUIRE(probabilities.ok());
        REQUIRE_EQ(probabilities.value().size(), 2u);
        REQUIRE_EQ(probabilities.value()[0], 0.);
        REQUIRE_EQ(probabilities.value()[1], 1.);
        state.legalActions.clear();
        REQUIRE(!infer(report.value().policy, state).ok());
        state.legalActions = {1, 1};
        REQUIRE(!infer(report.value().policy, state).ok());
        state.legalActions          = {1};
        auto invalidPolicy          = report.value().policy;
        invalidPolicy.schemaVersion = 2;
        REQUIRE(!validatePolicy(invalidPolicy).ok());
        REQUIRE(!infer(invalidPolicy, state).ok());
        invalidPolicy            = report.value().policy;
        invalidPolicy.weights[0] = std::numeric_limits<double>::infinity();
        REQUIRE(!infer(invalidPolicy, state).ok());
    };

    SUB_CASE("agent contract: replay validates trace before reset and preserves adapter errors") {
        Domain domain;
        auto   report = run(budget(), domain);
        REQUIRE(report.ok());
        auto      trace     = report.value().best;
        const int before    = domain.resets;
        trace.schemaVersion = 2;
        REQUIRE(!replay(trace, domain).ok());
        REQUIRE_EQ(domain.resets, before);
        trace                      = report.value().best;
        trace.steps.front().action = 55;
        REQUIRE(!replay(trace, domain).ok());
        REQUIRE_EQ(domain.resets, before);
        trace = report.value().best;
        REQUIRE(!replay(trace, domain, -1.).ok());
        REQUIRE_EQ(domain.resets, before);
        domain.failReset = true;
        auto failed      = replay(trace, domain);
        REQUIRE(!failed.ok());
        REQUIRE(failed.status().code() == eve::StatusCode::Unsupported);
    };
}

TEST_CASE("Agent codec and provider failure contract") {
    SUB_CASE("agent contract: codecs preserve lossless seed and reject unknown fields") {
        Domain domain;
        auto   config          = budget();
        config.environmentSeed = std::numeric_limits<std::uint64_t>::max();
        auto report            = run(config, domain);
        REQUIRE(report.ok());
        auto policyJson = encodePolicy(report.value().policy).toJson();
        REQUIRE(policyJson.ok());
        auto policyValue = eve::Value::fromJson(policyJson.value());
        REQUIRE(policyValue.ok());
        auto policy = decodePolicy(policyValue.value());
        REQUIRE(policy.ok());
        REQUIRE(policy.value().weights == report.value().policy.weights);
        policyValue.value().getIf<eve::Value::Object>()->emplace("unknown", 1);
        REQUIRE(!decodePolicy(policyValue.value()).ok());

        auto traceJson = encodeTrace(report.value().best).toJson();
        REQUIRE(traceJson.ok());
        auto traceValue = eve::Value::fromJson(traceJson.value());
        REQUIRE(traceValue.ok());
        auto trace = decodeTrace(traceValue.value());
        REQUIRE(trace.ok());
        REQUIRE_EQ(trace.value().environmentSeed, config.environmentSeed);
        REQUIRE(replay(trace.value(), domain).ok());
        REQUIRE(!decodeConfig(eve::Value::object({{"population", 1.5}})).ok());
        REQUIRE(!decodeConfig(eve::Value::object({{"strategy", "typo"}})).ok());
    };

    SUB_CASE("agent contract: GPU provider availability and training failure precede or propagate domain work") {
        Domain domain;
        auto   config      = budget();
        config.backend     = Backend::Gpu;
        config.population  = 1;
        config.elites      = 1;
        config.generations = 1;
        REQUIRE(!run(config, domain).ok());
        REQUIRE_EQ(domain.resets, 0);
        GpuFailure provider;
        eve::cap::provide<IGpuPolicyBackend>(&provider);
        struct Revoke {
            IGpuPolicyBackend* provider;
            ~Revoke() { eve::cap::revoke<IGpuPolicyBackend>(provider); }
        } revoke{&provider};
        REQUIRE(!run(config, domain).ok());
        REQUIRE_EQ(domain.resets, 0);
        provider.ready = true;
        auto failed    = run(config, domain);
        REQUIRE(!failed.ok());
        REQUIRE(failed.status().code() == eve::StatusCode::Cancelled);
        REQUIRE_EQ(provider.trainCalls, 1);
    };
}
