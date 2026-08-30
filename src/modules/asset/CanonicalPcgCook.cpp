#include "asset/CanonicalPcgCook.h"

#include "common/Value.h"

#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>

namespace eve::asset {
namespace {

template <class T>
Result<T> failure(DiagnosticCode code, std::string message, std::string path = {}) {
    return Result<T>::failure(Diagnostic::error(code, std::move(message), std::move(path), {},
                                                "asset.cook.pcg"));
}

const Value* field(const Value::Object& object, std::string_view name) {
    const auto found = object.find(std::string(name));
    return found == object.end() ? nullptr : &found->second;
}

bool number(const Value* value, double& output) {
    if (!value || !value->isNumeric()) return false;
    output = value->isInt64() ? double(value->asInt()) : value->asDouble();
    return std::isfinite(output);
}

void node(std::ostringstream& out, std::string_view id, std::string_view operation) {
    out << "NODE " << std::quoted(std::string(id)) << ' ' << std::quoted(std::string(operation)) << '\n';
}

void edge(std::ostringstream& out, std::string_view from, std::string_view to) {
    out << "EDGE " << std::quoted(std::string(from)) << ' ' << std::quoted(std::string(to)) << " 0\n";
}

}  // namespace

Result<CookedCanonicalPcgGraph> cookCanonicalPcgGraph(
    std::span<const std::uint8_t> definition, std::uint32_t maximumRules,
    std::uint64_t maximumPlanBytes) {
    auto parsed = Value::fromJson(std::string_view(reinterpret_cast<const char*>(definition.data()),
                                                  definition.size()));
    if (!parsed) return Result<CookedCanonicalPcgGraph>::failure(parsed.status());
    const auto* root = parsed.value().getIf<Value::Object>();
    const Value* schema = root ? field(*root, "schema") : nullptr;
    const Value* version = root ? field(*root, "schemaVersion") : nullptr;
    const Value* algorithm = root ? field(*root, "algorithm") : nullptr;
    const Value* rulesValue = root ? field(*root, "rules") : nullptr;
    const auto* rules = rulesValue ? rulesValue->getIf<Value::Array>() : nullptr;
    if (!schema || !schema->isString() || schema->asString() != "eve.pcg-graph" ||
        !version || !version->isInt64() || version->asInt() != 1 || !algorithm ||
        !algorithm->isString() || algorithm->asString() != "terrain-layer-scatter-v1" || !rules)
        return failure<CookedCanonicalPcgGraph>(DiagnosticCode::Unsupported,
                                                "unsupported eve.pcg-graph/1 definition");
    if (rules->empty() || rules->size() > maximumRules)
        return failure<CookedCanonicalPcgGraph>(DiagnosticCode::InvalidArgument,
                                                "PCG rule count exceeds Cook limits", "$.rules");

    std::ostringstream plan;
    plan << "EVPCG_POINT_GRAPH 1\n" << std::setprecision(9);
    std::vector<std::string> outputs;
    Value::Array slots;
    constexpr double radiansToDegrees = 57.29577951308232;
    for (std::size_t i = 0; i < rules->size(); ++i) {
        const auto* rule = (*rules)[i].getIf<Value::Object>();
        const Value* id = rule ? field(*rule, "id") : nullptr;
        const Value* prototype = rule ? field(*rule, "prototype") : nullptr;
        const Value* layer = rule ? field(*rule, "layer") : nullptr;
        const Value* seed = rule ? field(*rule, "seed") : nullptr;
        double density = 0, minimumSlope = 0, maximumSlope = 0;
        if (!id || !id->isString() || id->asString().empty() || !prototype ||
            !prototype->isString() || prototype->asString().empty() || !layer || !layer->isString() ||
            !seed || !seed->isInt64() || seed->asInt() < 0 ||
            !number(rule ? field(*rule, "densityPerSquareMeter") : nullptr, density) || density < 0 ||
            !number(rule ? field(*rule, "minimumSlopeRadians") : nullptr, minimumSlope) ||
            !number(rule ? field(*rule, "maximumSlopeRadians") : nullptr, maximumSlope) ||
            minimumSlope < 0 || maximumSlope < minimumSlope || maximumSlope > 3.141592653589793)
            return failure<CookedCanonicalPcgGraph>(DiagnosticCode::InvalidArgument,
                                                    "PCG scatter rule is invalid",
                                                    "$.rules[" + std::to_string(i) + "]");
        const std::string suffix = std::to_string(i);
        const std::string sample = "sample." + suffix;
        const std::string project = "project." + suffix;
        const std::string slope = "slope." + suffix;
        const std::string taggedPrototype = "prototype." + suffix;
        const std::string taggedLayer = "layer." + suffix;
        node(plan, sample, "spatial.sample");
        plan << "FLOAT " << std::quoted(sample) << " \"spacing\" "
             << (density > 0 ? std::sqrt(1.0 / density) : 1.0) << '\n';
        plan << "INT " << std::quoted(sample) << " \"seed\" "
             << (seed->asInt() & 0x7fffffff) << '\n';
        node(plan, project, "spatial.project"); edge(plan, sample, project);
        node(plan, slope, "filter.slope"); edge(plan, project, slope);
        plan << "FLOAT " << std::quoted(slope) << " \"minDegrees\" "
             << minimumSlope * radiansToDegrees << '\n';
        plan << "FLOAT " << std::quoted(slope) << " \"maxDegrees\" "
             << maximumSlope * radiansToDegrees << '\n';
        std::string scatterOutput = slope;
        if (density == 0) {
            const std::string culled = "cull." + suffix;
            node(plan, culled, "density.cull"); edge(plan, slope, culled);
            plan << "FLOAT " << std::quoted(culled) << " \"multiplier\" 0\n";
            scatterOutput = culled;
        }
        node(plan, taggedPrototype, "attribute.set.string"); edge(plan, scatterOutput, taggedPrototype);
        plan << "STRING " << std::quoted(taggedPrototype) << " \"attribute\" \"prototype\"\n"
             << "STRING " << std::quoted(taggedPrototype) << " \"value\" "
             << std::quoted(prototype->asString()) << '\n';
        node(plan, taggedLayer, "attribute.set.string"); edge(plan, taggedPrototype, taggedLayer);
        plan << "STRING " << std::quoted(taggedLayer) << " \"attribute\" \"layer\"\n"
             << "STRING " << std::quoted(taggedLayer) << " \"value\" "
             << std::quoted(layer->asString()) << '\n';
        outputs.push_back(taggedLayer);
        slots.emplace_back(Value::Object{{"node", Value(sample)}, {"role", Value("terrain")}});
        slots.emplace_back(Value::Object{{"node", Value(project)}, {"role", Value("terrain")}});
    }
    std::string output = outputs.front();
    for (std::size_t i = 1; i < outputs.size(); ++i) {
        const std::string merged = "merge." + std::to_string(i);
        node(plan, merged, "merge"); edge(plan, output, merged);
        plan << "EDGE " << std::quoted(outputs[i]) << ' ' << std::quoted(merged) << " 1\n";
        output = merged;
    }
    plan << "END\n";
    const std::string encodedPlan = plan.str();
    if (encodedPlan.size() > maximumPlanBytes)
        return failure<CookedCanonicalPcgGraph>(DiagnosticCode::InvalidArgument,
                                                "compiled PCG plan exceeds Cook budget");
    Value::Object runtime{{"schema", Value("eve.pcg-graph")},
                          {"schemaVersion", Value(std::int64_t(1))},
                          {"executionPlan", Value("chunk:1")},
                          {"executionPlanFormat", Value("EVPCG_POINT_GRAPH/1")},
                          {"outputNode", Value(output)}, {"spatialSlots", Value(std::move(slots))}};
    auto encodedDefinition = Value(std::move(runtime)).toJson();
    if (!encodedDefinition)
        return Result<CookedCanonicalPcgGraph>::failure(encodedDefinition.status());
    return Result<CookedCanonicalPcgGraph>::success(
        {{encodedDefinition.value().begin(), encodedDefinition.value().end()},
         {encodedPlan.begin(), encodedPlan.end()}});
}

}  // namespace eve::asset
