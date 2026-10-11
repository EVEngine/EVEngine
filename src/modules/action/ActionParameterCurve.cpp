#include "action/ActionParameterCurve.h"

#include <algorithm>
#include <cmath>
#include <string_view>
#include <utility>

namespace eve::action {
namespace {

template <typename T>
Result<T> invalid(std::string message, std::string path) {
    return Result<T>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::move(message), std::move(path)));
}

Result<double> number(const Value& value, std::string path) {
    double result = 0.0;
    if (const auto* integer = value.getIf<std::int64_t>())
        result = static_cast<double>(*integer);
    else if (const auto* decimal = value.getIf<double>())
        result = *decimal;
    else
        return invalid<double>("parameter curve field must be numeric", std::move(path));
    if (!std::isfinite(result)) return invalid<double>("parameter curve field must be finite", std::move(path));
    return Result<double>::success(result);
}

std::optional<ActionParameterInterpolation> parseInterpolation(std::string_view value) {
    if (value == "step") return ActionParameterInterpolation::Step;
    if (value == "linear") return ActionParameterInterpolation::Linear;
    if (value == "cubic") return ActionParameterInterpolation::Cubic;
    return std::nullopt;
}

std::optional<ActionParameterOperation> parseOperation(std::string_view value) {
    if (value == "replace") return ActionParameterOperation::Replace;
    if (value == "add") return ActionParameterOperation::Add;
    if (value == "multiply") return ActionParameterOperation::Multiply;
    return std::nullopt;
}

}  // namespace

std::string_view actionParameterInterpolationName(ActionParameterInterpolation value) noexcept {
    switch (value) {
        case ActionParameterInterpolation::Step: return "step";
        case ActionParameterInterpolation::Linear: return "linear";
        case ActionParameterInterpolation::Cubic: return "cubic";
    }
    return "linear";
}

std::optional<ActionParameterInterpolation> actionParameterInterpolationFromName(
    std::string_view value) noexcept {
    return parseInterpolation(value);
}

std::string_view actionParameterOperationName(ActionParameterOperation value) noexcept {
    switch (value) {
        case ActionParameterOperation::Replace: return "replace";
        case ActionParameterOperation::Add: return "add";
        case ActionParameterOperation::Multiply: return "multiply";
    }
    return "replace";
}

Result<ActionParameterCurveBinding> ActionParameterCurveBinding::fromPayload(const Value::Object& payload) {
    ActionParameterCurveBinding candidate;
    const auto target = payload.find("target");
    if (target == payload.end() || !target->second.getIf<std::string>())
        return invalid<ActionParameterCurveBinding>("parameter target must be text", "target");
    auto parsedTarget = LogicalId::parse(*target->second.getIf<std::string>());
    if (!parsedTarget)
        return invalid<ActionParameterCurveBinding>("parameter target must be a LogicalId", "target");
    candidate.target = std::move(*parsedTarget);

    if (const auto found = payload.find("operation"); found != payload.end()) {
        const auto* text = found->second.getIf<std::string>();
        if (!text || !parseOperation(*text))
            return invalid<ActionParameterCurveBinding>("parameter operation is unknown", "operation");
        candidate.operation = *parseOperation(*text);
    }

    const auto foundKeys = payload.find("keys");
    const auto* keys = foundKeys == payload.end() ? nullptr : foundKeys->second.getIf<Value::Array>();
    if (!keys || keys->size() < 2 || keys->size() > 1024)
        return invalid<ActionParameterCurveBinding>("parameter curve requires 2 to 1024 keys", "keys");
    candidate.keys.reserve(keys->size());
    for (std::size_t index = 0; index < keys->size(); ++index) {
        const auto* object = (*keys)[index].getIf<Value::Object>();
        const std::string path = "keys[" + std::to_string(index) + "]";
        if (!object) return invalid<ActionParameterCurveBinding>("parameter key must be an object", path);
        const auto time = object->find("time");
        const auto value = object->find("value");
        if (time == object->end() || value == object->end())
            return invalid<ActionParameterCurveBinding>("parameter key requires time and value", path);
        auto parsedTime = number(time->second, path + ".time");
        if (!parsedTime) return Result<ActionParameterCurveBinding>::failure(parsedTime.status());
        auto parsedValue = number(value->second, path + ".value");
        if (!parsedValue) return Result<ActionParameterCurveBinding>::failure(parsedValue.status());
        if (parsedTime.value() < 0.0 || parsedTime.value() > 1.0 ||
            (!candidate.keys.empty() && parsedTime.value() <= candidate.keys.back().time))
            return invalid<ActionParameterCurveBinding>("parameter key times must be strictly ordered in [0, 1]",
                                                        path + ".time");
        ActionParameterKey key;
        key.time  = parsedTime.value();
        key.value = parsedValue.value();
        if (const auto tangent = object->find("inTangent"); tangent != object->end()) {
            auto parsed = number(tangent->second, path + ".inTangent");
            if (!parsed) return Result<ActionParameterCurveBinding>::failure(parsed.status());
            key.inTangent = parsed.value();
        }
        if (const auto tangent = object->find("outTangent"); tangent != object->end()) {
            auto parsed = number(tangent->second, path + ".outTangent");
            if (!parsed) return Result<ActionParameterCurveBinding>::failure(parsed.status());
            key.outTangent = parsed.value();
        }
        if (const auto mode = object->find("interpolation"); mode != object->end()) {
            const auto* text = mode->second.getIf<std::string>();
            if (!text || !parseInterpolation(*text))
                return invalid<ActionParameterCurveBinding>("parameter interpolation is unknown",
                                                            path + ".interpolation");
            key.interpolation = *parseInterpolation(*text);
        }
        candidate.keys.push_back(key);
    }
    if (candidate.keys.front().time != 0.0 || candidate.keys.back().time != 1.0)
        return invalid<ActionParameterCurveBinding>("parameter curve endpoints must be at 0 and 1", "keys");
    return Result<ActionParameterCurveBinding>::success(std::move(candidate));
}

Value::Object ActionParameterCurveBinding::toPayload(Value::Object extensions) const {
    extensions["target"]    = target.format();
    extensions["operation"] = std::string(actionParameterOperationName(operation));
    Value::Array encoded;
    encoded.reserve(keys.size());
    for (const auto& key : keys)
        encoded.emplace_back(Value::Object{{"time", key.time},
                                           {"value", key.value},
                                           {"inTangent", key.inTangent},
                                           {"outTangent", key.outTangent},
                                           {"interpolation", std::string(actionParameterInterpolationName(
                                                                       key.interpolation))}});
    extensions["keys"] = Value(std::move(encoded));
    return extensions;
}

double ActionParameterCurveBinding::sample(double progress) const noexcept {
    if (keys.empty()) return 0.0;
    progress = std::clamp(progress, 0.0, 1.0);
    const auto upper = std::upper_bound(keys.begin(), keys.end(), progress,
                                        [](double value, const ActionParameterKey& key) { return value < key.time; });
    if (upper == keys.begin()) return keys.front().value;
    if (upper == keys.end()) return keys.back().value;
    const auto& left  = *(upper - 1);
    const auto& right = *upper;
    if (left.interpolation == ActionParameterInterpolation::Step) return left.value;
    const double span = right.time - left.time;
    const double t    = (progress - left.time) / span;
    if (left.interpolation == ActionParameterInterpolation::Linear)
        return left.value + (right.value - left.value) * t;
    const double t2 = t * t;
    const double t3 = t2 * t;
    return (2.0 * t3 - 3.0 * t2 + 1.0) * left.value +
           (t3 - 2.0 * t2 + t) * left.outTangent * span +
           (-2.0 * t3 + 3.0 * t2) * right.value +
           (t3 - t2) * right.inTangent * span;
}

}  // namespace eve::action
