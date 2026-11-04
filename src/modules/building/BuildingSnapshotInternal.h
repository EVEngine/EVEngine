#pragma once
#include "building/BuildingSnapshot.h"
namespace eve::building::codec {
/** @brief Borrow an optional object field. @return Borrowed pointer owned by value, or null.
 * @lifetime Valid until value is mutated or destroyed; never retained by codecs.
 * @thread Caller thread; no callbacks or mutation. */
const Value* field(const Value& value, const char* key);
Value        numberArray(const std::vector<double>& values);
}  // namespace eve::building::codec
