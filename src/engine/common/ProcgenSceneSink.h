#pragma once

#include "common/Export.h"
#include "common/Result.h"

#include <cstdint>
#include <string>
#include <vector>

namespace eve {

/** @brief Engine-neutral description of one procedural scene instance. */
struct EVENGINE_API ProcgenInstanceDesc {
    /** @brief Stable source PointId; zero denotes a legacy instance without delta identity. */
    uint64_t    sourcePointId = 0;
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

/** @brief Identity-based, revision-checked mutation of one procedural scene batch. */
struct EVENGINE_API ProcgenInstanceDelta {
    uint64_t                         baseRevision   = 0;
    uint64_t                         targetRevision = 0;
    std::vector<ProcgenInstanceDesc> added;
    std::vector<ProcgenInstanceDesc> updated;
    /** @brief Stable source PointIds removed from the target batch. */
    std::vector<uint64_t>            removedPointIds;
    /** @brief Compatibility removals addressed directly by Scene instance id. */
    std::vector<std::string>         removed;
    std::vector<std::string>         targetOrder;
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
    /**
     * @brief Apply one prevalidated identity delta atomically to an existing batch.
     * @return The committed target revision, or a structured stale/validation/provider failure.
     * @thread Called synchronously on the scene-owning thread; implementations must not retain references.
     * @reentrant Not reentrant for the same batch.
     */
    [[nodiscard]] virtual Result<uint64_t> applyDelta(const std::string& batchId,
                                                       const ProcgenInstanceDelta& delta) = 0;
    /** @brief Current committed revision of a batch, or zero when it is absent. */
    virtual uint64_t batchRevision(const std::string& batchId) const = 0;
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
