#pragma once
#include "common/Export.h"

#include "common/Result.h"
#include "common/Time.h"

#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace eve::particles {

class ParticleEmitter;

/**
 * @brief One authored cross-emitter event route inside a particle effect asset.
 *
 * Routes compile onto the existing sub-emitter runtime (`birth` / `death` /
 * `collision`). Ownership of both emitters remains with the effect instance.
 */
struct ParticleEffectEventRoute {
    std::string from;
    std::string on             = "death";
    std::string to;
    float       inheritVelocity = 0.f;
};

/**
 * @brief One effect-local timeline cue evaluated against the effect clock.
 *
 * Supported actions: `start`, `stop`, `pause`, `reset`, `emit`, `setParameter`.
 * `emitter` selects a named layer for emitter-scoped actions; `parameter` /
 * `value` are used by `setParameter`; `count` is used by `emit`.
 */
struct ParticleEffectTimelineCue {
    float       time      = 0.f;
    std::string action;
    std::string emitter;
    std::string parameter;
    float       value     = 0.f;
    int         count     = 0;
};

/**
 * @brief Effect-level timeline controls (schema v2, optional on v1).
 *
 * @remarks Unknown fields in a supported schema version are ignored. Unsupported
 *          schema versions fail closed at parse time. Duration zero means "no
 *          authored clip length"; looping then has no effect until duration > 0.
 */
struct ParticleEffectTimeline {
    float                                duration = 0.f;
    bool                                 looping  = false;
    std::vector<ParticleEffectTimelineCue> cues;
};

/**
 * @brief Runtime instance of a versioned, multi-emitter particle effect asset.
 *
 * The instance owns every emitter created by the asset. Its transform, playback,
 * visibility, layer, exposed parameters, event routes, and timeline controls are
 * applied to the group as a unit.
 *
 * @ownership Caller owns the ParticleEffect; it owns every layer emitter.
 * @thread Owner thread only. Not reentrant from particle/sub-emitter callbacks.
 */
class EVENGINE_API_DOMAINS ParticleEffect {
public:
    /** @brief Highest schema version accepted by this build. */
    static constexpr int kMaxSupportedVersion = 2;
    /** @brief Lowest schema version accepted by this build. */
    static constexpr int kMinSupportedVersion = 1;

    /** @brief Destroy the group and every emitter it owns. */
    ~ParticleEffect();

    ParticleEffect(const ParticleEffect&)            = delete;
    ParticleEffect& operator=(const ParticleEffect&) = delete;

    /**
     * @brief Parse and instantiate an effect asset from JSON text.
     * @remarks Compatibility facade; prefer tryFromText for structured errors.
     */
    static ParticleEffect* fromText(const std::string& json, const std::string& sourcePath,
                                    std::string* error = nullptr);
    /**
     * @brief Read, parse, and instantiate an effect asset.
     * @remarks Compatibility facade; prefer tryFromFile for structured errors.
     */
    static ParticleEffect* fromFile(const std::string& path, std::string* error = nullptr);

    /** @brief Parse an effect asset and return a structured Result. */
    [[nodiscard]] static Result<ParticleEffect*> tryFromText(const std::string& json,
                                                             const std::string& sourcePath = {});
    /** @brief Load an effect asset file and return a structured Result. */
    [[nodiscard]] static Result<ParticleEffect*> tryFromFile(const std::string& path);

    /** @brief Return the asset schema version. */
    int getVersion() const { return version_; }
    /** @brief Return the source file path, or empty text for an in-memory asset. */
    std::string getSourcePath() const { return sourcePath_; }
    /** @brief Return the number of named emitter layers. */
    int getEmitterCount() const;
    /** @brief Return a stable layer name by index, or empty text. */
    std::string getEmitterName(int index) const;
    /** @brief Return an emitter layer by index, or null. */
    ParticleEmitter* getEmitter(int index) const;
    /** @brief Return an emitter layer by name, or null. */
    ParticleEmitter* getEmitterByName(const std::string& name) const;

    /** @brief Return authored event routes (borrowed snapshot of asset data). */
    [[nodiscard]] std::span<const ParticleEffectEventRoute> eventRoutes() const;
    /** @brief Return authored timeline controls. */
    [[nodiscard]] const ParticleEffectTimeline& timeline() const { return timeline_; }
    /** @brief Return the current effect-local timeline clock in seconds. */
    [[nodiscard]] float getTimelineSeconds() const { return static_cast<float>(timelineSeconds_); }
    /** @brief Return true while the effect timeline is advancing. */
    [[nodiscard]] bool isTimelinePlaying() const { return timelinePlaying_ && !timelinePaused_; }

    /**
     * @brief Calculate when every enabled finite emitter and its particles have ended.
     * @return Maximum non-looping lifetime, or one loop period when every enabled layer loops.
     * @remarks Owner-thread-only. Unbounded emitters return Unsupported instead of a guessed duration.
     *          When an authored timeline duration is set, the larger of the two values is returned.
     */
    [[nodiscard]] Result<Duration> naturalDuration() const;

    /**
     * @brief Atomically replace this instance from JSON text.
     * @remarks On failure the live emitters and clock are unchanged. On success
     *          world transform, visibility, and layer offset are preserved.
     */
    [[nodiscard]] Result<void> reloadFromText(const std::string& json);
    /**
     * @brief Atomically reload from `sourcePath`, or an explicit path override.
     * @remarks On failure the live emitters and clock are unchanged.
     */
    [[nodiscard]] Result<void> reloadFromFile(const std::string& path = {});

    /**
     * @brief Advance the effect-local timeline and fire due cues.
     * @param step Injected scheduler step; invalid durations are rejected.
     */
    [[nodiscard]] Result<void> advanceTimeline(const SimulationStep& step);
    /**
     * @brief Legacy seconds facade for timeline cues; consumes Result internally.
     * @remarks Prefer advanceTimeline. No-op when the timeline is stopped or paused.
     */
    void updateTimeline(float dt);
    /** @brief Advance every effect that auto-registered a playing timeline. */
    static void tickRegisteredTimelines(float dt);
    /** @brief Advance registered timelines from an injected scheduler step. */
    [[nodiscard]] static Result<void> advanceRegisteredTimelines(const SimulationStep& step);

    /** @brief Set the group transform origin in world space. */
    void setPosition(float x, float y);
    /** @brief Return the group origin X coordinate. */
    float getX() const { return x_; }
    /** @brief Return the group origin Y coordinate. */
    float getY() const { return y_; }
    /** @brief Rotate local offsets and emitter directions in radians. */
    void setRotation(float radians);
    /** @brief Return the group rotation in radians. */
    float getRotation() const { return rotation_; }
    /** @brief Scale local emitter offsets. */
    void setScale(float scale);
    /** @brief Return the local-offset scale. */
    float getScale() const { return scale_; }
    /** @brief Offset all emitter render layers while preserving asset-local ordering. */
    void setLayer(int layer);
    /** @brief Return the group render-layer offset. */
    int getLayer() const { return layer_; }
    /** @brief Set visibility for every emitter layer. */
    void setVisible(bool visible);
    /** @brief Return the group visibility state. */
    bool isVisible() const { return visible_; }

    /** @brief Start every enabled emitter and the effect timeline clock. */
    void start();
    /** @brief Stop every emitter timeline and the effect clock. */
    void stop();
    /** @brief Pause every emitter timeline and the effect clock. */
    void pause();
    /** @brief Reset every emitter timeline, clear particles, and rewind cues. */
    void reset();
    /** @brief Emit a manual burst from one named layer. */
    bool emit(const std::string& emitterName, int count);

    /** @brief Set one gameplay parameter on every layer that declares or binds it. */
    void setFloatParameter(const std::string& name, float value);
    /** @brief Return an effect parameter value, or zero when unknown. */
    float getFloatParameter(const std::string& name) const;
    /** @brief Return true when the asset declares the parameter. */
    bool hasFloatParameter(const std::string& name) const;

private:
    struct Layer {
        std::string      name;
        ParticleEmitter* emitter       = nullptr;
        float            offsetX       = 0.f;
        float            offsetY       = 0.f;
        float            baseDirection = 0.f;
        int              baseLayer     = 0;
        bool             enabled       = true;
    };
    struct RuntimeCue {
        ParticleEffectTimelineCue cue;
        bool                      fired = false;
    };

    ParticleEffect() = default;
    [[nodiscard]] static Result<ParticleEffect*> parse(const std::string& json,
                                                       const std::string& sourcePath);
    void syncTransform();
    void syncLayer();
    void wireEventRoutes();
    void rewindTimelineClock();
    void registerForTimelineTicks();
    void unregisterFromTimelineTicks();
    [[nodiscard]] Result<void> fireDueCues(double previousSeconds, double currentSeconds);
    [[nodiscard]] Result<void> applyCue(const ParticleEffectTimelineCue& cue);
    void adoptParsed(ParticleEffect&& other, bool preserveWorldState);

    int                                      version_ = 1;
    std::string                              sourcePath_;
    std::vector<Layer>                       layers_;
    std::unordered_map<std::string, float>   parameters_;
    std::vector<ParticleEffectEventRoute>    eventRoutes_;
    ParticleEffectTimeline                   timeline_;
    std::vector<RuntimeCue>                  runtimeCues_;
    double                                   timelineSeconds_ = 0.0;
    bool                                     timelinePlaying_ = false;
    bool                                     timelinePaused_  = false;
    bool                                     timelineRegistered_ = false;
    float                                    x_        = 0.f;
    float                                    y_        = 0.f;
    float                                    rotation_ = 0.f;
    float                                    scale_    = 1.f;
    int                                      layer_    = 0;
    bool                                     visible_  = true;
};

}  // namespace eve::particles
