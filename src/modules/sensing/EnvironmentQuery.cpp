#include "sensing/EnvironmentQuery.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <numeric>
#include <set>

namespace eve::sensing {
namespace {
constexpr std::size_t   noIndex   = std::numeric_limits<std::size_t>::max();
constexpr std::uint32_t itemLimit = 65536;

template <class T>
Result<T> invalid(std::string message, std::string path) {
    return Result<T>::failure(
        Diagnostic::error(DiagnosticCode::InvalidArgument, std::move(message), std::move(path), {}, "sensing"));
}

bool filters(const EnvironmentTest& test) { return test.purpose != EnvironmentPurpose::Score; }
bool scores(const EnvironmentTest& test) { return test.purpose != EnvironmentPurpose::Filter; }

struct Position {
    double x, y, z;
};
Position position(const TargetLocation& location) {
    return std::visit([](const auto& p) { return Position{double(p.x()), double(p.y()), double(p.z())}; }, location);
}
bool validLocation(const TargetLocation& location, CoordinateSpace space) {
    return std::visit([space](const auto& p) { return p.isValid() && p.space() == space; }, location);
}

struct CompiledTest {
    EnvironmentTest           definition;
    std::size_t               context = 0;
    EnvironmentMetricContract contract;
};
struct CompiledOption {
    EnvironmentOption         definition;
    std::size_t               generatorContext = 0;
    std::vector<CompiledTest> tests;
    bool                      deferred = false;
};

Result<std::size_t> contextSlot(const EnvironmentQueryDefinition& definition, const std::string& name) {
    const auto found = std::find(definition.contexts.begin(), definition.contexts.end(), name);
    if (found == definition.contexts.end()) return invalid<std::size_t>("unknown context", name);
    return Result<std::size_t>::success(static_cast<std::size_t>(found - definition.contexts.begin()));
}

Result<CompiledTest> compileTest(const EnvironmentQueryDefinition& definition, const EnvironmentTest& test) {
    auto slot = contextSlot(definition, test.context);
    if (!slot) return Result<CompiledTest>::failure(slot.status());
    if (test.id.empty() || test.metric > EnvironmentMetricKind::Custom ||
        test.purpose > EnvironmentPurpose::FilterAndScore || test.filterOp > EnvironmentFilterOp::Any ||
        test.aggregate > EnvironmentAggregate::Maximum ||
        test.normalization > EnvironmentNormalization::ActivePopulation ||
        test.preference > EnvironmentPreference::Reference || test.curve > EnvironmentCurve::SquareRoot ||
        !std::isfinite(test.minimum) || !std::isfinite(test.maximum) || test.minimum > test.maximum ||
        !std::isfinite(test.scoreMinimum) || !std::isfinite(test.scoreMaximum) ||
        test.scoreMinimum >= test.scoreMaximum || !std::isfinite(test.scoreMaximum - test.scoreMinimum) ||
        !std::isfinite(test.referenceValue) || !std::isfinite(test.weight) || test.weight < 0) {
        return invalid<CompiledTest>("invalid metric, threshold or scoring configuration", test.id);
    }
    EnvironmentMetricContract contract{definition.space, EnvironmentCost::Low, true, false};
    if (test.metric == EnvironmentMetricKind::Custom) {
        if (!test.custom) return invalid<CompiledTest>("custom metric is missing", test.id);
        contract = test.custom->contract();
        if (contract.space != definition.space || contract.cost > EnvironmentCost::High)
            return invalid<CompiledTest>("metric does not support query space or cost", test.id);
    } else if (test.custom) {
        return invalid<CompiledTest>("built-in metric cannot carry a custom implementation", test.id);
    } else if (test.metric == EnvironmentMetricKind::LineOfSight) {
        contract.cost                = EnvironmentCost::High;
        contract.requiresLineOfSight = true;
    }
    return Result<CompiledTest>::success(CompiledTest{test, slot.value(), contract});
}

void optimize(CompiledOption& option, EnvironmentRunMode mode) {
    // Population-relative normalization is order-sensitive: retain authored order.
    if (std::any_of(option.tests.begin(), option.tests.end(), [](const auto& test) {
            return scores(test.definition) &&
                   test.definition.normalization == EnvironmentNormalization::ActivePopulation;
        }))
        return;
    auto begin = option.tests.begin();
    while (begin != option.tests.end()) {
        if (!begin->contract.reorderable) {
            ++begin;
            continue;
        }
        auto end = std::find_if(begin, option.tests.end(), [](const auto& test) { return !test.contract.reorderable; });
        std::stable_sort(begin, end, [](const auto& a, const auto& b) {
            // Preserve authored score accumulation order, including FilterAndScore,
            // so floating-point rounding cannot change a close ranking after optimization.
            if (scores(a.definition) != scores(b.definition)) return !scores(a.definition);
            if (scores(a.definition)) return false;
            return a.contract.cost < b.contract.cost;
        });
        begin = end;
    }
    if (mode != EnvironmentRunMode::Best || std::any_of(option.tests.begin(), option.tests.end(),
                                                        [](const auto& test) { return !test.contract.reorderable; }))
        return;
    std::size_t best = noIndex;
    for (std::size_t i = 0; i < option.tests.size(); ++i) {
        if (option.tests[i].definition.purpose == EnvironmentPurpose::Filter &&
            option.tests[i].contract.cost != EnvironmentCost::Low &&
            (best == noIndex || option.tests[i].contract.cost > option.tests[best].contract.cost))
            best = i;
    }
    if (best != noIndex) {
        auto deferred = std::move(option.tests[best]);
        option.tests.erase(option.tests.begin() + static_cast<std::ptrdiff_t>(best));
        option.tests.push_back(std::move(deferred));
        option.deferred = true;
    }
}

Result<std::vector<EnvironmentItem>> generate(const CompiledOption& option, const EnvironmentRequest& request,
                                              CoordinateSpace space) {
    const auto& generator = option.definition.generator;
    if (generator.kind == EnvironmentGeneratorKind::Input) {
        if (request.input.size() > generator.maxItems)
            return invalid<std::vector<EnvironmentItem>>("input exceeds candidate budget", option.definition.id);
        return Result<std::vector<EnvironmentItem>>::success(request.input);
    }
    const auto&  centers   = request.contexts[option.generatorContext].locations;
    const double sideValue = std::floor(2 * generator.radius / generator.spacing) + 1;
    const double perCenter =
        generator.kind == EnvironmentGeneratorKind::Ring ? double(generator.samples) : sideValue * sideValue;
    if (!std::isfinite(perCenter) || perCenter * double(centers.size()) > generator.maxItems)
        return invalid<std::vector<EnvironmentItem>>("generated candidate budget exceeded", option.definition.id);
    std::vector<EnvironmentItem> items;
    items.reserve(static_cast<std::size_t>(perCenter) * centers.size());
    auto append = [&](double x, double y, double z) -> Result<void> {
        auto point = space == CoordinateSpace::World2D ? WorldPoint::world2D(float(x), float(y))
                                                       : WorldPoint::world3D(float(x), float(y), float(z));
        if (!point) return Result<void>::failure(point.status());
        items.push_back({items.size(), TargetLocation{point.value()}, std::nullopt});
        return Result<void>::success();
    };
    for (const auto& center : centers) {
        const auto p = position(center);
        if (generator.kind == EnvironmentGeneratorKind::Ring) {
            for (std::uint32_t i = 0; i < generator.samples; ++i) {
                const double angle = 6.2831853071795864769 * i / generator.samples;
                auto         added =
                    append(p.x + generator.radius * std::cos(angle), p.y + generator.radius * std::sin(angle), p.z);
                if (!added) return Result<std::vector<EnvironmentItem>>::failure(added.status());
            }
        } else {
            const auto side = static_cast<std::uint32_t>(sideValue);
            for (std::uint32_t x = 0; x < side; ++x) {
                for (std::uint32_t y = 0; y < side; ++y) {
                    auto added = append(p.x + (double(x) - double(side - 1) / 2) * generator.spacing,
                                        p.y + (double(y) - double(side - 1) / 2) * generator.spacing, p.z);
                    if (!added) return Result<std::vector<EnvironmentItem>>::failure(added.status());
                }
            }
        }
    }
    return Result<std::vector<EnvironmentItem>>::success(std::move(items));
}

struct Measurement {
    double      raw    = 0;
    bool        passed = false;
    std::size_t calls  = 0;
};
Result<Measurement> measure(const CompiledTest& test, const EnvironmentItem& item,
                            std::span<const TargetLocation> contexts, const EnvironmentServices& services) {
    Measurement result;
    std::size_t passed = 0;
    for (const auto& context : contexts) {
        double raw = 0;
        if (test.definition.metric == EnvironmentMetricKind::Custom) {
            auto measured = test.definition.custom->measure(item, context, services);
            if (!measured) return Result<Measurement>::failure(measured.status());
            raw = measured.value();
        } else if (test.definition.metric == EnvironmentMetricKind::LineOfSight) {
            auto visible = services.lineOfSight->get().query(context, item.location);
            if (!visible) return Result<Measurement>::failure(visible.status());
            raw = visible.value().visible ? 1 : 0;
        } else {
            const auto a = position(item.location), b = position(context);
            raw = std::hypot(a.x - b.x, a.y - b.y, a.z - b.z);
        }
        if (!std::isfinite(raw)) return invalid<Measurement>("metric produced a non-finite value", test.definition.id);
        if (raw >= test.definition.minimum && raw <= test.definition.maximum) ++passed;
        ++result.calls;
        if (result.calls == 1)
            result.raw = raw;
        else if (test.definition.aggregate == EnvironmentAggregate::Minimum)
            result.raw = std::min(result.raw, raw);
        else if (test.definition.aggregate == EnvironmentAggregate::Maximum)
            result.raw = std::max(result.raw, raw);
        else
            result.raw = std::lerp(result.raw, raw, 1.0 / double(result.calls));
    }
    result.passed = !filters(test.definition) ||
                    (test.definition.filterOp == EnvironmentFilterOp::All ? passed == contexts.size() : passed > 0);
    return Result<Measurement>::success(result);
}

double utility(const EnvironmentTest& test, double raw, double lower, double upper) {
    if (upper == lower) return 0;  // No discrimination in a constant relative population.
    const double value      = std::clamp(raw, lower, upper);
    double       normalized = (value - lower) / (upper - lower);
    if (test.preference == EnvironmentPreference::Lower) normalized = 1 - normalized;
    if (test.preference == EnvironmentPreference::Reference) {
        const double reference = std::clamp(test.referenceValue, lower, upper);
        normalized             = 1 - std::abs(value - reference) / std::max(reference - lower, upper - reference);
    }
    normalized = std::clamp(normalized, 0.0, 1.0);
    if (test.curve == EnvironmentCurve::Square) normalized *= normalized;
    if (test.curve == EnvironmentCurve::SquareRoot) normalized = std::sqrt(normalized);
    return normalized;
}
}  // namespace

struct EnvironmentPlanData {
    EnvironmentQueryDefinition  definition;
    std::vector<CompiledOption> options;
};

Result<EnvironmentQueryPlan> EnvironmentQueryPlan::compile(const EnvironmentQueryDefinition& definition) {
    if (definition.id.empty() || definition.revision == 0 || definition.options.empty() ||
        definition.options.size() > 32 || definition.contexts.size() > 256 ||
        definition.space > CoordinateSpace::Grid3D || definition.mode > EnvironmentRunMode::Best ||
        definition.trace > EnvironmentTraceLevel::Detailed || definition.maxResults == 0 ||
        definition.maxResults > itemLimit || definition.maxTraceItems > itemLimit) {
        return invalid<EnvironmentQueryPlan>("invalid query definition or resource limits", "query");
    }
    std::set<std::string> names;
    for (const auto& context : definition.contexts) {
        if (context.empty() || !names.insert(context).second)
            return invalid<EnvironmentQueryPlan>("empty or duplicate context", context);
    }
    auto plan        = std::make_shared<EnvironmentPlanData>();
    plan->definition = definition;
    names.clear();
    for (const auto& option : definition.options) {
        const auto& generator = option.generator;
        if (option.id.empty() || !names.insert(option.id).second || option.tests.size() > 64 ||
            generator.kind > EnvironmentGeneratorKind::Grid || generator.maxItems == 0 ||
            generator.maxItems > itemLimit || !std::isfinite(generator.radius) || generator.radius < 0 ||
            !std::isfinite(generator.spacing) || generator.spacing <= 0 || generator.samples == 0 ||
            generator.samples > itemLimit) {
            return invalid<EnvironmentQueryPlan>("invalid option or generator", option.id);
        }
        CompiledOption compiled;
        compiled.definition = option;
        if (generator.kind != EnvironmentGeneratorKind::Input) {
            if (definition.space != CoordinateSpace::World2D && definition.space != CoordinateSpace::World3D)
                return invalid<EnvironmentQueryPlan>("ring/grid generators require world coordinates", option.id);
            auto slot = contextSlot(definition, generator.context);
            if (!slot) return Result<EnvironmentQueryPlan>::failure(slot.status());
            compiled.generatorContext = slot.value();
        }
        std::set<std::string> testNames;
        for (const auto& test : option.tests) {
            if (!testNames.insert(test.id).second) return invalid<EnvironmentQueryPlan>("duplicate test id", test.id);
            auto compiledTest = compileTest(definition, test);
            if (!compiledTest) return Result<EnvironmentQueryPlan>::failure(compiledTest.status());
            compiled.tests.push_back(std::move(compiledTest).takeValue());
        }
        if (definition.optimize) optimize(compiled, definition.mode);
        plan->options.push_back(std::move(compiled));
    }
    return Result<EnvironmentQueryPlan>::success(EnvironmentQueryPlan{std::move(plan)});
}

Result<EnvironmentQueryInstance> EnvironmentQueryPlan::instantiate(EnvironmentRequest request) const {
    if (!data_) return invalid<EnvironmentQueryInstance>("moved-from query plan", "plan");
    if (request.input.size() > itemLimit || request.contexts.size() != data_->definition.contexts.size())
        return invalid<EnvironmentQueryInstance>("input limit or context count mismatch", "request");
    std::set<std::uint64_t> ids;
    for (const auto& item : request.input) {
        if (!validLocation(item.location, data_->definition.space) || !ids.insert(item.id).second ||
            (item.subjectKey && item.subjectKey->empty()))
            return invalid<EnvironmentQueryInstance>("invalid candidate location, identity or duplicate id", "input");
    }
    std::vector<EnvironmentContext> resolved;
    resolved.reserve(request.contexts.size());
    for (const auto& name : data_->definition.contexts) {
        auto found = std::find_if(request.contexts.begin(), request.contexts.end(),
                                  [&](const auto& c) { return c.name == name; });
        if (found == request.contexts.end() || found->locations.empty() || found->locations.size() > 256)
            return invalid<EnvironmentQueryInstance>("missing, empty or oversized context", name);
        for (const auto& location : found->locations) {
            if (!validLocation(location, data_->definition.space))
                return invalid<EnvironmentQueryInstance>("context coordinate mismatch", name);
        }
        resolved.push_back(std::move(*found));
    }
    request.contexts = std::move(resolved);
    return Result<EnvironmentQueryInstance>::success(EnvironmentQueryInstance{data_, std::move(request)});
}

EnvironmentQueryInstance::EnvironmentQueryInstance(std::shared_ptr<const EnvironmentPlanData> plan,
                                                   EnvironmentRequest                         request)
    : plan_(std::move(plan)), request_(std::move(request)) {
    trace_.query    = plan_->definition.id;
    trace_.revision = plan_->definition.revision;
}

Result<EnvironmentQueryResult> EnvironmentQueryInstance::run(const EnvironmentServices& services) {
    if (started_ || !plan_)
        return invalid<EnvironmentQueryResult>("query instance already consumed or moved", "instance");
    started_     = true;
    trace_.state = "running";
    auto result  = [&]() -> Result<EnvironmentQueryResult> {
        try {
            return evaluate(services);
        } catch (const std::exception& error) {
            return Result<EnvironmentQueryResult>::failure(
                Diagnostic::error(DiagnosticCode::CallbackFailure, error.what(), "environment.metric", {}, "sensing"));
        } catch (...) {
            return Result<EnvironmentQueryResult>::failure(
                Diagnostic::error(DiagnosticCode::CallbackFailure, "query provider threw an unknown exception",
                                   "environment.metric", {}, "sensing"));
        }
    }();
    trace_.state = result.ok() ? "completed" : "failed";
    if (!result.ok()) trace_.diagnostics = result.diagnostics();
    return result;
}

Result<EnvironmentQueryResult> EnvironmentQueryInstance::evaluate(const EnvironmentServices& services) {
    const auto& definition = plan_->definition;
    for (const auto& option : plan_->options) {
        // Missing required providers are errors, even when generation would produce no items.
        for (const auto& test : option.tests) {
            if (test.contract.requiresLineOfSight && !services.lineOfSight) {
                return Result<EnvironmentQueryResult>::failure(
                    Diagnostic::error(DiagnosticCode::Unsupported, "query requires a line-of-sight provider",
                                      test.definition.id, {}, "sensing"));
            }
        }
        auto generated = generate(option, request_, definition.space);
        if (!generated) return Result<EnvironmentQueryResult>::failure(generated.status());
        auto                     items = std::move(generated).takeValue();
        std::vector<std::size_t> active(items.size());
        std::iota(active.begin(), active.end(), 0);
        std::vector<double>      totals(items.size(), 0);
        std::vector<std::size_t> traceIndex(items.size(), noIndex);
        if (definition.trace != EnvironmentTraceLevel::None)
            trace_.stages.push_back({option.definition.id, "generate", 0, items.size(), 0, false});
        if (definition.trace == EnvironmentTraceLevel::Detailed) {
            for (std::size_t i = 0; i < items.size(); ++i) {
                if (trace_.items.size() >= definition.maxTraceItems) {
                    trace_.truncated = true;
                    break;
                }
                traceIndex[i] = trace_.items.size();
                trace_.items.push_back({option.definition.id, items[i].id, {}, {}});
            }
        }
        auto rank = [&](std::size_t a, std::size_t b) {
            if (totals[a] != totals[b]) return totals[a] > totals[b];
            return items[a].id < items[b].id;
        };
        for (std::size_t ti = 0; ti < option.tests.size() && !active.empty(); ++ti) {
            const auto& compiled = option.tests[ti];
            const auto& test     = compiled.definition;
            const bool  deferred = option.deferred && ti + 1 == option.tests.size();
            if (deferred) std::sort(active.begin(), active.end(), rank);
            EnvironmentStageTrace stage{option.definition.id, test.id, active.size(), 0, 0, deferred};
            std::vector<std::pair<std::size_t, Measurement>> measured;
            measured.reserve(active.size());
            for (auto index : active) {
                auto measurement =
                    measure(compiled, items[index], request_.contexts[compiled.context].locations, services);
                if (!measurement) {
                    if (definition.trace != EnvironmentTraceLevel::None) trace_.stages.push_back(stage);
                    return Result<EnvironmentQueryResult>::failure(measurement.status());
                }
                const auto value = measurement.value();
                stage.measured += value.calls;
                if (value.passed) ++stage.output;
                measured.emplace_back(index, value);
                if (deferred && value.passed) break;
            }
            double lower = test.scoreMinimum, upper = test.scoreMaximum;
            if (scores(test) && test.normalization == EnvironmentNormalization::ActivePopulation) {
                lower = std::numeric_limits<double>::max();
                upper = std::numeric_limits<double>::lowest();
                for (const auto& [index, value] : measured) {
                    (void)index;
                    if (value.passed) {
                        lower = std::min(lower, value.raw);
                        upper = std::max(upper, value.raw);
                    }
                }
                if (stage.output == 0) lower = upper = 0;
                if (!std::isfinite(upper - lower))
                    return invalid<EnvironmentQueryResult>("normalization span overflow", test.id);
            }
            active.clear();
            for (const auto& [index, value] : measured) {
                const double normalized = value.passed && scores(test) ? utility(test, value.raw, lower, upper) : 0;
                const double weighted   = normalized * test.weight;
                if (value.passed) {
                    totals[index] += weighted;
                    if (!std::isfinite(totals[index]))
                        return invalid<EnvironmentQueryResult>("score accumulation overflow", test.id);
                    active.push_back(index);
                }
                if (traceIndex[index] != noIndex) {
                    auto& detail = trace_.items[traceIndex[index]];
                    detail.contributions.push_back(
                        {test.id, value.raw, lower, upper, normalized, weighted, value.passed});
                    if (!value.passed) detail.rejectedBy = test.id;
                }
            }
            if (definition.trace != EnvironmentTraceLevel::None) trace_.stages.push_back(std::move(stage));
        }
        if (active.empty()) continue;
        std::sort(active.begin(), active.end(), rank);
        const auto count = definition.mode == EnvironmentRunMode::Best ? 1u : definition.maxResults;
        if (active.size() > count) active.resize(count);
        EnvironmentQueryResult output;
        output.option = option.definition.id;
        for (auto index : active) output.ranked.push_back({std::move(items[index]), totals[index]});
        return Result<EnvironmentQueryResult>::success(std::move(output));
    }
    return Result<EnvironmentQueryResult>::success(EnvironmentQueryResult{});
}

Value EnvironmentTrace::toValue() const {
    Value::Array stageValues, itemValues, failures;
    for (const auto& diagnostic : diagnostics) {
        failures.emplace_back(Value::Object{
            {"code", std::int64_t(diagnostic.code())}, {"message", diagnostic.message()}, {"path", diagnostic.path()}});
    }
    for (const auto& stage : stages) {
        stageValues.emplace_back(Value::Object{{"option", stage.option},
                                               {"test", stage.test},
                                               {"input", std::int64_t(stage.input)},
                                               {"output", std::int64_t(stage.output)},
                                               {"measurements", std::int64_t(stage.measured)},
                                               {"deferred", stage.deferred}});
    }
    for (const auto& item : items) {
        Value::Array contributions;
        for (const auto& contribution : item.contributions) {
            contributions.emplace_back(Value::Object{{"test", contribution.test},
                                                     {"raw", contribution.raw},
                                                     {"lower", contribution.lower},
                                                     {"upper", contribution.upper},
                                                     {"normalized", contribution.normalized},
                                                     {"weighted", contribution.weighted},
                                                     {"passed", contribution.passed}});
        }
        itemValues.emplace_back(Value::Object{{"option", item.option},
                                              {"id", std::to_string(item.id)},
                                              {"rejectedBy", item.rejectedBy},
                                              {"contributions", std::move(contributions)}});
    }
    return Value::Object{{"schema", "eve.sensing.environmentTrace"},
                         {"version", 1},
                         {"query", query},
                         {"revision", std::to_string(revision)},
                         {"state", state},
                         {"truncated", truncated},
                         {"diagnostics", std::move(failures)},
                         {"stages", std::move(stageValues)},
                         {"items", std::move(itemValues)}};
}

}  // namespace eve::sensing

namespace eve::sensing {
Value EnvironmentQueryResult::toValue() const {
    Value::Array values;
    for (const auto& candidate : ranked) {
        const auto p     = position(candidate.item.location);
        const auto space = std::visit([](const auto& value) { return value.space(); }, candidate.item.location);
        values.emplace_back(Value::Object{
            {"id", std::to_string(candidate.item.id)},
            {"subject", candidate.item.subjectKey ? Value(*candidate.item.subjectKey) : Value()},
            {"location",
             Value::Object{{"space", std::string(coordinateSpaceName(space))}, {"x", p.x}, {"y", p.y}, {"z", p.z}}},
            {"score", candidate.score}});
    }
    return Value::Object{
        {"schema", "eve.sensing.environmentResult"}, {"version", 1}, {"option", option}, {"ranked", std::move(values)}};
}
}  // namespace eve::sensing
