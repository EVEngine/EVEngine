#include "combat/CombatCharacterPose.h"

#include <cmath>

namespace eve::combat {

Result<MeleePose> CombatCharacterPoseSource::pose(SubjectRef subject, std::string_view) const {
    if (!characters_)
        return Result<MeleePose>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "character pose source has no runtime", "characters"));
    auto state = characters_->state(subject);
    if (!state) return Result<MeleePose>::failure(state.status());
    MeleePose pose;
    pose.position   = {state.value().position.x, state.value().position.y + chestHeight_, state.value().position.z};
    pose.yawRadians = std::atan2(-state.value().facing.x, state.value().facing.z);
    return Result<MeleePose>::success(pose);
}

}  // namespace eve::combat
