#pragma once
#include "common/Export.h"

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
class EVENGINE_API_WORLD SpineAnim {
public:
    /** @brief Spine anim. */
    explicit SpineAnim(SpineSkeleton *skeleton);
    /** @brief Spine anim. */
    ~SpineAnim();

    SpineAnim(const SpineAnim &)            = delete;
    SpineAnim &operator=(const SpineAnim &) = delete;

    /** @brief Returns the skeleton. */
    SpineSkeleton *getSkeleton() const { return skeleton_; }

    /** @brief Sets the atlas. */
    void       setAtlas(SpineAtlas *atlas);
    /** @brief Returns the atlas. */
    SpineAtlas *getAtlas() const { return atlas_; }

    /** @brief Bind a GPU texture to an atlas page (by index or page image name). */
    void setPageTexture(int pageIndex, graphics::Texture *texture);
    /** @brief Sets the page texture by name. */
    void setPageTextureByName(const std::string &pageName, graphics::Texture *texture);
    /** @brief Returns the page texture. */
    graphics::Texture *getPageTexture(int pageIndex) const;

    /** @brief Play. */
    bool play(const std::string &animationName);
    /** @brief Stops . */
    void stop();
    /** @brief Pause. */
    void pause();
    /** @brief Resume. */
    void resume();

    /** @brief Sets the speed. */
    void  setSpeed(float speed);
    /** @brief Returns the speed. */
    float getSpeed() const { return speed_; }
    /** @brief Sets the time. */
    void  setTime(float seconds);
    /** @brief Returns the time. */
    float getTime() const { return time_; }
    /** @brief Sets the loop. */
    void  setLoop(bool loop) { loop_ = loop; }
    /** @brief Returns the loop. */
    bool  getLoop() const { return loop_; }

    /** @brief When true (default), Spine Y-up is flipped for screen Y-down draw items. */
    void setFlipY(bool flip) { flipY_ = flip; }
    /** @brief Returns the flip y. */
    bool getFlipY() const { return flipY_; }

    /** @brief Sets the position. */
    void  setPosition(float x, float y);
    /** @brief Returns the x. */
    float getX() const { return x_; }
    /** @brief Returns the y. */
    float getY() const { return y_; }
    /** @brief Sets the scale. */
    void  setScale(float sx, float sy);
    /** @brief Returns the scale x. */
    float getScaleX() const { return scaleX_; }
    /** @brief Returns the scale y. */
    float getScaleY() const { return scaleY_; }
    /** @brief Sets the layer. */
    void  setLayer(int layer) { layer_ = layer; }
    /** @brief Returns the layer. */
    int   getLayer() const { return layer_; }
    /** @brief Sets the color. */
    void  setColor(float r, float g, float b, float a = 1.f);

    /** @brief True when playing. */
    bool isPlaying() const { return playing_ && !paused_; }
    /** @brief True when paused. */
    bool isPaused() const { return paused_; }
    /** @brief True when finished. */
    bool isFinished() const { return finished_; }
    /** @brief Returns the animation. */
    std::string getAnimation() const { return animName_; }
    /** @brief Returns the animation duration. */
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
    /** @brief Returns the draw slot x. */
    float getDrawSlotX(int index) const;
    /** @brief Returns the draw slot y. */
    float getDrawSlotY(int index) const;
    /** @brief Returns the draw slot width. */
    float getDrawSlotWidth(int index) const;
    /** @brief Returns the draw slot height. */
    float getDrawSlotHeight(int index) const;
    /** @brief Returns the draw slot rotation. */
    float getDrawSlotRotation(int index) const;
    /** @brief Returns the draw slot page. */
    int   getDrawSlotPage(int index) const;
    /** @brief Returns the draw slot region. */
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
