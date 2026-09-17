#pragma once

#include "animation/AnimMath.h"

#include <string>
#include <vector>

namespace eve::animation {

class AnimClip;
class AnimPose;
class AnimRetargetProfile;
class AnimSkeleton;

/** @brief Coarse body-part tag used to select MeshRet-style interaction pairs. */
enum class AnimSmrBodyPart {
    Torso = 0,
    Arm,
    Leg,
    Head,
    Other,
};

/**
 * @brief One semantically consistent sensor attached to a bone (classical SCS stand-in).
 *
 * MeshRet places sensors via bone-medial ray casts into the mesh. This engine port attaches
 * bind-local offsets to bones and tags them by body part so interaction pairs stay stable
 * across differently proportioned skeletons.
 */
struct AnimSmrSensor {
    int             boneIndex = -1;
    AnimSmrBodyPart part      = AnimSmrBodyPart::Other;
    float           localX    = 0.f;
    float           localY    = 0.f;
    float           localZ    = 0.f;
    std::string     semanticKey;  ///< Normalized bone name + sample id for cross-skeleton match.
};

/** @brief Two-bone IK chain used to realize an interaction correction. */
struct AnimSmrIkChain {
    std::string rootBone;
    std::string midBone;
    std::string tipBone;
};

/**
 * @brief Bone-attached sensor cloud for skinned-motion-retarget interaction queries.
 * Script type: `AnimSmrSensorCloud`.
 */
class AnimSmrSensorCloud {
public:
    /**
     * @brief Build sensors for every bone: one at the joint and one mid-segment toward the first child.
     * @param skeleton Non-null borrowed skeleton; not retained.
     * @throws Exception if skeleton is null.
     * @thread Owner thread.
     */
    static AnimSmrSensorCloud fromSkeleton(const AnimSkeleton* skeleton);

    /**
     * @brief Build denser MeshRet-style SCS rings around each bone segment.
     * @param ringsPerBone Number of rings along each parent→child segment (>=1).
     * @param pointsPerRing Samples around each ring (>=3).
     * @throws Exception if skeleton is null or ring counts are invalid.
     */
    static AnimSmrSensorCloud fromSkeletonDense(const AnimSkeleton* skeleton, int ringsPerBone, int pointsPerRing);

    int                  getSensorCount() const { return static_cast<int>(sensors_.size()); }
    const AnimSmrSensor& getSensor(int index) const;
    AnimSmrBodyPart      getSensorPart(int index) const;

    /**
     * @brief Evaluate world-space sensor positions for a pose that already has computeWorld() applied.
     * @param outXYZ Packed xyz triples; resized to getSensorCount()*3.
     * @throws Exception if pose is null.
     */
    void evaluateWorldPositions(const AnimPose* pose, std::vector<float>& outXYZ) const;

private:
    std::vector<AnimSmrSensor> sensors_;
};

/**
 * @brief Refine an FK-retargeted clip so source body-part proximity/contact is preserved on the target.
 *
 * Classical stand-in for MeshRet's DMI alignment: detect close sensor pairs on the source,
 * map them to the target, and pull tip bones with two-bone IK. Returns the number of
 * frames that received at least one chain correction.
 *
 * @throws Exception on null skeleton arguments.
 * @thread Owner thread; not re-entrant on the same targetClip.
 */
int smrRefineRetargetedClip(const AnimClip& sourceClip, AnimClip& targetClip, const AnimSkeleton* sourceSkeleton,
                            const AnimSkeleton* targetSkeleton, AnimRetargetProfile& profile);

}  // namespace eve::animation
