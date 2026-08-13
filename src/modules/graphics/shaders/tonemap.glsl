// Peak-only SDR shoulder. Include with:
//   #extension GL_GOOGLE_include_directive : enable
//   #include "tonemap.glsl"

#ifndef TONEMAP_GLSL
#define TONEMAP_GLSL

// Keep values below `white` linear so dim glTF / interiors stay readable.
// Compress only the HDR remainder into (white, 1].
vec3 tonemapPeak(vec3 color) {
    const float white = 0.85;
    vec3 over = max(color - vec3(white), vec3(0.0));
    float range = 1.0 - white;
    return min(color, vec3(white)) + vec3(range) * (over / (over + vec3(1.0)));
}

#endif
