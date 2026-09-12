#pragma once

#include "common/Result.h"

#include <cstdint>
#include <vector>

namespace eve {

/** @brief Operation executed by an optional mesh-deformation compute provider. */
enum class MeshDeformationComputeOperation : std::uint8_t { Inflate, Dent, Flatten, Directional, Smooth, Impact };

/**
 * @brief Owning, backend-neutral input for one synchronous GPU mesh deformation.
 *
 * Positions, normals, baseline, and targets contain tightly packed xyz triples. Baseline is
 * required by Impact and targets by Smooth. The provider retains no request memory.
 */
struct MeshDeformationComputeRequest {
    MeshDeformationComputeOperation operation = MeshDeformationComputeOperation::Inflate;
    std::vector<float> positions;
    std::vector<float> normals;
    std::vector<float> baseline;
    std::vector<float> targets;
    float centerX = 0.f, centerY = 0.f, centerZ = 0.f;
    float radius = 1.f;
    float strength = 0.f;
    float falloff = 1.f;
    float directionX = 0.f, directionY = 1.f, directionZ = 0.f;
    float maxDisplacement = 1.f;
};

/**
 * @brief Optional synchronous device-thread service for mesh sculpt and damage compute.
 *
 * Implementations own all temporary GPU resources and release them before returning. Calls are
 * device-thread-only, non-reentrant, and never invoke user callbacks.
 */
class IMeshDeformationCompute {
public:
    static constexpr const char* capabilityName = "eve.mesh-deformation-compute.v1";
    virtual ~IMeshDeformationCompute() = default;
    /** @brief Execute one request and return tightly packed xyz positions. */
    [[nodiscard]] virtual Result<std::vector<float>> deform(MeshDeformationComputeRequest request) = 0;
};

}  // namespace eve
