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

// 2D simplex noise (Ashima Arts webgl-noise / X-PostProcessing XNoiseLibrary, MIT).
// Needed by glitchWave (XPL GlitchWaveJitter).
float noiseMod289(float x) { return x - floor(x * (1.0 / 289.0)) * 289.0; }
vec2 noiseMod289(vec2 x) { return x - floor(x * (1.0 / 289.0)) * 289.0; }
vec3 noiseMod289(vec3 x) { return x - floor(x * (1.0 / 289.0)) * 289.0; }
vec3 noisePermute(vec3 x) { return noiseMod289(((x * 34.0) + 1.0) * x); }
vec3 noiseTaylorInvSqrt(vec3 r) { return 1.79284291400159 - 0.85373472095314 * r; }

float snoise(vec2 v) {
    const vec4 C = vec4(0.211324865405187,   // (3.0-sqrt(3.0))/6.0
                        0.366025403784439,   // 0.5*(sqrt(3.0)-1.0)
                        -0.577350269189626,  // -1.0 + 2.0 * C.x
                        0.024390243902439);  // 1.0 / 41.0
    vec2 i = floor(v + dot(v, C.yy));
    vec2 x0 = v - i + dot(i, C.xx);
    vec2 i1 = (x0.x > x0.y) ? vec2(1.0, 0.0) : vec2(0.0, 1.0);
    vec2 x1 = x0 + C.xx - i1;
    vec2 x2 = x0 + C.zz;
    i = noiseMod289(i);
    vec3 p = noisePermute(noisePermute(i.y + vec3(0.0, i1.y, 1.0)) + i.x + vec3(0.0, i1.x, 1.0));
    vec3 m = max(0.5 - vec3(dot(x0, x0), dot(x1, x1), dot(x2, x2)), 0.0);
    m = m * m;
    m = m * m;
    vec3 x = 2.0 * fract(p * C.www) - 1.0;
    vec3 h = abs(x) - 0.5;
    vec3 ox = floor(x + 0.5);
    vec3 a0 = x - ox;
    m *= noiseTaylorInvSqrt(a0 * a0 + h * h);
    vec3 g = vec3(a0.x * x0.x + h.x * x0.y, a0.y * x1.x + h.y * x1.y, a0.z * x2.x + h.z * x2.y);
    return 130.0 * dot(m, g);
}

#endif  // EVE_STYLIZE_NOISE_GLSL
