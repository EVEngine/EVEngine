#pragma once

/**
 * @file ArtifactProvider.h
 * @brief Scene-owned CPU registry provider for generated artifacts.
 */

#include "common/ArtifactPublication.h"
#include "common/RuntimeHandle.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace eve::scene {

/** @brief Owner tag for scene artifact runtime handles. */
struct SceneArtifactHandleTag {};
/** @brief Generation-qualified handle for a scene artifact record. */
using SceneArtifactHandle = eve::RuntimeHandle<SceneArtifactHandleTag>;

/** @brief Backend-neutral node observable emitted for one artifact part. */
struct SceneArtifactNode {
    std::string           role;
    eve::artifact::Bounds bounds;
};

/** @brief Queryable scene registry record for one published artifact. */
struct SceneArtifactRecord {
    eve::PersistentId              id;
    std::string                    buildKey;
    SceneArtifactHandle            handle;
    std::vector<SceneArtifactNode> nodes;
};

/**
 * @brief Real scene-module provider for generated artifact publication.
 *
 * The first backend is deliberately CPU/registry based. It is the stable
 * scene ownership boundary: a renderer or ECS SceneHost may later consume the
 * record without changing procgen or the capability contract.
 */
class SceneArtifactProvider final : public eve::artifact::ISceneArtifactAdapter {
public:
    /** @brief Stage one artifact and copy all data needed after prepare. */
    [[nodiscard]] eve::Result<std::unique_ptr<eve::artifact::PreparedPublication>> prepare(
        const eve::artifact::PublicationView& publication) override;
    /** @brief Export scene identity, build keys, handles and part bounds. */
    [[nodiscard]] eve::Result<eve::Value> snapshotState() const override;
    /** @brief Replace the registry from validated backend-neutral state. */
    [[nodiscard]] eve::Result<void> restoreState(const eve::Value& state) override;
    /** @brief Return whether the scene registry has no committed records. */
    [[nodiscard]] bool emptyState() const noexcept override { return records_.empty(); }
    /** @brief Clear provider state through the common restore-cleanup hook. */
    void clearState() noexcept override { clear(); }

    /**
     * @brief Find a committed scene artifact by persistent identity.
     * @return Borrowed nullable record owned by this provider.
     * @ownership SceneArtifactProvider owns committed records; callers must not delete or mutate the result.
     * @lifetime Valid until clear(), restoreState(), or provider destruction; retain SceneArtifactHandle across frames.
     * @thread Call on the scene provider's owning thread.
     * @reentrancy The lookup invokes no callbacks and is invalid across re-entrant provider mutation.
     */
    [[nodiscard]] const SceneArtifactRecord* find(eve::PersistentId id) const noexcept;
    /** @brief Return the number of committed scene artifact records. */
    [[nodiscard]] std::size_t size() const noexcept { return records_.size(); }
    /** @brief Remove all records and reset the local handle allocator. */
    void clear() noexcept;
    /** @brief Inject a prepare failure for composition tests. */
    void setPrepareFailure(bool enabled) noexcept { failPrepare_ = enabled; }

private:
    friend class SceneArtifactStage;
    void                             commit(SceneArtifactRecord record) noexcept;
    std::vector<SceneArtifactRecord> records_;
    std::uint32_t                    nextIndex_   = 0;
    bool                             failPrepare_ = false;
};

/** @brief Return the process-owned scene artifact provider singleton. */
[[nodiscard]] SceneArtifactProvider& sceneArtifactProvider() noexcept;
/** @brief Register the scene provider in the common capability registry. */
void registerSceneArtifactProvider();

}  // namespace eve::scene
