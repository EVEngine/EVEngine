#pragma once
#include "common/Export.h"

#include <array>
#include <cstdint>
#include <glm/vec3.hpp>
#include <span>
#include "common/Result.h"

namespace eve::graphics {

class IResourceFactory;
class Texture;
class VegetationField;
struct PbrSurface;
struct VegetationAtlas;
struct VegetationChannelAtlas;

/** @brief Immutable GPU projection of nine layers from one vegetation field channel.
 * The source field remains authoritative; fieldRevision detects stale projections.
 * Texture ownership remains with factory and the caller must release it through that factory.
 */
struct VegetationGpuAtlas {
    Texture*  texture       = nullptr;
    uint64_t  fieldRevision = 0;
    uint32_t  width = 0, height = 0, layers = 0;
    glm::vec3 center{0.f}, extent{1.f};
};
using VegetationExtrasGpuAtlas = VegetationGpuAtlas;
using VegetationColorsGpuAtlas = VegetationGpuAtlas;
using VegetationMotionGpuAtlas = VegetationGpuAtlas;
using VegetationVertexGpuAtlas = VegetationGpuAtlas;

/** @brief Coherent detached projections for all four vegetation channels.
 * Textures are factory-owned. The caller must release the set through
 * releaseVegetationGpuFieldSet on the same graphics thread and factory.
 */
struct VegetationGpuFieldSet {
    VegetationExtrasGpuAtlas extras;
    VegetationColorsGpuAtlas colors;
    VegetationMotionGpuAtlas motion;
    VegetationVertexGpuAtlas vertex;
};

/** @brief Runtime-owned global inputs shared by one coherent vegetation field binding.
 * Time is explicit for deterministic replay. Noise and atlas textures remain borrowed.
 */
struct VegetationGpuRuntime {
    Texture*             motionNoise = nullptr;
    Texture*             fadeNoise   = nullptr;
    std::array<float, 2> motionDirection{1, 0};
    std::array<float, 3> worldOrigin{0, 0, 0};
    double               time          = 0;
    float                globalBending = 1, globalBranch = 1, globalFlutter = 1, noiseTiling = 1;
    float                motionFadeDistance = 100;
    float                cameraFadeMin = 0, cameraFadeMax = 100, fadeNoiseTiling = 1;
    bool                 enableMotion = true;
};

/** @brief Bake and atomically upload all nine Extras layers as a linear RGBA16F 2D array.
 * @param factory Borrowed resource factory; used synchronously on its graphics thread.
 * @param field Borrowed immutable authoritative field; no writer may run during this call.
 * @param center Requested world-space center; X/Z may be snapped to texel increments.
 * @param extent Positive world-space half extent.
 * @param width Atlas width in [1,2048].
 * @param height Atlas height in [1,2048].
 * @param snapToTexel Whether to stabilize X/Z sampling against sub-texel camera motion.
 * @return Factory-owned texture and detached projection metadata. Failure publishes no texture.
 * @thread Graphics thread only because upload is synchronous and non-reentrant.
 */
[[nodiscard]] EVENGINE_API_BACKENDS Result<VegetationExtrasGpuAtlas> uploadVegetationExtrasAtlas(IResourceFactory&      factory,
                                                                           const VegetationField& field,
                                                                           glm::vec3 center, glm::vec3 extent,
                                                                           uint32_t width, uint32_t height,
                                                                           bool snapToTexel = true);

/** @brief Bake and atomically upload all nine Colors layers as a linear RGBA16F 2D array.
 * Ownership, revision, geometry, budget and threading contracts match uploadVegetationExtrasAtlas.
 */
[[nodiscard]] EVENGINE_API_BACKENDS Result<VegetationColorsGpuAtlas> uploadVegetationColorsAtlas(IResourceFactory&      factory,
                                                                           const VegetationField& field,
                                                                           glm::vec3 center, glm::vec3 extent,
                                                                           uint32_t width, uint32_t height,
                                                                           bool snapToTexel = true);

/** @brief Bake and atomically upload all nine Motion layers as a linear RGBA16F 2D array. */
[[nodiscard]] EVENGINE_API_BACKENDS Result<VegetationMotionGpuAtlas> uploadVegetationMotionAtlas(IResourceFactory&      factory,
                                                                           const VegetationField& field,
                                                                           glm::vec3 center, glm::vec3 extent,
                                                                           uint32_t width, uint32_t height,
                                                                           bool snapToTexel = true);

/** @brief Bake and atomically upload all nine Vertex layers as a linear RGBA16F 2D array. */
[[nodiscard]] EVENGINE_API_BACKENDS Result<VegetationVertexGpuAtlas> uploadVegetationVertexAtlas(IResourceFactory&      factory,
                                                                           const VegetationField& field,
                                                                           glm::vec3 center, glm::vec3 extent,
                                                                           uint32_t width, uint32_t height,
                                                                           bool snapToTexel = true);

/** @brief Bake and upload all four nine-layer projections as one publication.
 * Earlier uploads are released if any later channel fails. The returned set is
 * coherent by revision, snapped bounds and dimensions and owns no factory lifetime.
 * @return Four factory-owned textures, or failure with no published texture.
 * @thread Graphics thread only. Synchronous, non-reentrant and without callbacks.
 */
[[nodiscard]] EVENGINE_API_BACKENDS Result<VegetationGpuFieldSet> uploadVegetationGpuFieldSet(IResourceFactory&      factory,
                                                                        const VegetationField& field, glm::vec3 center,
                                                                        glm::vec3 extent, uint32_t width,
                                                                        uint32_t height, bool snapToTexel = true);

/** @brief Atomically upload nine pre-baked layer atlases into four coherent RGBA16F arrays.
 * @param factory Borrowed resource factory used synchronously on its graphics thread.
 * @param layers Exactly nine immutable atlases with identical dimensions and world mapping.
 * @param sourceRevision Nonzero revision owned by the caller's authoritative scene state.
 * @return Four factory-owned textures; a later failure releases every earlier allocation.
 */
[[nodiscard]] EVENGINE_API_BACKENDS Result<VegetationGpuFieldSet> uploadVegetationGpuFieldSet(
    IResourceFactory& factory, std::span<const VegetationAtlas, 9> layers, std::uint64_t sourceRevision);

/** @brief Atomically upload four independently sized groups of nine pre-baked channel layers.
 * Each group must be internally coherent. The combined RGBA16F upload is limited to 256 MiB.
 * @return Four factory-owned textures; a later failure releases every earlier allocation.
 */
[[nodiscard]] EVENGINE_API_BACKENDS Result<VegetationGpuFieldSet> uploadVegetationGpuFieldSet(
    IResourceFactory& factory, std::span<const VegetationChannelAtlas, 9> colors,
    std::span<const VegetationChannelAtlas, 9> extras, std::span<const VegetationChannelAtlas, 9> motion,
    std::span<const VegetationChannelAtlas, 9> vertex, std::uint64_t sourceRevision);

/** @brief Release every texture still present in a detached field set.
 * Successful entries are nulled. If a foreign/already-released entry is found,
 * remaining entries are still attempted and the set retains only failed pointers.
 * @return Success when every non-null texture was released; otherwise Failed.
 * @thread Same graphics thread and factory used for upload; no callbacks.
 */
[[nodiscard]] EVENGINE_API_BACKENDS Result<void> releaseVegetationGpuFieldSet(IResourceFactory& factory, VegetationGpuFieldSet& set);

/** @brief Atomically bind four coherent GPU projections to an owning PBR parameter snapshot.
 * @param surface Owning parameter snapshot; unchanged on validation failure.
 * @param field Borrowed authoritative field used only to reject stale projections.
 * @param extras Borrowed projection; its factory-owned texture must outlive every material using surface.
 * @param colors Borrowed projection with the same revision; geometry may differ from extras.
 * @param motion Borrowed projection with the same revision; geometry may differ from extras.
 * @param vertex Borrowed projection with the same revision; geometry may differ from extras.
 * @param runtime Owning scalar snapshot; motionNoise remains borrowed and must outlive material use.
 * @return InvalidArgument without mutation for incoherent/stale projections or invalid runtime values.
 * @thread Graphics thread only. Synchronous, non-reentrant and without callbacks.
 */
[[nodiscard]] EVENGINE_API_BACKENDS Result<void> bindVegetationGpuFields(PbrSurface& surface, const VegetationField& field,
                                                   const VegetationExtrasGpuAtlas& extras,
                                                   const VegetationColorsGpuAtlas& colors,
                                                   const VegetationMotionGpuAtlas& motion,
                                                   const VegetationVertexGpuAtlas& vertex,
                                                   const VegetationGpuRuntime&     runtime);

/** @brief Convenience overload for a coherent detached four-channel set. */
[[nodiscard]] EVENGINE_API_BACKENDS Result<void> bindVegetationGpuFields(PbrSurface& surface, const VegetationField& field,
                                                   const VegetationGpuFieldSet& set,
                                                   const VegetationGpuRuntime&  runtime);

/** @brief Bind a coherent field set owned by a scene/runtime revision rather than VegetationField. */
[[nodiscard]] EVENGINE_API_BACKENDS Result<void> bindVegetationGpuFields(PbrSurface& surface, const VegetationGpuFieldSet& set,
                                                   std::uint64_t expectedRevision,
                                                   const VegetationGpuRuntime& runtime);

}  // namespace eve::graphics
