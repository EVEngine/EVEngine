#pragma once
#include <memory>
#include "common/Export.h"
#include "common/Result.h"
namespace eve::graphics {
class Graphics;
class Mesh;
}  // namespace eve::graphics
namespace eve::physics {
class DeformTrack;
class DeformPlayback;
/** @brief Prepared dynamic mesh presentation over an owned deformation player.
 * @ownership Owns immutable asset and CPU buffers only; the caller owns playback state. Never retains Graphics, Mesh,
 * ECS or source simulation pointers.
 * @thread Render-owner thread, exclusive use; no callbacks. Caller owns graphics-resource lifecycle.
 */
class EVENGINE_API_WORLD DeformMeshPresentation {
public:
    /** @brief Prepare double-sided triangles or capped eight-sided tubes from a metre-space clip.
     * @param ropeRadius Positive finite tube radius in metres, used only for segment topology.
     * @return Owning CPU presentation or invalid coordinate/budget/allocation diagnostic.
     * @cost Linear in output vertices and indices, allocating once per presentation instance outside playback.
     */
    [[nodiscard]] static Result<std::unique_ptr<DeformMeshPresentation>> prepare(DeformTrack track, float ropeRadius);
    ~DeformMeshPresentation();
    DeformMeshPresentation(const DeformMeshPresentation&)            = delete;
    DeformMeshPresentation& operator=(const DeformMeshPresentation&) = delete;
    /** @brief Sample the player and create one Graphics-owned mesh with current topology.
     * @ownership Returned pointer is borrowed from Graphics; caller manages its release and must not outlive Graphics.
     * @lifetime Graphics and mesh are not retained. Successful Graphics::releaseMesh transfers facade deletion to
     * caller.
     * @cost Linear CPU preparation and GPU allocation/upload, once per mesh before repeated uploads.
     * @return Borrowed mesh or checked sampling/backend failure; no partially registered adapter state.
     */
    [[nodiscard]] Result<graphics::Mesh*> createMesh(DeformPlayback& player, graphics::Graphics& graphics);
    /** @brief Sample and upload all prepared streams to a live mesh owned by the supplied Graphics.
     * @ownership Borrows both objects only for this call; no pointers survive return.
     * @cost Linear in output vertices and indices, reusing CPU buffers; GPU backend may grow buffers if needed.
     * @return Success or sampling/foreign-mesh/backend failure. Sampling failure performs no GPU write.
     * Backend failure does not promise GPU rollback; caller must skip drawing until a successful upload.
     */
    [[nodiscard]] Result<void> upload(DeformPlayback& player, graphics::Graphics& graphics, graphics::Mesh& mesh);

private:
    struct Impl;
    explicit DeformMeshPresentation(std::unique_ptr<Impl> impl);
    [[nodiscard]] Result<void> rebuild(DeformPlayback& player);
    std::unique_ptr<Impl>      impl_;
};
}  // namespace eve::physics
