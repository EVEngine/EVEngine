#pragma once

#include "common/Json.h"
#include "common/Value.h"

namespace eve {

/**
 * @brief Read a required string field, including an empty string, as an owning value.
 * @param object Borrowed object, stable during this synchronous call; never retained.
 * @param name Borrowed field name, never retained.
 * @return Copied string, or ParseError for a missing or non-string field.
 * @cost One object lookup and a copy linear in field bytes per call.
 * @note Caller synchronizes object access; no callbacks or shared mutable state.
 */
[[nodiscard]] EVENGINE_API_FOUNDATION Result<std::string> readStringMember(const Value::Object& object,
                                                                           std::string_view     name);

/**
 * @brief Read an Int64 value as a range-checked native int without numeric coercion.
 * @param value Borrowed stable input, used synchronously and never retained.
 * @return Independent integer, or TypeMismatch for another kind and InvalidArgument for overflow.
 * @remarks Doubles, booleans and numeric strings are rejected, even if they represent an integer.
 * @note Callable on any thread with stable input; no callbacks or shared mutable state.
 */
[[nodiscard]] EVENGINE_API_FOUNDATION Result<int> readIntValue(const Value& value);

/**
 * @brief Read a Bool without coercing integers, strings or other kinds.
 * @param value Borrowed stable input, used synchronously and never retained.
 * @return Independent Boolean payload, including false, or TypeMismatch for another kind.
 * @note Callable on any thread with stable input; no callbacks or shared mutable state.
 */
[[nodiscard]] EVENGINE_API_FOUNDATION Result<bool> readBoolValue(const Value& value);

/**
 * @brief Read an Int64 without narrowing or numeric coercion.
 * @param value Borrowed stable input, used synchronously and never retained.
 * @return Independent signed integer, or TypeMismatch for another kind.
 * @note Callable on any thread with stable input; no callbacks or shared mutable state.
 */
[[nodiscard]] EVENGINE_API_FOUNDATION Result<std::int64_t> readInt64Value(const Value& value);

/**
 * @brief Read an Int64 as a range-checked UInt32 without numeric coercion.
 * @param value Borrowed stable input, used synchronously and never retained.
 * @return Independent UInt32, or TypeMismatch for another kind and InvalidArgument for a negative/large integer.
 * @note Callable on any thread with stable input; no callbacks or shared mutable state.
 */
[[nodiscard]] EVENGINE_API_FOUNDATION Result<std::uint32_t> readUInt32Value(const Value& value);

/**
 * @brief Read an Int64 or Double as a double without imposing a finite-value policy.
 * @param value Borrowed stable input, used synchronously and never retained.
 * @return Independent double, or TypeMismatch for all other kinds.
 * @remarks Int64 uses static_cast<double>; Double preserves negative zero, NaN and infinities.
 *          Consumers validate finiteness after applying their own assignment/diagnostic policy.
 * @note Callable on any thread with stable input; no callbacks or shared mutable state.
 */
[[nodiscard]] EVENGINE_API_FOUNDATION Result<double> readNumericDoubleValue(const Value& value);

/**
 * @brief Convert an Integer or Double to a finite Float after validating its source range.
 * @param value Borrowed stable input, used synchronously and never retained.
 * @return Independent float, or TypeMismatch for another kind and InvalidArgument for nonfinite/out-of-range input.
 * @remarks Int64 conversion intentionally goes through double before float, preserving the existing codec rounding.
 *          Underflow to zero is accepted, and negative zero is preserved. No caller state changes on failure.
 * @note Callable on any thread with stable input; no callbacks or shared mutable state.
 */
[[nodiscard]] EVENGINE_API_FOUNDATION Result<float> readFiniteFloatValue(const Value& value);

/**
 * @brief Scan an unsigned decimal prefix from a required nonempty String value.
 * @param value Borrowed stable input, used synchronously and never retained.
 * @return Owning integer prefix and input lengths, or ParseError for invalid kind/input/overflow.
 * @remarks No whitespace or signs are accepted. Consumers must compare consumed to size for full parsing.
 * @note Callable on any thread with stable input; no callbacks or shared mutable state.
 */
[[nodiscard]] EVENGINE_API_FOUNDATION Result<json::UnsignedIntegerScan> scanUnsignedStringValue(const Value& value);

/**
 * @brief Read a required nonempty string field as an independent owning value.
 * @param object Borrowed object, stable for this synchronous call only; never retained.
 * @param name Borrowed field name, never retained.
 * @return Copied string, or ParseError for a missing, empty or non-string field.
 * @cost One object lookup and a copy linear in field bytes per call.
 * @note Caller synchronizes object access; no callbacks or shared mutable state.
 */
[[nodiscard]] EVENGINE_API_FOUNDATION Result<std::string> readNonEmptyStringMember(const Value::Object& object,
                                                                                   std::string_view     name);

/**
 * @brief Scan a required nonempty decimal-string field using from_chars semantics.
 * @param object Borrowed object, stable during the call; never retained.
 * @param name Borrowed field name, never retained.
 * @return Owning integer prefix and consumed length, or ParseError for invalid type/input/overflow.
 * @remarks Compatibility adapters compare consumed to the field length after assigning the prefix.
 * @note Synchronous on the caller thread; caller synchronizes object access; no callbacks.
 */
[[nodiscard]] EVENGINE_API_FOUNDATION Result<json::UnsignedIntegerScan> scanUnsignedStringMember(
    const Value::Object& object, std::string_view name);

}  // namespace eve
