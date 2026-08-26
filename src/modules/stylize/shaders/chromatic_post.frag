#version 450
// Chromatic aberration post-effect (lens dispersion).
// R and B channels are offset radially from the center by `strength` while G
// stays put, plus a small independent RGB split for anaglyph-like edge color.
// Push: 0 strength, 1 radialMix, 2 rgbSplit, 3 screenW, 4 screenH

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragUV;
layout(location = 0) out vec4 outColor;
layout(binding = 0) uniform sampler2D MainTex;
layout(push_constant) uniform Externals { float data[32]; } u;

void main() {
    float strength = clamp(u.data[0], 0.0, 1.0);
    float radialMix = clamp(u.data[1], 0.0, 1.0);
    float rgbSplit = clamp(u.data[2], 0.0, 1.0);
    float screenW = max(u.data[3], 1.0);
    float screenH = max(u.data[4], 1.0);

    vec4 src = texture(MainTex, fragUV) * fragColor;

    vec2 uvCenter = fragUV - 0.5;
    vec2 dir = uvCenter;
    if (length(dir) < 1e-5)
        dir = vec2(1.0, 0.0);
    vec2 ndir = normalize(dir);

    // Radial dispersion grows toward the frame edge.
    float radius = clamp(length(uvCenter) * 2.0, 0.0, 1.0);
    float radial = strength * radius * radialMix;

    // Independent per-channel shifts (constant + radial).
    vec2 ca = ndir * radial;
    float split = strength * rgbSplit * 0.5;
    vec2 rgbDir = vec2(split, split * screenH / screenW);

    float r = texture(MainTex, fragUV + ca + rgbDir).r;
    float g = texture(MainTex, fragUV).g;
    float b = texture(MainTex, fragUV - ca - rgbDir).b;

    outColor = vec4(r, g, b, src.a);
}