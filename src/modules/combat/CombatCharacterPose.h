#pragma once
#include "common/Export.h"

/** @file CombatCharacterPose.h @brief Character-runtime pose adapter for melee hitboxes. */

#include "combat/CombatCharacter.h"
#include "combat/MeleeHit.h"

namespace eve::combat {

/**
 * @brief IMeleePoseSource that samples CombatCharacterRuntime position and yaw.
 *
 * Hitbox local offsets stay in the melee catalog (local +Z is facing). This
 * adapter only supplies the character origin plus a chest-height lift so
 * sphere/capsule tests sit on the body.
 */
class EVENGINE_API_BACKENDS CombatCharacterPoseSource final : public IMeleePoseSource {
public:
    /** @brief Borrow a character runtime that must outlive this adapter. */
    explicit CombatCharacterPoseSource(const CombatCharacterRuntime& characters, double chestHeight = 1.0) noexcept
        : characters_(&characters), chestHeight_(chestHeight) {}

    /** @copydoc IMeleePoseSource::pose */
    [[nodiscard]] Result<MeleePose> pose(SubjectRef subject, std::string_view hitboxId) const override;

private:
    const CombatCharacterRuntime* characters_  = nullptr;
    double                        chestHeight_ = 1.0;
};

}  // namespace eve::combat
