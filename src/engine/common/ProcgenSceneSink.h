#pragma once

#include "common/Export.h"

#include <cstdint>
#include <string>
#include <vector>

namespace eve {

/** @brief Engine-neutral description of one procedural scene instance. */
struct EVENGINE_API ProcgenInstanceDesc {
    std::string id;
    std::string asset;
    float       x      = 0.f;
    float       y      = 0.f;
    float       z      = 0.f;
    float       yaw    = 0.f;
    float       scaleX = 1.f;
    float       scaleY = 1.f;
    float       scaleZ = 1.f;
    uint32_t    seed   = 1;
};

/**
 * @brief Optional scene consumer for procedural instance batches.
 *
 * The interface lives in common so procgen can publish results without an
 * upward dependency on scene. Scene providers must replace a batch atomically
 * and preserve stable instance ids across rebuilds.
 */
class EVENGINE_API IProcgenSceneSink {
public:
    static constexpr const char* capabilityName = "IProcgenSceneSink";

    virtual ~IProcgenSceneSink() = default;
    /** @brief Create or reconcile all instances in a named batch. */
    virtual bool applyBatch(const std::string& batchId,
                            const std::vector<ProcgenInstanceDesc>& instances) = 0;
    /** @brief Remove the visible contents of a batch. */
    virtual bool removeBatch(const std::string& batchId) = 0;
    /** @brief Number of currently published instances in a batch. */
    virtual int instanceCount(const std::string& batchId) const = 0;
    /** @brief Instances newly created by the batch's latest successful apply. */
    virtual int lastCreatedCount(const std::string& batchId) const = 0;
    /** @brief Stable-id instances reused by the batch's latest successful apply. */
    virtual int lastReusedCount(const std::string& batchId) const = 0;
    /** @brief Instances removed by the batch's latest successful apply or removal. */
    virtual int lastRemovedCount(const std::string& batchId) const = 0;
};

}  // namespace eve
