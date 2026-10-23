#pragma once
#include <cmath>
#include <glm/glm.hpp>
#include <stdexcept>
#include "common/Value.h"

namespace eve::fluids::ocean_codec {
inline void keys(const Value& value, std::initializer_list<const char*> names) {
    if (!value.isObject() || value.keys().size() != names.size()) throw std::runtime_error("object fields");
    for (const auto* name : names)
        if (!value.find(name)) throw std::runtime_error(name);
}
/** @brief Borrow a required child of value; throws on a missing field.
 * @param value Owner of the returned child; must outlive its use and remain unmodified.
 * @param name Non-null field name borrowed for this call only, never retained.
 * @return Borrowed reference valid until value is moved, structurally modified or destroyed.
 * @ownership The caller's value owns the result; no ownership transfer.
 * @thread Const/reentrant, no callbacks or retained input pointers.
 */
inline const Value& field(const Value& value, const char* name) {
    const auto* child = value.find(name);
    if (!child) throw std::runtime_error(name);
    return *child;
}
inline double number(const Value& value) {
    if (!value.isNumeric()) throw std::runtime_error("number type");
    const double result = value.isInt64() ? static_cast<double>(value.asInt()) : value.asDouble();
    if (!std::isfinite(result)) throw std::runtime_error("finite number");
    return result;
}
inline glm::dvec2 vector(const Value& value) {
    if (!value.isArray() || value.arraySize() != 2) throw std::runtime_error("two vector components");
    return {number(value.at(0)), number(value.at(1))};
}
inline Value      vector(glm::dvec2 value) { return Value::Array{Value(value.x), Value(value.y)}; }
inline glm::dvec3 vector3(const Value& value) {
    if (!value.isArray() || value.arraySize() != 3) throw std::runtime_error("three vector components");
    return {number(value.at(0)), number(value.at(1)), number(value.at(2))};
}
inline Value vector(glm::dvec3 value) { return Value::Array{Value(value.x), Value(value.y), Value(value.z)}; }
}  // namespace eve::fluids::ocean_codec
