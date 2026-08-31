#pragma once

namespace eve::graphics {

class Canvas;
class Graphics;
class Shader;
class Texture;

/** @brief GPU percentile metering, eye adaptation, and linear HDR pre-exposure. */
class Exposure {
public:
    /** @brief Create cross-backend exposure shaders and 1x1 history targets. */
    explicit Exposure(Graphics *gfx);

    /**
     * @brief Apply manual and optional automatic exposure to a linear HDR source.
     * @param source Linear HDR source texture.
     * @param manualExposure Linear manual exposure multiplier.
     * @param automatic Enable percentile metering and eye adaptation.
     * @param minEV Minimum automatic exposure EV.
     * @param maxEV Maximum automatic exposure EV.
     * @param meterSource Optional pre-bloom HDR source used for luminance metering.
     * @param deltaSeconds Frame delta used by eye adaptation.
     * @return Full-resolution pre-exposed linear HDR texture.
     * @lifetime The returned texture is owned by this Exposure until the next target resize.
     */
    Texture *apply(Texture *source, float manualExposure, bool automatic, float minEV,
                   float maxEV, Texture *meterSource = nullptr,
                   float deltaSeconds = 1.f / 60.f);

    /** @brief Invalidate eye-adaptation history, for camera cuts and mode changes. */
    void invalidateHistory() { historyValid_ = false; }

private:
    void ensureTargets(int width, int height);

    Graphics *gfx_ = nullptr;
    Shader *meterShader_ = nullptr;
    Shader *adaptShader_ = nullptr;
    Shader *applyShader_ = nullptr;
    Canvas *meter_ = nullptr;
    Canvas *historyA_ = nullptr;
    Canvas *historyB_ = nullptr;
    Canvas *historyRead_ = nullptr;
    Canvas *historyWrite_ = nullptr;
    Canvas *output_ = nullptr;
    int width_ = 0;
    int height_ = 0;
    bool historyValid_ = false;
};

}  // namespace eve::graphics
