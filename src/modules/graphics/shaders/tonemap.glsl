// Peak-only SDR shoulder. Include with:
//   #extension GL_GOOGLE_include_directive : enable
//   #include "tonemap.glsl"

#ifndef TONEMAP_GLSL
#define TONEMAP_GLSL

// Keep values below `white` linear so dim glTF / interiors stay readable.
// Compress only the HDR remainder into (white, 1].
vec3 tonemapPeak(vec3 color) {
    const float white = 0.85;
    color = max(color, vec3(0.0));
    float peak = max(color.r, max(color.g, color.b));
    if (peak <= white)
        return color;
    float over = peak - white;
    float range = 1.0 - white;
    float mappedPeak = white + range * (over / (over + 1.0));
    return color * (mappedPeak / max(peak, 1e-6));
}

#endif
