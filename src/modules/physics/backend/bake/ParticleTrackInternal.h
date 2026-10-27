#pragma once
#include "physics/backend/bake/ParticleTrack.h"
namespace eve::physics {
struct ParticleTrack::Data {
    float                             duration;
    std::vector<ParticleTrajectory>   trajectories;
    std::shared_ptr<const BakeRecipe> recipe;
};
}  // namespace eve::physics
