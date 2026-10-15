#pragma once
#include <box3d/id.h>
#include "common/Result.h"
#include "common/Value.h"
namespace eve::physics::snapshot_detail {
/** @brief Capture solver-owned parameters on the physics owner thread into an owning value. */
Value captureJoint3DParameters(b3JointId id);
/** @brief Validate the complete parameter object before updating a live candidate joint; no retained references. */
[[nodiscard]] Result<void> restoreJoint3DParameters(b3JointId id, const Value& value);
}  // namespace eve::physics::snapshot_detail
