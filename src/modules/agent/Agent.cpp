#include "agent/Agent.h"
#include "agent/Learning.h"
#include "common/Capability.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

namespace eve::agent {
namespace {

template <class T>
Result<T> invalid(std::string message) {
    return Result<T>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::move(message), {}, {}, "agent"));
}

bool validObservation(const Observation& o, std::size_t features, std::size_t actions) {
    if (o.features.size() != features || o.legalActions.size() > actions || !std::isfinite(o.reward) ||
        std::abs(o.reward) > 1e9 || o.coverage.size() > 64 || o.finding.size() > 4096)
        return false;
    if (o.outcome != Outcome::Running && o.outcome != Outcome::Success && o.outcome != Outcome::Failure) return false;
    if (o.outcome == Outcome::Running && o.legalActions.empty()) return false;
    if (o.outcome == Outcome::Failure && o.finding.empty()) return false;
    std::set<std::uint32_t> seen;
    for (auto a : o.legalActions)
        if (a >= actions || !seen.insert(a).second) return false;
    for (float f : o.features)
        if (!std::isfinite(f) || std::abs(f) > 1e6f) return false;
    for (const auto& key : o.coverage)
        if (key.empty() || key.size() > 256) return false;
    return true;
}

bool validConfig(const Config& c) {
    return c.featureCount > 0 && c.featureCount <= 1024 && c.actionCount > 0 && c.actionCount <= 1024 &&
           c.hiddenWidth > 0 && c.hiddenWidth <= 64 && c.population > 0 && c.population <= 256 && c.generations > 0 &&
           c.generations <= 1024 && c.horizon > 0 && c.horizon <= 1024 &&
           std::uint64_t(c.population) * c.generations * c.horizon <= 1000000 &&
           std::uint64_t(c.population) * c.horizon * c.featureCount <= 1048576 &&
           (std::uint64_t(c.population) + c.maxFindings + 1) * c.horizon <= 16384 &&
           std::uint64_t(c.generations) * c.trainingEpochs * c.elites * c.horizon <= 1000000 && c.elites > 0 &&
           c.elites <= c.population && c.trainingEpochs <= 64 && c.maxFindings <= 256 && std::isfinite(c.dt) &&
           c.dt > 0 && c.dt <= 60 && std::isfinite(c.mutationProbability) && c.mutationProbability >= 0 &&
           c.mutationProbability <= 1 && std::isfinite(c.randomProbability) && c.randomProbability >= 0 &&
           c.randomProbability <= 1 && std::isfinite(c.coverageWeight) && c.coverageWeight >= 0 &&
           c.coverageWeight <= 1e6 && std::isfinite(c.failureWeight) && c.failureWeight >= 0 &&
           c.failureWeight <= 1e6 && std::isfinite(c.learningRate) && c.learningRate > 0 && c.learningRate <= 1 &&
           (c.strategy == Strategy::Random || c.strategy == Strategy::EvolutionLearning) &&
           (c.backend == Backend::Cpu || c.backend == Backend::Tensor || c.backend == Backend::Gpu);
}

struct Candidate {
    Trace  trace;
    double score = 0;
};

Result<std::uint32_t> sample(const Policy& policy, const Observation& o, detail::Random& random, bool uniform,
                             Backend backend) {
    if (uniform) return Result<std::uint32_t>::success(o.legalActions[random.index(o.legalActions.size())]);
    auto evaluated = infer(policy, o, backend);
    if (!evaluated) return Result<std::uint32_t>::failure(evaluated.status());
    const auto& probabilities = evaluated.value();
    double      u             = random.unit();
    for (auto a : o.legalActions) {
        u -= probabilities[a];
        if (u <= 0) return Result<std::uint32_t>::success(a);
    }
    return Result<std::uint32_t>::success(o.legalActions.back());
}

bool matches(const Observation& a, const Observation& b, double tolerance) {
    if (a.features.size() != b.features.size() || a.legalActions != b.legalActions || a.coverage != b.coverage ||
        a.outcome != b.outcome || a.finding != b.finding || !std::isfinite(b.reward) ||
        std::abs(a.reward - b.reward) > tolerance)
        return false;
    for (std::size_t i = 0; i < a.features.size(); ++i)
        if (!std::isfinite(b.features[i]) || std::abs(double(a.features[i]) - b.features[i]) > tolerance) return false;
    return true;
}

Result<void> divergence() {
    return Result<void>::failure(
        Diagnostic::error(DiagnosticCode::Conflict, "Replay observation diverged", {}, {}, "agent"));
}

}  // namespace

Result<void> validatePolicy(const Policy& policy) {
    if (policy.schemaId != "evengine.agent.policy" || policy.schemaVersion != 1 || policy.featureCount == 0 ||
        policy.featureCount > 1024 || policy.actionCount == 0 || policy.actionCount > 1024 || policy.hiddenWidth == 0 ||
        policy.hiddenWidth > 64 ||
        policy.weights.size() != detail::weightCount(policy.featureCount, policy.hiddenWidth, policy.actionCount))
        return invalid<void>("Unsupported policy schema, version or dimensions");
    for (double w : policy.weights)
        if (!std::isfinite(w) || std::abs(w) > 1e6) return invalid<void>("Invalid policy weight");
    return Result<void>::success();
}

Result<std::string> backendName(Backend backend) {
    if (backend == Backend::Cpu) return Result<std::string>::success("cpu-mlp");
    if (backend == Backend::Gpu) {
        if (auto* provider = cap::query<IGpuPolicyBackend>()) {
            auto ready = provider->available();
            if (!ready) return Result<std::string>::failure(ready.status());
            return Result<std::string>::success(provider->name());
        }
        return Result<std::string>::failure(Diagnostic::error(
            DiagnosticCode::Unsupported, "GPU requires an active AgentTensor module", {}, {}, "agent"));
    }
    if (backend != Backend::Tensor) return invalid<std::string>("Unknown backend");
    if (auto* provider = cap::query<IPolicyBackend>()) return Result<std::string>::success(provider->name());
    return Result<std::string>::failure(Diagnostic::error(
        DiagnosticCode::Unsupported, "Tensor backend requires an active AgentTensor module", {}, {}, "agent"));
}

Result<std::vector<double>> infer(const Policy& policy, const Observation& observation, Backend backend) {
    auto validated = validatePolicy(policy);
    if (!validated) return Result<std::vector<double>>::failure(validated.status());
    if (!validObservation(observation, policy.featureCount, policy.actionCount) || observation.legalActions.empty())
        return invalid<std::vector<double>>("Invalid observation or empty legal action mask");
    if (backend == Backend::Cpu) return Result<std::vector<double>>::success(detail::forward(policy, observation));
    auto available = backendName(backend);
    if (!available) return Result<std::vector<double>>::failure(available.status());
    IPolicyBackend* provider = backend == Backend::Gpu ? cap::query<IGpuPolicyBackend>() : cap::query<IPolicyBackend>();
    EV_ASSERT(provider, "backend registration must remain stable during inference");
    auto result = provider->evaluate(policy, observation);
    if (!result) return result;
    if (result.value().size() != policy.actionCount) return invalid<std::vector<double>>("Invalid backend shape");
    double sum = 0;
    for (std::size_t i = 0; i < result.value().size(); ++i) {
        const auto value = result.value()[i];
        if (!std::isfinite(value) || value < 0 ||
            (std::find(observation.legalActions.begin(), observation.legalActions.end(), i) ==
                 observation.legalActions.end() &&
             value != 0))
            return invalid<std::vector<double>>("Invalid backend probability");
        sum += value;
    }
    if (std::abs(sum - 1) > 1e-5) return invalid<std::vector<double>>("Backend probabilities must sum to one");
    return result;
}

Result<Report> run(const Config& c, IEnvironment& environment) {
    if (!validConfig(c)) return invalid<Report>("Invalid dimensions, budgets, strategy, probabilities or time");
    auto selectedBackend = backendName(c.backend);
    if (!selectedBackend) return Result<Report>::failure(selectedBackend.status());
    Report report;
    report.policy          = detail::makePolicy(c);
    report.backend         = c.strategy == Strategy::Random ? "uniform-random" : selectedBackend.value();
    report.trainingBackend = c.strategy == Strategy::Random ? "none"
                             : c.backend == Backend::Gpu    ? "tensor-gpu-sgd"
                                                            : "cpu-sgd";
    report.bestScore       = -std::numeric_limits<double>::infinity();
    detail::Random         random(c.searchSeed);
    std::vector<Candidate> parents;
    std::set<std::string>  coverage, findingKeys;
    for (std::uint32_t generation = 0; generation < c.generations; ++generation) {
        std::vector<Candidate> candidates;
        for (std::uint32_t member = 0; member < c.population; ++member) {
            Candidate candidate;
            candidate.trace.environmentSeed = c.environmentSeed;
            candidate.trace.dt              = c.dt;
            auto reset                      = environment.reset(c.environmentSeed);
            if (!reset) return Result<Report>::failure(reset.status());
            if (!validObservation(reset.value(), c.featureCount, c.actionCount) || reset.value().reward != 0)
                return invalid<Report>("Environment returned invalid initial observation");
            candidate.trace.initial           = std::move(reset).takeValue();
            Observation           observation = candidate.trace.initial;
            std::set<std::string> episodeCoverage(observation.coverage.begin(), observation.coverage.end());
            const auto            parent = parents.empty() ? 0 : random.index(parents.size());
            for (std::uint32_t tick = 0; tick < c.horizon && observation.outcome == Outcome::Running; ++tick) {
                const bool uniform = c.strategy == Strategy::Random || random.unit() < c.randomProbability;
                auto       sampled = sample(report.policy, observation, random, uniform, c.backend);
                if (!sampled) return Result<Report>::failure(sampled.status());
                auto action = sampled.value();
                if (!uniform && !parents.empty() && tick < parents[parent].trace.steps.size() &&
                    random.unit() >= c.mutationProbability) {
                    const auto inherited = parents[parent].trace.steps[tick].action;
                    if (std::find(observation.legalActions.begin(), observation.legalActions.end(), inherited) !=
                        observation.legalActions.end())
                        action = inherited;
                }
                auto stepped = environment.step(action, c.dt);
                if (!stepped) return Result<Report>::failure(stepped.status());
                if (!validObservation(stepped.value(), c.featureCount, c.actionCount))
                    return invalid<Report>("Environment returned invalid step observation");
                observation = std::move(stepped).takeValue();
                candidate.score += observation.reward;
                episodeCoverage.insert(observation.coverage.begin(), observation.coverage.end());
                candidate.trace.steps.push_back({action, observation});
                ++report.steps;
            }
            ++report.episodes;
            coverage.insert(episodeCoverage.begin(), episodeCoverage.end());
            if (coverage.size() > 65536)
                return Result<Report>::failure(Diagnostic::error(
                    DiagnosticCode::Cancelled, "Coverage storage budget exceeded (65536 points)", {}, {}, "agent"));
            candidate.score += c.coverageWeight * double(episodeCoverage.size());
            if (observation.outcome == Outcome::Failure) {
                ++report.failures;
                candidate.score += c.failureWeight;
                if (report.findings.size() < c.maxFindings && findingKeys.insert(observation.finding).second)
                    report.findings.push_back(candidate.trace);
            }
            if (candidate.score > report.bestScore) {
                report.bestScore = candidate.score;
                report.best      = candidate.trace;
            }
            candidates.push_back(std::move(candidate));
        }
        std::stable_sort(candidates.begin(), candidates.end(),
                         [](const auto& a, const auto& b) { return a.score > b.score; });
        candidates.resize(c.elites);
        if (c.strategy == Strategy::EvolutionLearning) {
            for (std::uint32_t epoch = 0; epoch < c.trainingEpochs; ++epoch)
                for (const auto& elite : candidates) {
                    const Observation* previous = &elite.trace.initial;
                    for (const auto& step : elite.trace.steps) {
                        if (c.backend == Backend::Gpu) {
                            auto* provider = cap::query<IGpuPolicyBackend>();
                            if (!provider)
                                return Result<Report>::failure(Diagnostic::error(
                                    DiagnosticCode::Unsupported,
                                    "GPU provider was removed during an environment callback", {}, {}, "agent"));
                            auto trained = provider->train(report.policy, *previous, step.action, c.learningRate);
                            if (!trained) return Result<Report>::failure(trained.status());
                            auto valid = validatePolicy(trained.value());
                            if (!valid) return Result<Report>::failure(valid.status());
                            if (trained.value().featureCount != c.featureCount ||
                                trained.value().hiddenWidth != c.hiddenWidth ||
                                trained.value().actionCount != c.actionCount)
                                return invalid<Report>("GPU training changed policy shape");
                            report.policy = std::move(trained).takeValue();
                        } else
                            detail::train(report.policy, *previous, step.action, c.learningRate);
                        previous = &step.observation;
                        ++report.trainingSamples;
                    }
                }
            parents = std::move(candidates);
        }
    }
    report.coverage.assign(coverage.begin(), coverage.end());
    return Result<Report>::success(std::move(report));
}

Result<void> replay(const Trace& trace, IEnvironment& environment, double tolerance) {
    if (trace.schemaId != "evengine.agent.trace" || trace.schemaVersion != 1 || !std::isfinite(trace.dt) ||
        trace.dt <= 0 || trace.dt > 60 || !std::isfinite(tolerance) || tolerance < 0 || trace.steps.size() > 1024 ||
        trace.initial.features.empty() || trace.initial.features.size() > 1024 || trace.initial.reward != 0)
        return invalid<void>("Unsupported trace schema/version, time, length or tolerance");
    const Observation* previous = &trace.initial;
    if (!validObservation(*previous, trace.initial.features.size(), 1024))
        return invalid<void>("Invalid initial trace observation");
    for (const auto& step : trace.steps) {
        if (previous->outcome != Outcome::Running ||
            std::find(previous->legalActions.begin(), previous->legalActions.end(), step.action) ==
                previous->legalActions.end() ||
            !validObservation(step.observation, trace.initial.features.size(), 1024))
            return invalid<void>("Invalid trace action or observation");
        previous = &step.observation;
    }
    auto reset = environment.reset(trace.environmentSeed);
    if (!reset) return Result<void>::failure(reset.status());
    if (!matches(trace.initial, reset.value(), tolerance)) return divergence();
    for (const auto& step : trace.steps) {
        auto result = environment.step(step.action, trace.dt);
        if (!result) return Result<void>::failure(result.status());
        if (!matches(step.observation, result.value(), tolerance)) return divergence();
    }
    return Result<void>::success();
}

}  // namespace eve::agent
