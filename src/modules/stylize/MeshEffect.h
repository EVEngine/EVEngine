#pragma once

#include "common/RuntimeHandle.h"
#include "stylize/StyleInstance.h"

#include <string>

namespace eve::graphics {
class Graphics;
class Shader;
}

namespace eve::stylize {

struct MeshEffectTargetTag;
using MeshEffectTargetHandle = eve::RuntimeHandle<MeshEffectTargetTag>;

/** @brief Playback phase of a mesh-attached visual effect. */
enum class MeshEffectState { Stopped, FadingIn, Active, FadingOut, Finished };

/** @brief Deterministic timing configuration for one mesh effect playback. */
struct MeshEffectPlayback {
    float fadeIn = 0.08f;
    float duration = 0.25f;
    float fadeOut = 0.12f;
    bool loop = false;
};

/**
 * @brief Runtime state for a shader effect attached to a renderable mesh.
 *
 * The instance owns style overrides and playback state, but does not own or
 * resolve the target. Graphics supplies and resolves MeshEffectTargetHandle;
 * a stale target must be rejected by that owner before drawing. Time advances
 * only through update(), making playback deterministic for an identical dt
 * sequence. The object is main-thread affine and invokes no callbacks.
 */
class MeshEffectInstance {
public:
    /** @brief Construct an unbound effect from a registered mesh style. */
    explicit MeshEffectInstance(std::string style);

    MeshEffectInstance(const MeshEffectInstance&) = delete;
    MeshEffectInstance& operator=(const MeshEffectInstance&) = delete;

    /** @brief Bind a process-local target identity without taking ownership. */
    void bindTarget(MeshEffectTargetHandle target);
    /** @brief Remove the observed target identity. */
    void unbindTarget() noexcept;
    /** @brief Return the observed target identity, possibly invalid. */
    [[nodiscard]] MeshEffectTargetHandle target() const noexcept { return target_; }
    /** @brief Return whether a target identity is currently bound. */
    [[nodiscard]] bool isBound() const noexcept { return target_.isValid(); }

    /** @brief Replace timing after validating finite non-negative durations. */
    void setPlayback(MeshEffectPlayback playback);
    /** @brief Return a value snapshot of the timing configuration. */
    [[nodiscard]] MeshEffectPlayback playback() const noexcept { return playback_; }
    /** @brief Restart playback from time zero. */
    void play() noexcept;
    /** @brief Stop immediately or fade from the current envelope intensity. */
    void stop(float fadeOutSeconds = 0.f);
    /** @brief Advance playback using caller-provided simulation time. */
    void update(float dtSeconds);

    /** @brief Return the current playback phase. */
    [[nodiscard]] MeshEffectState state() const noexcept { return state_; }
    /** @brief Return elapsed playback time in seconds. */
    [[nodiscard]] float elapsed() const noexcept { return elapsed_; }
    /** @brief Return the normalized lifecycle envelope in [0, 1]. */
    [[nodiscard]] float intensity() const noexcept;
    /** @brief Access the owned mutable style parameter instance. */
    [[nodiscard]] StyleInstance& style() noexcept { return style_; }
    /** @brief Access the owned immutable style parameter instance. */
    [[nodiscard]] const StyleInstance& style() const noexcept { return style_; }

    /** @brief Create a shader owned by gfx with current overrides and time. */
    [[nodiscard]] graphics::Shader* newMeshShader(graphics::Graphics* gfx);

private:
    void updateTimelineState() noexcept;
    static void validateDuration(float value, const char* name);

    StyleInstance style_;
    MeshEffectTargetHandle target_;
    MeshEffectPlayback playback_;
    MeshEffectState state_ = MeshEffectState::Stopped;
    float elapsed_ = 0.f;
    float stopElapsed_ = 0.f;
    float stopDuration_ = 0.f;
    float stopStartIntensity_ = 0.f;
};

}  // namespace eve::stylize
