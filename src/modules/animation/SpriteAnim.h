#pragma once

#include "common/Time.h"

#include <string>
#include <vector>

namespace eve::graphics {
class Quad;
class Renderable2D;
}

namespace eve::animation {

class Animation;
class SpriteClip;
class SpriteSheet;

/**
 * @brief 2D sprite-sheet clip player.
 *
 * Advances frame time, optionally keeps a bound Quad in sync with the current
 * sheet cell. Register with Animation for module-level `anim.advance(step)`.
 * Script type: `SpriteAnim`.
 */
class EVENGINE_API_WORLD SpriteAnim {
public:
    /** @brief Sprite anim. */
    SpriteAnim();
    /** @brief Sprite anim. */
    ~SpriteAnim();

    SpriteAnim(const SpriteAnim &)            = delete;
    SpriteAnim &operator=(const SpriteAnim &) = delete;

    /** @brief Optional sheet used by applyToQuad / bindQuad. */
    void         setSheet(SpriteSheet *sheet);
    /** @brief Returns the sheet. */
    SpriteSheet *getSheet() const { return sheet_; }

    /** @brief Play. */
    void       play(SpriteClip *clip);
    /** @brief Start at the clip end and play backward (speed becomes negative). */
    void       playReverse(SpriteClip *clip);
    /** @brief Stops . */
    void       stop();
    /** @brief Pause. */
    void       pause();
    /** @brief Resume. */
    void       resume();

    /** @brief Sets the speed. */
    void  setSpeed(float speed);
    /** @brief Returns the speed. */
    float getSpeed() const { return speed_; }
    /** @brief Add a piecewise-linear speed multiplier key at curve time in seconds. */
    void addSpeedCurveKey(float seconds, float multiplier);
    /** @brief Remove all speed-curve keys and restore constant-speed playback. */
    void clearSpeedCurve();
    /** @brief Restart speed-curve sampling from zero without changing clip time. */
    void resetSpeedCurve();
    /** @brief Choose whether speed-curve time wraps after its last key. */
    void setSpeedCurveLoop(bool loop) { speedCurveLoop_ = loop; }
    /** @brief Set speed-curve interpolation: linear, smooth, or cubic. */
    void setSpeedCurveInterpolation(const std::string &mode);
    /** @brief Return the current sampled curve multiplier. */
    float getSpeedCurveValue() const;
    /** @brief Sets the frame. */
    void setFrame(int clipFrame);
    /** @brief Step. */
    void step(int frames);
    /** @brief Play once. */
    void playOnce(SpriteClip *clip);
    /** @brief Queue. */
    void queue(SpriteClip *clip);
    /** @brief Consume event. */
    std::string consumeEvent();
    /** @brief Sets the time. */
    void  setTime(float seconds);
    /** @brief Returns the time. */
    float getTime() const { return time_; }
    /** @brief Sets the loop. */
    void  setLoop(bool loop);
    /** @brief Returns the loop. */
    bool  getLoop() const;

    /** @brief True when playing. */
    bool isPlaying() const { return playing_ && !paused_; }
    /** @brief True when paused. */
    bool isPaused() const { return paused_; }
    /** @brief True when finished. */
    bool isFinished() const { return finished_; }
    /** @brief Return loop boundaries crossed since play started. */
    int  getLoopCount() const { return loopCount_; }
    /** @brief Consume and clear the one-shot completion event. */
    bool consumeCompleted();
    /** @brief Consume and clear pending loop events, returning their count. */
    int  consumeLooped();

    /** @brief Returns the clip. */
    SpriteClip *getClip() const { return clip_; }
    /** @brief Index inside the current clip (0..clipFrameCount-1), or -1. */
    int getClipFrame() const { return clipFrame_; }
    /** @brief Sheet frame index for the current cell, or -1. */
    int getSheetFrame() const;

    /** @brief Keep this Quad's viewport updated each update/play. */
    void           bindQuad(graphics::Quad *quad);
    /** @brief Unbinds quad. */
    void           unbindQuad();
    /** @brief Returns the bound quad. */
    graphics::Quad *getBoundQuad() const { return boundQuad_; }
    /** @brief Bind a Sprite2D so trimmed frame layout is synchronized automatically. */
    void bindSprite(graphics::Renderable2D *sprite);

    /** @brief Write current sheet frame into quad (requires sheet). */
    void applyToQuad(graphics::Quad *quad) const;

    /**
     * @brief Advance playback. Returns true while still active (playing or paused).
     * Auto-applies to boundQuad when sheet is set.
     */
    /** @brief Advance playback by one scheduler-owned deterministic step. */
    [[nodiscard]] eve::Result<void> advance(const eve::SimulationStep &step);
    /** @brief Whether this sprite animation has consumed a scheduler step. */
    [[nodiscard]] bool hasCurrentTick() const noexcept { return hasLastTick_; }
    /** @brief Last scheduler tick consumed by this sprite animation. */
    [[nodiscard]] eve::SimulationTick currentTick() const noexcept { return lastTick_; }
    /** @brief Legacy seconds facade; explicitly forwards to advance(). */
    bool update(float dt);

private:
    friend class Animation;

    void setOwner(Animation *owner) { owner_ = owner; }
    Animation *owner() const { return owner_; }

    void refreshFrame();
    void syncBoundQuad();
    float sampleSpeedCurve(float seconds) const;

    struct SpeedKey {
        float time = 0.f;
        float value = 1.f;
    };

    Animation         *owner_      = nullptr;
    SpriteSheet       *sheet_      = nullptr;
    SpriteClip        *clip_       = nullptr;
    graphics::Quad    *boundQuad_  = nullptr;
    graphics::Renderable2D *boundSprite_ = nullptr;
    float              speed_      = 1.f;
    float              time_       = 0.f;
    int                clipFrame_  = -1;
    bool               playing_    = false;
    bool               paused_     = false;
    bool               finished_   = false;
    bool               loopOverride_ = false;
    bool               loopValue_    = true;
    int                loopCount_    = 0;
    int                pendingLoops_ = 0;
    bool               pendingComplete_ = false;
    std::vector<SpeedKey> speedCurve_;
    float              speedCurveTime_ = 0.f;
    bool               speedCurveLoop_ = true;
    std::string        speedCurveInterpolation_ = "linear";
    SpriteClip        *queuedClip_ = nullptr;
    std::string        pendingEvent_;
    eve::SimulationTick     lastTick_    = eve::SimulationTick::zero();
    bool                    hasLastTick_ = false;

    bool updateUnchecked(float dt);
};

}  // namespace eve::animation
