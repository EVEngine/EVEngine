#pragma once
#include <map>
#include "common/Export.h"
#include "common/Result.h"
#include "physics/destruction/FractureRecipe.h"
namespace eve::physics::destruction_cook {
/** @brief Independent controls for mesh granularity, internal cohesion and parent-joint strength. */
struct FractureComponentPolicy {
    int   fragments          = 1;
    float cohesion           = 1.f;
    float connectionStrength = 1.f;
};
/** @brief Offline assembly settings; per-component policy overrides its authored cluster policy.
 * Source cluster ids name author-defined structural/material groups, not a universal building hierarchy.
 * @ownership Owns all policies. Cook borrows them synchronously and publishes owning data.
 * @thread Single caller while editing; immutable settings can be cooked concurrently.
 */
struct EVENGINE_API_DOMAINS AssemblyFractureSettings {
    FractureRecipe                         recipe;
    float                                  contactTolerance = .04f;
    std::map<int, FractureComponentPolicy> groups;
    std::map<int, FractureComponentPolicy> components;
    /** @brief Validate recipe, tolerances and all policies before offline cooking. */
    [[nodiscard]] Result<void> validate() const;
    /** @brief Set an authored cluster policy; invalid input leaves settings unchanged. */
    [[nodiscard]] Result<void> setGroupPolicy(int clusterId, FractureComponentPolicy policy);
    /** @brief Override one source component; invalid input leaves settings unchanged. */
    [[nodiscard]] Result<void> setComponentPolicy(int component, FractureComponentPolicy policy);
};
}  // namespace eve::physics::destruction_cook
