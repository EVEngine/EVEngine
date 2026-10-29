#include "agent/Balance.h"
#include "agent/Learning.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <set>

namespace eve::agent::balance {
namespace {
Diagnostic invalid(std::string message) {
    return Diagnostic::error(DiagnosticCode::InvalidArgument, std::move(message), {}, {}, "agent.balance");
}
bool finite(double v) { return std::isfinite(v) && std::abs(v) <= 1e9; }
bool valid(const Problem& p) {
    if (p.parameters.empty() || p.parameters.size() > 64 || p.targets.empty() || p.targets.size() > 128 ||
        p.trainingSeeds.empty() || p.trainingSeeds.size() > 128 || p.validationSeeds.empty() ||
        p.validationSeeds.size() > 128 || p.candidates < 1 || p.candidates > 4096 ||
        std::uint64_t(p.candidates) * p.trainingSeeds.size() + 2 * p.validationSeeds.size() > 65536 || p.steps < 1 ||
        p.steps > 100000 || !finite(p.dt) || p.dt <= 0 || p.dt > 60 || !finite(p.changeWeight) || p.changeWeight < 0 ||
        p.changeWeight > 1e6)
        return false;
    std::set<std::string> names;
    for (const auto& x : p.parameters) {
        if (x.name.empty() || x.name.size() > 128 || !names.insert(x.name).second || !finite(x.initial) ||
            !finite(x.minimum) || !finite(x.maximum) || x.minimum > x.initial || x.initial > x.maximum)
            return false;
    }
    names.clear();
    for (const auto& t : p.targets) {
        if (t.name.empty() || t.name.size() > 128 || !names.insert(t.name).second || !finite(t.minimum) ||
            !finite(t.maximum) || t.minimum > t.maximum || !finite(t.scale) || t.scale < 1e-9 || !finite(t.weight) ||
            t.weight <= 0 || t.weight > 1e6)
            return false;
    }
    std::set<std::uint64_t> seeds;
    for (auto s : p.trainingSeeds)
        if (!seeds.insert(s).second) return false;
    for (auto s : p.validationSeeds)
        if (!seeds.insert(s).second) return false;
    return true;
}

Result<Assessment> assess(const Problem& p, const std::vector<double>& values, const std::vector<std::uint64_t>& seeds,
                          IEvaluator& evaluator, std::uint64_t& evaluations) {
    Assessment a;
    a.metrics.resize(p.targets.size());
    std::size_t count = 0;
    for (auto seed : seeds) {
        auto sample = [&]() -> Result<std::vector<double>> {
            try {
                return evaluator.evaluate(values, seed, p.dt, p.steps);
            } catch (const std::exception& error) {
                return Result<std::vector<double>>::failure(
                    Diagnostic::error(DiagnosticCode::CallbackFailure, error.what(), {}, {}, "agent.balance"));
            }
        }();
        ++evaluations;
        if (!sample) return Result<Assessment>::failure(sample.status());
        if (sample.value().size() != p.targets.size())
            return Result<Assessment>::failure(invalid("Evaluator metric count differs from targets"));
        ++count;
        for (std::size_t i = 0; i < a.metrics.size(); ++i) {
            const double value = sample.value()[i];
            if (!finite(value))
                return Result<Assessment>::failure(invalid("Evaluator returned nonfinite/oversized metric"));
            auto&        m     = a.metrics[i];
            const double delta = value - m.mean;
            m.mean += delta / double(count);
            m.deviation += delta * (value - m.mean);
        }
    }
    for (std::size_t i = 0; i < a.metrics.size(); ++i) {
        auto& m               = a.metrics[i];
        m.deviation           = count > 1 ? std::sqrt(std::max(0.0, m.deviation / double(count - 1))) : 0;
        const auto&  t        = p.targets[i];
        const double distance = (m.mean - std::clamp(m.mean, t.minimum, t.maximum)) / t.scale;
        a.loss += t.weight * distance * distance;
    }
    double drift = 0;
    for (std::size_t i = 0; i < values.size(); ++i) {
        const auto& x = p.parameters[i];
        if (x.maximum == x.minimum) continue;
        const double delta = (values[i] - x.initial) / (x.maximum - x.minimum);
        drift += delta * delta;
    }
    a.score = a.loss + p.changeWeight * drift / double(values.size());
    return Result<Assessment>::success(std::move(a));
}
}  // namespace

Result<void> BalanceSearch::configure(const Problem& p) {
    if (active_) return Result<void>::failure(Diagnostic::error(DiagnosticCode::Conflict, "Balance search is active"));
    if (!valid(p)) return Result<void>::failure(invalid("Invalid balance bounds, names, targets, seeds or budget"));
    problem_ = p;
    return Result<void>::success();
}

Result<Report> BalanceSearch::run(IEvaluator& evaluator) {
    if (active_) return Result<Report>::failure(Diagnostic::error(DiagnosticCode::Conflict, "Balance search reentry"));
    if (!problem_) return Result<Report>::failure(invalid("Configure the balance search before run"));
    struct Guard {
        bool& active;
        explicit Guard(bool& a) : active(a) { active = true; }
        ~Guard() { active = false; }
    } guard(active_);
    Report report;
    report.problem        = *problem_;
    const auto&         p = report.problem;
    std::vector<double> initial;
    for (const auto& x : p.parameters) initial.push_back(x.initial);
    report.parameters = initial;
    auto baseline     = assess(p, initial, p.trainingSeeds, evaluator, report.evaluations);
    if (!baseline) return Result<Report>::failure(baseline.status());
    report.baseline = std::move(baseline).takeValue();
    report.tuned    = report.baseline;
    detail::Random random(p.searchSeed);
    for (std::uint32_t trial = 1; trial < p.candidates; ++trial) {
        auto values = report.parameters;
        if (trial <= 2) {
            // Boundary anchors expose interactions that small local mutations cannot reach.
            for (std::size_t i = 0; i < values.size(); ++i)
                values[i] = trial == 1 ? p.parameters[i].minimum : p.parameters[i].maximum;
        } else if (trial == 3 && values.size() >= 4) {
            // Probe a sparse corner: the outer parameters stay at their initial values.
            // This can cross a coupled plateau that single-coordinate repairs cannot.
            for (std::size_t i = 0; i < values.size(); ++i) values[i] = p.parameters[i].minimum;
            values.front() = p.parameters.front().initial;
            values.back()  = p.parameters.back().initial;
        } else if (report.tuned.loss == 0 && trial - 3 < values.size()) {
            // Once feasible, spend bounded probes restoring one parameter at a time.
            const auto index = std::size_t(trial - 3);
            values[index]    = p.parameters[index].initial;
        } else {
            const bool   explore = random.unit() < 0.25;
            const double radius  = 0.25 * (1.0 - double(trial) / double(p.candidates)) + 0.01;
            for (std::size_t i = 0; i < values.size(); ++i) {
                const auto&  x     = p.parameters[i];
                const double width = x.maximum - x.minimum;
                values[i] =
                    explore ? x.minimum + random.unit() * width
                            : std::clamp(values[i] + (random.unit() * 2 - 1) * width * radius, x.minimum, x.maximum);
            }
        }
        auto measured = assess(p, values, p.trainingSeeds, evaluator, report.evaluations);
        if (!measured) return Result<Report>::failure(measured.status());
        if (measured.value().score < report.tuned.score) {
            report.parameters = std::move(values);
            report.tuned      = std::move(measured).takeValue();
        }
    }
    auto before = assess(p, initial, p.validationSeeds, evaluator, report.evaluations);
    if (!before) return Result<Report>::failure(before.status());
    report.baselineValidation = std::move(before).takeValue();
    auto after                = assess(p, report.parameters, p.validationSeeds, evaluator, report.evaluations);
    if (!after) return Result<Report>::failure(after.status());
    report.validation = std::move(after).takeValue();
    report.verdict    = Verdict::WithinTargets;
    for (std::size_t i = 0; i < p.targets.size(); ++i) {
        const double mean = report.validation.metrics[i].mean;
        if (mean < p.targets[i].minimum || mean > p.targets[i].maximum) report.verdict = Verdict::OutsideTargets;
    }
    return Result<Report>::success(std::move(report));
}
}  // namespace eve::agent::balance
