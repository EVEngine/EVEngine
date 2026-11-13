#pragma once
// Private finite World2D coordinate admission, shared by regions and transient activation input.
#include "spawn/SpawnDecode.inl"
#include "spawn/SpawnMap.h"
namespace eve::spawn::detail {
inline double coordinate(const Reader& reader) {
    if (!reader.value.isNumeric()) reject(reader.path, "Expected finite coordinate");
    const double result = reader.value.isInt64() ? static_cast<double>(reader.value.asInt()) : reader.value.asDouble();
    if (!std::isfinite(result)) reject(reader.path, "Expected finite coordinate");
    return result;
}
inline Point point(const Reader& reader) {
    const auto& values = reader.array();
    if (values.size() != 2) reject(reader.path, "World2D point requires exactly two coordinates");
    return {coordinate({values[0], reader.path + "[0]"}), coordinate({values[1], reader.path + "[1]"})};
}
}  // namespace eve::spawn::detail
