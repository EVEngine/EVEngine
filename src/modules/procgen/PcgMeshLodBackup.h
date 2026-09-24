#pragma once

#include "common/Result.h"

#include <cstdint>
#include <memory>

namespace eve::graphics { class Renderable3D; }

namespace eve::procgen {

/**
 * @brief Caller-owned transactional backup of one Renderable3D renderer before Pcg LOD replacement.
 *
 * The snapshot retains Graphics-owned resource pointers already held by the renderer. The owning Graphics
 * instance and those resources must outlive this backup. Capture and restore are main/render-thread only,
 * do not invoke callbacks, and validate the entity id plus generation before publication.
 */
class EVENGINE_API_DOMAINS PcgMeshLodBackup {
public:
    PcgMeshLodBackup();
    ~PcgMeshLodBackup();
    PcgMeshLodBackup(const PcgMeshLodBackup&) = delete;
    PcgMeshLodBackup& operator=(const PcgMeshLodBackup&) = delete;
    PcgMeshLodBackup(PcgMeshLodBackup&&) noexcept;
    PcgMeshLodBackup& operator=(PcgMeshLodBackup&&) noexcept;

    /** @brief Replace the backup with the complete current renderer state of one live entity. */
    [[nodiscard]] Result<void> capture(graphics::Renderable3D& renderable);
    /** @brief Restore and consume the backup after exact entity-generation validation. */
    [[nodiscard]] Result<void> restore(graphics::Renderable3D& renderable);
    /** @brief Forget the snapshot without changing its entity. */
    void discard() noexcept;
    /** @brief Return whether a restorable snapshot is present. */
    [[nodiscard]] bool isCaptured() const noexcept;
    /** @brief Return the captured table-local entity id, or zero when empty. */
    [[nodiscard]] std::uint32_t getEntityId() const noexcept;
    /** @brief Return the captured ECS generation, or zero when empty. */
    [[nodiscard]] std::uint32_t getEntityGeneration() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace eve::procgen
