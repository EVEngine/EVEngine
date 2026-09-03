#pragma once

#include "stylize/MeshEffect.h"
#include "stylize/TrailEffect.h"

#include <glm/vec3.hpp>

#include <memory>

namespace eve::stylize {

/** @brief Built-in gameplay-oriented mesh VFX compositions. */
enum class SkillMeshEffectKind { WeaponSlash, ImpactFlash, ChargeAura, BurningBody };

/**
 * @brief Owning runtime composition for one common gameplay mesh effect.
 *
 * The composition owns deterministic playback and optional CPU trail state.
 * It does not own scene targets or GPU resources. Call update() with simulation
 * dt, resolve the target through its authoritative owner, then submit effect()
 * and trail().buildMesh() through MeshEffectRenderer on the render thread.
 */
class SkillMeshEffect {
public:
    /** @brief Construct one built-in composition with validated preset data. */
    explicit SkillMeshEffect(SkillMeshEffectKind kind);

    SkillMeshEffect(const SkillMeshEffect&) = delete;
    SkillMeshEffect& operator=(const SkillMeshEffect&) = delete;

    /** @brief Return the immutable built-in recipe identity. */
    [[nodiscard]] SkillMeshEffectKind kind() const noexcept { return kind_; }
    /** @brief Return the owned primary mesh-effect runtime. */
    [[nodiscard]] MeshEffectInstance& effect() noexcept { return *effect_; }
    /** @brief Return the owned primary mesh-effect runtime. */
    [[nodiscard]] const MeshEffectInstance& effect() const noexcept { return *effect_; }
    /** @brief Return whether this recipe includes a weapon ribbon. */
    [[nodiscard]] bool hasTrail() const noexcept { return trail_ != nullptr; }
    /** @brief Return the owned ribbon or throw when this recipe has no ribbon. */
    [[nodiscard]] TrailEmitter& trail();
    /** @brief Return the owned ribbon or throw when this recipe has no ribbon. */
    [[nodiscard]] const TrailEmitter& trail() const;

    /** @brief Bind the primary effect to a process-local target identity. */
    void bindTarget(MeshEffectTargetHandle target);
    /** @brief Restart playback and clear any retained ribbon samples. */
    void play() noexcept;
    /** @brief Stop playback, optionally fading from the current envelope. */
    void stop(float fadeOutSeconds = 0.f);
    /** @brief Advance all owned deterministic runtime state. */
    void update(float dtSeconds);
    /** @brief Append one blade-edge sample or throw if the recipe has no trail. */
    [[nodiscard]] TrailAppendResult appendBlade(glm::vec3 root, glm::vec3 tip);

private:
    SkillMeshEffectKind kind_;
    std::unique_ptr<MeshEffectInstance> effect_;
    std::unique_ptr<TrailEmitter> trail_;
};

}  // namespace eve::stylize
