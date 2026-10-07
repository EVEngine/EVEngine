#include "map/MapObjectContract.h"

#include "common/Value.h"

#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <map>
#include <optional>
#include <set>
#include <string>

namespace eve::map {
namespace {

const eve::Value* member(const eve::Value::Object& object, std::string_view name) {
    const auto found = object.find(std::string(name));
    return found == object.end() ? nullptr : &found->second;
}

bool booleanMember(const eve::Value::Object& object, std::string_view name, bool fallback, bool& output) {
    const eve::Value* value = member(object, name);
    if (!value) {
        output = fallback;
        return true;
    }
    if (!value->isBool()) return false;
    output = value->asBool();
    return true;
}

bool numericValue(const eve::Value& value, double& output) {
    if (value.isInt64()) output = static_cast<double>(value.asInt());
    else if (value.isDouble()) output = value.asDouble();
    else return false;
    return std::isfinite(output);
}

bool parseInteger(std::string_view text, std::int64_t& output) {
    if (text.empty()) return false;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), output);
    return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
}

bool parseNumber(const std::string& text, double& output) {
    if (text.empty()) return false;
    char* end = nullptr;
    errno = 0;
    output = std::strtod(text.c_str(), &end);
    return errno != ERANGE && end == text.c_str() + text.size() && std::isfinite(output);
}

bool stableName(std::string_view name) {
    if (name.empty() || name.size() > 256) return false;
    for (unsigned char character : name)
        if (character < 0x20 || character == 0x7f) return false;
    return true;
}

struct PropertyRule {
    std::string name;
    std::string kind;
    bool required = false;
    std::optional<double> minimum;
    std::optional<double> maximum;
    std::set<std::string> choices;
};

struct TypeRule {
    bool allowUnknownProperties = false;
    std::map<std::string, PropertyRule> properties;
};

eve::Result<void> validateProperty(const std::string& value, const PropertyRule& rule,
                                   const std::string& path) {
    double numeric = 0.0;
    if (rule.kind == "string") {
        if (!rule.choices.empty() && !rule.choices.contains(value))
            return eve::Result<void>::failure(eve::Diagnostic::error(
        eve::DiagnosticCode::InvalidArgument, "property value is outside its enum", std::move(path), {}, "map.object-contract"));
    } else if (rule.kind == "int") {
        std::int64_t integer = 0;
        if (!parseInteger(value, integer))
            return eve::Result<void>::failure(eve::Diagnostic::error(
        eve::DiagnosticCode::TypeMismatch, "property must be an integer", std::move(path), {}, "map.object-contract"));
        numeric = static_cast<double>(integer);
    } else if (rule.kind == "number") {
        if (!parseNumber(value, numeric))
            return eve::Result<void>::failure(eve::Diagnostic::error(
        eve::DiagnosticCode::TypeMismatch, "property must be a finite number", std::move(path), {}, "map.object-contract"));
    } else if (rule.kind == "bool") {
        if (value != "true" && value != "false")
            return eve::Result<void>::failure(eve::Diagnostic::error(
        eve::DiagnosticCode::TypeMismatch, "property must be true or false", std::move(path), {}, "map.object-contract"));
    } else {
        return eve::Result<void>::failure(eve::Diagnostic::error(
        eve::DiagnosticCode::Unsupported, "unsupported property kind", std::move(path), {}, "map.object-contract"));
    }
    if (rule.minimum && numeric < *rule.minimum)
        return eve::Result<void>::failure(eve::Diagnostic::error(
        eve::DiagnosticCode::InvalidArgument, "property is below its minimum", std::move(path), {}, "map.object-contract"));
    if (rule.maximum && numeric > *rule.maximum)
        return eve::Result<void>::failure(eve::Diagnostic::error(
        eve::DiagnosticCode::InvalidArgument, "property exceeds its maximum", std::move(path), {}, "map.object-contract"));
    return eve::Result<void>::success();
}

}  // namespace

eve::Result<void> validateMapObjects(std::span<const MapObject> objects, std::string_view contractJson) {
    auto parsed = eve::Value::fromJson(contractJson);
    if (!parsed.ok()) return eve::Result<void>::failure(parsed.status());
    const auto* root = parsed.value().getIf<eve::Value::Object>();
    if (!root) return eve::Result<void>::failure(eve::Diagnostic::error(
        eve::DiagnosticCode::TypeMismatch, "contract root must be an object", "$contract", {}, "map.object-contract"));

    const eve::Value* schema = member(*root, "schema");
    const eve::Value* version = member(*root, "version");
    const eve::Value* typesValue = member(*root, "types");
    if (!schema || !schema->isString() || schema->asString() != "eve.map.object-contract")
        return eve::Result<void>::failure(eve::Diagnostic::error(
        eve::DiagnosticCode::InvalidArgument, "unsupported contract schema", "$contract.schema", {}, "map.object-contract"));
    if (!version || !version->isInt64() || version->asInt() != 1)
        return eve::Result<void>::failure(eve::Diagnostic::error(
        eve::DiagnosticCode::UnknownVersion, "unsupported contract version", "$contract.version", {}, "map.object-contract"));
    const auto* types = typesValue ? typesValue->getIf<eve::Value::Array>() : nullptr;
    if (!types) return eve::Result<void>::failure(eve::Diagnostic::error(
        eve::DiagnosticCode::TypeMismatch, "types must be an array", "$contract.types", {}, "map.object-contract"));

    bool requireUniqueNames = true;
    bool allowUnknownTypes = false;
    if (!booleanMember(*root, "requireUniqueNames", true, requireUniqueNames))
        return eve::Result<void>::failure(eve::Diagnostic::error(
        eve::DiagnosticCode::TypeMismatch, "requireUniqueNames must be boolean", "$contract.requireUniqueNames", {}, "map.object-contract"));
    if (!booleanMember(*root, "allowUnknownTypes", false, allowUnknownTypes))
        return eve::Result<void>::failure(eve::Diagnostic::error(
        eve::DiagnosticCode::TypeMismatch, "allowUnknownTypes must be boolean", "$contract.allowUnknownTypes", {}, "map.object-contract"));

    std::map<std::string, TypeRule> rules;
    for (std::size_t typeIndex = 0; typeIndex < types->size(); ++typeIndex) {
        const std::string base = "$contract.types[" + std::to_string(typeIndex) + "]";
        const auto* definition = (*types)[typeIndex].getIf<eve::Value::Object>();
        if (!definition) return eve::Result<void>::failure(eve::Diagnostic::error(
        eve::DiagnosticCode::TypeMismatch, "type rule must be an object", std::move(base), {}, "map.object-contract"));
        const eve::Value* type = member(*definition, "type");
        if (!type || !type->isString() || type->asString().empty())
            return eve::Result<void>::failure(eve::Diagnostic::error(
        eve::DiagnosticCode::InvalidArgument, "type must be a non-empty string", std::move(base + ".type"), {}, "map.object-contract"));
        TypeRule typeRule;
        if (!booleanMember(*definition, "allowUnknownProperties", false, typeRule.allowUnknownProperties))
            return eve::Result<void>::failure(eve::Diagnostic::error(
        eve::DiagnosticCode::TypeMismatch, "allowUnknownProperties must be boolean", std::move(base + ".allowUnknownProperties"), {}, "map.object-contract"));
        const eve::Value* propertiesValue = member(*definition, "properties");
        const auto* properties = propertiesValue ? propertiesValue->getIf<eve::Value::Array>() : nullptr;
        if (propertiesValue && !properties)
            return eve::Result<void>::failure(eve::Diagnostic::error(
        eve::DiagnosticCode::TypeMismatch, "properties must be an array", std::move(base + ".properties"), {}, "map.object-contract"));
        if (properties) {
            for (std::size_t propertyIndex = 0; propertyIndex < properties->size(); ++propertyIndex) {
                const std::string propertyPath = base + ".properties[" + std::to_string(propertyIndex) + "]";
                const auto* definitionObject = (*properties)[propertyIndex].getIf<eve::Value::Object>();
                if (!definitionObject)
                    return eve::Result<void>::failure(eve::Diagnostic::error(
        eve::DiagnosticCode::TypeMismatch, "property rule must be an object", std::move(propertyPath), {}, "map.object-contract"));
                const eve::Value* name = member(*definitionObject, "name");
                const eve::Value* kind = member(*definitionObject, "kind");
                if (!name || !name->isString() || name->asString().empty())
                    return eve::Result<void>::failure(eve::Diagnostic::error(
        eve::DiagnosticCode::InvalidArgument, "property name must be non-empty", std::move(propertyPath + ".name"), {}, "map.object-contract"));
                if (!kind || !kind->isString())
                    return eve::Result<void>::failure(eve::Diagnostic::error(
        eve::DiagnosticCode::TypeMismatch, "property kind must be a string", std::move(propertyPath + ".kind"), {}, "map.object-contract"));
                PropertyRule propertyRule{name->asString(), kind->asString()};
                if (!booleanMember(*definitionObject, "required", false, propertyRule.required))
                    return eve::Result<void>::failure(eve::Diagnostic::error(
        eve::DiagnosticCode::TypeMismatch, "required must be boolean", std::move(propertyPath + ".required"), {}, "map.object-contract"));
                for (std::string_view field : {std::string_view("min"), std::string_view("max")}) {
                    if (const eve::Value* bound = member(*definitionObject, field)) {
                        double number = 0.0;
                        if (!numericValue(*bound, number))
                            return eve::Result<void>::failure(eve::Diagnostic::error(
        eve::DiagnosticCode::TypeMismatch, std::move(std::string(field) + " must be numeric"), std::move(propertyPath + "." + std::string(field)), {}, "map.object-contract"));
                        (field == "min" ? propertyRule.minimum : propertyRule.maximum) = number;
                    }
                }
                if (propertyRule.minimum && propertyRule.maximum && *propertyRule.minimum > *propertyRule.maximum)
                    return eve::Result<void>::failure(eve::Diagnostic::error(
        eve::DiagnosticCode::InvalidArgument, "min must not exceed max", std::move(propertyPath), {}, "map.object-contract"));
                if (const eve::Value* enumValue = member(*definitionObject, "enum")) {
                    const auto* choices = enumValue->getIf<eve::Value::Array>();
                    if (!choices)
                        return eve::Result<void>::failure(eve::Diagnostic::error(
        eve::DiagnosticCode::TypeMismatch, "enum must be an array", std::move(propertyPath + ".enum"), {}, "map.object-contract"));
                    for (const eve::Value& choice : *choices) {
                        if (!choice.isString())
                            return eve::Result<void>::failure(eve::Diagnostic::error(
        eve::DiagnosticCode::TypeMismatch, "enum values must be strings", std::move(propertyPath + ".enum"), {}, "map.object-contract"));
                        if (!propertyRule.choices.emplace(choice.asString()).second)
                            return eve::Result<void>::failure(eve::Diagnostic::error(
        eve::DiagnosticCode::AlreadyExists, "duplicate enum value", std::move(propertyPath + ".enum"), {}, "map.object-contract"));
                    }
                }
                if (!typeRule.properties.emplace(propertyRule.name, std::move(propertyRule)).second)
                    return eve::Result<void>::failure(eve::Diagnostic::error(
        eve::DiagnosticCode::AlreadyExists, "duplicate property rule", std::move(propertyPath + ".name"), {}, "map.object-contract"));
            }
        }
        if (!rules.emplace(type->asString(), std::move(typeRule)).second)
            return eve::Result<void>::failure(eve::Diagnostic::error(
        eve::DiagnosticCode::AlreadyExists, "duplicate type rule", std::move(base + ".type"), {}, "map.object-contract"));
    }

    std::set<std::string> names;
    for (std::size_t objectIndex = 0; objectIndex < objects.size(); ++objectIndex) {
        const MapObject& object = objects[objectIndex];
        const std::string base = "$objects[" + std::to_string(objectIndex) + "]";
        if (!std::isfinite(object.x) || !std::isfinite(object.y) || !std::isfinite(object.width) ||
            !std::isfinite(object.height) || object.width < 0.f || object.height < 0.f)
            return eve::Result<void>::failure(eve::Diagnostic::error(
        eve::DiagnosticCode::InvalidArgument, "object geometry must be finite and non-negative", std::move(base), {}, "map.object-contract"));
        if (requireUniqueNames && (!stableName(object.name) || !names.emplace(object.name).second))
            return eve::Result<void>::failure(eve::Diagnostic::error(
        eve::DiagnosticCode::Conflict, "object names must be unique, bounded, non-empty, and control-free", std::move(base + ".name"), {}, "map.object-contract"));
        const auto type = rules.find(object.type);
        if (type == rules.end()) {
            if (!allowUnknownTypes)
                return eve::Result<void>::failure(eve::Diagnostic::error(
        eve::DiagnosticCode::Unsupported, "object type is not admitted", std::move(base + ".type"), {}, "map.object-contract"));
            continue;
        }
        for (const auto& [name, rule] : type->second.properties) {
            const auto property = object.properties.find(name);
            if (property == object.properties.end()) {
                if (rule.required)
                    return eve::Result<void>::failure(eve::Diagnostic::error(
        eve::DiagnosticCode::NotFound, "required property is missing", std::move(base + ".properties." + name), {}, "map.object-contract"));
                continue;
            }
            auto valid = validateProperty(property->second, rule, base + ".properties." + name);
            if (!valid.ok()) return valid;
        }
        if (!type->second.allowUnknownProperties) {
            for (const auto& [name, value] : object.properties) {
                (void)value;
                if (!type->second.properties.contains(name))
                    return eve::Result<void>::failure(eve::Diagnostic::error(
        eve::DiagnosticCode::Unsupported, "property is not admitted", std::move(base + ".properties." + name), {}, "map.object-contract"));
            }
        }
    }
    return eve::Result<void>::success();
}

}  // namespace eve::map
