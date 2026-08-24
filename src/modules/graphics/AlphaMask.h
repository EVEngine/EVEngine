#pragma once

namespace eve::graphics {

class Graphics;
class Shader;
class Texture;

/** @brief Reusable two-texture alpha mask for immediate 2D composition. */
class AlphaMask {
public:
    /** @brief Create a mask renderer backed by the supplied graphics device. */
    explicit AlphaMask(Graphics *graphics);
    ~AlphaMask();

    /** @brief Set the mask cutoff in normalized alpha space. */
    void setThreshold(float threshold);
    /** @brief Return the current normalized cutoff. */
    float getThreshold() const { return threshold_; }
    /** @brief Set the normalized soft-edge half width. */
    void setSoftness(float softness);
    /** @brief Return the current soft-edge width. */
    float getSoftness() const { return softness_; }
    /** @brief Invert mask coverage. */
    void setInverted(bool inverted);
    /** @brief Return whether mask coverage is inverted. */
    bool getInverted() const { return inverted_; }

    /**
     * @brief Draw a color texture multiplied by a same-UV alpha mask.
     * @param color Source color texture.
     * @param mask Mask texture; its red channel supplies coverage.
     * @param x Destination left edge.
     * @param y Destination top edge.
     * @param width Destination width.
     * @param height Destination height.
     * @param r Red tint.
     * @param g Green tint.
     * @param b Blue tint.
     * @param a Alpha tint.
     */
    void draw(Texture *color, Texture *mask, float x, float y, float width, float height,
              float r = 1.f, float g = 1.f, float b = 1.f, float a = 1.f);

private:
    void syncUniforms();

    Graphics *graphics_ = nullptr;
    Shader *shader_ = nullptr;
    float threshold_ = 0.5f;
    float softness_ = 0.02f;
    bool inverted_ = false;
};

}  // namespace eve::graphics
