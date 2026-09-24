#pragma once

#include "common/Export.h"
#include "common/Result.h"

#include <cstdint>
#include <array>
#include <string>
#include <vector>

namespace eve {

/** @brief Engine-neutral description of one generated reflection or light probe. */
struct EVENGINE_API_FOUNDATION_INLINE ProcgenProbeDesc {
    std::uint64_t sourcePointId = 0;
    std::string resource;
    int type = 0;
    float x = 0, y = 0, z = 0;
    float extentX = 1, extentY = 1, extentZ = 1;
    int resolution = 128;
    float clipDistance = 1000;
    float shadowDistance = 80;
    /** Baked diffuse L0 irradiance used by generated light probes. */
    float irradianceR = 0.12F, irradianceG = 0.12F, irradianceB = 0.14F;
    /** Optional directional L0..L2 RGB coefficients; coefficient zero falls back to irradianceRGB. */
    std::array<float, 27> sphericalHarmonics{};
    bool hasSphericalHarmonics = false;
};

/**
 * @brief Optional graphics consumer for complete generated probe batches.
 * Implementations own all runtime resources and replace a batch atomically. Inputs are borrowed only for the call.
 */
class EVENGINE_API_FOUNDATION_INLINE IProcgenProbeSink {
public:
    static constexpr const char* capabilityName = "IProcgenProbeSink";
    virtual ~IProcgenProbeSink() = default;
    /** @brief Atomically create or reconcile a complete stable-identity probe batch. */
    [[nodiscard]] virtual Result<int> replaceProbeBatch(const std::string& batchId,
                                                         const std::vector<ProcgenProbeDesc>& probes) = 0;
    /** @brief Remove a published probe batch and all of its owned capture resources. */
    [[nodiscard]] virtual Result<int> removeProbeBatch(const std::string& batchId) = 0;
    /** @brief Advance bounded reflection capture and filtering work for all batches. */
    [[nodiscard]] virtual Result<int> tickProbeBatches(int faceBudget, int filterBudget, int filterSamples) = 0;
    /** @brief Return all probes currently owned by one batch. */
    [[nodiscard]] virtual int probeCount(const std::string& batchId) const = 0;
    /** @brief Return reflection probes currently owned by one batch. */
    [[nodiscard]] virtual int reflectionProbeCount(const std::string& batchId) const = 0;
    /** @brief Return light probes currently owned by one batch. */
    [[nodiscard]] virtual int lightProbeCount(const std::string& batchId) const = 0;
};

}  // namespace eve
