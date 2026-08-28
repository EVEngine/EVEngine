#pragma once

/**
 * @file ArtifactPublish.h
 * @brief Capability-driven, transactional publication of generated artifacts.
 */

#include "common/Result.h"
#include "common/Snapshot.h"
#include "procgen/GeneratedArtifact.h"

#include <memory>

namespace eve::procgen {

// The low-level contracts live in common so scene/map/graphics/physics can
// implement them without a reverse include of procgen. These aliases preserve
// the established procgen-facing source API.
using PreparedArtifactPublish  = eve::artifact::PreparedPublication;
using ISceneArtifactAdapter    = eve::artifact::ISceneArtifactAdapter;
using IMapArtifactAdapter      = eve::artifact::IMapArtifactAdapter;
using IGraphicsArtifactAdapter = eve::artifact::IGraphicsArtifactAdapter;
using IPhysicsArtifactAdapter  = eve::artifact::IPhysicsArtifactAdapter;
using ArtifactPublishOptions   = eve::artifact::PublishOptions;

/** @brief Compatibility receipt retaining procgen ArtifactId at this boundary. */
struct ArtifactPublishReceipt {
    ArtifactId id;
    bool       scenePublished    = false;
    bool       graphicsPublished = false;
    bool       physicsPublished  = false;
    bool       mapPublished      = false;
};

/** @brief Snapshot context supplied by the caller; time and hashing are injected. */
struct ArtifactSnapshotContext {
    eve::PersistentId         instanceId;
    eve::Revision             revision;
    eve::SimulationTick       tick;
    eve::SnapshotHashProvider hashProvider;
};

/**
 * @brief Coordinates adapter preparation and atomic CPU-store publication.
 * @note Main/simulation thread only. Adapter callbacks must not re-enter this
 *       publisher for the same ArtifactStore.
 */
class ArtifactPublisher {
public:
    /** @brief Bind a publisher to a store it does not own. */
    explicit ArtifactPublisher(ArtifactStore& store) noexcept : store_(store) {}

    /**
     * @brief Publish through required capabilities and then atomically into the store.
     * @param artifact Owning CPU artifact.
     * @param options Required scene/graphics/physics adapters.
     * @return Receipt, Unsupported for a missing capability, or provider/store failure.
     * @remarks Any Result failure rolls back every prepared adapter and leaves
     *          the ArtifactStore unchanged.
     */
    [[nodiscard]] eve::Result<ArtifactPublishReceipt> publish(GeneratedArtifact      artifact,
                                                              ArtifactPublishOptions options);

    /**
     * @brief Capture store and registered provider state in a sealed snapshot.
     * @param context Stable instance/revision/tick and injected content hash.
     * @return Versioned snapshot, or Unsupported when a requested provider is absent.
     */
    [[nodiscard]] eve::Result<eve::SnapshotEnvelope> snapshot(const ArtifactSnapshotContext& context) const;

    /**
     * @brief Restore store and provider state transactionally from a sealed snapshot.
     * @param snapshot Verified artifact publication snapshot.
     * @param hashProvider Hash provider used to verify the envelope.
     * @return Applied or a structured validation/provider error.
     */
    [[nodiscard]] eve::Result<void> restore(const eve::SnapshotEnvelope&     snapshot,
                                            const eve::SnapshotHashProvider& hashProvider);

private:
    ArtifactStore& store_;
};

}  // namespace eve::procgen
