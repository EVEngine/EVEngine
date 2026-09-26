#pragma once

#include "graphics/Color.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace eve::graphics::display {

/** @brief Active present color encoding after swapchain selection. */
enum class ActiveColorSpace : uint8_t { Sdr = 0, ScRgb = 1, Hdr10 = 2 };

/** @brief Requested present preference; Auto picks Hdr10 then ScRgb then Sdr. */
enum class OutputMode : uint8_t { Sdr = 0, Auto = 1, Hdr10 = 2, ScRgb = 3 };

struct Support {
    bool sdr = true;
    bool hdr10 = false;
    bool scRgb = false;
};

inline constexpr float kDefaultPaperWhiteNits = 200.f;
inline constexpr float kDefaultPeakNits = 1000.f;

/** @brief Clamp paper-white / peak luminance into the engine-supported range. */
inline void clampCalibration(float &paperWhiteNits, float &peakNits) noexcept {
    paperWhiteNits = std::clamp(paperWhiteNits, 80.f, 400.f);
    peakNits = std::clamp(peakNits, std::max(paperWhiteNits, 200.f), 10000.f);
}

/** @brief Pack paper-white and peak nits into one float for the present resolve tint. */
inline float packNits(float paperWhiteNits, float peakNits) noexcept {
    clampCalibration(paperWhiteNits, peakNits);
    const float paperQ = std::round(paperWhiteNits * 10.f);
    const float peakQ = std::round(peakNits / 10.f);
    return paperQ + peakQ * 65536.f;
}

/** @brief Unpack paper-white and peak nits from the present resolve tint channel. */
inline void unpackNits(float packed, float &paperWhiteNits, float &peakNits) noexcept {
    const float rounded = std::round(packed);
    paperWhiteNits = std::fmod(rounded, 65536.f) * 0.1f;
    peakNits = std::floor(rounded / 65536.f) * 10.f;
    clampCalibration(paperWhiteNits, peakNits);
}

/** @brief Encode the active present mode into the resolve tint G channel. */
inline float packActiveMode(ActiveColorSpace space) noexcept {
    switch (space) {
        case ActiveColorSpace::ScRgb:
            return 1.f;
        case ActiveColorSpace::Hdr10:
            return 2.f;
        case ActiveColorSpace::Sdr:
        default:
            return 0.f;
    }
}

/** @brief Decode the present mode from the resolve tint G channel. */
inline ActiveColorSpace unpackActiveMode(float packed) noexcept {
    const int mode = int(std::round(packed));
    if (mode == 1) return ActiveColorSpace::ScRgb;
    if (mode == 2) return ActiveColorSpace::Hdr10;
    return ActiveColorSpace::Sdr;
}

/** @brief Build the Color tint used by the final scene present resolve. */
inline Color sceneResolveTint(bool aces, ActiveColorSpace space, bool attachmentEncodesSrgb,
                              float paperWhiteNits, float peakNits) noexcept {
    const float encodeSrgb = (!attachmentEncodesSrgb && space == ActiveColorSpace::Sdr) ? 65536.f : 0.f;
    return Color(aces ? 1.f : 0.f, packActiveMode(space), packNits(paperWhiteNits, peakNits), encodeSrgb);
}

/** @brief Build the Color tint used when compositing SDR UI onto an HDR swapchain. */
inline Color uiResolveTint(ActiveColorSpace space, float paperWhiteNits, float peakNits) noexcept {
    // r < 0 selects the UI passthrough+encode path in scene_tonemap.frag.
    return Color(-1.f, packActiveMode(space), packNits(paperWhiteNits, peakNits), 0.f);
}

inline float luminanceRec709(float r, float g, float b) noexcept {
    return 0.2126f * r + 0.7152f * g + 0.0722f * b;
}

/** @brief Convert display-linear Rec.709 RGB into Rec.2020. */
inline void rec709ToRec2020(float &r, float &g, float &b) noexcept {
    const float nr = 0.6274040f * r + 0.3292820f * g + 0.0433136f * b;
    const float ng = 0.0690970f * r + 0.9195400f * g + 0.0113612f * b;
    const float nb = 0.0163916f * r + 0.0880132f * g + 0.8955950f * b;
    r = nr;
    g = ng;
    b = nb;
}

/** @brief Apply the ST.2084 PQ OETF to a [0,1] normalized luminance sample. */
inline float linearToPq(float v) noexcept {
    v = std::max(v, 0.f);
    constexpr float m1 = 2610.f / 4096.f / 4.f;
    constexpr float m2 = 2523.f / 4096.f * 128.f;
    constexpr float c1 = 3424.f / 4096.f;
    constexpr float c2 = 2413.f / 4096.f * 32.f;
    constexpr float c3 = 2392.f / 4096.f * 32.f;
    const float cp = std::pow(v, m1);
    return std::pow((c1 + c2 * cp) / (1.f + c3 * cp), m2);
}

/**
 * @brief Map filmic display-linear RGB (1 = paper white) into HDR10 PQ codes.
 * @param r,g,b Display-linear Rec.709 channels relative to paper white.
 */
inline void encodeHdr10(float &r, float &g, float &b, float paperWhiteNits, float peakNits) noexcept {
    clampCalibration(paperWhiteNits, peakNits);
    const float peakRatio = peakNits / paperWhiteNits;
    r = std::clamp(r, 0.f, peakRatio);
    g = std::clamp(g, 0.f, peakRatio);
    b = std::clamp(b, 0.f, peakRatio);
    rec709ToRec2020(r, g, b);
    constexpr float st2084Max = 10000.f;
    r = linearToPq(r * paperWhiteNits / st2084Max);
    g = linearToPq(g * paperWhiteNits / st2084Max);
    b = linearToPq(b * paperWhiteNits / st2084Max);
}

/**
 * @brief Expand ACES [0,1] output into display-linear relative to paper white.
 * @details Tonemap in a scaled domain so the shoulder lands near peak/paperWhite.
 */
inline void acesToDisplayLinear(float &r, float &g, float &b, float paperWhiteNits,
                                float peakNits) noexcept {
    clampCalibration(paperWhiteNits, peakNits);
    const float peakRatio = peakNits / paperWhiteNits;
    const float inv = 1.f / peakRatio;
    auto aces = [](float c) {
        c = std::max(c, 0.f);
        constexpr float a = 2.51f, b = 0.03f, c0 = 2.43f, d = 0.59f, e = 0.14f;
        return std::clamp((c * (a * c + b)) / (c * (c0 * c + d) + e), 0.f, 1.f);
    };
    r = aces(r * inv) * peakRatio;
    g = aces(g * inv) * peakRatio;
    b = aces(b * inv) * peakRatio;
}

}  // namespace eve::graphics::display
