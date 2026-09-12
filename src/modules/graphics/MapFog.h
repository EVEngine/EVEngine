#pragma once

namespace eve::graphics {

class Graphics;
class Shader;
class Texture;

/**
 * @brief SLG / large-map war-fog overlay driven by a dual-scrolling cloud texture
 *        and an RGBA mask bridge (R unlock, G select, B dissolve).
 *
 * Presentation only: gameplay unlock state should live in map::Fov / game data;
 * upload that state into the mask R channel and call draw() after the map.
 */
class MapFog {
public:
    /** @brief Create a fog overlay renderer backed by the supplied graphics device. */
    explicit MapFog(Graphics *graphics);
    ~MapFog();

    MapFog(const MapFog &)            = delete;
    MapFog &operator=(const MapFog &) = delete;

    /** @brief Advance dual-scroll / blink timers. @param dt Seconds since last frame. */
    void update(float dt);

    /** @brief Override the animated time used by cloud scroll (seconds). */
    void setTime(float time);
    /** @brief Return the current animation time in seconds. */
    float getTime() const { return time_; }

    /**
     * @brief Set or replace the repeating cloud/color texture.
     * @param cloud Borrowed Graphics-owned texture; null falls back to a built-in noise cloud.
     * @ownership Does not take ownership of `cloud`; Graphics retains texture ownership.
     * @lifetime `cloud` must outlive subsequent draw() calls that use it.
     */
    void setCloudTexture(Texture *cloud);
    /**
     * @brief Return the active cloud texture (never null after first ensureCloudTexture).
     * @ownership Borrowed; Graphics owns the texture storage.
     * @lifetime Remains valid until Graphics releases the texture or shutdown.
     */
    Texture *getCloudTexture();

    /**
     * @brief Set the RGBA mask bridge texture.
     * @param mask Borrowed texture; R=unlocked, G=selected, B=dissolve threshold.
     * @ownership Does not take ownership of `mask`; the caller/Graphics retains it.
     * @lifetime `mask` must outlive subsequent draw() calls that use it.
     */
    void setMaskTexture(Texture *mask);
    /**
     * @brief Return the current mask texture, or null if unset.
     * @ownership Borrowed; ownership stays with the provider that created the texture.
     * @lifetime Valid until the provider releases the texture or this MapFog is destroyed.
     */
    Texture *getMaskTexture() const { return mask_; }

    /** @brief Dual-layer cloud tiling (layer A / B). */
    void setCloudTiling(float tileA, float tileB);
    /** @brief Dual-layer scroll speeds in UV units per second. */
    void setCloudSpeed(float speedA, float speedB);
    /** @brief Mask-UV distortion amount from desaturated cloud noise. */
    void setDistort(float amount);
    /** @brief Correct additive UV drift introduced by distortion. */
    void setDistortFix(float x, float y);
    /** @brief Fog tint RGB in 0..1. */
    void setFogColor(float r, float g, float b);
    /** @brief Overall fog opacity in 0..1. */
    void setFogAlpha(float alpha);
    /** @brief Soft edge half-width around the mask R threshold. */
    void setEdgeSoftness(float softness);
    /** @brief Enable the pre-pass cloud shadow (must draw before the main pass). */
    void setShadowEnabled(bool enabled);
    /** @brief Shadow UV offset and strength. */
    void setShadow(float offsetX, float offsetY, float strength);
    /** @brief Selection blink amplitude for mask G. */
    void setSelectStrength(float strength);
    /** @brief Dissolve noise tiling scale. */
    void setDissolveScale(float scale);
    /** @brief Mix weight between the two scrolling cloud samples (0..1). */
    void setCloudMix(float mix);
    /** @brief Shape cloud alpha: contrast (soft→hard) and bias (coverage). */
    void setCloudDensity(float contrast, float bias);

    float getCloudTileA() const { return tileA_; }
    float getCloudTileB() const { return tileB_; }
    float getCloudSpeedA() const { return speedA_; }
    float getCloudSpeedB() const { return speedB_; }
    float getDistort() const { return distort_; }
    float getFogAlpha() const { return fogAlpha_; }
    float getEdgeSoftness() const { return edgeSoft_; }
    bool  getShadowEnabled() const { return shadowEnabled_; }
    float getSelectStrength() const { return selectStrength_; }

    /**
     * @brief Draw fog over a destination rectangle (shadow pass then main pass).
     * @param x Destination left edge.
     * @param y Destination top edge.
     * @param width Destination width.
     * @param height Destination height.
     */
    void draw(float x, float y, float width, float height);

    /**
     * @brief Build a seamless procedural cloud noise texture owned by Graphics.
     * @param size Edge length in pixels (clamped to [16, 512]).
     * @return Borrowed Graphics-owned texture, or null on failure.
     * @ownership Graphics owns the returned texture; the caller must not delete it.
     * @lifetime Remains valid until Graphics releases the texture or shutdown.
     */
    Texture *makeCloudTexture(int size = 128);

private:
    void syncUniforms(float passMode);
    void ensureCloudTexture();

    Graphics *graphics_   = nullptr;
    Shader   *shader_     = nullptr;
    Texture  *cloud_      = nullptr;
    Texture  *ownedCloud_ = nullptr;
    Texture  *mask_       = nullptr;

    float time_            = 0.f;
    float tileA_           = 0.55f;
    float tileB_           = 0.95f;
    float speedA_          = 0.008f;
    float speedB_          = 0.015f;
    float distort_         = 0.22f;
    float fixX_            = 0.f;
    float fixY_            = 0.f;
    float fogR_            = 0.92f;
    float fogG_            = 0.94f;
    float fogB_            = 0.98f;
    float fogAlpha_        = 0.90f;
    float edgeSoft_        = 0.14f;
    bool  shadowEnabled_   = true;
    float shadowOffX_      = 0.034f;
    float shadowOffY_      = 0.048f;
    float shadowStrength_  = 0.78f;
    float selectStrength_  = 0.85f;
    float dissolveScale_   = 1.8f;
    float cloudMix_        = 0.35f;
    float densityContrast_ = 0.42f;
    float densityBias_     = 0.16f;
    float drawAspect_      = 1.f;
};

}  // namespace eve::graphics
