#include "common/Value.h"
#include "common/Assert.h"
#include "common/ValueRead.h"

#include "common/Json.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

#if defined(_WIN32)
template class EVENGINE_API_FOUNDATION std::vector<eve::Value>;
template class EVENGINE_API_FOUNDATION std::map<std::string, eve::Value>;
#else
template class std::vector<eve::Value>;
template class std::map<std::string, eve::Value>;
#endif

namespace eve {

Result<std::string> readStringMember(const Value::Object& object, std::string_view name) {
    const auto  found = object.find(std::string(name));
    const auto* text  = found == object.end() ? nullptr : found->second.getIf<std::string>();
    if (!text)
        return Result<std::string>::failure(Diagnostic::error(DiagnosticCode::ParseError, "expected a string field"));
    return Result<std::string>::success(*text);
}

Result<std::string> readNonEmptyStringMember(const Value::Object& object, std::string_view name) {
    auto text = readStringMember(object, name);
    if (!text || text.value().empty())
        return Result<std::string>::failure(
            Diagnostic::error(DiagnosticCode::ParseError, "expected a nonempty string field"));
    return text;
}

Result<int> readIntValue(const Value& value) {
    const auto* integer = value.getIf<std::int64_t>();
    if (!integer)
        return Result<int>::failure(Diagnostic::error(DiagnosticCode::TypeMismatch, "expected an Int64 value"));
    if (*integer < std::numeric_limits<int>::min() || *integer > std::numeric_limits<int>::max())
        return Result<int>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "integer exceeds int range"));
    return Result<int>::success(static_cast<int>(*integer));
}

Result<bool> readBoolValue(const Value& value) {
    const auto* boolean = value.getIf<bool>();
    if (!boolean)
        return Result<bool>::failure(Diagnostic::error(DiagnosticCode::TypeMismatch, "expected a Bool value"));
    return Result<bool>::success(*boolean);
}

Result<std::int64_t> readInt64Value(const Value& value) {
    const auto* integer = value.getIf<std::int64_t>();
    if (!integer)
        return Result<std::int64_t>::failure(
            Diagnostic::error(DiagnosticCode::TypeMismatch, "expected an Int64 value"));
    return Result<std::int64_t>::success(*integer);
}

Result<std::uint32_t> readUInt32Value(const Value& value) {
    auto integer = readInt64Value(value);
    if (!integer) return Result<std::uint32_t>::failure(integer.status());
    if (integer.value() < 0 || static_cast<std::uint64_t>(integer.value()) > std::numeric_limits<std::uint32_t>::max())
        return Result<std::uint32_t>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "integer exceeds UInt32 range"));
    return Result<std::uint32_t>::success(static_cast<std::uint32_t>(integer.value()));
}

Result<double> readNumericDoubleValue(const Value& value) {
    if (const auto* integer = value.getIf<std::int64_t>())
        return Result<double>::success(static_cast<double>(*integer));
    if (const auto* number = value.getIf<double>()) return Result<double>::success(*number);
    return Result<double>::failure(Diagnostic::error(DiagnosticCode::TypeMismatch, "expected Int64 or Double"));
}

Result<float> readFiniteFloatValue(const Value& value) {
    double number = 0.0;
    if (const auto* integer = value.getIf<std::int64_t>())
        number = static_cast<double>(*integer);
    else if (const auto* real = value.getIf<double>())
        number = *real;
    else
        return Result<float>::failure(Diagnostic::error(DiagnosticCode::TypeMismatch, "expected Integer or Double"));
    const double maximum = static_cast<double>(std::numeric_limits<float>::max());
    if (!std::isfinite(number) || number < -maximum || number > maximum)
        return Result<float>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "number exceeds finite Float range"));
    return Result<float>::success(static_cast<float>(number));
}

Result<json::UnsignedIntegerScan> scanUnsignedStringValue(const Value& value) {
    const auto* text = value.getIf<std::string>();
    if (!text || text->empty())
        return Result<json::UnsignedIntegerScan>::failure(
            Diagnostic::error(DiagnosticCode::ParseError, "expected a nonempty unsigned decimal string"));
    return json::scanUnsignedInteger(*text, json::UnsignedIntegerSyntax::Decimal);
}

Result<json::UnsignedIntegerScan> scanUnsignedStringMember(const Value::Object& object, std::string_view name) {
    auto text = readNonEmptyStringMember(object, name);
    if (!text) return Result<json::UnsignedIntegerScan>::failure(text.status());
    return json::scanUnsignedInteger(text.value(), json::UnsignedIntegerSyntax::Decimal);
}

Value::Value() noexcept = default;
Value::Value(std::nullptr_t) {}
Value::Value(bool value) : storage_(value) {}
Value::Value(std::int64_t value) : storage_(value) {}
Value::Value(int value) : storage_(static_cast<std::int64_t>(value)) {}
Value::Value(double value) : storage_(value) {}
Value::Value(float value) : storage_(static_cast<double>(value)) {}
Value::Value(const char* value) : storage_(std::string(value ? value : "")) {}
Value::Value(std::string value) : storage_(std::move(value)) {}
Value::Value(Array value) : storage_(std::move(value)) {}
Value::Value(Object value) : storage_(std::move(value)) {}
Value::~Value()                                 = default;
Value::Value(const Value& other)                = default;
Value::Value(Value&& other) noexcept            = default;
Value& Value::operator=(const Value& other)     = default;
Value& Value::operator=(Value&& other) noexcept = default;
bool   Value::operator==(const Value& other) const { return storage_ == other.storage_; }

Value Value::array(Array value) { return Value(std::move(value)); }
Value Value::object(Object value) { return Value(std::move(value)); }
Value Value::array(std::initializer_list<Value> values) { return Value(Array(values)); }
Value Value::object(std::initializer_list<Object::value_type> members) { return Value(Object(members)); }

namespace {

Result<Value> copyJsonValue(const json::Value& source) {
    if (source.isNull()) return Result<Value>::success(Value());
    if (source.isBool()) return Result<Value>::success(Value(source.asBool()));
    if (source.isString()) return Result<Value>::success(Value(source.asString()));

    if (source.isNumber()) {
        if (source.isIntegerLiteral()) {
            if (!source.isInt64()) {
                return Result<Value>::failure(
                    Diagnostic::error(DiagnosticCode::ParseError, "JSON integer is outside the canonical Int64 range"));
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

    return Result<Value>::failure(Diagnostic::error(DiagnosticCode::ParseError, "unsupported JSON value kind"));
}

}  // namespace

Result<Value> Value::fromJson(std::string_view jsonText) {
    const std::string input(jsonText);
    std::string       error;
    json::Document    document = json::Document::parse(input, &error);
    if (!document.valid()) {
        return Result<Value>::failure(Diagnostic::error(DiagnosticCode::ParseError, error));
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

#if defined(_WIN32) && defined(EVENGINE_MODULE_DLL)
template __declspec(dllexport) std::monostate* eve::Value::getIf<std::monostate>() noexcept;
template __declspec(dllexport) const std::monostate* eve::Value::getIf<std::monostate>() const noexcept;
template __declspec(dllexport) bool*                 eve::Value::getIf<bool>() noexcept;
template __declspec(dllexport) const bool*           eve::Value::getIf<bool>() const noexcept;
template __declspec(dllexport) std::int64_t* eve::Value::getIf<std::int64_t>() noexcept;
template __declspec(dllexport) const std::int64_t* eve::Value::getIf<std::int64_t>() const noexcept;
template __declspec(dllexport) double*             eve::Value::getIf<double>() noexcept;
template __declspec(dllexport) const double*       eve::Value::getIf<double>() const noexcept;
template __declspec(dllexport) std::string* eve::Value::getIf<std::string>() noexcept;
template __declspec(dllexport) const std::string* eve::Value::getIf<std::string>() const noexcept;
template __declspec(dllexport) eve::Value::Array* eve::Value::getIf<eve::Value::Array>() noexcept;
template __declspec(dllexport) const eve::Value::Array* eve::Value::getIf<eve::Value::Array>() const noexcept;
template __declspec(dllexport) eve::Value::Object* eve::Value::getIf<eve::Value::Object>() noexcept;
template __declspec(dllexport) const eve::Value::Object* eve::Value::getIf<eve::Value::Object>() const noexcept;
#endif
