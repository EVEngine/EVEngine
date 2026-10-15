#pragma once
#include "common/Export.h"

#include "common/Time.h"

#include <string>
#include <vector>

namespace eve::graphics {
class Graphics;
}

namespace eve::animation {

class AnimPose;

/**
 * @brief Motion trail / afterimage recorder + 2D trajectory drawer.
 *
 * Records timed samples (manual points or bone world positions) and draws a
 * fading path with `Graphics::drawSolidRect` (same dotted-segment style as Cloth).
 * Script type: `AnimTrail`.
 *
 * Typical frame:
 *   pose.computeWorld(sk);
 *   trail.sampleBone(pose, tipBone);  // or addPoint(x, y)
 *   trail.advance(step);
 *   // eve_render:
 *   trail.draw(gfx);
 *
 * Plane strings for 3D→2D projection: "xy" | "xz" | "yz".
 * Draw styles: "line" (connected segments) | "points" (sample dots only).
 */
class EVENGINE_API_WORLD AnimTrail {
public:
    /** @brief Anim trail. */
    explicit AnimTrail(int capacity = 64);
    /** @brief Anim trail. */
    ~AnimTrail() = default;

    AnimTrail(const AnimTrail &)            = delete;
    AnimTrail &operator=(const AnimTrail &) = delete;

    /** @brief Max retained samples (ring). Clamped to >= 2. */
    void setCapacity(int capacity);
    /** @brief Returns the capacity. */
    int  getCapacity() const { return capacity_; }

    /** @brief How long each sample lives (seconds). <= 0 keeps until capacity eviction. */
    void  setDuration(float seconds);
    /** @brief Returns the duration. */
    float getDuration() const { return duration_; }

    /** @brief Skip new samples closer than this distance to the newest point (0 = always add). */
    void  setMinDistance(float distance);
    /** @brief Returns the min distance. */
    float getMinDistance() const { return minDistance_; }

    /** @brief Sets the width. */
    void  setWidth(float pixels);
    /** @brief Returns the width. */
    float getWidth() const { return width_; }

    /** @brief Head (newest) color; alpha fades toward 0 along the trail when fade is on. */
    void  setColor(float r, float g, float b, float a = 1.f);
    /** @brief Returns the color r. */
    float getColorR() const { return colorR_; }
    /** @brief Returns the color g. */
    float getColorG() const { return colorG_; }
    /** @brief Returns the color b. */
    float getColorB() const { return colorB_; }
    /** @brief Returns the color a. */
    float getColorA() const { return colorA_; }

    /** @brief Sets the fade. */
    void setFade(bool enable);
    /** @brief Returns the fade. */
    bool getFade() const { return fade_; }

    /** @brief "line" | "points". Invalid → exception. */
    void        setStyle(const std::string &style);
    /** @brief Returns the style. */
    std::string getStyle() const;

    /**
     * @brief Applied at draw time: screen = (x,y) * scale + offset.
     * Useful when sampling bone world units into pixel space.
     */
    void  setDrawScale(float sx, float sy);
    /** @brief Returns the draw scale x. */
    float getDrawScaleX() const { return drawScaleX_; }
    /** @brief Returns the draw scale y. */
    float getDrawScaleY() const { return drawScaleY_; }
    /** @brief Sets the draw offset. */
    void  setDrawOffset(float ox, float oy);
    /** @brief Returns the draw offset x. */
    float getDrawOffsetX() const { return drawOffsetX_; }
    /** @brief Returns the draw offset y. */
    float getDrawOffsetY() const { return drawOffsetY_; }

    /** @brief Append a 2D sample (z stored as 0). */
    void addPoint(float x, float y);
    /** @brief Append a 3D sample (draw uses set plane / first two mapped axes via sampleBone). */
    void addPoint3(float x, float y, float z);

    /**
     * @brief Sample bone world position after `pose->computeWorld`.
     * `plane` maps axes to draw (x,y): "xy" | "xz" | "yz".
     * Optional local offset is applied in bone world space (translation only).
     */
    void sampleBone(const AnimPose *pose, int boneIndex, const std::string &plane = "xy");
    /** @brief Sample bone offset. */
    void sampleBoneOffset(const AnimPose *pose, int boneIndex, float ox, float oy, float oz,
                          const std::string &plane = "xy");

    /** @brief Clears . */
    void clear();

    /** @brief Age samples by one scheduler-owned deterministic step. */
    [[nodiscard]] eve::Result<void> advance(const eve::SimulationStep &step);
    /** @brief Legacy seconds facade; explicitly forwards to advance(). */
    void update(float dt);

    /** @brief Returns the point count. */
    int   getPointCount() const { return static_cast<int>(points_.size()); }
    /** @brief Index 0 = oldest retained sample; count-1 = newest. */
    float getPointX(int index) const;
    /** @brief Returns the point y. */
    float getPointY(int index) const;
    /** @brief Returns the point z. */
    float getPointZ(int index) const;
    /** @brief Returns the point age. */
    float getPointAge(int index) const;
    /** @brief Effective draw alpha in [0, colorA] after fade. */
    float getPointAlpha(int index) const;

    /** @brief Draw trajectory in 2D pixel/world space via Graphics. */
    void draw(graphics::Graphics *gfx);

private:
    enum class Style { Line, Points };

    struct Point {
        float x = 0.f, y = 0.f, z = 0.f;
        float age = 0.f;
    };

    void      pushPoint(float x, float y, float z);
    void      requireIndex(int index) const;
    void      projectPlane(float x, float y, float z, const std::string &plane, float *outX,
                         float *outY) const;
    float     alphaFor(const Point &p) const;
    void      toScreen(float x, float y, float *sx, float *sy) const;
    void      drawSegment(graphics::Graphics *gfx, float x0, float y0, float x1, float y1, float a0,
                        float a1) const;
    void      drawDot(graphics::Graphics *gfx, float x, float y, float alpha) const;

    int   capacity_    = 64;
    float duration_    = 0.5f;
    float minDistance_ = 0.f;
    float width_       = 3.f;
    float colorR_ = 1.f, colorG_ = 0.85f, colorB_ = 0.35f, colorA_ = 1.f;
    bool  fade_  = true;
    Style style_ = Style::Line;

    float drawScaleX_  = 1.f;
    float drawScaleY_  = 1.f;
    float drawOffsetX_ = 0.f;
    float drawOffsetY_ = 0.f;

    std::vector<Point> points_;
    eve::SimulationTick lastTick_    = eve::SimulationTick::zero();
    bool                hasLastTick_ = false;

    void updateUnchecked(float dt);
};

}  // namespace eve::animation
