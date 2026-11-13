#pragma once
// Private canonical targeting field kernel shared by asset and execution persistence.
#include "common/Value.h"
#include "sensing/Targeting.h"
namespace eve::action::detail {
Value                          encodeActionTargeting(const sensing::TargetingSpec& spec);
Result<sensing::TargetingSpec> decodeActionTargeting(const Value& value);
}  // namespace eve::action::detail
