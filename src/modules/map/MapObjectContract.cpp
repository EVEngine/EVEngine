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

eve::Result<void> fail(eve::DiagnosticCode code, std::string message, std::string path) {
    return eve::Result<void>::failure(eve::Diagnostic::error(
        code, std::move(message), std::move(path), {}, "map.object-contract"));
}

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
            return fail(eve::DiagnosticCode::InvalidArgument, "property value is outside its enum", path);
    } else if (rule.kind == "int") {
        std::int64_t integer = 0;
        if (!parseInteger(value, integer))
            return fail(eve::DiagnosticCode::TypeMismatch, "property must be an integer", path);
        numeric = static_cast<double>(integer);
    } else if (rule.kind == "number") {
        if (!parseNumber(value, numeric))
            return fail(eve::DiagnosticCode::TypeMismatch, "property must be a finite number", path);
    } else if (rule.kind == "bool") {
        if (value != "true" && value != "false")
            return fail(eve::DiagnosticCode::TypeMismatch, "property must be true or false", path);
    } else {
        return fail(eve::DiagnosticCode::Unsupported, "unsupported property kind", path);
    }
    if (rule.minimum && numeric < *rule.minimum)
        return fail(eve::DiagnosticCode::InvalidArgument, "property is below its minimum", path);
    if (rule.maximum && numeric > *rule.maximum)
        return fail(eve::DiagnosticCode::InvalidArgument, "property exceeds its maximum", path);
    return eve::Result<void>::success();
}

}  // namespace

eve::Result<void> validateMapObjects(std::span<const MapObject> objects, std::string_view contractJson) {
    auto parsed = eve::Value::fromJson(contractJson);
    if (!parsed.ok()) return eve::Result<void>::failure(parsed.status());
    const auto* root = parsed.value().getIf<eve::Value::Object>();
    if (!root) return fail(eve::DiagnosticCode::TypeMismatch, "contract root must be an object", "$contract");

    const eve::Value* schema = member(*root, "schema");
    const eve::Value* version = member(*root, "version");
    const eve::Value* typesValue = member(*root, "types");
    if (!schema || !schema->isString() || schema->asString() != "eve.map.object-contract")
        return fail(eve::DiagnosticCode::InvalidArgument, "unsupported contract schema", "$contract.schema");
    if (!version || !version->isInt64() || version->asInt() != 1)
        return fail(eve::DiagnosticCode::UnknownVersion, "unsupported contract version", "$contract.version");
    const auto* types = typesValue ? typesValue->getIf<eve::Value::Array>() : nullptr;
    if (!types) return fail(eve::DiagnosticCode::TypeMismatch, "types must be an array", "$contract.types");

    bool requireUniqueNames = true;
    bool allowUnknownTypes = false;
    if (!booleanMember(*root, "requireUniqueNames", true, requireUniqueNames))
        return fail(eve::DiagnosticCode::TypeMismatch, "requireUniqueNames must be boolean",
                    "$contract.requireUniqueNames");
    if (!booleanMember(*root, "allowUnknownTypes", false, allowUnknownTypes))
        return fail(eve::DiagnosticCode::TypeMismatch, "allowUnknownTypes must be boolean",
                    "$contract.allowUnknownTypes");

    std::map<std::string, TypeRule> rules;
    for (std::size_t typeIndex = 0; typeIndex < types->size(); ++typeIndex) {
        const std::string base = "$contract.types[" + std::to_string(typeIndex) + "]";
        const auto* definition = (*types)[typeIndex].getIf<eve::Value::Object>();
        if (!definition) return fail(eve::DiagnosticCode::TypeMismatch, "type rule must be an object", base);
        const eve::Value* type = member(*definition, "type");
        if (!type || !type->isString() || type->asString().empty())
            return fail(eve::DiagnosticCode::InvalidArgument, "type must be a non-empty string", base + ".type");
        TypeRule typeRule;
        if (!booleanMember(*definition, "allowUnknownProperties", false, typeRule.allowUnknownProperties))
            return fail(eve::DiagnosticCode::TypeMismatch, "allowUnknownProperties must be boolean",
                        base + ".allowUnknownProperties");
        const eve::Value* propertiesValue = member(*definition, "properties");
        const auto* properties = propertiesValue ? propertiesValue->getIf<eve::Value::Array>() : nullptr;
        if (propertiesValue && !properties)
            return fail(eve::DiagnosticCode::TypeMismatch, "properties must be an array", base + ".properties");
        if (properties) {
            for (std::size_t propertyIndex = 0; propertyIndex < properties->size(); ++propertyIndex) {
                const std::string propertyPath = base + ".properties[" + std::to_string(propertyIndex) + "]";
                const auto* definitionObject = (*properties)[propertyIndex].getIf<eve::Value::Object>();
                if (!definitionObject)
                    return fail(eve::DiagnosticCode::TypeMismatch, "property rule must be an object", propertyPath);
                const eve::Value* name = member(*definitionObject, "name");
                const eve::Value* kind = member(*definitionObject, "kind");
                if (!name || !name->isString() || name->asString().empty())
                    return fail(eve::DiagnosticCode::InvalidArgument, "property name must be non-empty",
                                propertyPath + ".name");
                if (!kind || !kind->isString())
                    return fail(eve::DiagnosticCode::TypeMismatch, "property kind must be a string",
                                propertyPath + ".kind");
                PropertyRule propertyRule{name->asString(), kind->asString()};
                if (!booleanMember(*definitionObject, "required", false, propertyRule.required))
                    return fail(eve::DiagnosticCode::TypeMismatch, "required must be boolean",
                                propertyPath + ".required");
                for (std::string_view field : {std::string_view("min"), std::string_view("max")}) {
                    if (const eve::Value* bound = member(*definitionObject, field)) {
                        double number = 0.0;
                        if (!numericValue(*bound, number))
                            return fail(eve::DiagnosticCode::TypeMismatch, std::string(field) + " must be numeric",
                                        propertyPath + "." + std::string(field));
                        (field == "min" ? propertyRule.minimum : propertyRule.maximum) = number;
                    }
                }
                if (propertyRule.minimum && propertyRule.maximum && *propertyRule.minimum > *propertyRule.maximum)
                    return fail(eve::DiagnosticCode::InvalidArgument, "min must not exceed max", propertyPath);
                if (const eve::Value* enumValue = member(*definitionObject, "enum")) {
                    const auto* choices = enumValue->getIf<eve::Value::Array>();
                    if (!choices)
                        return fail(eve::DiagnosticCode::TypeMismatch, "enum must be an array", propertyPath + ".enum");
                    for (const eve::Value& choice : *choices) {
                        if (!choice.isString())
                            return fail(eve::DiagnosticCode::TypeMismatch, "enum values must be strings",
                                        propertyPath + ".enum");
                        if (!propertyRule.choices.emplace(choice.asString()).second)
                            return fail(eve::DiagnosticCode::AlreadyExists, "duplicate enum value",
                                        propertyPath + ".enum");
                    }
                }
                if (!typeRule.properties.emplace(propertyRule.name, std::move(propertyRule)).second)
                    return fail(eve::DiagnosticCode::AlreadyExists, "duplicate property rule", propertyPath + ".name");
            }
        }
        if (!rules.emplace(type->asString(), std::move(typeRule)).second)
            return fail(eve::DiagnosticCode::AlreadyExists, "duplicate type rule", base + ".type");
    }

    std::set<std::string> names;
    for (std::size_t objectIndex = 0; objectIndex < objects.size(); ++objectIndex) {
        const MapObject& object = objects[objectIndex];
        const std::string base = "$objects[" + std::to_string(objectIndex) + "]";
        if (!std::isfinite(object.x) || !std::isfinite(object.y) || !std::isfinite(object.width) ||
            !std::isfinite(object.height) || object.width < 0.f || object.height < 0.f)
            return fail(eve::DiagnosticCode::InvalidArgument, "object geometry must be finite and non-negative", base);
        if (requireUniqueNames && (!stableName(object.name) || !names.emplace(object.name).second))
            return fail(eve::DiagnosticCode::Conflict,
                        "object names must be unique, bounded, non-empty, and control-free", base + ".name");
        const auto type = rules.find(object.type);
        if (type == rules.end()) {
            if (!allowUnknownTypes)
                return fail(eve::DiagnosticCode::Unsupported, "object type is not admitted", base + ".type");
            continue;
        }
        for (const auto& [name, rule] : type->second.properties) {
            const auto property = object.properties.find(name);
            if (property == object.properties.end()) {
                if (rule.required)
                    return fail(eve::DiagnosticCode::NotFound, "required property is missing",
                                base + ".properties." + name);
                continue;
            }
            auto valid = validateProperty(property->second, rule, base + ".properties." + name);
            if (!valid.ok()) return valid;
        }
        if (!type->second.allowUnknownProperties) {
            for (const auto& [name, value] : object.properties) {
                (void)value;
                if (!type->second.properties.contains(name))
                    return fail(eve::DiagnosticCode::Unsupported, "property is not admitted",
                                base + ".properties." + name);
            }
        }
    }
    return eve::Result<void>::success();
}

}  // namespace eve::map
