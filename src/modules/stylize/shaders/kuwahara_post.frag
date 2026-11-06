#version 450
// Basic Kuwahara painterly post-effect.
// Ported from GarrettGunnell/Post-Processing Assets/Kuwahara Filter/Kuwahara.shader
// (MIT License, Copyright (C) Acerola / Garrett Gunnell). Unity Blit shell and
// animated-kernel branch omitted for wave-2 Lane A; static four-quadrant path kept.
//
// @cost Fullscreen, ~4 * (kernelSize+1)^2 taps (kernelSize clamped to 8). Much
//       heavier than grain/glitch; prefer modest kernelSize or StyleChain once.
//       Near-identity when strength is 0.
//
// Push: 0 strength, 1 kernelSize, 2 texelW, 3 texelH

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragUV;
layout(location = 0) out vec4 outColor;
layout(binding = 0) uniform sampler2D MainTex;
layout(push_constant) uniform Externals { float data[32]; } u;

#include "post_common.glsl"

const int kMaxKernel = 8;

float kuwaLuma(vec3 color) {
    return dot(color, vec3(0.299, 0.587, 0.114));
}

// Returns avg color in .rgb, luminance variance proxy in .a (Acerola "std").
vec4 sampleQuadrant(vec2 uv, vec2 texel, int x1, int x2, int y1, int y2, float n) {
    float luminanceSum = 0.0;
    float luminanceSum2 = 0.0;
    vec3 colSum = vec3(0.0);

    for (int x = -kMaxKernel; x <= kMaxKernel; ++x) {
        if (x < x1 || x > x2) continue;
        for (int y = -kMaxKernel; y <= kMaxKernel; ++y) {
            if (y < y1 || y > y2) continue;
            vec3 sampleColor = texture(MainTex, uv + vec2(float(x), float(y)) * texel).rgb;
            sampleColor = clamp(sampleColor, 0.0, 1.0);
            float l = kuwaLuma(sampleColor);
            luminanceSum += l;
            luminanceSum2 += l * l;
            colSum += sampleColor;
        }
    }

    float mean = luminanceSum / n;
    float stdv = abs(luminanceSum2 / n - mean * mean);
    return vec4(colSum / n, stdv);
}

void main() {
    float strength = clamp(u.data[0], 0.0, 1.0);
    float kernelSizeF = clamp(u.data[1], 1.0, float(kMaxKernel));
    float texelW = max(u.data[2], 0.00001);
    float texelH = max(u.data[3], 0.00001);

    vec4 src = texture(MainTex, fragUV) * fragColor;
    if (strength < 1e-5) {
        outColor = src;
        return;
    }

    int kernelSize = int(floor(kernelSizeF + 0.5));
    kernelSize = clamp(kernelSize, 1, kMaxKernel);

    float windowSize = 2.0 * float(kernelSize) + 1.0;
    int quadrantSize = int(ceil(windowSize / 2.0));
    float numSamples = float(quadrantSize * quadrantSize);
    vec2 texel = vec2(texelW, texelH);

    vec4 q1 = sampleQuadrant(fragUV, texel, -kernelSize, 0, -kernelSize, 0, numSamples);
    vec4 q2 = sampleQuadrant(fragUV, texel, 0, kernelSize, -kernelSize, 0, numSamples);
    vec4 q3 = sampleQuadrant(fragUV, texel, 0, kernelSize, 0, kernelSize, numSamples);
    vec4 q4 = sampleQuadrant(fragUV, texel, -kernelSize, 0, 0, kernelSize, numSamples);

    float minStd = min(q1.a, min(q2.a, min(q3.a, q4.a)));
    vec4 mask = step(abs(vec4(q1.a, q2.a, q3.a, q4.a) - vec4(minStd)), vec4(1e-5));

    vec3 filtered;
    if (dot(mask, vec4(1.0)) > 1.0) {
        filtered = (q1.rgb + q2.rgb + q3.rgb + q4.rgb) * 0.25;
    } else {
        filtered = q1.rgb * mask.x + q2.rgb * mask.y + q3.rgb * mask.z + q4.rgb * mask.w;
    }
    filtered = clamp(filtered, 0.0, 1.0);

    outColor = vec4(mix(src.rgb, filtered * fragColor.rgb, strength), src.a);
}
