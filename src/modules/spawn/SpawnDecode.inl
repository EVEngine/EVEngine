#pragma once

// Private admission helpers, shared by the canonical template and map codecs.
#include "spawn/SpawnDefinition.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace eve::spawn::detail {
// Admission-only parser: exceptions stay inside compileTemplate and never carry partial state out.
struct InvalidInput {
    Diagnostic diagnostic;
};
[[noreturn]] inline void reject(const std::string& path, std::string message,
                                DiagnosticCode code = DiagnosticCode::InvalidArgument) {
    throw InvalidInput{Diagnostic::error(code, std::move(message), path)};
}
struct Reader {
    const Value& value;
    std::string  path;
    void         fields(std::initializer_list<std::string_view> names) const {
        if (!value.isObject()) reject(path, "Expected an object");
        for (const auto& key : value.keys()) {
            if (std::find(names.begin(), names.end(), key) == names.end()) reject(path + "." + key, "Unknown field");
        }
    }
    Reader at(const std::string& name) const {
        const auto* item = value.find(name);
        if (item == nullptr) reject(path + "." + name, "Required field is missing");
        return {*item, path + "." + name};
    }
    std::string text() const {
        if (!value.isString() || value.asString().empty()) reject(path, "Expected non-empty text");
        return value.asString();
    }
    void literal(std::string_view expected) const {
        if (text() != expected) reject(path, "Unsupported value", DiagnosticCode::Unsupported);
    }
    std::uint64_t integer64() const {
        if (!value.isInt64() || value.asInt() <= 0) reject(path, "Expected positive integer");
        return static_cast<std::uint64_t>(value.asInt());
    }
    std::uint32_t integer(bool allowZero = false) const {
        if (!value.isInt64() || value.asInt() < (allowZero ? 0 : 1) ||
            value.asInt() > std::numeric_limits<std::uint32_t>::max())
            reject(path, "Expected bounded unsigned integer");
        return static_cast<std::uint32_t>(value.asInt());
    }
    double number() const {
        if (!value.isNumeric()) reject(path, "Expected a finite non-negative number");
        const double result = value.isInt64() ? static_cast<double>(value.asInt()) : value.asDouble();
        if (!std::isfinite(result) || result < 0) reject(path, "Expected a finite non-negative number");
        return result;
    }
    Duration duration(bool positive = false) const {
        auto converted = Duration::fromSeconds(number());
        if (!converted) reject(path, "Duration is out of range");
        auto result = std::move(converted).takeValue();
        if (positive && result.isZero()) reject(path, "Duration must be at least one nanosecond");
        return result;
    }
    const Value::Array& arrayValues() const {
        const auto* result = value.getIf<Value::Array>();
        if (result == nullptr) reject(path, "Expected array");
        return *result;
    }
    const Value::Array& array() const {
        const auto* result = value.getIf<Value::Array>();
        if (result == nullptr || result->empty()) reject(path, "Expected non-empty array");
        return *result;
    }
};
inline std::string identifier(const Reader& reader) {
    auto text   = reader.text();
    auto parsed = DefinitionRef::parse("spawn:" + text);
    if (!parsed) reject(reader.path, "Invalid stable identifier");
    return text;
}
}  // namespace eve::spawn::detail
