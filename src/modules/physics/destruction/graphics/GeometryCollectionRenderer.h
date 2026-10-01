#pragma once
#include "common/Export.h"

#include "common/Result.h"

#include <cstdint>

namespace eve::graphics {
class Graphics;
class Mesh;
}  // namespace eve::graphics

namespace eve::physics {

class GeometryCollectionInstance;

/**
 * @brief Presentation bridge for a borrowed geometry-collection instance.
 *
 * Draws box-proxy bones. Attached bones use the exterior color; Detached bones
 * use the interior color as a broken-face stand-in. Sleeping bones are merged
 * into one batched mesh rebuilt when `sleepBatchRevision()` changes.
 *
 * @ownership Never owns Instance, Graphics, or Body3D. Mesh caches are produced
 * through Graphics and not retained as exclusive ownership.
 * @lifetime Borrows Instance and Graphics only for each call; do not call draw
 * after Instance or its World3D is destroyed.
 */
class EVENGINE_API_DOMAINS GeometryCollectionRenderer final {
public:
    /**
     * @brief Construct a renderer that borrows an instance.
     * @param instance Borrowed instance, or null to disable drawing.
     * @ownership Does not take ownership of instance.
     * @lifetime instance must outlive draws, or be cleared via setInstance(null).
     */
    explicit GeometryCollectionRenderer(GeometryCollectionInstance* instance) noexcept;

    GeometryCollectionRenderer(const GeometryCollectionRenderer&)            = delete;
    GeometryCollectionRenderer& operator=(const GeometryCollectionRenderer&) = delete;
    ~GeometryCollectionRenderer();

    /**
     * @brief Replace the borrowed instance; null disables drawing.
     * @ownership Does not take ownership of instance.
     * @lifetime instance must outlive subsequent draws, or be cleared again.
     */
    void setInstance(GeometryCollectionInstance* instance);
    /**
     * @brief Return the currently borrowed instance.
     * @ownership Borrowed; caller must not delete through this pointer.
     * @lifetime Valid until setInstance or renderer destruction.
     */
    [[nodiscard]] GeometryCollectionInstance* getInstance() const noexcept { return instance_; }

    /** @brief Exterior RGBA for Attached bones. */
    void setExteriorColor(float r, float g, float b, float a);
    /** @brief Interior RGBA for Detached bones (broken-face stand-in). */
    void setInteriorColor(float r, float g, float b, float a);
    /** @brief Sleeping-batch RGBA. */
    void setSleepColor(float r, float g, float b, float a);

    /**
     * @brief Synchronize caches and draw all bones into the current 3D frame.
     * @param graphics Borrowed Graphics used for mesh allocation and drawMesh.
     * @return Success, or StaleHandle/InvalidArgument when the instance/world is gone.
     * @ownership Does not take ownership of graphics.
     * @lifetime graphics must remain live for the duration of this call.
     */
    [[nodiscard("check geometry-collection draw")]] eve::Result<void> draw(graphics::Graphics* graphics);

    /** @brief Number of bones drawn individually on the last successful draw. */
    [[nodiscard]] int lastActiveDrawCount() const noexcept { return lastActiveDrawCount_; }
    /** @brief Number of bones included in the sleeping batch on the last draw. */
    [[nodiscard]] int lastSleepBatchCount() const noexcept { return lastSleepBatchCount_; }

private:
    void ensureUnitBox(graphics::Graphics& graphics);
    void invalidateSleepBatch();
    [[nodiscard]] eve::Result<void> rebuildSleepBatch(graphics::Graphics& graphics);

    GeometryCollectionInstance* instance_ = nullptr;
    graphics::Mesh* unitBox_ = nullptr;
    graphics::Mesh* sleepBatch_ = nullptr;
    std::uint64_t sleepBatchRevision_ = 0;
    int sleepBatchBoneCount_ = 0;
    int lastActiveDrawCount_ = 0;
    int lastSleepBatchCount_ = 0;
    float exteriorR_ = 0.62f, exteriorG_ = 0.58f, exteriorB_ = 0.52f, exteriorA_ = 1.f;
    float interiorR_ = 0.78f, interiorG_ = 0.42f, interiorB_ = 0.28f, interiorA_ = 1.f;
    float sleepR_ = 0.45f, sleepG_ = 0.45f, sleepB_ = 0.48f, sleepA_ = 1.f;
};

}  // namespace eve::physics
