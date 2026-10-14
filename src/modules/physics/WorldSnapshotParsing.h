#pragma once

#include "common/Result.h"
#include "common/Snapshot.h"

#include <initializer_list>
#include <string>
#include <string_view>

namespace eve::physics::snapshot_detail {
/** @brief Checks the strict snapshot field set without mutating the payload. */
bool hasExactFields(const eve::Value::Object& object, std::initializer_list<std::string_view> expected);
/**
 * @brief Finds a snapshot field without creating it.
 * @return Borrowed nullable pointer into object.
 * @ownership The caller owns object and the returned value.
 * @lifetime Until object is mutated or destroyed.
 * @thread Call while object is not being mutated on another thread.
 * @reentrancy No callbacks.
 */
const eve::Value* field(const eve::Value::Object& object, std::string_view name);
/** @brief Reads a required owning string. */
[[nodiscard]] eve::Result<std::string> readString(const eve::Value::Object& object, std::string_view name);
/** @brief Reads a required unsigned decimal integer string. */
[[nodiscard]] eve::Result<std::uint64_t> readUint64(const eve::Value::Object& object, std::string_view name);
/** @brief Reads a required signed decimal integer string. */
[[nodiscard]] eve::Result<std::int64_t> readInt64(const eve::Value::Object& object, std::string_view name);
/** @brief Reads a required finite number. */
[[nodiscard]] eve::Result<double> readNumber(const eve::Value::Object& object, std::string_view name);
/** @brief Reads a required finite number representable by float. */
[[nodiscard]] eve::Result<float> readFloat(const eve::Value::Object& object, std::string_view name);
/** @brief Reads a required boolean. */
[[nodiscard]] eve::Result<bool> readBool(const eve::Value::Object& object, std::string_view name);
}  // namespace eve::physics::snapshot_detail
