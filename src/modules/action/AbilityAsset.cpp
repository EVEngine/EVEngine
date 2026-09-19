#include "action/AbilityAsset.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <set>

namespace eve::action {
namespace {

const Value* field(const Value& value, std::string_view name) {
    if (!value.isObject()) return nullptr;
    const auto& object = *value.getIf<Value::Object>();
    const auto  found  = object.find(std::string(name));
    return found == object.end() ? nullptr : &found->second;
}

Result<std::string> text(const Value& value, std::string_view name, std::string path) {
    const auto* item = field(value, name);
    if (!item || !item->isString())
        return Result<std::string>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "expected text field", std::move(path)));
    return Result<std::string>::success(item->asString());
}

Result<std::int64_t> integer(const Value& value, std::string_view name, std::string path) {
    const auto* item = field(value, name);
    if (!item || !item->isInt64())
        return Result<std::int64_t>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "expected integer field", std::move(path)));
    return Result<std::int64_t>::success(item->asInt());
}

Result<bool> boolean(const Value& value, std::string_view name, std::string path) {
    const auto* item = field(value, name);
    if (!item || !item->isBool())
        return Result<bool>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "expected boolean field", std::move(path)));
    return Result<bool>::success(item->asBool());
}

Result<double> number(const Value& value, std::string_view name, std::string path) {
    const auto* item = field(value, name);
    if (!item || (!item->isDouble() && !item->isInt64()))
        return Result<double>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "expected numeric field", std::move(path)));
    const double result = item->isDouble() ? item->asDouble() : static_cast<double>(item->asInt());
    if (!std::isfinite(result))
        return Result<double>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "numeric field must be finite", std::move(path)));
    return Result<double>::success(result);
}

Result<void> rejectUnknown(const Value& value, const std::set<std::string, std::less<>>& known,
                           std::string path) {
    if (!value.isObject())
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "expected object", std::move(path)));
    for (const auto& [name, item] : *value.getIf<Value::Object>()) {
        (void)item;
        if (!known.contains(name))
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument, "unknown field", path + "." + name));
    }
    return Result<void>::success();
}

std::string_view instancingName(AbilityInstancingPolicy value) {
    switch (value) {
        case AbilityInstancingPolicy::NonInstanced: return "non_instanced";
        case AbilityInstancingPolicy::PerOwner: return "per_owner";
        case AbilityInstancingPolicy::PerExecution: return "per_execution";
    }
    return "unknown";
}

Result<AbilityInstancingPolicy> parseInstancing(std::string_view value) {
    if (value == "non_instanced")
        return Result<AbilityInstancingPolicy>::success(AbilityInstancingPolicy::NonInstanced);
    if (value == "per_owner") return Result<AbilityInstancingPolicy>::success(AbilityInstancingPolicy::PerOwner);
    if (value == "per_execution")
        return Result<AbilityInstancingPolicy>::success(AbilityInstancingPolicy::PerExecution);
    return Result<AbilityInstancingPolicy>::failure(
        Diagnostic::error(DiagnosticCode::InvalidArgument, "unknown ability instancing policy", "instancing"));
}

std::string_view groupName(AbilityActivationGroup value) {
    switch (value) {
        case AbilityActivationGroup::Independent: return "independent";
        case AbilityActivationGroup::ExclusiveReplaceable: return "exclusive_replaceable";
        case AbilityActivationGroup::ExclusiveBlocking: return "exclusive_blocking";
    }
    return "unknown";
}

Result<AbilityActivationGroup> parseGroup(std::string_view value) {
    if (value == "independent") return Result<AbilityActivationGroup>::success(AbilityActivationGroup::Independent);
    if (value == "exclusive_replaceable")
        return Result<AbilityActivationGroup>::success(AbilityActivationGroup::ExclusiveReplaceable);
    if (value == "exclusive_blocking")
        return Result<AbilityActivationGroup>::success(AbilityActivationGroup::ExclusiveBlocking);
    return Result<AbilityActivationGroup>::failure(
        Diagnostic::error(DiagnosticCode::InvalidArgument, "unknown ability activation group", "activationGroup"));
}

std::string_view targetingModeName(TargetingMode value) {
    switch (value) {
        case TargetingMode::None: return "none";
        case TargetingMode::Explicit: return "explicit";
        case TargetingMode::Query: return "query";
    }
    return "unknown";
}

Result<TargetingMode> parseTargetingMode(std::string_view value) {
    if (value == "none") return Result<TargetingMode>::success(TargetingMode::None);
    if (value == "explicit") return Result<TargetingMode>::success(TargetingMode::Explicit);
    if (value == "query") return Result<TargetingMode>::success(TargetingMode::Query);
    return Result<TargetingMode>::failure(
        Diagnostic::error(DiagnosticCode::InvalidArgument, "unknown targeting mode", "action.targetingMode"));
}

Value encodeCondition(const decision::Condition& condition) {
    Value::Object object{{"kind", std::string(decision::conditionKindName(condition.kind()))}};
    if (condition.kind() == decision::ConditionKind::All ||
        condition.kind() == decision::ConditionKind::Any ||
        condition.kind() == decision::ConditionKind::Not) {
        Value::Array children;
        for (const auto& child : condition.children()) children.push_back(encodeCondition(child));
        object["children"] = std::move(children);
    }
    switch (condition.kind()) {
        case decision::ConditionKind::Compare:
            object["key"]      = condition.key();
            object["operator"] = std::string(decision::compareOperatorName(condition.compareOperator()));
            object["expected"] = condition.expected();
            break;
        case decision::ConditionKind::StateEquals:
            object["key"]      = condition.key();
            object["expected"] = condition.expected();
            break;
        case decision::ConditionKind::HasTag:
        case decision::ConditionKind::HasAttribute:
        case decision::ConditionKind::HasResource:
        case decision::ConditionKind::AuthorityCheck: object["key"] = condition.key(); break;
        case decision::ConditionKind::PolicyCall: {
            object["key"]       = condition.key();
            object["arguments"] = condition.arguments();
            if (condition.scriptDeclaration()) {
                Value::Array dependencies;
                for (const auto& dependency : condition.scriptDeclaration()->dependencies)
                    dependencies.emplace_back(dependency);
                object["script"] = Value::Object{
                    {"name", condition.scriptDeclaration()->name},
                    {"dependencies", std::move(dependencies)},
                    {"determinism", static_cast<std::int64_t>(condition.scriptDeclaration()->determinism)}};
            }
            break;
        }
        case decision::ConditionKind::All:
        case decision::ConditionKind::Any:
        case decision::ConditionKind::Not: break;
    }
    return Value(std::move(object));
}

Result<std::vector<std::string>> decodeStrings(const Value& parent, std::string_view name, std::string path);

Result<decision::Condition> decodeCondition(const Value& value, const std::string& path) {
    auto kind = text(value, "kind", path + ".kind");
    if (!kind) return Result<decision::Condition>::failure(kind.status());
    auto children = [&]() -> Result<std::vector<decision::Condition>> {
        const auto* source = field(value, "children");
        if (!source || !source->isArray())
            return Result<std::vector<decision::Condition>>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument, "expected children array", path + ".children"));
        std::vector<decision::Condition> result;
        const auto& array = *source->getIf<Value::Array>();
        result.reserve(array.size());
        for (std::size_t index = 0; index < array.size(); ++index) {
            auto child = decodeCondition(array[index], path + ".children[" + std::to_string(index) + "]");
            if (!child) return Result<std::vector<decision::Condition>>::failure(child.status());
            result.push_back(std::move(child).takeValue());
        }
        return Result<std::vector<decision::Condition>>::success(std::move(result));
    };
    if (kind.value() == "all" || kind.value() == "any" || kind.value() == "not") {
        auto decoded = children();
        if (!decoded) return Result<decision::Condition>::failure(decoded.status());
        if (kind.value() == "all")
            return Result<decision::Condition>::success(decision::Condition::all(std::move(decoded).takeValue()));
        if (kind.value() == "any")
            return Result<decision::Condition>::success(decision::Condition::any(std::move(decoded).takeValue()));
        if (decoded.value().size() != 1)
            return Result<decision::Condition>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "not condition requires one child", path + ".children"));
        return Result<decision::Condition>::success(decision::Condition::not_(std::move(decoded.value().front())));
    }
    auto key = text(value, "key", path + ".key");
    if (!key) return Result<decision::Condition>::failure(key.status());
    if (kind.value() == "compare") {
        auto op = text(value, "operator", path + ".operator");
        const auto* expected = field(value, "expected");
        if (!op || !expected)
            return Result<decision::Condition>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument, "compare condition is incomplete", path));
        const std::pair<std::string_view, decision::CompareOperator> operators[] = {
            {"eq", decision::CompareOperator::Equal}, {"ne", decision::CompareOperator::NotEqual},
            {"lt", decision::CompareOperator::Less}, {"le", decision::CompareOperator::LessEqual},
            {"gt", decision::CompareOperator::Greater}, {"ge", decision::CompareOperator::GreaterEqual}};
        for (const auto& [name, parsed] : operators)
            if (op.value() == name)
                return Result<decision::Condition>::success(
                    decision::Condition::compare(key.value(), parsed, *expected));
        return Result<decision::Condition>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "unknown comparison operator", path + ".operator"));
    }
    if (kind.value() == "state_equals") {
        const auto* expected = field(value, "expected");
        if (!expected)
            return Result<decision::Condition>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument, "state condition is incomplete", path));
        return Result<decision::Condition>::success(decision::Condition::stateEquals(key.value(), *expected));
    }
    if (kind.value() == "has_tag")
        return Result<decision::Condition>::success(decision::Condition::hasTag(key.value()));
    if (kind.value() == "has_attribute")
        return Result<decision::Condition>::success(decision::Condition::hasAttribute(key.value()));
    if (kind.value() == "has_resource")
        return Result<decision::Condition>::success(decision::Condition::hasResource(key.value()));
    if (kind.value() == "authority_check")
        return Result<decision::Condition>::success(decision::Condition::authorityCheck(key.value()));
    if (kind.value() == "policy_call") {
        const auto* arguments = field(value, "arguments");
        if (!arguments)
            return Result<decision::Condition>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "policy arguments are missing", path + ".arguments"));
        std::optional<decision::ScriptConditionDeclaration> declaration;
        if (const auto* script = field(value, "script")) {
            auto name = text(*script, "name", path + ".script.name");
            if (!name) return Result<decision::Condition>::failure(name.status());
            auto dependencies = decodeStrings(*script, "dependencies", path + ".script.dependencies");
            if (!dependencies) return Result<decision::Condition>::failure(dependencies.status());
            auto determinism = integer(*script, "determinism", path + ".script.determinism");
            if (!determinism) return Result<decision::Condition>::failure(determinism.status());
            if (determinism.value() < 0 || determinism.value() >
                                               static_cast<std::int64_t>(
                                                   decision::DeterminismLevel::ExplicitlyNondeterministic))
                return Result<decision::Condition>::failure(Diagnostic::error(
                    DiagnosticCode::InvalidArgument, "script condition declaration is invalid", path + ".script"));
            declaration = decision::ScriptConditionDeclaration{
                name.value(), std::move(dependencies).takeValue(),
                static_cast<decision::DeterminismLevel>(determinism.value())};
        }
        return Result<decision::Condition>::success(
            decision::Condition::policyCall(key.value(), *arguments, std::move(declaration)));
    }
    return Result<decision::Condition>::failure(
        Diagnostic::error(DiagnosticCode::InvalidArgument, "unknown condition kind", path + ".kind"));
}

Value::Array strings(const std::vector<std::string>& values) {
    Value::Array result;
    result.reserve(values.size());
    for (const auto& value : values) result.emplace_back(value);
    return result;
}

Result<std::vector<std::string>> decodeStrings(const Value& parent, std::string_view name, std::string path) {
    const auto* source = field(parent, name);
    if (!source || !source->isArray())
        return Result<std::vector<std::string>>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "expected text array", path));
    std::vector<std::string> result;
    for (std::size_t index = 0; index < source->getIf<Value::Array>()->size(); ++index) {
        const auto& item = source->getIf<Value::Array>()->at(index);
        if (!item.isString())
            return Result<std::vector<std::string>>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "expected text item", path + "[" + std::to_string(index) + "]"));
        result.push_back(item.asString());
    }
    return Result<std::vector<std::string>>::success(std::move(result));
}

Value encodeTargetingSpec(const sensing::TargetingSpec& spec) {
    Value::Object result{{"space", std::string(sensing::coordinateSpaceName(spec.space))},
                         {"domain", static_cast<std::int64_t>(spec.domain)},
                         {"minCount", static_cast<std::int64_t>(spec.minCount)},
                         {"maxCount", static_cast<std::int64_t>(spec.maxCount)},
                         {"minRange", spec.minRange},
                         {"maxRange", std::isfinite(spec.maxRange) ? Value(spec.maxRange) : Value()},
                         {"requiredTags", strings(spec.requiredTags)},
                         {"excludedTags", strings(spec.excludedTags)},
                         {"lineOfSight", spec.lineOfSight == sensing::LineOfSightMode::Required}};
    if (spec.zone) result["zone"] = spec.zone->logicalId().format();
    if (spec.worldArea) {
        const auto first = spec.worldArea->first();
        const auto second = spec.worldArea->second();
        result["worldArea"] = Value::Object{
            {"shape", static_cast<std::int64_t>(spec.worldArea->shape())},
            {"first", Value::Array{first.x(), first.y(), first.z()}},
            {"second", Value::Array{second.x(), second.y(), second.z()}},
            {"radius", spec.worldArea->radius()}};
    }
    if (spec.gridArea) {
        const auto minimum = spec.gridArea->minimum();
        const auto maximum = spec.gridArea->maximum();
        result["gridArea"] = Value::Object{
            {"shape", static_cast<std::int64_t>(spec.gridArea->shape())},
            {"minimum", Value::Array{minimum.x(), minimum.y(), minimum.z()}},
            {"maximum", Value::Array{maximum.x(), maximum.y(), maximum.z()}}};
    }
    return Value(std::move(result));
}

Result<std::array<double, 3>> vector3(const Value& value, std::string_view name, std::string path) {
    const auto* item = field(value, name);
    if (!item || !item->isArray() || item->getIf<Value::Array>()->size() != 3)
        return Result<std::array<double, 3>>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "expected three-component array", std::move(path)));
    std::array<double, 3> result{};
    for (std::size_t index = 0; index < result.size(); ++index) {
        const auto& component = item->getIf<Value::Array>()->at(index);
        if (!component.isDouble() && !component.isInt64())
            return Result<std::array<double, 3>>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument, "vector component must be numeric", path));
        result[index] = component.isDouble() ? component.asDouble() : static_cast<double>(component.asInt());
        if (!std::isfinite(result[index]))
            return Result<std::array<double, 3>>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument, "vector component must be finite", path));
    }
    return Result<std::array<double, 3>>::success(result);
}

Result<std::array<std::int32_t, 3>> gridVector3(const Value& value, std::string_view name,
                                                std::string path) {
    auto decoded = vector3(value, name, path);
    if (!decoded) return Result<std::array<std::int32_t, 3>>::failure(decoded.status());
    std::array<std::int32_t, 3> result{};
    for (std::size_t index = 0; index < result.size(); ++index) {
        const double component = decoded.value()[index];
        if (std::trunc(component) != component ||
            component < static_cast<double>(std::numeric_limits<std::int32_t>::min()) ||
            component > static_cast<double>(std::numeric_limits<std::int32_t>::max()))
            return Result<std::array<std::int32_t, 3>>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument, "grid component must be an int32", path));
        result[index] = static_cast<std::int32_t>(component);
    }
    return Result<std::array<std::int32_t, 3>>::success(result);
}

Result<sensing::TargetingSpec> decodeTargetingSpec(const Value& value) {
    sensing::TargetingSpec result;
    auto space = text(value, "space", "action.targetingSpec.space");
    if (!space) return Result<sensing::TargetingSpec>::failure(space.status());
    auto domain = integer(value, "domain", "action.targetingSpec.domain");
    if (!domain) return Result<sensing::TargetingSpec>::failure(domain.status());
    auto minCount = integer(value, "minCount", "action.targetingSpec.minCount");
    if (!minCount) return Result<sensing::TargetingSpec>::failure(minCount.status());
    auto maxCount = integer(value, "maxCount", "action.targetingSpec.maxCount");
    if (!maxCount) return Result<sensing::TargetingSpec>::failure(maxCount.status());
    auto minRange = number(value, "minRange", "action.targetingSpec.minRange");
    if (!minRange) return Result<sensing::TargetingSpec>::failure(minRange.status());
    auto required = decodeStrings(value, "requiredTags", "action.targetingSpec.requiredTags");
    if (!required) return Result<sensing::TargetingSpec>::failure(required.status());
    auto excluded = decodeStrings(value, "excludedTags", "action.targetingSpec.excludedTags");
    if (!excluded) return Result<sensing::TargetingSpec>::failure(excluded.status());
    auto lineOfSight = boolean(value, "lineOfSight", "action.targetingSpec.lineOfSight");
    if (!lineOfSight) return Result<sensing::TargetingSpec>::failure(lineOfSight.status());
    const std::pair<std::string_view, sensing::CoordinateSpace> spaces[] = {
        {"world2d", sensing::CoordinateSpace::World2D}, {"world3d", sensing::CoordinateSpace::World3D},
        {"grid2d", sensing::CoordinateSpace::Grid2D}, {"grid3d", sensing::CoordinateSpace::Grid3D}};
    bool spaceFound = false;
    for (const auto& [name, parsed] : spaces)
        if (space.value() == name) { result.space = parsed; spaceFound = true; break; }
    if (!spaceFound || domain.value() < 0 ||
        domain.value() > static_cast<std::int64_t>(sensing::TargetDomain::Neutral) ||
        minCount.value() < 0 || maxCount.value() < 0 ||
        minCount.value() > std::numeric_limits<std::uint32_t>::max() ||
        maxCount.value() > std::numeric_limits<std::uint32_t>::max())
        return Result<sensing::TargetingSpec>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "targeting specification enum or count is invalid",
                              "action.targetingSpec"));
    result.domain = static_cast<sensing::TargetDomain>(domain.value());
    result.minCount = static_cast<std::uint32_t>(minCount.value());
    result.maxCount = static_cast<std::uint32_t>(maxCount.value());
    result.minRange = static_cast<float>(minRange.value());
    const auto* maxRange = field(value, "maxRange");
    if (!maxRange)
        return Result<sensing::TargetingSpec>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "maximum range is missing", "action.targetingSpec.maxRange"));
    if (maxRange->isNull()) result.maxRange = std::numeric_limits<float>::infinity();
    else if (maxRange->isDouble() || maxRange->isInt64())
        result.maxRange = static_cast<float>(maxRange->isDouble() ? maxRange->asDouble() : maxRange->asInt());
    else
        return Result<sensing::TargetingSpec>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "maximum range must be numeric or null", "action.targetingSpec.maxRange"));
    result.requiredTags = std::move(required).takeValue();
    result.excludedTags = std::move(excluded).takeValue();
    result.lineOfSight = lineOfSight.value() ? sensing::LineOfSightMode::Required
                                             : sensing::LineOfSightMode::NotRequired;
    if (const auto* zone = field(value, "zone")) {
        if (!zone->isString())
            return Result<sensing::TargetingSpec>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument, "zone must be text", "action.targetingSpec.zone"));
        auto id = LogicalId::parse(zone->asString());
        if (!id)
            return Result<sensing::TargetingSpec>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument, "zone id is invalid", "action.targetingSpec.zone"));
        result.zone = sensing::ZoneRef::fromLogicalId(*id);
    }
    if (const auto* area = field(value, "worldArea")) {
        auto shape = integer(*area, "shape", "action.targetingSpec.worldArea.shape");
        if (!shape) return Result<sensing::TargetingSpec>::failure(shape.status());
        auto first = vector3(*area, "first", "action.targetingSpec.worldArea.first");
        if (!first) return Result<sensing::TargetingSpec>::failure(first.status());
        auto second = vector3(*area, "second", "action.targetingSpec.worldArea.second");
        if (!second) return Result<sensing::TargetingSpec>::failure(second.status());
        auto radius = number(*area, "radius", "action.targetingSpec.worldArea.radius");
        if (!radius) return Result<sensing::TargetingSpec>::failure(radius.status());
        if (shape.value() < 0 || shape.value() > 3)
            return Result<sensing::TargetingSpec>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "world area is invalid", "action.targetingSpec.worldArea"));
        const auto makePoint = [&](const std::array<double, 3>& point) {
            return result.space == sensing::CoordinateSpace::World2D
                       ? sensing::WorldPoint::world2D(static_cast<float>(point[0]), static_cast<float>(point[1]))
                       : sensing::WorldPoint::world3D(static_cast<float>(point[0]), static_cast<float>(point[1]),
                                                      static_cast<float>(point[2]));
        };
        auto a = makePoint(first.value());
        if (!a) return Result<sensing::TargetingSpec>::failure(a.status());
        auto b = makePoint(second.value());
        if (!b) return Result<sensing::TargetingSpec>::failure(b.status());
        Result<sensing::WorldArea> decoded =
            shape.value() == 0
                ? sensing::WorldArea::circle2D(a.value(), static_cast<float>(radius.value()))
                : shape.value() == 1
                      ? sensing::WorldArea::box2D(a.value(), b.value())
                      : shape.value() == 2
                            ? sensing::WorldArea::sphere3D(a.value(), static_cast<float>(radius.value()))
                            : sensing::WorldArea::box3D(a.value(), b.value());
        if (!decoded) return Result<sensing::TargetingSpec>::failure(decoded.status());
        result.worldArea = std::move(decoded).takeValue();
    }
    if (const auto* area = field(value, "gridArea")) {
        auto shape = integer(*area, "shape", "action.targetingSpec.gridArea.shape");
        if (!shape) return Result<sensing::TargetingSpec>::failure(shape.status());
        auto minimum = gridVector3(*area, "minimum", "action.targetingSpec.gridArea.minimum");
        if (!minimum) return Result<sensing::TargetingSpec>::failure(minimum.status());
        auto maximum = gridVector3(*area, "maximum", "action.targetingSpec.gridArea.maximum");
        if (!maximum) return Result<sensing::TargetingSpec>::failure(maximum.status());
        if (shape.value() < 0 || shape.value() > 1)
            return Result<sensing::TargetingSpec>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "grid area is invalid", "action.targetingSpec.gridArea"));
        const auto makePoint = [&](const std::array<std::int32_t, 3>& point) {
            return result.space == sensing::CoordinateSpace::Grid2D
                       ? sensing::GridPoint::grid2D(point[0], point[1])
                       : sensing::GridPoint::grid3D(point[0], point[1], point[2]);
        };
        const auto a = makePoint(minimum.value());
        const auto b = makePoint(maximum.value());
        auto decoded = shape.value() == 0 ? sensing::GridArea::box2D(a, b) : sensing::GridArea::box3D(a, b);
        if (!decoded) return Result<sensing::TargetingSpec>::failure(decoded.status());
        result.gridArea = std::move(decoded).takeValue();
    }
    auto valid = result.validate();
    if (!valid) return Result<sensing::TargetingSpec>::failure(valid.status());
    return Result<sensing::TargetingSpec>::success(std::move(result));
}

Result<Duration> durationField(const Value& value, std::string_view nsName, std::string_view msName,
                               std::uint64_t version, std::string path) {
    if (field(value, version == 1 ? nsName : msName))
        return Result<Duration>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "duration field does not match schema version", std::move(path)));
    auto raw = integer(value, version == 1 ? msName : nsName, path);
    if (!raw) return Result<Duration>::failure(raw.status());
    if (raw.value() < 0)
        return Result<Duration>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "duration must be non-negative", std::move(path)));
    if (version == 1 && raw.value() > std::numeric_limits<std::int64_t>::max() / 1'000'000)
        return Result<Duration>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "millisecond duration overflows nanoseconds", std::move(path)));
    return Result<Duration>::success(Duration::fromNanoseconds(version == 1 ? raw.value() * 1'000'000 : raw.value()));
}

}  // namespace

Result<Value> encodeAbilityAsset(const AbilityDefinition& definition) {
    auto valid = definition.validate();
    if (!valid) return Result<Value>::failure(valid.status());
    Value::Array costs;
    if (definition.action.cost)
        for (const auto& item : definition.action.cost->items())
            costs.emplace_back(Value::Object{{"resource", item.resource.value()}, {"amount", item.amount.value()}});
    Value::Array triggers;
    for (const auto& trigger : definition.triggers)
        triggers.emplace_back(Value::Object{
            {"gameplayTag", trigger.gameplayTag},
            {"match", trigger.match == tags::GameplayTagMatch::Exact ? "exact" : "include_descendants"}});
    Value timeline;
    if (definition.action.timeline) {
        auto encoded = definition.action.timeline->toValue();
        if (!encoded) return Result<Value>::failure(encoded.status());
        timeline = std::move(encoded).takeValue();
    }
    Value::Object action{{"id", definition.action.id.format()},
                         {"timing", Value::Object{{"windupNs", definition.action.timing.windup.nanoseconds()},
                                                  {"activeNs", definition.action.timing.active.nanoseconds()},
                                                  {"recoverNs", definition.action.timing.recover.nanoseconds()}}},
                         {"condition", encodeCondition(definition.action.condition)},
                         {"targetingMode", std::string(targetingModeName(definition.action.targetingMode))},
                         {"targetingSpec", definition.action.targetingSpec
                                               ? encodeTargetingSpec(*definition.action.targetingSpec)
                                               : Value()},
                         {"cost", definition.action.cost ? Value(std::move(costs)) : Value()},
                         {"effectIds", strings(definition.action.effectIds)},
                         {"activeExecutionRequired", definition.action.activeExecutionRequired},
                         {"metadata", definition.action.metadata},
                         {"timeline", std::move(timeline)}};
    Value::Object root{{"schema", std::string(kAbilityAssetSchemaId)},
                       {"schemaVersion", static_cast<std::int64_t>(kAbilityAssetSchemaVersion)},
                       {"id", definition.id.format()},
                       {"action", std::move(action)},
                       {"cooldownNs", definition.cooldown.nanoseconds()},
                       {"instancing", std::string(instancingName(definition.instancing))},
                       {"activationGroup", std::string(groupName(definition.activationGroup))},
                       {"triggers", std::move(triggers)}};
    return Result<Value>::success(Value(std::move(root)));
}

Result<AbilityDefinition> decodeAbilityAsset(const Value& value) {
    auto rootFields = rejectUnknown(value, {"schema", "schemaVersion", "id", "action", "cooldownNs", "cooldownMs",
                                            "instancing", "activationGroup", "triggers"}, "ability");
    if (!rootFields) return Result<AbilityDefinition>::failure(rootFields.status());
    auto schema = text(value, "schema", "schema");
    if (!schema) return Result<AbilityDefinition>::failure(schema.status());
    auto versionRaw = integer(value, "schemaVersion", "schemaVersion");
    if (!versionRaw) return Result<AbilityDefinition>::failure(versionRaw.status());
    if (schema.value() != kAbilityAssetSchemaId)
        return Result<AbilityDefinition>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "unexpected ability asset schema", "schema"));
    if (versionRaw.value() < 1 || versionRaw.value() > static_cast<std::int64_t>(kAbilityAssetSchemaVersion))
        return Result<AbilityDefinition>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "unsupported ability asset schema version", "schemaVersion"));
    const auto version = static_cast<std::uint64_t>(versionRaw.value());
    auto idText = text(value, "id", "id");
    if (!idText) return Result<AbilityDefinition>::failure(idText.status());
    auto cooldown = durationField(value, "cooldownNs", "cooldownMs", version, "cooldown");
    if (!cooldown) return Result<AbilityDefinition>::failure(cooldown.status());
    auto instancingText = text(value, "instancing", "instancing");
    if (!instancingText) return Result<AbilityDefinition>::failure(instancingText.status());
    auto groupText = text(value, "activationGroup", "activationGroup");
    if (!groupText) return Result<AbilityDefinition>::failure(groupText.status());
    const auto* actionValue = field(value, "action");
    const auto* triggerValue = field(value, "triggers");
    if (!actionValue || !triggerValue || !triggerValue->isArray())
        return Result<AbilityDefinition>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "ability asset is incomplete", "ability"));
    auto id = LogicalId::parse(idText.value());
    auto instancing = parseInstancing(instancingText.value());
    if (!instancing) return Result<AbilityDefinition>::failure(instancing.status());
    auto group = parseGroup(groupText.value());
    if (!group) return Result<AbilityDefinition>::failure(group.status());
    if (!id || !actionValue->isObject())
        return Result<AbilityDefinition>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "ability identity or policy is invalid", "ability"));

    AbilityDefinition result;
    result.id = *id;
    result.cooldown = cooldown.value();
    result.instancing = instancing.value();
    result.activationGroup = group.value();
    auto actionIdText = text(*actionValue, "id", "action.id");
    if (!actionIdText) return Result<AbilityDefinition>::failure(actionIdText.status());
    const auto* timing = field(*actionValue, "timing");
    auto modeText = text(*actionValue, "targetingMode", "action.targetingMode");
    if (!modeText) return Result<AbilityDefinition>::failure(modeText.status());
    auto activeRequired = boolean(*actionValue, "activeExecutionRequired", "action.activeExecutionRequired");
    if (!activeRequired) return Result<AbilityDefinition>::failure(activeRequired.status());
    auto effects = decodeStrings(*actionValue, "effectIds", "action.effectIds");
    if (!effects) return Result<AbilityDefinition>::failure(effects.status());
    const auto* metadata = field(*actionValue, "metadata");
    const auto* condition = field(*actionValue, "condition");
    if (!timing || !timing->isObject() || !metadata || !metadata->isObject() || !condition)
        return Result<AbilityDefinition>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "action definition is incomplete", "action"));
    auto actionId = LogicalId::parse(actionIdText.value());
    auto windup = durationField(*timing, "windupNs", "windupMs", version, "action.timing.windup");
    if (!windup) return Result<AbilityDefinition>::failure(windup.status());
    auto active = durationField(*timing, "activeNs", "activeMs", version, "action.timing.active");
    if (!active) return Result<AbilityDefinition>::failure(active.status());
    auto recover = durationField(*timing, "recoverNs", "recoverMs", version, "action.timing.recover");
    if (!recover) return Result<AbilityDefinition>::failure(recover.status());
    auto mode = parseTargetingMode(modeText.value());
    if (!mode) return Result<AbilityDefinition>::failure(mode.status());
    auto decodedCondition = decodeCondition(*condition, "action.condition");
    if (!decodedCondition) return Result<AbilityDefinition>::failure(decodedCondition.status());
    if (!actionId)
        return Result<AbilityDefinition>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "action identity is invalid", "action.id"));
    result.action.id = *actionId;
    result.action.timing = {windup.value(), active.value(), recover.value()};
    result.action.condition = std::move(decodedCondition).takeValue();
    result.action.targetingMode = mode.value();
    result.action.activeExecutionRequired = activeRequired.value();
    result.action.effectIds = std::move(effects).takeValue();
    result.action.metadata = *metadata->getIf<Value::Object>();
    if (const auto* spec = field(*actionValue, "targetingSpec"); spec && !spec->isNull()) {
        auto decoded = decodeTargetingSpec(*spec);
        if (!decoded) return Result<AbilityDefinition>::failure(decoded.status());
        result.action.targetingSpec = std::move(decoded).takeValue();
    }
    if (const auto* cost = field(*actionValue, "cost"); cost && !cost->isNull()) {
        if (!cost->isArray())
            return Result<AbilityDefinition>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "action cost must be an array or null", "action.cost"));
        std::vector<resource::ResourceCost> items;
        const auto& array = *cost->getIf<Value::Array>();
        for (std::size_t index = 0; index < array.size(); ++index) {
            auto resourceName = text(array[index], "resource", "action.cost[" + std::to_string(index) + "].resource");
            if (!resourceName) return Result<AbilityDefinition>::failure(resourceName.status());
            auto amount = integer(array[index], "amount", "action.cost[" + std::to_string(index) + "].amount");
            if (!amount) return Result<AbilityDefinition>::failure(amount.status());
            auto item = resource::ResourceCost::create(resourceName.value(), amount.value());
            if (!item) return Result<AbilityDefinition>::failure(item.status());
            items.push_back(std::move(item).takeValue());
        }
        auto costSpec = resource::CostSpec::create(std::move(items));
        if (!costSpec) return Result<AbilityDefinition>::failure(costSpec.status());
        result.action.cost = std::move(costSpec).takeValue();
    }
    if (const auto* timeline = field(*actionValue, "timeline"); timeline && !timeline->isNull()) {
        auto decoded = ActionTimeline::fromValue(*timeline);
        if (!decoded) return Result<AbilityDefinition>::failure(decoded.status());
        result.action.timeline = std::move(decoded).takeValue();
    }
    for (std::size_t index = 0; index < triggerValue->getIf<Value::Array>()->size(); ++index) {
        const auto& item = triggerValue->getIf<Value::Array>()->at(index);
        auto tag = text(item, "gameplayTag", "triggers[" + std::to_string(index) + "].gameplayTag");
        if (!tag) return Result<AbilityDefinition>::failure(tag.status());
        auto match = text(item, "match", "triggers[" + std::to_string(index) + "].match");
        if (!match) return Result<AbilityDefinition>::failure(match.status());
        if (match.value() != "exact" && match.value() != "include_descendants")
            return Result<AbilityDefinition>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                        "ability trigger is invalid",
                                                                        "triggers[" + std::to_string(index) + "]"));
        result.triggers.push_back({tag.value(), match.value() == "exact" ? tags::GameplayTagMatch::Exact
                                                                         : tags::GameplayTagMatch::IncludeDescendants});
    }
    auto valid = result.validate();
    if (!valid) return Result<AbilityDefinition>::failure(valid.status());
    return Result<AbilityDefinition>::success(std::move(result));
}

}  // namespace eve::action
