#include "action/AbilityAsset.h"
#include "action/ActionTargetingCodecInternal.h"

#include "common/ConditionCodec.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <set>

namespace eve::action {
namespace {

template <typename T>
Result<T> invalid(std::string message, std::string path) {
    return Result<T>::failure(
        Diagnostic::error(DiagnosticCode::InvalidArgument, message, path));
}

const Value* field(const Value& value, std::string_view name) {
    if (!value.isObject()) return nullptr;
    const auto& object = *value.getIf<Value::Object>();
    const auto  found  = object.find(std::string(name));
    return found == object.end() ? nullptr : &found->second;
}

Result<std::string> text(const Value& value, std::string_view name, std::string path) {
    const auto* item = field(value, name);
    if (!item || !item->isString()) return invalid<std::string>("expected text field", std::move(path));
    return Result<std::string>::success(item->asString());
}

Result<std::int64_t> integer(const Value& value, std::string_view name, std::string path) {
    const auto* item = field(value, name);
    if (!item || !item->isInt64()) return invalid<std::int64_t>("expected integer field", std::move(path));
    return Result<std::int64_t>::success(item->asInt());
}

Result<bool> boolean(const Value& value, std::string_view name, std::string path) {
    const auto* item = field(value, name);
    if (!item || !item->isBool()) return invalid<bool>("expected boolean field", std::move(path));
    return Result<bool>::success(item->asBool());
}

Result<double> number(const Value& value, std::string_view name, std::string path) {
    const auto* item = field(value, name);
    if (!item || (!item->isDouble() && !item->isInt64()))
        return invalid<double>("expected numeric field", std::move(path));
    const double result = item->isDouble() ? item->asDouble() : static_cast<double>(item->asInt());
    if (!std::isfinite(result)) return invalid<double>("numeric field must be finite", std::move(path));
    return Result<double>::success(result);
}

Result<void> rejectUnknown(const Value& value, const std::set<std::string, std::less<>>& known,
                           std::string path) {
    if (!value.isObject()) return invalid<void>("expected object", std::move(path));
    for (const auto& [name, item] : *value.getIf<Value::Object>()) {
        (void)item;
        if (!known.contains(name)) return invalid<void>("unknown field", path + "." + name);
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
    return invalid<AbilityInstancingPolicy>("unknown ability instancing policy", "instancing");
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
    return invalid<AbilityActivationGroup>("unknown ability activation group", "activationGroup");
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
    return invalid<TargetingMode>("unknown targeting mode", "action.targetingMode");
}

Result<std::vector<std::string>> decodeStrings(const Value& parent, std::string_view name, std::string path);

Result<condition::Condition> decodeAbilityCondition(const Value& value, const std::string& path) {
    (void)path;
    return eve::condition::decodeCondition(value);
}

Value::Array strings(const std::vector<std::string>& values) {
    Value::Array result;
    result.reserve(values.size());
    for (const auto& value : values) result.emplace_back(value);
    return result;
}

Result<std::vector<std::string>> decodeStrings(const Value& parent, std::string_view name, std::string path) {
    const auto* source = field(parent, name);
    if (!source || !source->isArray()) return invalid<std::vector<std::string>>("expected text array", path);
    std::vector<std::string> result;
    for (std::size_t index = 0; index < source->getIf<Value::Array>()->size(); ++index) {
        const auto& item = source->getIf<Value::Array>()->at(index);
        if (!item.isString())
            return invalid<std::vector<std::string>>("expected text item",
                                                     path + "[" + std::to_string(index) + "]");
        result.push_back(item.asString());
    }
    return Result<std::vector<std::string>>::success(std::move(result));
}

Value encodeTargetingSpec(const sensing::TargetingSpec& spec) {
    Value::Object result{
        {"space", std::string(sensing::coordinateSpaceName(spec.space))},
        {"domain", static_cast<std::int64_t>(spec.domain)},
        {"countPolicy",
         spec.countPolicy == sensing::CountPolicy::FailIfOutOfRange ? "fail_if_out_of_range" : "truncate_to_max"},
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
        result["worldArea"] = Value::Object{{"shape", static_cast<std::int64_t>(spec.worldArea->shape())},
                                            {"first", Value::Array{first.x(), first.y(), first.z()}},
                                            {"second", Value::Array{second.x(), second.y(), second.z()}},
                                            {"radius", spec.worldArea->radius()},
                                            {"halfAngle", spec.worldArea->halfAngle()}};
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
        return invalid<std::array<double, 3>>("expected three-component array", std::move(path));
    std::array<double, 3> result{};
    for (std::size_t index = 0; index < result.size(); ++index) {
        const auto& component = item->getIf<Value::Array>()->at(index);
        if (!component.isDouble() && !component.isInt64())
            return invalid<std::array<double, 3>>("vector component must be numeric", path);
        result[index] = component.isDouble() ? component.asDouble() : static_cast<double>(component.asInt());
        if (!std::isfinite(result[index]))
            return invalid<std::array<double, 3>>("vector component must be finite", path);
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
            return invalid<std::array<std::int32_t, 3>>("grid component must be an int32", path);
        result[index] = static_cast<std::int32_t>(component);
    }
    return Result<std::array<std::int32_t, 3>>::success(result);
}

Result<sensing::TargetingSpec> decodeTargetingSpec(const Value& value, std::uint64_t version) {
    if (version >= 3) {
        auto fields = rejectUnknown(value,
                                    {"space", "domain", "minCount", "maxCount", "countPolicy", "minRange", "maxRange",
                                     "requiredTags", "excludedTags", "lineOfSight", "zone", "worldArea", "gridArea"},
                                    "action.targetingSpec");
        if (!fields) return Result<sensing::TargetingSpec>::failure(fields.status());
    }
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
        return invalid<sensing::TargetingSpec>("targeting specification enum or count is invalid",
                                               "action.targetingSpec");
    if (version >= 3) {
        auto policy = text(value, "countPolicy", "action.targetingSpec.countPolicy");
        if (!policy) return Result<sensing::TargetingSpec>::failure(policy.status());
        if (policy.value() == "fail_if_out_of_range")
            result.countPolicy = sensing::CountPolicy::FailIfOutOfRange;
        else if (policy.value() == "truncate_to_max")
            result.countPolicy = sensing::CountPolicy::TruncateToMax;
        else
            return invalid<sensing::TargetingSpec>("unknown count policy", "action.targetingSpec.countPolicy");
    }
    result.domain = static_cast<sensing::TargetDomain>(domain.value());
    result.minCount = static_cast<std::uint32_t>(minCount.value());
    result.maxCount = static_cast<std::uint32_t>(maxCount.value());
    result.minRange = static_cast<float>(minRange.value());
    const auto* maxRange = field(value, "maxRange");
    if (!maxRange) return invalid<sensing::TargetingSpec>("maximum range is missing", "action.targetingSpec.maxRange");
    if (maxRange->isNull()) result.maxRange = std::numeric_limits<float>::infinity();
    else if (maxRange->isDouble() || maxRange->isInt64()) {
        const auto raw = maxRange->isDouble() ? maxRange->asDouble() : static_cast<double>(maxRange->asInt());
        if (!std::isfinite(raw) || !std::isfinite(static_cast<float>(raw)))
            return invalid<sensing::TargetingSpec>("finite maxRange exceeds native range",
                                                   "action.targetingSpec.maxRange");
        result.maxRange = static_cast<float>(raw);
    } else
        return invalid<sensing::TargetingSpec>("maximum range must be numeric or null",
                                               "action.targetingSpec.maxRange");
    result.requiredTags = std::move(required).takeValue();
    result.excludedTags = std::move(excluded).takeValue();
    result.lineOfSight = lineOfSight.value() ? sensing::LineOfSightMode::Required
                                             : sensing::LineOfSightMode::NotRequired;
    if (const auto* zone = field(value, "zone")) {
        if (!zone->isString()) return invalid<sensing::TargetingSpec>("zone must be text", "action.targetingSpec.zone");
        auto id = LogicalId::parse(zone->asString());
        if (!id) return invalid<sensing::TargetingSpec>("zone id is invalid", "action.targetingSpec.zone");
        result.zone = sensing::ZoneRef::fromLogicalId(*id);
    }
    if (const auto* area = field(value, "worldArea")) {
        if (version >= 3) {
            auto fields = rejectUnknown(*area, {"shape", "first", "second", "radius", "halfAngle"},
                                        "action.targetingSpec.worldArea");
            if (!fields) return Result<sensing::TargetingSpec>::failure(fields.status());
        }
        auto shape = integer(*area, "shape", "action.targetingSpec.worldArea.shape");
        if (!shape) return Result<sensing::TargetingSpec>::failure(shape.status());
        auto first = vector3(*area, "first", "action.targetingSpec.worldArea.first");
        if (!first) return Result<sensing::TargetingSpec>::failure(first.status());
        auto second = vector3(*area, "second", "action.targetingSpec.worldArea.second");
        if (!second) return Result<sensing::TargetingSpec>::failure(second.status());
        auto radius = number(*area, "radius", "action.targetingSpec.worldArea.radius");
        if (!radius) return Result<sensing::TargetingSpec>::failure(radius.status());
        if (shape.value() < 0 || shape.value() > (version >= 3 ? 4 : 3))
            return invalid<sensing::TargetingSpec>("world area is invalid", "action.targetingSpec.worldArea");
        float halfAngle = 0;
        if (version >= 3) {
            auto angle = number(*area, "halfAngle", "action.targetingSpec.worldArea.halfAngle");
            if (!angle) return Result<sensing::TargetingSpec>::failure(angle.status());
            halfAngle       = static_cast<float>(angle.value());
            const bool twoD = shape.value() == 0 || shape.value() == 1 || shape.value() == 4;
            if ((twoD && result.space != sensing::CoordinateSpace::World2D) ||
                (!twoD && result.space != sensing::CoordinateSpace::World3D) ||
                (twoD && (first.value()[2] != 0 || second.value()[2] != 0)) || (shape.value() != 4 && halfAngle != 0) ||
                ((shape.value() == 1 || shape.value() == 3) && radius.value() != 0) ||
                ((shape.value() == 0 || shape.value() == 2) &&
                 (second.value()[0] != 0 || second.value()[1] != 0 || second.value()[2] != 0)))
                return invalid<sensing::TargetingSpec>("world geometry fields do not match shape/space",
                                                       "action.targetingSpec.worldArea");
        }
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
            shape.value() == 0   ? sensing::WorldArea::circle2D(a.value(), static_cast<float>(radius.value()))
            : shape.value() == 1 ? sensing::WorldArea::box2D(a.value(), b.value())
            : shape.value() == 2 ? sensing::WorldArea::sphere3D(a.value(), static_cast<float>(radius.value()))
            : shape.value() == 3 ? sensing::WorldArea::box3D(a.value(), b.value())
                                 : sensing::WorldArea::cone2D(a.value(), b.value().x(), b.value().y(), halfAngle,
                                                              static_cast<float>(radius.value()));
        if (!decoded) return Result<sensing::TargetingSpec>::failure(decoded.status());
        result.worldArea = std::move(decoded).takeValue();
    }
    if (const auto* area = field(value, "gridArea")) {
        if (version >= 3) {
            auto fields = rejectUnknown(*area, {"shape", "minimum", "maximum"}, "action.targetingSpec.gridArea");
            if (!fields) return Result<sensing::TargetingSpec>::failure(fields.status());
        }
        auto shape = integer(*area, "shape", "action.targetingSpec.gridArea.shape");
        if (!shape) return Result<sensing::TargetingSpec>::failure(shape.status());
        auto minimum = gridVector3(*area, "minimum", "action.targetingSpec.gridArea.minimum");
        if (!minimum) return Result<sensing::TargetingSpec>::failure(minimum.status());
        auto maximum = gridVector3(*area, "maximum", "action.targetingSpec.gridArea.maximum");
        if (!maximum) return Result<sensing::TargetingSpec>::failure(maximum.status());
        if (shape.value() < 0 || shape.value() > 1)
            return invalid<sensing::TargetingSpec>("grid area is invalid", "action.targetingSpec.gridArea");
        if (version >= 3 && ((shape.value() == 0 && result.space != sensing::CoordinateSpace::Grid2D) ||
                             (shape.value() == 1 && result.space != sensing::CoordinateSpace::Grid3D) ||
                             (result.space == sensing::CoordinateSpace::Grid2D &&
                              (minimum.value()[2] != 0 || maximum.value()[2] != 0))))
            return invalid<sensing::TargetingSpec>("grid geometry dimension mismatch", "action.targetingSpec.gridArea");
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
        return invalid<Duration>("duration field does not match schema version", std::move(path));
    auto raw = integer(value, version == 1 ? msName : nsName, path);
    if (!raw) return Result<Duration>::failure(raw.status());
    if (raw.value() < 0) return invalid<Duration>("duration must be non-negative", std::move(path));
    if (version == 1 && raw.value() > std::numeric_limits<std::int64_t>::max() / 1'000'000)
        return invalid<Duration>("millisecond duration overflows nanoseconds", std::move(path));
    return Result<Duration>::success(Duration::fromNanoseconds(version == 1 ? raw.value() * 1'000'000 : raw.value()));
}

}  // namespace

namespace {
Result<Value> encodeActionFields(const ActionDefinition& definition) {
    auto valid = definition.validate();
    if (!valid) return Result<Value>::failure(valid.status());
    auto condition = condition::encodeCondition(definition.condition);
    if (!condition) return Result<Value>::failure(condition.status());
    Value::Array costs;
    if (definition.cost)
        for (const auto& item : definition.cost->items())
            costs.emplace_back(Value::Object{{"resource", item.resource.value()}, {"amount", item.amount.value()}});
    Value timeline;
    if (definition.timeline) {
        auto encoded = definition.timeline->toValue();
        if (!encoded) return Result<Value>::failure(encoded.status());
        timeline = std::move(encoded).takeValue();
    }
    Value result = Value::Object{
        {"id", definition.id.format()},
        {"timing", Value::Object{{"windupNs", definition.timing.windup.nanoseconds()},
                                 {"activeNs", definition.timing.active.nanoseconds()},
                                 {"recoverNs", definition.timing.recover.nanoseconds()}}},
        {"condition", std::move(condition).takeValue()},
        {"targetingMode", std::string(targetingModeName(definition.targetingMode))},
        {"targetingSpec", definition.targetingSpec ? encodeTargetingSpec(*definition.targetingSpec) : Value{}},
        {"cost", definition.cost ? Value(std::move(costs)) : Value{}},
        {"effectIds", strings(definition.effectIds)},
        {"activeExecutionRequired", definition.activeExecutionRequired},
        {"metadata", definition.metadata},
        {"timeline", std::move(timeline)}};
    auto serializable = result.toJson();
    if (!serializable) return Result<Value>::failure(serializable.status());
    return Result<Value>::success(std::move(result));
}
Result<ActionDefinition> decodeActionFields(const Value& value, std::uint64_t version) {
    const auto*      actionValue = &value;
    ActionDefinition result;
    if (version >= 3) {
        auto fields = rejectUnknown(value,
                                    {"id", "timing", "condition", "targetingMode", "targetingSpec", "cost", "effectIds",
                                     "activeExecutionRequired", "metadata", "timeline"},
                                    "action");
        if (!fields) return Result<ActionDefinition>::failure(fields.status());
        if (!field(value, "targetingSpec") || !field(value, "cost") || !field(value, "timeline"))
            return invalid<ActionDefinition>("complete action fields are missing", "action");
        const auto* timingValue = field(value, "timing");
        if (!timingValue) return invalid<ActionDefinition>("timing missing", "action.timing");
        auto timingFields = rejectUnknown(*timingValue, {"windupNs", "activeNs", "recoverNs"}, "action.timing");
        if (!timingFields) return Result<ActionDefinition>::failure(timingFields.status());
    }
    auto actionIdText = text(*actionValue, "id", "action.id");
    if (!actionIdText) return Result<ActionDefinition>::failure(actionIdText.status());
    const auto* timing   = field(*actionValue, "timing");
    auto        modeText = text(*actionValue, "targetingMode", "action.targetingMode");
    if (!modeText) return Result<ActionDefinition>::failure(modeText.status());
    auto activeRequired = boolean(*actionValue, "activeExecutionRequired", "action.activeExecutionRequired");
    if (!activeRequired) return Result<ActionDefinition>::failure(activeRequired.status());
    auto effects = decodeStrings(*actionValue, "effectIds", "action.effectIds");
    if (!effects) return Result<ActionDefinition>::failure(effects.status());
    const auto* metadata  = field(*actionValue, "metadata");
    const auto* condition = field(*actionValue, "condition");
    if (!timing || !timing->isObject() || !metadata || !metadata->isObject() || !condition)
        return invalid<ActionDefinition>("action definition is incomplete", "action");
    auto actionId = LogicalId::parse(actionIdText.value());
    auto windup   = durationField(*timing, "windupNs", "windupMs", version, "action.timing.windup");
    if (!windup) return Result<ActionDefinition>::failure(windup.status());
    auto active = durationField(*timing, "activeNs", "activeMs", version, "action.timing.active");
    if (!active) return Result<ActionDefinition>::failure(active.status());
    auto recover = durationField(*timing, "recoverNs", "recoverMs", version, "action.timing.recover");
    if (!recover) return Result<ActionDefinition>::failure(recover.status());
    auto mode = parseTargetingMode(modeText.value());
    if (!mode) return Result<ActionDefinition>::failure(mode.status());
    auto decodedCondition = decodeAbilityCondition(*condition, "action.condition");
    if (!decodedCondition) return Result<ActionDefinition>::failure(decodedCondition.status());
    if (!actionId) return invalid<ActionDefinition>("action identity is invalid", "action.id");
    result.id                      = *actionId;
    result.timing                  = {windup.value(), active.value(), recover.value()};
    result.condition               = std::move(decodedCondition).takeValue();
    result.targetingMode           = mode.value();
    result.activeExecutionRequired = activeRequired.value();
    result.effectIds               = std::move(effects).takeValue();
    result.metadata                = *metadata->getIf<Value::Object>();
    if (const auto* spec = field(*actionValue, "targetingSpec"); spec && !spec->isNull()) {
        auto decoded = decodeTargetingSpec(*spec, version);
        if (!decoded) return Result<ActionDefinition>::failure(decoded.status());
        result.targetingSpec = std::move(decoded).takeValue();
    }
    if (const auto* cost = field(*actionValue, "cost"); cost && !cost->isNull()) {
        if (!cost->isArray()) return invalid<ActionDefinition>("action cost must be an array or null", "action.cost");
        std::vector<resource::ResourceCost> items;
        const auto&                         array = *cost->getIf<Value::Array>();
        for (std::size_t index = 0; index < array.size(); ++index) {
            if (version >= 3) {
                auto fields = rejectUnknown(array[index], {"resource", "amount"}, "action.cost");
                if (!fields) return Result<ActionDefinition>::failure(fields.status());
            }
            auto resourceName = text(array[index], "resource", "action.cost[" + std::to_string(index) + "].resource");
            if (!resourceName) return Result<ActionDefinition>::failure(resourceName.status());
            auto amount = integer(array[index], "amount", "action.cost[" + std::to_string(index) + "].amount");
            if (!amount) return Result<ActionDefinition>::failure(amount.status());
            auto item = resource::ResourceCost::create(resourceName.value(), amount.value());
            if (!item) return Result<ActionDefinition>::failure(item.status());
            items.push_back(std::move(item).takeValue());
        }
        auto costSpec = resource::CostSpec::create(std::move(items));
        if (!costSpec) return Result<ActionDefinition>::failure(costSpec.status());
        result.cost = std::move(costSpec).takeValue();
    }
    if (const auto* timeline = field(*actionValue, "timeline"); timeline && !timeline->isNull()) {
        auto decoded = ActionTimeline::fromValue(*timeline);
        if (!decoded) return Result<ActionDefinition>::failure(decoded.status());
        result.timeline = std::move(decoded).takeValue();
    }
    auto valid = result.validate();
    if (!valid) return Result<ActionDefinition>::failure(valid.status());
    if (version >= 3) {
        auto representable = encodeActionFields(result);
        if (!representable) return Result<ActionDefinition>::failure(representable.status());
    }
    return Result<ActionDefinition>::success(std::move(result));
}
}  // namespace
namespace detail {
Value encodeActionTargeting(const sensing::TargetingSpec& spec) { return encodeTargetingSpec(spec); }
Result<sensing::TargetingSpec> decodeActionTargeting(const Value& value) { return decodeTargetingSpec(value, 3); }
}  // namespace detail
Result<Value> ActionDefinition::encodeState() const {
    auto fields = encodeActionFields(*this);
    if (!fields) return Result<Value>::failure(fields.status());
    return Result<Value>::success(Value::object(
        {{"schema", "action.definition"}, {"version", 1}, {"definition", std::move(fields).takeValue()}}));
}
Result<ActionDefinition> ActionDefinition::prepareState(const Value& state) {
    auto fields = rejectUnknown(state, {"schema", "version", "definition"}, "action.definition");
    if (!fields) return Result<ActionDefinition>::failure(fields.status());
    auto schema = text(state, "schema", "schema");
    if (!schema) return Result<ActionDefinition>::failure(schema.status());
    auto version = integer(state, "version", "version");
    if (!version) return Result<ActionDefinition>::failure(version.status());
    if (schema.value() != "action.definition") return invalid<ActionDefinition>("unexpected action schema", "schema");
    if (version.value() != 1)
        return Result<ActionDefinition>::failure(
            Diagnostic::error(DiagnosticCode::UnknownVersion, "unknown action definition version"));
    const auto* definition = field(state, "definition");
    if (!definition) return invalid<ActionDefinition>("definition missing", "definition");
    return decodeActionFields(*definition, 3);
}

Result<Value> encodeAbilityAsset(const AbilityDefinition& definition) {
    auto valid = definition.validate();
    if (!valid) return Result<Value>::failure(valid.status());
    auto encodedAction = encodeActionFields(definition.action);
    if (!encodedAction) return Result<Value>::failure(encodedAction.status());
    Value::Array triggers;
    for (const auto& trigger : definition.triggers)
        triggers.emplace_back(
            Value::Object{{"gameplayTag", trigger.gameplayTag},
                          {"match", trigger.match == tags::GameplayTagMatch::Exact ? "exact" : "include_descendants"}});
    Value::Object root{{"schema", std::string(kAbilityAssetSchemaId)},
                       {"schemaVersion", static_cast<std::int64_t>(kAbilityAssetSchemaVersion)},
                       {"id", definition.id.format()},
                       {"action", std::move(encodedAction).takeValue()},
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
        return invalid<AbilityDefinition>("unexpected ability asset schema", "schema");
    if (versionRaw.value() < 1 || versionRaw.value() > static_cast<std::int64_t>(kAbilityAssetSchemaVersion))
        return invalid<AbilityDefinition>("unsupported ability asset schema version", "schemaVersion");
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
        return invalid<AbilityDefinition>("ability asset is incomplete", "ability");
    auto id = LogicalId::parse(idText.value());
    auto instancing = parseInstancing(instancingText.value());
    if (!instancing) return Result<AbilityDefinition>::failure(instancing.status());
    auto group = parseGroup(groupText.value());
    if (!group) return Result<AbilityDefinition>::failure(group.status());
    if (!id || !actionValue->isObject())
        return invalid<AbilityDefinition>("ability identity or policy is invalid", "ability");

    AbilityDefinition result;
    result.id = *id;
    result.cooldown = cooldown.value();
    result.instancing = instancing.value();
    result.activationGroup = group.value();
    auto decodedAction     = decodeActionFields(*actionValue, version);
    if (!decodedAction) return Result<AbilityDefinition>::failure(decodedAction.status());
    result.action = std::move(decodedAction).takeValue();
    for (std::size_t index = 0; index < triggerValue->getIf<Value::Array>()->size(); ++index) {
        const auto& item = triggerValue->getIf<Value::Array>()->at(index);
        auto tag = text(item, "gameplayTag", "triggers[" + std::to_string(index) + "].gameplayTag");
        if (!tag) return Result<AbilityDefinition>::failure(tag.status());
        auto match = text(item, "match", "triggers[" + std::to_string(index) + "].match");
        if (!match) return Result<AbilityDefinition>::failure(match.status());
        if (match.value() != "exact" && match.value() != "include_descendants")
            return invalid<AbilityDefinition>("ability trigger is invalid", "triggers[" + std::to_string(index) + "]");
        result.triggers.push_back({tag.value(), match.value() == "exact" ? tags::GameplayTagMatch::Exact
                                                                         : tags::GameplayTagMatch::IncludeDescendants});
    }
    auto valid = result.validate();
    if (!valid) return Result<AbilityDefinition>::failure(valid.status());
    return Result<AbilityDefinition>::success(std::move(result));
}

Result<AbilityInstancingPolicy> parseAbilityInstancingPolicy(std::string_view text) {
    if (text == "non-instanced") return Result<AbilityInstancingPolicy>::success(AbilityInstancingPolicy::NonInstanced);
    if (text == "per-owner") return Result<AbilityInstancingPolicy>::success(AbilityInstancingPolicy::PerOwner);
    if (text == "per-execution") return Result<AbilityInstancingPolicy>::success(AbilityInstancingPolicy::PerExecution);
    return invalid<AbilityInstancingPolicy>("ability instancing policy is invalid", "instancing");
}

Result<AbilityActivationGroup> parseAbilityActivationGroup(std::string_view text) {
    if (text == "independent") return Result<AbilityActivationGroup>::success(AbilityActivationGroup::Independent);
    if (text == "exclusive-replaceable")
        return Result<AbilityActivationGroup>::success(AbilityActivationGroup::ExclusiveReplaceable);
    if (text == "exclusive-blocking")
        return Result<AbilityActivationGroup>::success(AbilityActivationGroup::ExclusiveBlocking);
    return invalid<AbilityActivationGroup>("ability activation group is invalid", "activationGroup");
}

Result<AbilityDefinition> abilityDefinitionFromTimeline(LogicalId definitionId, ActionTimeline timeline,
                                                        Duration cooldown, AbilityInstancingPolicy instancing,
                                                        AbilityActivationGroup activationGroup,
                                                        std::string_view       triggerTag) {
    auto timing = actionTimingFromPhysicalSections(timeline);
    if (!timing) return Result<AbilityDefinition>::failure(timing.status());
    AbilityDefinition definition;
    definition.id              = std::move(definitionId);
    definition.action.id       = timeline.actionId;
    definition.action.timing   = timing.value();
    definition.action.timeline = std::move(timeline);
    definition.cooldown        = cooldown;
    definition.instancing      = instancing;
    definition.activationGroup = activationGroup;
    if (!triggerTag.empty()) {
        if (!tags::isValidGameplayTagName(triggerTag))
            return invalid<AbilityDefinition>("ability trigger tag is invalid", "triggerTag");
        definition.triggers.push_back({std::string(triggerTag), tags::GameplayTagMatch::Exact});
    }
    auto valid = definition.validate();
    if (!valid) return Result<AbilityDefinition>::failure(valid.status());
    return Result<AbilityDefinition>::success(std::move(definition));
}

}  // namespace eve::action
