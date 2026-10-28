#version 450
// Film-grain post-effect. Luminance-adaptive value noise; animated by `time`
// so the grain crawls. Can also be used as a static dither when time stays 0.
// Push: 0 strength, 1 grainSize, 2 time, 3 lumaAmount, 4 colorAmount

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragUV;
layout(location = 0) out vec4 outColor;
layout(binding = 0) uniform sampler2D MainTex;
layout(push_constant) uniform Externals { float data[32]; } u;

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

void main() {
    float strength = clamp(u.data[0], 0.0, 1.0);
    float grainSize = max(u.data[1], 1.0);
    float time = u.data[2];
    float lumaAmount = clamp(u.data[3], 0.0, 1.0);
    float colorAmount = clamp(u.data[4], 0.0, 1.0);

    vec4 src = texture(MainTex, fragUV) * fragColor;

    vec2 coord = fragUV * vec2(textureSize(MainTex, 0)) / grainSize + vec2(time * 60.0, 0.0);
    float n = valueNoise(coord);
    vec3 grain = vec3(n, n, n);

    // Greyscale grain, with an optional color tint for a "chromatic" film look.
    grain = mix(grain, vec3(hash21(coord + 11.7), hash21(coord + 41.3),
                            hash21(coord + 71.9)),
                colorAmount);

    // Luminance-adaptive: stronger in dark areas (perceptual film grain).
    float luma = dot(src.rgb, vec3(0.2126, 0.7152, 0.0722));
    float amt = strength * mix(1.0, 1.0 - luma, lumaAmount);
    vec3 grainMix = (grain - 0.5) * amt;

    outColor = vec4(clamp(src.rgb + grainMix, 0.0, 1.0), src.a);
}