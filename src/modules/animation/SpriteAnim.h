#pragma once

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
 * sheet cell. Register with Animation for module-level `anim.update(dt)`.
 * Script type: `SpriteAnim`.
 */
class SpriteAnim {
public:
    SpriteAnim();
    ~SpriteAnim();

    SpriteAnim(const SpriteAnim &)            = delete;
    SpriteAnim &operator=(const SpriteAnim &) = delete;

    /** @brief Optional sheet used by applyToQuad / bindQuad. */
    void         setSheet(SpriteSheet *sheet);
    SpriteSheet *getSheet() const { return sheet_; }

    void       play(SpriteClip *clip);
    /** @brief Start at the clip end and play backward (speed becomes negative). */
    void       playReverse(SpriteClip *clip);
    void       stop();
    void       pause();
    void       resume();

    void  setSpeed(float speed);
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
    void setFrame(int clipFrame);
    void step(int frames);
    void playOnce(SpriteClip *clip);
    void queue(SpriteClip *clip);
    std::string consumeEvent();
    void  setTime(float seconds);
    float getTime() const { return time_; }
    void  setLoop(bool loop);
    bool  getLoop() const;

    bool isPlaying() const { return playing_ && !paused_; }
    bool isPaused() const { return paused_; }
    bool isFinished() const { return finished_; }
    /** @brief Return loop boundaries crossed since play started. */
    int  getLoopCount() const { return loopCount_; }
    /** @brief Consume and clear the one-shot completion event. */
    bool consumeCompleted();
    /** @brief Consume and clear pending loop events, returning their count. */
    int  consumeLooped();

    SpriteClip *getClip() const { return clip_; }
    /** @brief Index inside the current clip (0..clipFrameCount-1), or -1. */
    int getClipFrame() const { return clipFrame_; }
    /** @brief Sheet frame index for the current cell, or -1. */
    int getSheetFrame() const;

    /** @brief Keep this Quad's viewport updated each update/play. */
    void           bindQuad(graphics::Quad *quad);
    void           unbindQuad();
    graphics::Quad *getBoundQuad() const { return boundQuad_; }
    /** @brief Bind a Sprite2D so trimmed frame layout is synchronized automatically. */
    void bindSprite(graphics::Renderable2D *sprite);

    /** @brief Write current sheet frame into quad (requires sheet). */
    void applyToQuad(graphics::Quad *quad) const;

    /**
     * @brief Advance playback. Returns true while still active (playing or paused).
     * Auto-applies to boundQuad when sheet is set.
     */
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
};

}  // namespace eve::animation
