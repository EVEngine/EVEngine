#pragma once
#include "common/Export.h"

#include "animation/AnimSmr.h"

#include <vector>

namespace eve::animation {

class AnimClip;
class AnimSkeleton;
class AnimSkin;

/** @brief Packed MeshRet-style features for one retarget inference window. */
struct SmrFeatureBatch {
    int frames  = 0;
    int joints  = 0;
    int sensors = 0;
    int pairs   = 0;

    std::vector<float> sourceRot6d;  ///< [T*J*6]
    std::vector<float> sourceGeom;   ///< [S*7]
    std::vector<float> targetGeom;   ///< [S*7]
    std::vector<float> sourceDmi;    ///< [T*P*10]
    std::vector<int>   jointMap;     ///< target joint -> source joint (-1 if unmatched)
};

/**
 * @brief Build dense SCS + DMI tensors from skeletons/clip (no tensor headers).
 * @param sourceSkeleton Borrowed; must outlive the call. Required.
 * @param targetSkeleton Borrowed; must outlive the call. Required.
 * @param sourceSkin Optional borrowed LBS skin for denser SCS; nullptr skips skin rings.
 * @param targetSkin Optional borrowed LBS skin; nullptr skips skin rings.
 * @return Owning packed feature batch.
 * @throws Exception on null required skeletons or empty clip.
 * @ownership Does not retain any input pointers after return.
 * @lifetime All raw pointers are borrowed for the duration of this call only.
 * @thread Owner thread.
 */
EVENGINE_API_ORCHESTRATION SmrFeatureBatch buildSmrFeatures(const AnimClip&     sourceClip,
                                                            const AnimSkeleton* sourceSkeleton,
                                                            const AnimSkeleton* targetSkeleton,
                                                            const AnimSkin* sourceSkin, const AnimSkin* targetSkin,
                                                            int ringsPerBone = 2, int pointsPerRing = 4);

/** @brief Convert unit quaternion to MeshRet-style rotation-6D. */
EVENGINE_API_ORCHESTRATION void quatToRot6d(float qx, float qy, float qz, float qw, float out[6]);

/** @brief Convert rotation-6D back to a unit quaternion. */
EVENGINE_API_ORCHESTRATION void rot6dToQuat(const float in[6], float& qx, float& qy, float& qz, float& qw);

}  // namespace eve::animation
