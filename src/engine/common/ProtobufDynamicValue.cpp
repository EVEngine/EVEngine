#include "common/ProtobufDynamicValue.h"

#include "dynamic_value.pb.h"

#include <cmath>
#include <utility>

namespace eve::proto_support {
namespace {

constexpr unsigned kMaxDepth = 100;

bool encode(const Value& source, persistence::proto::DynamicValue& target, unsigned depth) {
    if (depth > kMaxDepth) return false;
    if (source.isNull())
        target.set_null_value(true);
    else if (source.isBool())
        target.set_bool_value(source.asBool());
    else if (source.isInt64())
        target.set_int_value(source.asInt());
    else if (source.isDouble()) {
        if (!std::isfinite(source.asDouble())) return false;
        target.set_double_value(source.asDouble());
    } else if (source.isString())
        target.set_string_value(source.asString());
    else if (const auto* array = source.getIf<Value::Array>()) {
        auto* list = target.mutable_list_value();
        for (const auto& element : *array)
            if (!encode(element, *list->add_values(), depth + 1)) return false;
    } else if (const auto* object = source.getIf<Value::Object>()) {
        auto* saved = target.mutable_object_value();
        for (const auto& [key, value] : *object) {
            auto* field = saved->add_fields();
            field->set_key(key);
            if (!encode(value, *field->mutable_value(), depth + 1)) return false;
        }
    } else
        return false;
    return true;
}

Result<Value> decode(const persistence::proto::DynamicValue& source, unsigned depth) {
    const auto invalid = [] {
        return Result<Value>::failure(Diagnostic::error(DiagnosticCode::ParseError, "invalid dynamic Protobuf value"));
    };
    if (depth > kMaxDepth) return invalid();
    switch (source.kind_case()) {
        case persistence::proto::DynamicValue::kNullValue:
            return source.null_value() ? Result<Value>::success(Value()) : invalid();
        case persistence::proto::DynamicValue::kBoolValue: return Result<Value>::success(Value(source.bool_value()));
        case persistence::proto::DynamicValue::kIntValue: return Result<Value>::success(Value(source.int_value()));
        case persistence::proto::DynamicValue::kDoubleValue:
            return std::isfinite(source.double_value()) ? Result<Value>::success(Value(source.double_value()))
                                                        : invalid();
        case persistence::proto::DynamicValue::kStringValue:
            return Result<Value>::success(Value(source.string_value()));
        case persistence::proto::DynamicValue::kListValue: {
            Value::Array values;
            values.reserve(source.list_value().values_size());
            for (const auto& element : source.list_value().values()) {
                auto decoded = decode(element, depth + 1);
                if (!decoded.ok()) return decoded;
                values.push_back(std::move(decoded).takeValue());
            }
            return Result<Value>::success(Value(std::move(values)));
        }
        case persistence::proto::DynamicValue::kObjectValue: {
            Value::Object values;
            for (const auto& field : source.object_value().fields()) {
                if (!field.has_key() || !field.has_value()) return invalid();
                auto decoded = decode(field.value(), depth + 1);
                if (!decoded.ok() || !values.emplace(field.key(), std::move(decoded).takeValue()).second)
                    return invalid();
            }
            return Result<Value>::success(Value(std::move(values)));
        }
        default: return invalid();
    }
}

}  // namespace

Result<void> encodeDynamicValue(const Value& source, persistence::proto::DynamicValue& target) {
    persistence::proto::DynamicValue candidate;
    if (!encode(source, candidate, 0))
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::SerializationError, "unsupported or too-deep dynamic Protobuf value"));
    target.Swap(&candidate);
    return Result<void>::success();
}

Result<Value> decodeDynamicValue(const persistence::proto::DynamicValue& source) { return decode(source, 0); }

}  // namespace eve::proto_support
