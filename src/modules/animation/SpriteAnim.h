#pragma once

#include <string>
#include <vector>

namespace eve::graphics {
class Quad;
}

namespace eve::animation {

class Animation;
class SpriteClip;
class SpriteSheet;

/**
 * 2D sprite-sheet clip player.
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

    /** Optional sheet used by applyToQuad / bindQuad. */
    void         setSheet(SpriteSheet *sheet);
    SpriteSheet *getSheet() const { return sheet_; }

    void       play(SpriteClip *clip);
    void       stop();
    void       pause();
    void       resume();

    void  setSpeed(float speed);
    float getSpeed() const { return speed_; }
    void  setTime(float seconds);
    float getTime() const { return time_; }
    void  setLoop(bool loop);
    bool  getLoop() const;

    bool isPlaying() const { return playing_ && !paused_; }
    bool isPaused() const { return paused_; }
    bool isFinished() const { return finished_; }

    SpriteClip *getClip() const { return clip_; }
    /** Index inside the current clip (0..clipFrameCount-1), or -1. */
    int getClipFrame() const { return clipFrame_; }
    /** Sheet frame index for the current cell, or -1. */
    int getSheetFrame() const;

    /** Keep this Quad's viewport updated each update/play. */
    void           bindQuad(graphics::Quad *quad);
    void           unbindQuad();
    graphics::Quad *getBoundQuad() const { return boundQuad_; }

    /** Write current sheet frame into quad (requires sheet). */
    void applyToQuad(graphics::Quad *quad) const;

    /**
     * Advance playback. Returns true while still active (playing or paused).
     * Auto-applies to boundQuad when sheet is set.
     */
    bool update(float dt);

private:
    friend class Animation;

    void setOwner(Animation *owner) { owner_ = owner; }
    Animation *owner() const { return owner_; }

    void refreshFrame();
    void syncBoundQuad();

    Animation         *owner_      = nullptr;
    SpriteSheet       *sheet_      = nullptr;
    SpriteClip        *clip_       = nullptr;
    graphics::Quad    *boundQuad_  = nullptr;
    float              speed_      = 1.f;
    float              time_       = 0.f;
    int                clipFrame_  = -1;
    bool               playing_    = false;
    bool               paused_     = false;
    bool               finished_   = false;
    bool               loopOverride_ = false;
    bool               loopValue_    = true;
};

}  // namespace eve::animation
