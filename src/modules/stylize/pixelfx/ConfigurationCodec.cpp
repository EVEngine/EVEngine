// Canonical cold Value boundary shared by native, script and future authoring consumers.
#include <stdexcept>
#include "common/BorrowedRef.h"
#include "stylize/pixelfx/Descriptor.h"
#include "stylize/pixelfx/Runtime.h"

namespace eve::stylize {
namespace {
Diagnostic invalid(std::string message, std::string path) {
    return Diagnostic(DiagnosticCode::InvalidArgument, Severity::Error, std::move(message), std::move(path));
}
std::string pointerKey(std::string_view key) {
    std::string result;
    for (char c : key) result += c == '~' ? "~0" : c == '/' ? "~1" : std::string(1, c);
    return result;
}
ResultRef<const pixelfx::Descriptor> lookup(PixelFxKind kind) {
    try {
        return ResultRef<const pixelfx::Descriptor>::success(std::cref(pixelfx::descriptorFor(kind)));
    } catch (const std::invalid_argument&) {
        return ResultRef<const pixelfx::Descriptor>::failure(
            Diagnostic(DiagnosticCode::NotFound, Severity::Error, "Unknown PixelFx effect kind", "/effect"));
    }
}
Value encode(const pixelfx::Descriptor& descriptor, const PixelFxSettings& settings) {
    Value::Object options;
    for (auto bindings : {pixelfx::commonOptions(), descriptor.options})
        for (const auto& option : bindings)
            options.emplace(std::string(option.name), pixelfx::readOption(settings, option));
    return Value::object({{"schema", Value("eve.pixelfx.configuration")},
                          {"version", Value(int64_t(1))},
                          {"effect", Value(std::string(descriptor.info.id))},
                          {"options", Value::object(std::move(options))}});
}
}  // namespace
Value         PixelFx::configurationValue() const { return encode(*descriptor_, program_->settings); }
Result<Value> PixelFx::describeParameters(PixelFxKind kind) {
    auto found = lookup(kind);
    if (!found) return Result<Value>::failure(found.status());
    const auto&  descriptor = found.value().get();
    const auto   defaults   = PixelFx::defaults(kind);
    Value::Array parameters;
    for (auto bindings : {pixelfx::commonOptions(), descriptor.options})
        for (const auto& option : bindings) {
            auto       value = pixelfx::readOption(defaults, option);
            const bool point =
                std::holds_alternative<std::array<double, 2> PixelFxTransitionSettings::*>(option.member);
            const std::string type = point              ? "point2"
                                     : value.isObject() ? "actor"
                                     : value.isArray()  ? "palette"
                                     : value.isString() ? "string"
                                     : value.isBool()   ? "boolean"
                                     : value.isInt64()  ? (option.color ? "color" : "integer")
                                                        : "number";
            Value parameter        = Value::object({{"name", Value(std::string(option.name))}, {"type", Value(type)}});
            if (value.isObject()) {
                auto constraints =
                    pixelfx::actorConstraints(descriptor.actorRequired || descriptor.minimumActorCells > 0,
                                              descriptor.minimumActorCells, descriptor.minimumActorHeight);
                if (descriptor.actorRule) {
                    const auto& rule = descriptor.actorRule->get();
                    constraints.set("geometryRule",
                                    Value::object({{"id", Value(std::string(rule.id))},
                                                   {"description", Value(std::string(rule.description))}}));
                }
                parameter.set("constraints", std::move(constraints));
            } else if (point) {
                parameter.set("minimum", Value(option.minimum));
                parameter.set("maximum", Value(option.maximum));
                parameter.set("size", Value(int64_t(2)));
                parameter.set("unit", Value("logical-pixels"));
            } else if (value.isString()) {
                parameter.set("minimum", Value(int64_t(option.minimum)));
                parameter.set("maximum", Value(int64_t(option.maximum)));
                parameter.set("lengthUnit", Value("unicode-scalars"));
                parameter.set("encoding", Value("utf-8"));
            } else if (!value.isBool()) {
                parameter.set("minimum", value.isDouble() ? Value(option.minimum) : Value(int64_t(option.minimum)));
                parameter.set("maximum", value.isDouble() ? Value(option.maximum) : Value(int64_t(option.maximum)));
                parameter.set("exclusiveMinimum", Value(option.exclusiveMinimum));
                parameter.set(
                    "rampSteps",
                    Value(int64_t(value.isArray() ? (option.rampSteps ? option.rampSteps : descriptor.paletteSteps)
                                                  : 0)));
            }
            if (std::holds_alternative<std::vector<int64_t> PixelFxPickupSettings::*>(option.member))
                parameter.set("colorNumberRepresentation", Value("signed-or-unsigned-32"));
            parameter.set("default", std::move(value));
            parameters.push_back(std::move(parameter));
        }
    return Result<Value>::success(Value::object({{"schema", Value("eve.pixelfx.parameters")},
                                                 {"version", Value(int64_t(1))},
                                                 {"effect", Value(std::string(descriptor.info.id))},
                                                 {"colorEncoding", Value("rgba8-abgr")},
                                                 {"parameters", Value::array(std::move(parameters))}}));
}
Result<PixelFxPreparedConfig> PixelFx::prepareConfigurationValue(PixelFxKind kind, const Value& configuration) {
    using R    = Result<PixelFxPreparedConfig>;
    auto found = lookup(kind);
    if (!found) return R::failure(found.status());
    const auto& descriptor = found.value().get();
    const auto* root = configuration.getIf<Value::Object>();  // Borrowed only until the synchronous decode returns.
    if (!root) return R::failure(invalid("Configuration must be a versioned object", "/"));
    for (const auto& [key, value] : *root) {
        if (key != "schema" && key != "version" && key != "effect" && key != "options")
            return R::failure(invalid("Unknown configuration field", "/" + pointerKey(key)));
    }
    const auto* schema = configuration.find("schema");
    if (!schema || !schema->isString() || schema->asString() != "eve.pixelfx.configuration")
        return R::failure(invalid("Expected eve.pixelfx.configuration schema", "/schema"));
    const auto* version = configuration.find("version");
    if (!version || !version->isInt64() || version->asInt() != 1)
        return R::failure(Diagnostic(DiagnosticCode::UnknownVersion, Severity::Error,
                                     "Unsupported configuration version", "/version"));
    const auto* effect = configuration.find("effect");
    if (!effect || !effect->isString() || effect->asString() != descriptor.info.id)
        return R::failure(invalid("Configuration effect id does not match target", "/effect"));
    const auto* options = configuration.find("options");
    if (!options || !options->isObject()) return R::failure(invalid("Expected an options object", "/options"));
    auto candidate = PixelFx::defaults(kind);
    for (const auto& [key, value] : *options->getIf<Value::Object>()) {
        const pixelfx::Option* binding = nullptr;
        for (auto declarations : {pixelfx::commonOptions(), descriptor.options})
            for (const auto& option : declarations)
                if (option.name == key) binding = &option;
        if (!binding) return R::failure(invalid("Unknown option for this effect", "/options/" + pointerKey(key)));
        auto decoded = pixelfx::writeOption(candidate, *binding, value);
        if (!decoded) return R::failure(decoded.status());
    }
    return prepareConfiguration(kind, candidate);
}
}  // namespace eve::stylize
