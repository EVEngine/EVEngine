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
#include "agent/Balance.h"
#include "agent/BalanceCodec.h"
#include "agent/Codec.h"
#include "common/Capability.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <cmath>
#include <limits>
#include <stdexcept>
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

using namespace eve::agent::balance;

namespace {
Problem problem() {
    Problem p;
    p.parameters   = {{"skill", 1, 0, 10}, {"equipment", 2, 2, 2}};
    p.targets      = {{"damage", 6.9, 7.1, 1, 1}};
    p.candidates   = 160;
    p.changeWeight = 0;
    return p;
}
class Damage final : public IEvaluator {
public:
    std::vector<std::uint64_t>       seen;
    BalanceSearch*                   search         = nullptr;
    bool                             reenter        = false;
    bool                             fail           = false;
    bool                             nonfinite      = false;
    bool                             throwException = false;
    bool                             overfit        = false;
    eve::Result<std::vector<double>> evaluate(std::span<const double> x, std::uint64_t seed, double dt,
                                              std::uint32_t steps) override {
        seen.push_back(seed);
        REQUIRE_EQ(dt, 0.1);
        REQUIRE_EQ(steps, 120u);
        REQUIRE_EQ(x[1], 2.0);
        if (reenter) {
            REQUIRE(!search->configure(problem()).ok());
            REQUIRE(!search->run(*this).ok());
        }
        if (throwException) throw std::runtime_error("injected native exception");
        if (fail)
            return eve::Result<std::vector<double>>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Cancelled, "injected evaluation failure"));
        const double value =
            nonfinite ? std::numeric_limits<double>::quiet_NaN() : x[0] + x[1] + (overfit && seed >= 100 ? 20 : 0);
        return eve::Result<std::vector<double>>::success({value});
    }
};
}  // namespace

TEST_CASE("agent.balance.improvesBoundsDeterminismAndHoldout") {
    BalanceSearch search;
    auto          p = problem();
    REQUIRE(search.configure(p).ok());
    Damage domain;
    auto   r = search.run(domain);
    REQUIRE(r.ok());
    REQUIRE(r.value().tuned.loss < r.value().baseline.loss);
    REQUIRE(r.value().verdict == Verdict::WithinTargets);
    REQUIRE((r.value().parameters[0] >= 0 && r.value().parameters[0] <= 10));
    REQUIRE_EQ(r.value().parameters[1], 2.0);
    REQUIRE_EQ(r.value().evaluations, std::uint64_t(160 * 3 + 6));
    REQUIRE_EQ(domain.seen.size(), r.value().evaluations);
    for (std::size_t i = 0; i < 160 * 3; ++i) REQUIRE(domain.seen[i] < 100);
    for (std::size_t i = 160 * 3; i < domain.seen.size(); ++i) REQUIRE(domain.seen[i] >= 100);
    Damage repeat;
    auto   again = search.run(repeat);
    REQUIRE(again.ok());
    REQUIRE_EQ(again.value().parameters[0], r.value().parameters[0]);
    auto encoded = encodeBalanceReport(r.value());
    auto parsed  = decodeProblem(encoded.getIf<eve::Value::Object>()->at("problem"));
    REQUIRE(parsed.ok());
    REQUIRE_EQ(parsed.value().trainingSeeds, p.trainingSeeds);
}

TEST_CASE("agent.balance.validationFailureAtomicityAndRecovery") {
    BalanceSearch search;
    Damage        domain;
    REQUIRE(!search.run(domain).ok());
    auto p = problem();
    REQUIRE(search.configure(p).ok());
    p.parameters[0].maximum = -1;
    REQUIRE(!search.configure(p).ok());
    p                 = problem();
    p.validationSeeds = {11};
    REQUIRE(!search.configure(p).ok());
    p                  = problem();
    p.targets[0].scale = 0;
    REQUIRE(!search.configure(p).ok());
    p = problem();
    p.parameters.push_back(p.parameters[0]);
    REQUIRE(!search.configure(p).ok());
    domain.fail = true;
    REQUIRE(!search.run(domain).ok());
    domain.fail      = false;
    domain.nonfinite = true;
    REQUIRE(!search.run(domain).ok());
    domain.nonfinite      = false;
    domain.throwException = true;
    REQUIRE(!search.run(domain).ok());
    domain.throwException = false;
    domain.reenter        = true;
    domain.search         = &search;
    auto valid            = search.run(domain);
    REQUIRE(valid.ok());
    REQUIRE(valid.value().tuned.loss < valid.value().baseline.loss);
}

TEST_CASE("agent.balance.holdoutDoesNotSelectAndUnknownFieldsReject") {
    BalanceSearch search;
    REQUIRE(search.configure(problem()).ok());
    Damage domain;
    domain.overfit = true;
    auto r         = search.run(domain);
    REQUIRE(r.ok());
    REQUIRE_EQ(r.value().tuned.loss, 0.0);
    REQUIRE(r.value().validation.loss > 0);
    REQUIRE(r.value().verdict == Verdict::OutsideTargets);
    auto encoded = encodeBalanceReport(r.value());
    auto input   = encoded.getIf<eve::Value::Object>()->at("problem");
    input.getIf<eve::Value::Object>()->emplace("typo", eve::Value(1));
    REQUIRE(!decodeProblem(input).ok());
    input.getIf<eve::Value::Object>()->erase("typo");
    input.getIf<eve::Value::Object>()->at("version") = eve::Value(99);
    REQUIRE(!decodeProblem(input).ok());
}

TEST_CASE("agent.balance.metricUnitsVarianceAndCandidateZero") {
    class Samples final : public IEvaluator {
    public:
        double                           multiplier = 1;
        eve::Result<std::vector<double>> evaluate(std::span<const double> values, std::uint64_t seed, double,
                                                  std::uint32_t) override {
            if (values[0] != 4.0)
                return eve::Result<std::vector<double>>::failure(
                    eve::Diagnostic::error(eve::DiagnosticCode::Conflict, "locked parameter changed"));
            return eve::Result<std::vector<double>>::success({double(seed) * multiplier});
        }
    } samples;
    Problem p;
    p.parameters      = {{"fixed", 4, 4, 4}};
    p.targets         = {{"resource", 1.5, 2.5, 2, 3}};
    p.trainingSeeds   = {1, 2, 3};
    p.validationSeeds = {4, 5};
    p.candidates      = 1;
    BalanceSearch search;
    REQUIRE(search.configure(p).ok());
    auto report = search.run(samples);
    REQUIRE(report.ok());
    REQUIRE_EQ(report.value().baseline.metrics[0].mean, 2.0);
    REQUIRE_EQ(report.value().baseline.metrics[0].deviation, 1.0);
    REQUIRE_EQ(report.value().validation.metrics[0].mean, 4.5);
    REQUIRE(std::abs(report.value().validation.metrics[0].deviation - std::sqrt(0.5)) < 1e-12);
    REQUIRE_EQ(report.value().validation.loss, 3.0);
    REQUIRE_EQ(report.value().evaluations, 7u);
    // A squared tiny violation can underflow; status must compare the actual means.
    samples.multiplier = 1e-300;
    p.targets          = {{"resource", 0, 0, 1, 1}};
    REQUIRE(search.configure(p).ok());
    auto tiny = search.run(samples);
    REQUIRE(tiny.ok());
    REQUIRE(tiny.value().verdict == Verdict::OutsideTargets);
}

TEST_CASE("agent.balance.boundaryAnchorsReachCoupledTargetsWithinBudget") {
    class Coupled final : public IEvaluator {
    public:
        eve::Result<std::vector<double>> evaluate(std::span<const double> values, std::uint64_t, double,
                                                  std::uint32_t) override {
            return eve::Result<std::vector<double>>::success({values[0] + values[1]});
        }
    } domain;
    Problem p;
    p.parameters   = {{"skill", 5, 0, 10}, {"equipment", 5, 0, 10}};
    p.targets      = {{"combined", 0, 0, 1, 1}};
    p.candidates   = 2;
    p.changeWeight = 0;
    BalanceSearch search;
    REQUIRE(search.configure(p).ok());
    auto lower = search.run(domain);
    REQUIRE(lower.ok());
    REQUIRE_EQ(lower.value().tuned.loss, 0.0);
    REQUIRE_EQ(lower.value().parameters[0], 0.0);
    REQUIRE_EQ(lower.value().parameters[1], 0.0);
    REQUIRE_EQ(lower.value().evaluations, 12u);

    p.targets    = {{"combined", 20, 20, 1, 1}};
    p.candidates = 3;
    REQUIRE(search.configure(p).ok());
    auto upper = search.run(domain);
    REQUIRE(upper.ok());
    REQUIRE_EQ(upper.value().tuned.loss, 0.0);
    REQUIRE_EQ(upper.value().parameters[0], 10.0);
    REQUIRE_EQ(upper.value().parameters[1], 10.0);
    REQUIRE_EQ(upper.value().evaluations, 15u);
}

TEST_CASE("agent.balance.feasibleCornerRestoresUnneededParameter") {
    class SkillOnly final : public IEvaluator {
    public:
        eve::Result<std::vector<double>> evaluate(std::span<const double> values, std::uint64_t, double,
                                                  std::uint32_t) override {
            return eve::Result<std::vector<double>>::success({values[0]});
        }
    } domain;
    Problem p;
    p.parameters   = {{"skill", 5, 0, 10}, {"equipment", 5, 0, 10}};
    p.targets      = {{"skillOutput", 0, 0, 1, 1}};
    p.candidates   = 5;
    p.changeWeight = 1;
    BalanceSearch search;
    REQUIRE(search.configure(p).ok());
    auto report = search.run(domain);
    REQUIRE(report.ok());
    REQUIRE_EQ(report.value().tuned.loss, 0.0);
    REQUIRE_EQ(report.value().parameters[0], 0.0);
    REQUIRE_EQ(report.value().parameters[1], 5.0);
    REQUIRE(report.value().tuned.score < 0.13);
}

TEST_CASE("agent.balance.sparseCornerCrossesCoupledPlateau") {
    class Coupled final : public IEvaluator {
    public:
        eve::Result<std::vector<double>> evaluate(std::span<const double> values, std::uint64_t, double,
                                                  std::uint32_t) override {
            return eve::Result<std::vector<double>>::success({values[0] + values[3] - values[1] - values[2]});
        }
    } domain;
    Problem p;
    p.parameters   = {{"a", 5, 0, 10}, {"b", 5, 0, 10}, {"c", 5, 0, 10}, {"d", 5, 0, 10}};
    p.targets      = {{"coupled", 10, 10, 1, 1}};
    p.candidates   = 4;
    p.changeWeight = 0;
    BalanceSearch search;
    REQUIRE(search.configure(p).ok());
    auto report = search.run(domain);
    REQUIRE(report.ok());
    REQUIRE_EQ(report.value().tuned.loss, 0.0);
    REQUIRE_EQ(report.value().parameters, (std::vector<double>{5, 0, 0, 5}));
}
