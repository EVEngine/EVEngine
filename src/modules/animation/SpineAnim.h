#pragma once

#include "common/Time.h"

#include <string>
#include <vector>
#include "animation/SpineSkeletonData.h"

namespace eve::graphics {
struct DrawItem2D;
class Texture;
}  // namespace eve::graphics

#include <string>
#include <vector>

namespace eve::graphics {
class Graphics;
}

namespace eve::animation {

class Animation;
class SpineAtlas;
class SpineSkeleton;

/**
 * @brief Spine animation player + 2D draw collector (region attachments).
 *
 * Applies bone/slot timelines onto a SpineSkeleton, then emits DrawItem2D
 * quads for the shared 2D queue. Bind atlas page textures before collecting.
 * Script type: `SpineAnim`.
 */
class SpineAnim {
public:
    explicit SpineAnim(SpineSkeleton *skeleton);
    ~SpineAnim();

    SpineAnim(const SpineAnim &)            = delete;
    SpineAnim &operator=(const SpineAnim &) = delete;

    SpineSkeleton *getSkeleton() const { return skeleton_; }

    void       setAtlas(SpineAtlas *atlas);
    SpineAtlas *getAtlas() const { return atlas_; }

    /** @brief Bind a GPU texture to an atlas page (by index or page image name). */
    void setPageTexture(int pageIndex, graphics::Texture *texture);
    void setPageTextureByName(const std::string &pageName, graphics::Texture *texture);
    graphics::Texture *getPageTexture(int pageIndex) const;

    bool play(const std::string &animationName);
    void stop();
    void pause();
    void resume();

    void  setSpeed(float speed);
    float getSpeed() const { return speed_; }
    void  setTime(float seconds);
    float getTime() const { return time_; }
    void  setLoop(bool loop) { loop_ = loop; }
    bool  getLoop() const { return loop_; }

    /** @brief When true (default), Spine Y-up is flipped for screen Y-down draw items. */
    void setFlipY(bool flip) { flipY_ = flip; }
    bool getFlipY() const { return flipY_; }

    void  setPosition(float x, float y);
    float getX() const { return x_; }
    float getY() const { return y_; }
    void  setScale(float sx, float sy);
    float getScaleX() const { return scaleX_; }
    float getScaleY() const { return scaleY_; }
    void  setLayer(int layer) { layer_ = layer; }
    int   getLayer() const { return layer_; }
    void  setColor(float r, float g, float b, float a = 1.f);

    bool isPlaying() const { return playing_ && !paused_; }
    bool isPaused() const { return paused_; }
    bool isFinished() const { return finished_; }
    std::string getAnimation() const { return animName_; }
    float       getAnimationDuration() const;

    /** @brief Apply current animation time to skeleton and update world transforms. */
    void apply();

    /** @brief Advance playback by one scheduler-owned deterministic step. */
    [[nodiscard]] eve::Result<void> advance(const eve::SimulationStep &step);
    /** @brief Whether this Spine animation has consumed a scheduler step. */
    [[nodiscard]] bool hasCurrentTick() const noexcept { return hasLastTick_; }
    /** @brief Last scheduler tick consumed by this Spine animation. */
    [[nodiscard]] eve::SimulationTick currentTick() const noexcept { return lastTick_; }
    /** @brief Legacy seconds facade; explicitly forwards to advance(). */
    bool update(float dt);

    /** @brief Append region attachment quads into the shared 2D draw queue. */
    void collectDrawItems(std::vector<graphics::DrawItem2D> &out);
    /** @brief Draw the current pose into an existing frame without clearing or presenting it. */
    void draw(graphics::Graphics *gfx);

    /**
     * @brief Query posed draw slots without textures (for unit tests).
     * Returns number of visible region attachments after apply().
     */
    int   getDrawSlotCount() const;
    float getDrawSlotX(int index) const;
    float getDrawSlotY(int index) const;
    float getDrawSlotWidth(int index) const;
    float getDrawSlotHeight(int index) const;
    float getDrawSlotRotation(int index) const;
    int   getDrawSlotPage(int index) const;
    std::string getDrawSlotRegion(int index) const;

private:
    friend class Animation;

    struct DrawSlot {
        float       x = 0.f, y = 0.f;
        float       w = 0.f, h = 0.f;
        float       rotation = 0.f;
        int         page     = 0;
        int         region   = -1;
        int         order    = 0;  // slot draw order (back-to-front)
        bool        rotated  = false;  // atlas region packed rotated (90°)
        std::string regionName;
        float       u0 = 0.f, v0 = 0.f, u1 = 1.f, v1 = 1.f;
    };

    void setOwner(Animation *owner) { owner_ = owner; }
    Animation *owner() const { return owner_; }

    void rebuildDrawSlots();
    void checkDrawSlot(int index) const;

    static float sampleFloat(const std::vector<SpineSkeletonData::FloatKey> &keys, float time,
                             float fallback);
    static void  sampleTranslate(const std::vector<SpineSkeletonData::TranslateKey> &keys,
                                 float time, float &x, float &y);
    static void  sampleScale(const std::vector<SpineSkeletonData::ScaleKey> &keys, float time,
                             float &x, float &y);
    static std::string sampleAttachment(const std::vector<SpineSkeletonData::AttachmentKey> &keys,
                                        float time, const std::string &fallback);

    Animation                         *owner_     = nullptr;
    SpineSkeleton                     *skeleton_  = nullptr;
    SpineAtlas                        *atlas_     = nullptr;
    std::vector<graphics::Texture *>   pageTextures_;
    int                                animIndex_ = -1;
    std::string                        animName_;
    float                              speed_     = 1.f;
    float                              time_      = 0.f;
    bool                               loop_      = true;
    bool                               playing_   = false;
    bool                               paused_    = false;
    bool                               finished_  = false;
    bool                               flipY_     = true;
    float                              x_ = 0.f, y_ = 0.f;
    float                              scaleX_ = 1.f, scaleY_ = 1.f;
    int                                layer_ = 0;
    float                              r_ = 1.f, g_ = 1.f, b_ = 1.f, a_ = 1.f;
    std::vector<DrawSlot>              drawSlots_;
    eve::SimulationTick                lastTick_    = eve::SimulationTick::zero();
    bool                               hasLastTick_ = false;

    bool updateUnchecked(float dt);
};

}  // namespace eve::animation
