#include "agent/BalanceCodec.h"

#include <charconv>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace eve::agent::balance {
namespace {
struct ParseError : std::runtime_error {
    using std::runtime_error::runtime_error;
};
const Value::Object& object(const Value& value, std::initializer_list<std::string_view> fields) {
    const auto* o = value.getIf<Value::Object>();
    if (!o) throw ParseError("Expected object");
    for (const auto& [key, item] : *o) {
        bool known = false;
        for (auto field : fields)
            if (key == field) known = true;
        if (!known) throw ParseError("Unknown balance field: " + key);
    }
    return *o;
}
const Value& required(const Value::Object& o, const char* key) {
    auto it = o.find(key);
    if (it == o.end()) throw ParseError(std::string("Missing balance field: ") + key);
    return it->second;
}
std::string text(const Value& value) {
    const auto* s = value.getIf<std::string>();
    if (!s) throw ParseError("Expected string");
    return *s;
}
double number(const Value& v) {
    double result;
    if (const auto* n = v.getIf<double>())
        result = *n;
    else if (const auto* integer = v.getIf<std::int64_t>())
        result = double(*integer);
    else
        throw ParseError("Expected number");
    if (!std::isfinite(result)) throw ParseError("Nonfinite number");
    return result;
}
std::uint64_t integer(const Value& v) {
    if (const auto* n = v.getIf<std::int64_t>()) {
        if (*n < 0) throw ParseError("Expected nonnegative integer");
        return std::uint64_t(*n);
    }
    auto          s      = text(v);
    std::uint64_t n      = 0;
    auto          parsed = std::from_chars(s.data(), s.data() + s.size(), n);
    if (parsed.ec != std::errc{} || parsed.ptr != s.data() + s.size()) throw ParseError("Invalid integer");
    return n;
}
std::uint32_t u32(const Value& v) {
    auto n = integer(v);
    if (n > std::numeric_limits<std::uint32_t>::max()) throw ParseError("Integer overflow");
    return std::uint32_t(n);
}
const Value::Array& array(const Value& v, std::size_t limit) {
    const auto* a = v.getIf<Value::Array>();
    if (!a || a->size() > limit) throw ParseError("Invalid/oversized balance array");
    return *a;
}
Value seedsValue(const std::vector<std::uint64_t>& seeds) {
    Value::Array a;
    for (auto seed : seeds) a.emplace_back(std::to_string(seed));
    return Value(std::move(a));
}
Value problemValue(const Problem& p) {
    Value::Array parameters, targets;
    for (const auto& x : p.parameters)
        parameters.push_back(
            Value::object({{"name", x.name}, {"initial", x.initial}, {"min", x.minimum}, {"max", x.maximum}}));
    for (const auto& t : p.targets)
        targets.push_back(Value::object(
            {{"name", t.name}, {"min", t.minimum}, {"max", t.maximum}, {"scale", t.scale}, {"weight", t.weight}}));
    return Value::object({{"schema", "evengine.balance.problem"},
                          {"version", 1},
                          {"parameters", std::move(parameters)},
                          {"targets", std::move(targets)},
                          {"trainingSeeds", seedsValue(p.trainingSeeds)},
                          {"validationSeeds", seedsValue(p.validationSeeds)},
                          {"searchSeed", std::to_string(p.searchSeed)},
                          {"candidates", std::int64_t(p.candidates)},
                          {"steps", std::int64_t(p.steps)},
                          {"dt", p.dt},
                          {"changeWeight", p.changeWeight}});
}
Value assessmentValue(const Problem& p, const Assessment& a) {
    Value::Object metrics;
    for (std::size_t i = 0; i < p.targets.size(); ++i)
        metrics.emplace(p.targets[i].name,
                        Value::object({{"mean", a.metrics[i].mean}, {"deviation", a.metrics[i].deviation}}));
    return Value::object({{"metrics", std::move(metrics)}, {"loss", a.loss}, {"score", a.score}});
}
}  // namespace

Result<Problem> decodeProblem(const Value& value) {
    try {
        const auto& o = object(value, {"schema", "version", "parameters", "targets", "trainingSeeds", "validationSeeds",
                                       "searchSeed", "candidates", "steps", "dt", "changeWeight"});
        if (text(required(o, "schema")) != "evengine.balance.problem" || u32(required(o, "version")) != 1)
            throw ParseError("Unsupported balance schema/version");
        Problem p;
        for (const auto& item : array(required(o, "parameters"), 64)) {
            const auto& x = object(item, {"name", "initial", "min", "max"});
            p.parameters.push_back({text(required(x, "name")), number(required(x, "initial")),
                                    number(required(x, "min")), number(required(x, "max"))});
        }
        for (const auto& item : array(required(o, "targets"), 128)) {
            const auto& x = object(item, {"name", "min", "max", "scale", "weight"});
            Target      t{text(required(x, "name")), number(required(x, "min")), number(required(x, "max")),
                          number(required(x, "scale")), 1};
            if (auto it = x.find("weight"); it != x.end()) t.weight = number(it->second);
            p.targets.push_back(std::move(t));
        }
        if (auto it = o.find("trainingSeeds"); it != o.end()) {
            p.trainingSeeds.clear();
            for (const auto& s : array(it->second, 128)) p.trainingSeeds.push_back(integer(s));
        }
        if (auto it = o.find("validationSeeds"); it != o.end()) {
            p.validationSeeds.clear();
            for (const auto& s : array(it->second, 128)) p.validationSeeds.push_back(integer(s));
        }
        if (auto it = o.find("searchSeed"); it != o.end()) p.searchSeed = integer(it->second);
        if (auto it = o.find("candidates"); it != o.end()) p.candidates = u32(it->second);
        if (auto it = o.find("steps"); it != o.end()) p.steps = u32(it->second);
        if (auto it = o.find("dt"); it != o.end()) p.dt = number(it->second);
        if (auto it = o.find("changeWeight"); it != o.end()) p.changeWeight = number(it->second);
        return Result<Problem>::success(std::move(p));
    } catch (const ParseError& e) {
        return Result<Problem>::failure(
            Diagnostic::error(DiagnosticCode::ParseError, e.what(), {}, {}, "agent.balance"));
    }
}

Value encodeBalanceReport(const Report& r) {
    Value::Object parameters, changes;
    for (std::size_t i = 0; i < r.problem.parameters.size(); ++i) {
        const auto& p = r.problem.parameters[i];
        parameters.emplace(p.name, Value(r.parameters[i]));
        changes.emplace(
            p.name,
            Value::object({{"before", p.initial}, {"after", r.parameters[i]}, {"delta", r.parameters[i] - p.initial}}));
    }
    return Value::object({{"schema", "evengine.balance.report"},
                          {"version", 1},
                          {"problem", problemValue(r.problem)},
                          {"parameters", std::move(parameters)},
                          {"changes", std::move(changes)},
                          {"baseline", assessmentValue(r.problem, r.baseline)},
                          {"tuned", assessmentValue(r.problem, r.tuned)},
                          {"baselineValidation", assessmentValue(r.problem, r.baselineValidation)},
                          {"validation", assessmentValue(r.problem, r.validation)},
                          {"evaluations", std::int64_t(r.evaluations)},
                          {"status", r.verdict == Verdict::WithinTargets ? "within_targets" : "outside_targets"},
                          {"optimizer", "boundary-seeded-repair-search"}});
}
}  // namespace eve::agent::balance
