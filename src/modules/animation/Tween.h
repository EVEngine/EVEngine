#pragma once
#include "common/Export.h"


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
class EVENGINE_API_WORLD Tween {
public:
    /** @brief Tween. */
    explicit Tween(float duration = 1.f);
    /** @brief Tween. */
    ~Tween();

    Tween(const Tween &)            = delete;
    Tween &operator=(const Tween &) = delete;

    /** @brief Sets the from. */
    void setFrom(const std::string &name, float value);
    /** @brief Sets the to. */
    void setTo(const std::string &name, float value);
    /** @brief Relative change: end = start + delta (resolved when start() runs). */
    void setDelta(const std::string &name, float delta);

    /** @brief Sets the from angle. */
    void setFromAngle(const std::string &name, float radians);
    /** @brief Sets the to angle. */
    void setToAngle(const std::string &name, float radians);
    /** @brief Sets the delta angle. */
    void setDeltaAngle(const std::string &name, float deltaRadians);

    /** @brief True when active. */
    bool  has(const std::string &name) const;
    /** @brief Returns the value. */
    float get(const std::string &name) const;
    /** @brief Returns the from. */
    float getFrom(const std::string &name) const;
    /** @brief Returns the to. */
    float getTo(const std::string &name) const;
    /** @brief Returns the delta. */
    float getDelta(const std::string &name) const;

    /** @brief Sets the duration. */
    void        setDuration(float seconds);
    /** @brief Returns the duration. */
    float       getDuration() const { return duration_; }
    /** @brief Sets the delay. */
    void        setDelay(float seconds);
    /** @brief Returns the delay. */
    float       getDelay() const { return delay_; }
    /** @brief Sets the ease. */
    void        setEase(const std::string &kind);
    /** @brief Returns the ease. */
    std::string getEase() const { return ease_; }

    /**
     * @brief Play count: 1 = once (default), N = N cycles, -1 = infinite.
     * Values < -1 are clamped to -1.
     */
    void setRepeat(int count);
    /** @brief Returns the repeat. */
    int  getRepeat() const { return repeat_; }
    /** @brief Sets the yoyo. */
    void setYoyo(bool enabled) { yoyo_ = enabled; }
    /** @brief Returns the yoyo. */
    bool getYoyo() const { return yoyo_; }

    /** @brief Starts . */
    void start();
    /** @brief Pause. */
    void pause();
    /** @brief Resume. */
    void resume();
    /** @brief Stops . */
    void stop();
    /** @brief Resets . */
    void reset();

    /** @brief True when running. */
    bool isRunning() const { return state_ == State::Running; }
    /** @brief True when paused. */
    bool isPaused() const { return state_ == State::Paused; }
    /** @brief True when finished. */
    bool isFinished() const { return state_ == State::Finished; }
    /** @brief True when stopped. */
    bool isStopped() const { return state_ == State::Idle || state_ == State::Stopped; }
    /** @brief True when delayed. */
    bool isDelayed() const { return state_ == State::Delayed; }
    /** @brief True when active. */
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
    [[nodiscard]] eve::Result<void> advance(const eve::SimulationStep &step);
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
    /** @brief Returns the property name. */
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
    eve::SimulationTick                     lastTick_    = eve::SimulationTick::zero();
    bool                                    hasLastTick_ = false;

    bool updateUnchecked(float dt);
};

}  // namespace eve::animation
