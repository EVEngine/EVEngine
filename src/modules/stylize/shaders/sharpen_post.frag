#version 450
// Contrast Adaptive Sharpening (CAS) post-effect.
// Ported from GarrettGunnell/Post-Processing Assets/Sharpness/
// ContrastAdaptiveSharpness.shader (MIT, Copyright (C) Acerola / Garrett Gunnell).
// AMD FidelityFX-style local contrast sharpen; Unity Blit shell removed.
// Outer `strength` mix keeps strength=0 near-identity (CAS amount alone is not).
//
// @cost Fullscreen, 9 taps; comparable to chromatic. Near-identity when strength
//       is 0.
//
// Push: 0 strength, 1 amount, 2 texelW, 3 texelH

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragUV;
layout(location = 0) out vec4 outColor;
layout(binding = 0) uniform sampler2D MainTex;
layout(push_constant) uniform Externals { float data[32]; } u;

vec3 casSample(vec2 uv, float dx, float dy, vec2 texel) {
    return clamp(texture(MainTex, uv + vec2(dx, dy) * texel).rgb, 0.0, 1.0);
}

void main() {
    float strength = clamp(u.data[0], 0.0, 1.0);
    float amount = clamp(u.data[1], 0.0, 1.0);
    float texelW = max(u.data[2], 0.00001);
    float texelH = max(u.data[3], 0.00001);
    vec2 texel = vec2(texelW, texelH);

    vec4 src = texture(MainTex, fragUV) * fragColor;
    if (strength < 1e-5) {
        outColor = src;
        return;
    }

    float sharpness = -(1.0 / mix(10.0, 7.0, amount));

    vec3 a = casSample(fragUV, -1.0, -1.0, texel);
    vec3 b = casSample(fragUV, 0.0, -1.0, texel);
    vec3 c = casSample(fragUV, 1.0, -1.0, texel);
    vec3 d = casSample(fragUV, -1.0, 0.0, texel);
    vec3 e = casSample(fragUV, 0.0, 0.0, texel);
    vec3 f = casSample(fragUV, 1.0, 0.0, texel);
    vec3 g = casSample(fragUV, -1.0, 1.0, texel);
    vec3 h = casSample(fragUV, 0.0, 1.0, texel);
    vec3 j = casSample(fragUV, 1.0, 1.0, texel);

    vec3 minRGB = min(min(min(d, e), f), min(b, h));
    vec3 minRGB2 = min(min(min(minRGB, a), c), min(g, j));
    minRGB += minRGB2;

    vec3 maxRGB = max(max(max(d, e), f), max(b, h));
    vec3 maxRGB2 = max(max(max(maxRGB, a), c), max(g, j));
    maxRGB += maxRGB2;

    vec3 rcpM = 1.0 / max(maxRGB, vec3(1e-5));
    vec3 amp = clamp(min(minRGB, 2.0 - maxRGB) * rcpM, 0.0, 1.0);
    amp = sqrt(amp);

    vec3 w = amp * sharpness;
    vec3 rcpW = 1.0 / (1.0 + 4.0 * w);
    vec3 sharpened = clamp((b * w + d * w + f * w + h * w + e) * rcpW, 0.0, 1.0);

    outColor = vec4(mix(src.rgb, sharpened * fragColor.rgb, strength), src.a);
}
