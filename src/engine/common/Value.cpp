#include "common/Value.h"

#include "common/Json.h"

#include <cmath>
#include <cstdio>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace eve {
namespace {

Result<Value> copyJsonValue(const json::Value& source) {
    if (source.isNull()) return Result<Value>::success(Value());
    if (source.isBool()) return Result<Value>::success(Value(source.asBool()));
    if (source.isString()) return Result<Value>::success(Value(source.asString()));

    if (source.isNumber()) {
        if (source.isIntegerLiteral()) {
            if (!source.isInt64()) {
                return Result<Value>::failure(Diagnostic::error(
                    DiagnosticCode::ParseError,
                    "JSON integer is outside the canonical Int64 range"));
            }
            return Result<Value>::success(Value(source.asInt64()));
        }
        return Result<Value>::success(Value(source.asDouble()));
    }

    if (source.isArray()) {
        Value::Array result;
        result.reserve(source.size());
        for (std::size_t index = 0; index < source.size(); ++index) {
            auto element = copyJsonValue(source.at(index));
            if (!element.ok()) return Result<Value>::failure(element.status());
            result.push_back(std::move(element).takeValue());
        }
        return Result<Value>::success(Value(std::move(result)));
    }

    if (source.isObject()) {
        Value::Object result;
        for (const std::string& key : source.keys()) {
            auto member = copyJsonValue(source.get(key.c_str()));
            if (!member.ok()) return Result<Value>::failure(member.status());
            result.emplace(key, std::move(member).takeValue());
        }
        return Result<Value>::success(Value(std::move(result)));
    }

    return Result<Value>::failure(
        Diagnostic::error(DiagnosticCode::ParseError, "unsupported JSON value kind"));
}

}  // namespace

Result<Value> Value::fromJson(std::string_view jsonText) {
    const std::string input(jsonText);
    std::string error;
    json::Document document = json::Document::parse(input, &error);
    if (!document.valid()) {
        return Result<Value>::failure(
            Diagnostic::error(DiagnosticCode::ParseError, std::move(error)));
    }
    return copyJsonValue(document.root());
}

Result<std::string> Value::toJson() const { return json::stringify(*this); }

std::int64_t Value::asInt() const {
    const auto* value = getIf<std::int64_t>();
    EV_ASSERT(value != nullptr, "Value::asInt requires an Int64 value");
    return value ? *value : 0;
}

double Value::asDouble() const {
    const auto* value = getIf<double>();
    EV_ASSERT(value != nullptr, "Value::asDouble requires a Double value");
    return value ? *value : 0.0;
}

bool Value::asBool() const {
    const auto* value = getIf<bool>();
    EV_ASSERT(value != nullptr, "Value::asBool requires a Bool value");
    return value ? *value : false;
}

const std::string& Value::asString() const {
    const auto* value = getIf<std::string>();
    EV_ASSERT(value != nullptr, "Value::asString requires a String value");
    static const std::string empty;
    return value ? *value : empty;
}

void Value::pushBack(Value value) {
    auto* array = getIf<Array>();
    EV_ASSERT(array != nullptr, "Value::pushBack requires an Array value");
    if (array) array->push_back(std::move(value));
}

std::size_t Value::arraySize() const {
    const auto* array = getIf<Array>();
    EV_ASSERT(array != nullptr, "Value::arraySize requires an Array value");
    return array ? array->size() : 0;
}

Value& Value::at(std::size_t index) {
    auto* array = getIf<Array>();
    EV_ASSERT(array != nullptr, "Value::at requires an Array value");
    return array->at(index);
}

const Value& Value::at(std::size_t index) const {
    const auto* array = getIf<Array>();
    EV_ASSERT(array != nullptr, "Value::at requires an Array value");
    return array->at(index);
}

void Value::set(const std::string& key, Value value) {
    auto* object = getIf<Object>();
    EV_ASSERT(object != nullptr, "Value::set requires an Object value");
    if (object) (*object)[key] = std::move(value);
}

Value* Value::find(const std::string& key) noexcept {
    auto* object = getIf<Object>();
    if (!object) return nullptr;
    const auto it = object->find(key);
    return it == object->end() ? nullptr : &it->second;
}

const Value* Value::find(const std::string& key) const noexcept {
    const auto* object = getIf<Object>();
    if (!object) return nullptr;
    const auto it = object->find(key);
    return it == object->end() ? nullptr : &it->second;
}

std::vector<std::string> Value::keys() const {
    const auto* object = getIf<Object>();
    if (!object) return {};
    std::vector<std::string> result;
    result.reserve(object->size());
    for (const auto& [key, value] : *object) {
        (void)value;
        result.push_back(key);
    }
    return result;
}

std::string Value::typeName() const {
    switch (type()) {
    case Type::Null: return "null";
    case Type::Bool: return "bool";
    case Type::Int64: return "int";
    case Type::Double: return "float";
    case Type::String: return "string";
    case Type::Array: return "array";
    case Type::Object: return "object";
    }
    return "null";
}

std::string Value::toString() const {
    if (isString()) return asString();
    if (isInt64()) return std::to_string(asInt());
    if (isDouble()) {
        char buffer[64];
        std::snprintf(buffer, sizeof(buffer), "%g", asDouble());
        return buffer;
    }
    if (isBool()) return asBool() ? "true" : "false";
    return {};
}

}  // namespace eve
