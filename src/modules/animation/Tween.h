#pragma once

#include "common/Time.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace eve::animation {

class Animation;

/**
 * @brief Property tween — interpolates named float properties from → to over time.
 *
 * Absolute end: setFrom + setTo.
 * Relative delta (“变动差值”): setFrom + setDelta (to = from + delta at start),
 *   or setDelta alone (from defaults to current/0).
 * Angle tracks use shortest-path lerp (setToAngle / setDeltaAngle).
 *
 * No API overloads; ease kind is a string (same set as Math.ease).
 */
class Tween {
public:
    explicit Tween(float duration = 1.f);
    ~Tween();

    Tween(const Tween &)            = delete;
    Tween &operator=(const Tween &) = delete;

    void setFrom(const std::string &name, float value);
    void setTo(const std::string &name, float value);
    /** @brief Relative change: end = start + delta (resolved when start() runs). */
    void setDelta(const std::string &name, float delta);

    void setFromAngle(const std::string &name, float radians);
    void setToAngle(const std::string &name, float radians);
    void setDeltaAngle(const std::string &name, float deltaRadians);

    bool  has(const std::string &name) const;
    float get(const std::string &name) const;
    float getFrom(const std::string &name) const;
    float getTo(const std::string &name) const;
    float getDelta(const std::string &name) const;

    void        setDuration(float seconds);
    float       getDuration() const { return duration_; }
    void        setDelay(float seconds);
    float       getDelay() const { return delay_; }
    void        setEase(const std::string &kind);
    std::string getEase() const { return ease_; }

    /**
     * @brief Play count: 1 = once (default), N = N cycles, -1 = infinite.
     * Values < -1 are clamped to -1.
     */
    void setRepeat(int count);
    int  getRepeat() const { return repeat_; }
    void setYoyo(bool enabled) { yoyo_ = enabled; }
    bool getYoyo() const { return yoyo_; }

    void start();
    void pause();
    void resume();
    void stop();
    void reset();

    bool isRunning() const { return state_ == State::Running; }
    bool isPaused() const { return state_ == State::Paused; }
    bool isFinished() const { return state_ == State::Finished; }
    bool isStopped() const { return state_ == State::Idle || state_ == State::Stopped; }
    bool isDelayed() const { return state_ == State::Delayed; }
    bool isActive() const {
        return state_ == State::Delayed || state_ == State::Running || state_ == State::Paused;
    }

    /** @brief Elapsed time in the current cycle (excludes delay). */
    float getElapsed() const { return elapsed_; }
    /** @brief Linear progress in the current cycle, [0,1]. */
    float getProgress() const;
    /** @brief Eased progress used for interpolation, [0,1]. */
    float getEasedProgress() const;

    /** @brief Advance by one scheduler-owned deterministic step. */
    [[nodiscard]] eve::Result<void> advance(const eve::SimulationStep& step);
    /** @brief Whether this tween has consumed a scheduler step. */
    [[nodiscard]] bool hasCurrentTick() const noexcept { return hasLastTick_; }
    /** @brief Last scheduler tick consumed by this tween. */
    [[nodiscard]] eve::SimulationTick currentTick() const noexcept { return lastTick_; }
    /** @brief Advance by dt seconds; legacy facade that forwards to advance(). */
    bool update(float dt);

    /** @brief Sample property at linear t in [0,1] using current from/to (no state change). */
    float evaluate(const std::string &name, float t) const;

    /** @brief Names of all property tracks (stable order = insertion order). */
    int         getPropertyCount() const { return static_cast<int>(order_.size()); }
    std::string getPropertyName(int index) const;

private:
    friend class Animation;

    enum class State { Idle, Delayed, Running, Paused, Finished, Stopped };

    enum class EndMode { Absolute, Delta };

    struct Track {
        float   from     = 0.f;
        float   to       = 0.f;
        float   delta    = 0.f;
        float   current  = 0.f;
        bool    hasFrom  = false;
        EndMode endMode  = EndMode::Absolute;
        bool    isAngle  = false;
        bool    resolved = false;
    };

    void          setOwner(Animation *owner) { owner_ = owner; }
    Animation    *owner() const { return owner_; }
    Track        &ensureTrack(const std::string &name);
    const Track  *findTrack(const std::string &name) const;
    void          resolveTracks();
    void          applyProgress(float linearT);
    void          finishCycle();
    static float  ease(float t, const std::string &kind);
    static float  lerpAngle(float a, float b, float t);
    static float  clamp01(float t);

    Animation                              *owner_    = nullptr;
    float                                   duration_ = 1.f;
    float                                   delay_    = 0.f;
    float                                   delayLeft_ = 0.f;
    float                                   elapsed_  = 0.f;
    int                                     repeat_   = 1;
    int                                     played_   = 0;
    bool                                    yoyo_     = false;
    bool                                    reverse_  = false;
    std::string                             ease_     = "linear";
    State                                   state_    = State::Idle;
    std::unordered_map<std::string, Track>  tracks_;
    std::vector<std::string>                order_;
    eve::SimulationTick                     lastTick_ = eve::SimulationTick::zero();
    bool                                    hasLastTick_ = false;

    bool updateUnchecked(float dt);
};

}  // namespace eve::animation
