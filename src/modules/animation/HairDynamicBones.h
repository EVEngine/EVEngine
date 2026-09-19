#pragma once
#include "common/Export.h"

#include "common/Result.h"

#include <string>
#include <vector>

namespace eve::animation {

class DynamicBoneSolver;

/**
 * @brief Convenience helpers for hair/tail/accessory spring chains on DynamicBoneSolver.
 *
 * This is the recommended path for card-based hair motion (guide bones), not strand
 * simulation. Graphics never includes this header — sample bone positions in avatar/
 * script code and feed card meshes separately.
 */
namespace hair {

/** @brief One named spring chain with game-friendly hair defaults. */
struct ChainDesc {
    std::string rootBone;
    std::string tipBone;
    float stiffness = 0.12f;
    float damping = 0.18f;
    float inertia = 0.8f;
    float gravityScale = 1.f;
    float radius = 0.02f;
    int iterations = 4;
    /** @brief Virtual tip length past the last bone; <=0 disables. */
    float endLength = 0.08f;
    bool selfCollision = true;
};

/**
 * @brief Add one hair/tail chain by bone name.
 * @return Chain index on success.
 */
[[nodiscard]] EVENGINE_API_WORLD Result<int> addChain(DynamicBoneSolver &solver, const ChainDesc &desc);

/**
 * @brief Replace or append hair chains.
 * @param clearFirst When true, clears existing chains before adding.
 */
[[nodiscard]] EVENGINE_API_WORLD Result<void> setupChains(DynamicBoneSolver &solver,
                                       const std::vector<ChainDesc> &chains,
                                       bool clearFirst = true);

/**
 * @brief Attach a bone-following sphere collider (typically the head) for hair chains.
 * @return Collider count after add, or failure when the bone cannot be resolved.
 */
[[nodiscard]] EVENGINE_API_WORLD Result<int> addHeadCollider(DynamicBoneSolver &solver, const std::string &boneName,
                                          float radius = 0.12f, float offsetX = 0.f,
                                          float offsetY = 0.f, float offsetZ = 0.f);

}  // namespace hair

}  // namespace eve::animation
