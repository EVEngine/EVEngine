#pragma once

#include "common/Result.h"

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

namespace ssq { class Table; }

namespace eve::graphics {
class Volumetric;
class Camera3D;
class Graphics;
class Renderable3D;

/** @brief One normalized RGB key in a Pcg underwater fog gradient. */
struct UnderwaterColorStop { float time = 0.0F; glm::vec3 color{1.0F}; };

/** @brief Caller-owned ordered color gradient used for depth or time-of-day fog. */
class EVENGINE_API_BACKENDS UnderwaterColorGradient {
public:
    /** @brief Add a finite key in [0,1]; duplicate times are rejected atomically. */
    [[nodiscard]] Result<void> addStop(float time, float r, float g, float b);
    /** @brief Remove every key. */
    void clear() { stops_.clear(); }
    /** @brief Evaluate with endpoint clamping and linear interpolation. */
    [[nodiscard]] Result<glm::vec3> evaluate(float time) const;
  private:
    std::vector<UnderwaterColorStop> stops_;
};

/** @brief Caller-owned normalized scalar curve used by Pcg underwater post exposure. */
class EVENGINE_API_BACKENDS UnderwaterScalarCurve {
public:
    /** @brief Add a finite key in [0,1]; duplicate times are rejected atomically. */
    [[nodiscard]] Result<void> addKey(float time, float value);
    /** @brief Remove every key. */
    void clear() { keys_.clear(); }
    /** @brief Evaluate with endpoint clamping and linear interpolation. */
    [[nodiscard]] Result<float> evaluate(float time) const;
  private:
    std::vector<glm::vec2> keys_;
};

/** @brief Authored Pcg underwater-effects policy; owns no renderer, audio source or scene object. */
struct WaterUnderwaterSettings {
    bool enabled = true;
    bool supportFog = true;
    bool supportPostFx = true;
    bool enableTransitionFx = true;
    bool useCaustics = true;
    bool hdrp = false;
    bool overrideFogColor = false;
    float fogDepth = 100.0F;
    float fogDistance = 45.0F;
    float hdrpFogDistance = 30.0F;
    float nearFogDistance = -4.0F;
    float fogDensity = 0.045F;
    float playbackVolume = 0.5F;
    float causticSize = 15.0F;
    int framesPerSecond = 24;
    int causticTextureCount = 0;
    glm::vec3 fogColorMultiplier{0.0F};
    bool photoModeFogColorEnabled = false;
    glm::vec3 photoModeFogColor{0.0F};
    glm::vec3 overrideFogMultiplier{1.0F};
    float overrideFogCurve = 1.0F;
    float anisotropyShallow = 0.225F;
    float anisotropyDeep = 0.0F;
    bool timeDrivenPostFx = false;
    float constantPostExposure = 0.0F;
    glm::vec3 constantPostColor{1.0F};
    float transitionHalfHeight = 0.025F;
    float transitionBlendDistance = 0.025F;
    float transitionVignette = 0.25F;
    float transitionVignetteSmoothness = 0.8F;
    float transitionLensDistortion = 0.252F;
    float transitionLensScale = 1.02F;
    glm::vec3 transitionLift{-0.020152892F, -0.009343888F, 0.005069838F};
    glm::vec3 transitionInverseGamma{1.35160142F, 1.35531419F, 1.26800077F};
    glm::vec3 transitionGain{0.67146943F, 0.68969287F, 0.72298255F};
    glm::vec3 transitionColorFilter{0.0F};
};

/** @brief Immutable per-frame water, camera and lighting snapshot. */
struct WaterUnderwaterInput {
    float cameraY = 0.0F;
    float seaLevel = 50.0F;
    float timeOfDay = 0.5F;
    glm::vec3 mainLightColor{1.0F};
    bool hasMainLight = true;
    int causticTicks = 0;
};

/** @brief Persistent caller-owned transition and caustic animation state. */
struct WaterUnderwaterState { bool initialized = false; bool isUnderwater = false; int causticFrame = 0; };

/** @brief Caller-owned enable state driven by Pcg disable-underwater trigger events. */
struct WaterUnderwaterDisableTriggerState { bool effectsEnabled = true; };

/** @brief Immutable normalized 3D trigger event snapshot supplied by the physics owner. */
struct WaterUnderwaterTriggerEvent {
    int sensorTag = 0;
    int visitorTag = 0;
    bool entered = false;
    bool exited = false;
};

/** @brief Effects snapshot for graphics, audio, particles and VFX providers to consume. */
struct WaterUnderwaterOutput {
    bool isUnderwater = false;
    bool entered = false;
    bool exited = false;
    bool playSubmergeDown = false;
    bool playSubmergeUp = false;
    bool loopAudio = false;
    bool particles = false;
    bool horizon = false;
    bool surfaceVfx = true;
    bool postFx = false;
    bool transitionFx = false;
    bool caustics = false;
    int causticFrame = 0;
    float causticSize = 0.0F;
    glm::vec3 fogColor{0.533F, 0.764F, 1.0F};
    float fogDensity = 0.0F;
    float fogStart = 0.0F;
    float fogEnd = 0.0F;
    float fogHeight = 0.0F;
    float depth01 = 0.0F;
    float hdrpBaseHeight = -150.0F;
    float hdrpMeanFreePath = 3000.0F;
    float hdrpProbeDimmer = 1.0F;
    float hdrpAnisotropy = 0.225F;
    float hdrpDepthExtent = 150.0F;
    float postExposure = 0.0F;
    glm::vec3 postColor{1.0F};
    glm::vec3 underwaterMaterialColor{0.8117647F};
    float transitionWeight = 0.0F;
    float transitionVignette = 0.0F;
    float transitionVignetteSmoothness = 0.2F;
    float transitionLensDistortion = 0.0F;
    float transitionLensScale = 1.0F;
    glm::vec3 transitionLift{0.0F};
    glm::vec3 transitionInverseGamma{1.0F};
    glm::vec3 transitionGain{1.0F};
    glm::vec3 transitionColorFilter{1.0F};
};

/** @brief Caller-owned surface fog values restored after leaving or disabling underwater effects. */
struct WaterSurfaceFogSnapshot {
    glm::vec3 color{0.55F, 0.58F, 0.62F};
    float density = 0.004F;
    float start = 2.0F;
    float end = 90.0F;
    float height = 0.0F;
    float heightFalloff = 0.04F;
};

/** @brief Caller-owned surface post-processing values restored above water. */
struct WaterSurfacePostFxSnapshot {
    float exposureEv = 0.0F;
    glm::vec3 color{1.0F};
    float vignette = 0.0F;
    float vignetteSmoothness = 0.2F;
    float lensDistortion = 0.0F;
    float lensScale = 1.0F;
    glm::vec3 lift{0.0F};
    glm::vec3 inverseGamma{1.0F};
    glm::vec3 gain{1.0F};
};

/** @brief Caller-owned water-renderable tint restored outside the underwater state. */
struct WaterSurfaceMaterialSnapshot {
    glm::vec3 color{1.0F};
    float alpha = 1.0F;
};

/**
 * @brief Advance Pcg underwater transitions and derive provider-neutral effects.
 * @param state Exclusively borrowed state, published only after validation and complete evaluation.
 * @param output Exclusively borrowed output, published atomically with state.
 * @param settings Immutable authored policy.
 * @param input Immutable finite frame snapshot with explicit caustic ticks.
 * @param depthGradient Required nonempty depth gradient when fog is enabled without override.
 * @param timeGradient Required nonempty time gradient when fog override is enabled.
 * @return Success or InvalidArgument; failure preserves state and output.
 * @thread Synchronous and deterministic; retains no pointer and invokes no callback.
 */
[[nodiscard]] EVENGINE_API_BACKENDS Result<void> advanceWaterUnderwaterEffects(
    WaterUnderwaterState& state, WaterUnderwaterOutput& output, const WaterUnderwaterSettings& settings,
    const WaterUnderwaterInput& input, const UnderwaterColorGradient& depthGradient,
    const UnderwaterColorGradient& timeGradient, const UnderwaterScalarCurve& postExposureCurve,
    const UnderwaterColorGradient& postColorGradient);

/**
 * @brief Apply one Box3D trigger event using Pcg DisableUnderwaterFXTrigger semantics.
 * @param state Exclusively borrowed state; matching enter disables and matching exit enables effects.
 * @param event Immutable event copied from World3D's begin/end trigger buffers.
 * @param expectedSensorTag Stable Shape3D tag identifying this disable zone.
 * @param expectedVisitorTag Stable Shape3D tag identifying the player/visitor.
 * @return Success or InvalidArgument; malformed events preserve state and mismatched tags are ignored.
 * @thread Deterministic simulation-owner operation; retains no pointer and invokes no callback.
 */
[[nodiscard]] EVENGINE_API_BACKENDS Result<void> advanceWaterUnderwaterDisableTrigger(
    WaterUnderwaterDisableTriggerState& state, const WaterUnderwaterTriggerEvent& event, int expectedSensorTag,
    int expectedVisitorTag);

/**
 * @brief Apply a water-effects snapshot to an existing EVEngine volumetric fog provider.
 * @param volumetric Borrowed provider used synchronously and never retained.
 * @param output Immutable current underwater output.
 * @param surface Immutable values restored when output is above water.
 * @return Success or InvalidArgument; validation completes before provider mutation.
 * @thread Must run on the owning graphics thread outside render callbacks.
 */
[[nodiscard]] EVENGINE_API_BACKENDS Result<void> applyWaterUnderwaterFog(Volumetric*                    volumetric,
                                                                         const WaterUnderwaterOutput&   output,
                                                                         const WaterSurfaceFogSnapshot& surface);

/**
 * @brief Apply underwater exposure and final-composite color to borrowed graphics providers.
 * @param graphics Borrowed graphics provider mutated synchronously and never retained.
 * @param camera Borrowed camera mutated synchronously and never retained.
 * @param output Immutable current underwater output.
 * @param surface Immutable values restored when the effect is inactive.
 * @return Success or InvalidArgument; validation completes before either provider is mutated.
 * @thread Must run on the owning graphics thread outside render callbacks.
 */
[[nodiscard]] EVENGINE_API_BACKENDS Result<void> applyWaterUnderwaterPostFx(Graphics* graphics, Camera3D* camera,
                                                                            const WaterUnderwaterOutput&      output,
                                                                            const WaterSurfacePostFxSnapshot& surface);

/**
 * @brief Synchronize Pcg's underwater horizon mesh visibility.
 * @param horizon Borrowed renderable mutated synchronously and never retained.
 * @param output Immutable current underwater output.
 * @return Success or InvalidArgument.
 * @thread Scene owner thread only; callbacks are not invoked.
 */
[[nodiscard]] EVENGINE_API_BACKENDS Result<void> applyWaterUnderwaterHorizon(Renderable3D*                horizon,
                                                                             const WaterUnderwaterOutput& output);

/**
 * @brief Apply Pcg's per-frame underwater material color to a dedicated water renderable.
 * @param renderable Borrowed water renderable mutated synchronously and never retained.
 * @param output Immutable current underwater output.
 * @param surface Immutable tint restored when the effect is inactive.
 * @return Success or InvalidArgument; validation completes before mutation.
 * @thread Scene owner thread only; callbacks are not invoked.
 */
[[nodiscard]] EVENGINE_API_BACKENDS Result<void> applyWaterUnderwaterMaterial(
    Renderable3D* renderable, const WaterUnderwaterOutput& output, const WaterSurfaceMaterialSnapshot& surface);

/** @brief Register underwater-effects value types and checked transition with the VM owner thread. */
void exposeWaterUnderwaterEffectsBindings(ssq::Table& table);

}  // namespace eve::graphics
