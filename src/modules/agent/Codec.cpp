#include "agent/Codec.h"

#include <charconv>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace eve::agent {
namespace {
struct ParseFailure : std::runtime_error {
    using std::runtime_error::runtime_error;
};

const Value::Object& object(const Value& v, std::initializer_list<std::string_view> fields) {
    const auto* result = v.getIf<Value::Object>();
    if (!result) throw ParseFailure("Expected object");
    for (const auto& [key, value] : *result) {
        bool known = false;
        for (auto field : fields)
            if (key == field) known = true;
        if (!known) throw ParseFailure("Unknown field: " + key);
    }
    return *result;
}
const Value& required(const Value::Object& o, const std::string& key) {
    const auto it = o.find(key);
    if (it == o.end()) throw ParseFailure("Missing field: " + key);
    return it->second;
}
std::string string(const Value& v) {
    const auto* value = v.getIf<std::string>();
    if (!value) throw ParseFailure("Expected string");
    return *value;
}
double number(const Value& v) {
    double result;
    if (const auto* n = v.getIf<double>())
        result = *n;
    else if (const auto* n = v.getIf<std::int64_t>())
        result = double(*n);
    else
        throw ParseFailure("Expected number");
    if (!std::isfinite(result)) throw ParseFailure("Nonfinite number");
    return result;
}
std::uint64_t unsignedInteger(const Value& v) {
    if (const auto* n = v.getIf<std::int64_t>()) {
        if (*n < 0) throw ParseFailure("Expected unsigned integer");
        return std::uint64_t(*n);
    }
    const auto    text   = string(v);
    std::uint64_t result = 0;
    const auto    parsed = std::from_chars(text.data(), text.data() + text.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) throw ParseFailure("Invalid integer");
    return result;
}
std::uint32_t u32(const Value& v) {
    const auto n = unsignedInteger(v);
    if (n > std::numeric_limits<std::uint32_t>::max()) throw ParseFailure("Integer overflow");
    return std::uint32_t(n);
}
const Value::Array& array(const Value& v, std::size_t limit) {
    const auto* a = v.getIf<Value::Array>();
    if (!a || a->size() > limit) throw ParseFailure("Invalid or oversized array");
    return *a;
}
Observation observation(const Value& v) {
    const auto& o = object(v, {"features", "legalActions", "coverage", "reward", "outcome", "finding"});
    Observation result;
    for (const auto& n : array(required(o, "features"), 1024)) {
        const auto value = number(n);
        if (std::abs(value) > 1e6) throw ParseFailure("Feature outside supported range");
        result.features.push_back(float(value));
    }
    for (const auto& n : array(required(o, "legalActions"), 1024)) result.legalActions.push_back(u32(n));
    if (auto it = o.find("coverage"); it != o.end())
        for (const auto& key : array(it->second, 64)) result.coverage.push_back(string(key));
    if (auto it = o.find("reward"); it != o.end()) result.reward = number(it->second);
    if (auto it = o.find("finding"); it != o.end()) result.finding = string(it->second);
    if (auto it = o.find("outcome"); it != o.end()) {
        const auto name = string(it->second);
        if (name == "running")
            result.outcome = Outcome::Running;
        else if (name == "success")
            result.outcome = Outcome::Success;
        else if (name == "failure")
            result.outcome = Outcome::Failure;
        else
            throw ParseFailure("Unknown outcome");
    }
    return result;
}
Value observationValue(const Observation& o) {
    Value::Array features, actions, coverage;
    for (auto n : o.features) features.emplace_back(n);
    for (auto n : o.legalActions) actions.emplace_back(std::int64_t(n));
    for (const auto& key : o.coverage) coverage.emplace_back(key);
    return Value::object({{"features", std::move(features)},
                          {"legalActions", std::move(actions)},
                          {"coverage", std::move(coverage)},
                          {"reward", o.reward},
                          {"finding", o.finding},
                          {"outcome", o.outcome == Outcome::Running   ? "running"
                                      : o.outcome == Outcome::Success ? "success"
                                                                      : "failure"}});
}
template <class T, class F>
Result<T> parse(F&& fn) {
    try {
        return Result<T>::success(fn());
    } catch (const ParseFailure& e) {
        return Result<T>::failure(Diagnostic::error(DiagnosticCode::ParseError, e.what(), {}, {}, "agent.codec"));
    }
}
}  // namespace

Result<Config> decodeConfig(const Value& value) {
    return parse<Config>([&] {
        const auto& o = object(value, {"featureCount",
                                       "actionCount",
                                       "population",
                                       "generations",
                                       "horizon",
                                       "elites",
                                       "hiddenWidth",
                                       "trainingEpochs",
                                       "maxFindings",
                                       "environmentSeed",
                                       "searchSeed",
                                       "learningSeed",
                                       "dt",
                                       "mutationProbability",
                                       "randomProbability",
                                       "coverageWeight",
                                       "failureWeight",
                                       "learningRate",
                                       "strategy",
                                       "backend"});
        Config      c;
        auto        integer = [&](const char* key, std::uint32_t& target) {
            if (auto it = o.find(key); it != o.end()) target = u32(it->second);
        };
        auto seed = [&](const char* key, std::uint64_t& target) {
            if (auto it = o.find(key); it != o.end()) target = unsignedInteger(it->second);
        };
        auto scalar = [&](const char* key, double& target) {
            if (auto it = o.find(key); it != o.end()) target = number(it->second);
        };
        integer("featureCount", c.featureCount);
        integer("actionCount", c.actionCount);
        integer("population", c.population);
        integer("generations", c.generations);
        integer("horizon", c.horizon);
        integer("elites", c.elites);
        integer("hiddenWidth", c.hiddenWidth);
        integer("trainingEpochs", c.trainingEpochs);
        integer("maxFindings", c.maxFindings);
        seed("environmentSeed", c.environmentSeed);
        seed("searchSeed", c.searchSeed);
        seed("learningSeed", c.learningSeed);
        scalar("dt", c.dt);
        scalar("mutationProbability", c.mutationProbability);
        scalar("randomProbability", c.randomProbability);
        scalar("coverageWeight", c.coverageWeight);
        scalar("failureWeight", c.failureWeight);
        scalar("learningRate", c.learningRate);
        if (auto it = o.find("strategy"); it != o.end()) {
            const auto name = string(it->second);
            if (name == "random")
                c.strategy = Strategy::Random;
            else if (name != "evolution")
                throw ParseFailure("Unknown strategy");
        }
        if (auto it = o.find("backend"); it != o.end()) {
            const auto name = string(it->second);
            if (name == "tensor")
                c.backend = Backend::Tensor;
            else if (name == "gpu")
                c.backend = Backend::Gpu;
            else if (name != "cpu")
                throw ParseFailure("Unknown backend");
        }
        return c;
    });
}
Result<Observation> decodeObservation(const Value& value) {
    return parse<Observation>([&] { return observation(value); });
}
Result<Policy> decodePolicy(const Value& value) {
    auto result = parse<Policy>([&] {
        const auto& o =
            object(value, {"schemaId", "schemaVersion", "featureCount", "actionCount", "hiddenWidth", "weights"});
        Policy p;
        p.schemaId      = string(required(o, "schemaId"));
        p.schemaVersion = u32(required(o, "schemaVersion"));
        p.featureCount  = u32(required(o, "featureCount"));
        p.actionCount   = u32(required(o, "actionCount"));
        p.hiddenWidth   = u32(required(o, "hiddenWidth"));
        for (const auto& n : array(required(o, "weights"), 140000)) p.weights.push_back(number(n));
        return p;
    });
    if (!result) return result;
    auto valid = validatePolicy(result.value());
    if (!valid) return Result<Policy>::failure(valid.status());
    return result;
}
Result<Trace> decodeTrace(const Value& value) {
    return parse<Trace>([&] {
        const auto& o = object(value, {"schemaId", "schemaVersion", "environmentSeed", "dt", "initial", "steps"});
        Trace       t;
        t.schemaId      = string(required(o, "schemaId"));
        t.schemaVersion = u32(required(o, "schemaVersion"));
        if (t.schemaId != "evengine.agent.trace" || t.schemaVersion != 1)
            throw ParseFailure("Unknown trace schema/version");
        t.environmentSeed = unsignedInteger(required(o, "environmentSeed"));
        t.dt              = number(required(o, "dt"));
        t.initial         = observation(required(o, "initial"));
        for (const auto& entry : array(required(o, "steps"), 1024)) {
            const auto& step = object(entry, {"action", "observation"});
            t.steps.push_back({u32(required(step, "action")), observation(required(step, "observation"))});
        }
        return t;
    });
}
Value encodePolicy(const Policy& p) {
    Value::Array weights;
    for (auto w : p.weights) weights.emplace_back(w);
    return Value::object({{"schemaId", p.schemaId},
                          {"schemaVersion", std::int64_t(p.schemaVersion)},
                          {"featureCount", std::int64_t(p.featureCount)},
                          {"actionCount", std::int64_t(p.actionCount)},
                          {"hiddenWidth", std::int64_t(p.hiddenWidth)},
                          {"weights", std::move(weights)}});
}
Value encodeTrace(const Trace& t) {
    Value::Array steps;
    for (const auto& s : t.steps)
        steps.push_back(
            Value::object({{"action", std::int64_t(s.action)}, {"observation", observationValue(s.observation)}}));
    return Value::object({{"schemaId", t.schemaId},
                          {"schemaVersion", std::int64_t(t.schemaVersion)},
                          {"environmentSeed", std::to_string(t.environmentSeed)},
                          {"dt", t.dt},
                          {"initial", observationValue(t.initial)},
                          {"steps", std::move(steps)}});
}
Value encodeReport(const Report& r) {
    Value::Array findings, coverage;
    for (const auto& t : r.findings) findings.push_back(encodeTrace(t));
    for (const auto& key : r.coverage) coverage.emplace_back(key);
    return Value::object({{"schemaId", "evengine.agent.report"},
                          {"schemaVersion", 1},
                          {"policy", encodePolicy(r.policy)},
                          {"best", encodeTrace(r.best)},
                          {"findings", std::move(findings)},
                          {"coverage", std::move(coverage)},
                          {"episodes", std::int64_t(r.episodes)},
                          {"steps", std::int64_t(r.steps)},
                          {"failures", std::int64_t(r.failures)},
                          {"trainingSamples", std::int64_t(r.trainingSamples)},
                          {"bestScore", r.bestScore},
                          {"backend", r.backend},
                          {"trainingBackend", r.trainingBackend}});
}
}  // namespace eve::agent
