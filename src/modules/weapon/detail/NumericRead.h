#pragma once

#include "common/ValueRead.h"

#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace eve::weapon::detail {

/**
 * @brief Decode an optional finite number for the weapon codecs.
 * @param object Borrowed object, stable for this synchronous call and never retained.
 * @param name Borrowed field name, never retained.
 * @param path Borrowed diagnostic path, copied only on failure.
 * @param source Borrowed diagnostic producer name, copied only on failure.
 * @return Owning optional double; missing means nullopt, invalid means an InvalidArgument diagnostic.
 * @remarks Integers convert through double; Boolean and String are not coerced.
 * @note Caller synchronizes input access; no callbacks or shared mutable state, callable on any thread.
 */
[[nodiscard]] inline Result<std::optional<double>> readOptionalDouble(const Value::Object& object,
                                                                      std::string_view name, std::string_view path,
                                                                      std::string_view source) {
    const auto found = object.find(std::string(name));
    if (found == object.end()) return Result<std::optional<double>>::success(std::nullopt);
    auto parsed = readNumericDoubleValue(found->second);
    if (!parsed)
        return Result<std::optional<double>>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "Expected number", std::string(path), {}, std::string(source)));
    if (!std::isfinite(parsed.value()))
        return Result<std::optional<double>>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "Expected finite number", std::string(path), {}, std::string(source)));
    return Result<std::optional<double>>::success(parsed.value());
}

/**
 * @brief Decode an optional native int, also accepting integral finite Doubles, for the weapon codecs.
 * @param object Borrowed object, stable for this synchronous call and never retained.
 * @param name Borrowed field name, never retained.
 * @param path Borrowed diagnostic path, copied only on failure.
 * @param source Borrowed diagnostic producer name, copied only on failure.
 * @return Owning optional int; missing means nullopt, invalid means an InvalidArgument diagnostic.
 * @remarks Nonintegral/nonfinite Doubles fail before range validation; negative zero becomes zero.
 * @note Caller synchronizes input access; no callbacks or shared mutable state, callable on any thread.
 */
[[nodiscard]] inline Result<std::optional<int>> readOptionalInt(const Value::Object& object, std::string_view name,
                                                                std::string_view path, std::string_view source) {
    const auto found = object.find(std::string(name));
    if (found == object.end()) return Result<std::optional<int>>::success(std::nullopt);
    const auto failure = [&](std::string message) {
        return Result<std::optional<int>>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, std::move(message), std::string(path), {}, std::string(source)));
    };
    if (const auto* integer = found->second.getIf<std::int64_t>()) {
        if (*integer < std::numeric_limits<int>::min() || *integer > std::numeric_limits<int>::max())
            return failure("Integer out of range");
        return Result<std::optional<int>>::success(static_cast<int>(*integer));
    }
    if (const auto* number = found->second.getIf<double>()) {
        if (!std::isfinite(*number) || *number != std::floor(*number)) return failure("Expected integer");
        if (*number < static_cast<double>(std::numeric_limits<int>::min()) ||
            *number > static_cast<double>(std::numeric_limits<int>::max()))
            return failure("Integer out of range");
        return Result<std::optional<int>>::success(static_cast<int>(*number));
    }
    return failure("Expected integer");
}

}  // namespace eve::weapon::detail
