// Shared hash / value-noise helpers for stylize post effects.
// Keep this file free of push-constant or sampler dependencies so any pass
// can #include it after declaring its own uniforms.

#ifndef EVE_STYLIZE_NOISE_GLSL
#define EVE_STYLIZE_NOISE_GLSL

// Classic sin-hash used by KinoGlitch Analog (and many Unity image effects).
float nrand(float x, float y) {
    return fract(sin(dot(vec2(x, y), vec2(12.9898, 78.233))) * 43758.5453);
}

float hash21(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

float valueNoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash21(i), hash21(i + vec2(1.0, 0.0)), f.x),
               mix(hash21(i + vec2(0.0, 1.0)), hash21(i + vec2(1.0, 1.0)), f.x),
               f.y);
}

#endif  // EVE_STYLIZE_NOISE_GLSL
