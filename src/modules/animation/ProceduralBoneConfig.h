#pragma once

#include "common/Result.h"
#include "common/Value.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace eve::animation {
class AnimSkeleton;
class DynamicBoneSolver;
class FootIKSolver;

/** @brief Persisted particle profile for one dynamic-bone chain particle. */
struct DynamicBoneParticleConfig {
    float stiffness=0.5f,damping=0.7f,inertia=0.9f,gravityScale=1.f,radius=0.f;
};

/** @brief Persisted name-based dynamic-bone chain configuration. */
struct DynamicBoneChainConfig {
    std::string rootBone,endBone;
    float stiffness=0.5f,damping=0.7f,inertia=0.9f,gravityScale=1.f,radius=0.f;
    int iterations=4,freezeAxis=0,endMode=0;
    bool selfCollision=false;
    float endLength=0.f,endX=0.f,endY=0.f,endZ=0.f;
    std::vector<DynamicBoneParticleConfig> particles;
};

/** @brief Persisted fixed or bone-following sphere/capsule configuration. */
struct DynamicBoneColliderConfig {
    std::string bone;
    float startX=0.f,startY=0.f,startZ=0.f,endX=0.f,endY=0.f,endZ=0.f,radius=0.f;
    bool capsule=false,inside=false,enabled=true;
};

/** @brief Persisted name-based leg and optional toe mapping. */
struct FootIKLegConfig {
    std::string hip,knee,foot,toe;
    float soleOffset=0.f,toeSoleOffset=0.f;
};

/** @brief Persisted paired-foot IK settings; runtime providers are intentionally excluded. */
struct FootIKConfig {
    bool enabled=false,footLockEnabled=false;
    std::string pelvisBone;
    FootIKLegConfig left,right;
    float maxPelvisOffset=0.35f,minGroundNormalY=0.35f,positionResponse=18.f,rotationResponse=14.f;
    float groundStartHeight=0.5f,groundQueryDistance=1.5f,contactGraceTime=0.08f;
    float lockEnterWeight=0.8f,lockExitWeight=0.2f;
};

/** @brief Versioned, forward-compatible dynamic-bone asset definition. */
struct DynamicBoneConfig {
    static constexpr std::string_view SchemaId="eve.dynamic-bone";
    static constexpr std::uint32_t SchemaVersion=1;

    float gravityX=0.f,gravityY=-9.8f,gravityZ=0.f;
    float externalX=0.f,externalY=0.f,externalZ=0.f;
    float weight=1.f,updateRate=60.f,teleportThreshold=2.f,objectMoveResponse=1.f,distanceLimit=0.f;
    std::vector<DynamicBoneChainConfig> chains;
    std::vector<DynamicBoneColliderConfig> colliders;
    FootIKConfig footIK;
    Value::Object unknownFields;

    /** @brief Validate ranges and structural resource invariants. */
    [[nodiscard]] Result<void> validate() const;
    /** @brief Encode schema v1 while preserving unknown root fields. */
    [[nodiscard]] Result<Value> toValue() const;
    /** @brief Decode schema v1 transactionally; future versions are rejected. */
    [[nodiscard]] static Result<DynamicBoneConfig> fromValue(const Value& value);
    /** @brief Resolve bone names and atomically replace a solver configuration. */
    [[nodiscard]] Result<void> apply(AnimSkeleton& skeleton,DynamicBoneSolver& solver) const;
    /** @brief Resolve names and atomically replace both procedural solvers. */
    [[nodiscard]] Result<void> apply(AnimSkeleton& skeleton,DynamicBoneSolver& dynamicSolver,FootIKSolver& footSolver) const;
};
}
