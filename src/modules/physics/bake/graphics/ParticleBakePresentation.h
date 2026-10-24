#pragma once
#include <memory>
#include <span>
#include "common/Export.h"
#include "common/Identity.h"
#include "common/Result.h"
namespace eve::graphics {
class Graphics;
struct DrawItem2D;
}  // namespace eve::graphics
namespace eve::physics {
class ParticleTrack;
class BakedParticlePlayback;
/** @brief One borrowed material template for an emitter-session identity.
 * @ownership Material is borrowed for a draw call only; its resources must survive Graphics frame completion.
 */
struct ParticleBakeMaterial {
    PersistentId                emitterSession;
    const graphics::DrawItem2D& material;
};
/** @brief Prepared sprite-batch consumer of recorded particles, independent of source emitters.
 * @ownership Owns player and CPU buffers only; never retains graphics or material resource references between calls.
 * @thread Render-owner thread with exclusive access; no scripts or simulation calls.
 */
class EVENGINE_API_WORLD ParticleBakePresentation {
public:
    /** @brief Allocate one player and reusable presentation buffers.
     * @cost Linear in recorded births for buffer reservation, once per instance outside playback.
     * @return Owning presentation or allocation diagnostic.
     */
    [[nodiscard]] static Result<std::unique_ptr<ParticleBakePresentation>> prepare(ParticleTrack track);
    ~ParticleBakePresentation();
    ParticleBakePresentation(const ParticleBakePresentation&)            = delete;
    ParticleBakePresentation& operator=(const ParticleBakePresentation&) = delete;
    /** @brief Borrow the owned player's clock and placement controls.
     * @ownership Reference remains valid for this presentation's lifetime; never move or reassign the player.
     */
    [[nodiscard]] BakedParticlePlayback& player() noexcept;
    /** @brief Sample and submit sprites through the ordinary 2D batch renderer.
     * @ownership Borrows Graphics and material only during the call; caller supplies live resource references.
     * @lifetime Textures, shaders, camera and canvas in material must remain valid through Graphics frame completion.
     * @remarks Material supplies resource/layer/blend/light policy. Recorded geometry, color, UV and identity order
     * replace those fields. Anchor is center, flip flags are disabled. No implicit time advance or source simulation
     * access.
     * @cost Linear in visible births plus existing renderer sorting/upload, per presented frame; CPU buffers reused.
     * @return Submitted count or sampling failure before submission. The renderer owns backend diagnostics;
     * successful submission does not by itself establish GPU completion. Canvas selection is restored to screen.
     */
    [[nodiscard]] Result<std::size_t> draw(graphics::Graphics& graphics, const graphics::DrawItem2D& material);

    /** @brief Draw using strictly identity-sorted, unique per-emitter material templates.
     * @ownership Borrows bindings and templates only during this call; no resource references retained afterward.
     * @lifetime Every supplied graphics resource must remain live through Graphics frame completion.
     * @cost Linear binding validation plus logarithmic lookup per visible birth and renderer sorting/upload.
     * @return Submitted count or invalid/missing binding/sample failure before any draw submission.
     * @remarks Up to 65536 bindings. Every visible emitter must be bound; extra bindings are allowed for dead emitters.
     */
    [[nodiscard]] Result<std::size_t> draw(graphics::Graphics&                   graphics,
                                           std::span<const ParticleBakeMaterial> materials);

private:
    /** @brief Shared synchronous implementation; borrows uniformMaterial only during this call. */
    [[nodiscard]] Result<std::size_t> drawImpl(graphics::Graphics&                   graphics,
                                               const graphics::DrawItem2D*           uniformMaterial,
                                               std::span<const ParticleBakeMaterial> materials);
    struct Impl;
    explicit ParticleBakePresentation(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
}  // namespace eve::physics
