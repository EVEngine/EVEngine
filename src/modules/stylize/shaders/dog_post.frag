#version 450
// Difference of Gaussians (DoG) comic / line-art post-effect.
// Ported from GarrettGunnell/Post-Processing Assets/Edge Detection/
// DifferenceOfGaussians.shader (MIT, Copyright (C) Acerola / Garrett Gunnell).
// Unity separable two-pass blur + difference collapsed into one isotropic
// 2D dual-Gaussian (product of 1D kernels matches Acerola's separable path).
//
// @cost Fullscreen, O(kernel^2) luma taps (kernel half-width capped at 8).
//       Heavier than neonEdge; near-identity when strength is 0.
//
// Push: 0 strength, 1 kernelSize, 2 sigma, 3 k, 4 tau,
//       5 thresholding, 6 useTanh, 7 phi, 8 threshold, 9 invert,
//       10 texelW, 11 texelH

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragUV;
layout(location = 0) out vec4 outColor;
layout(binding = 0) uniform sampler2D MainTex;
layout(push_constant) uniform Externals { float data[32]; } u;

const float kPi = 3.14159265358979323846;
const int kMaxKernel = 8;

float gaussian1d(float sigma, float pos) {
    return (1.0 / sqrt(2.0 * kPi * sigma * sigma)) * exp(-(pos * pos) / (2.0 * sigma * sigma));
}

float dogLuma(vec3 color) {
    return dot(color, vec3(0.299, 0.587, 0.114));
}

void main() {
    float strength = clamp(u.data[0], 0.0, 1.0);
    float kernelSizeF = clamp(u.data[1], 1.0, float(kMaxKernel));
    float sigma = max(u.data[2], 0.1);
    float kScale = max(u.data[3], 0.1);
    float tau = clamp(u.data[4], 0.01, 5.0);
    bool thresholding = u.data[5] >= 0.5;
    bool useTanh = u.data[6] >= 0.5;
    float phi = clamp(u.data[7], 0.01, 100.0);
    float threshold = clamp(u.data[8], -1.0, 1.0);
    bool invert = u.data[9] >= 0.5;
    float texelW = max(u.data[10], 0.00001);
    float texelH = max(u.data[11], 0.00001);
    vec2 texel = vec2(texelW, texelH);

    vec4 src = texture(MainTex, fragUV) * fragColor;
    if (strength < 1e-5) {
        outColor = src;
        return;
    }

    int kernelSize = int(floor(kernelSizeF + 0.5));
    kernelSize = clamp(kernelSize, 1, kMaxKernel);
    float sigma2 = sigma * kScale;

    float sum1 = 0.0;
    float sum2 = 0.0;
    float kernelSum1 = 0.0;
    float kernelSum2 = 0.0;

    for (int y = -kMaxKernel; y <= kMaxKernel; ++y) {
        if (y < -kernelSize || y > kernelSize) continue;
        float gy1 = gaussian1d(sigma, float(y));
        float gy2 = gaussian1d(sigma2, float(y));
        for (int x = -kMaxKernel; x <= kMaxKernel; ++x) {
            if (x < -kernelSize || x > kernelSize) continue;
            float gx1 = gaussian1d(sigma, float(x));
            float gx2 = gaussian1d(sigma2, float(x));
            float w1 = gx1 * gy1;
            float w2 = gx2 * gy2;
            float l = dogLuma(texture(MainTex, fragUV + vec2(float(x), float(y)) * texel).rgb);
            sum1 += l * w1;
            sum2 += l * w2;
            kernelSum1 += w1;
            kernelSum2 += w2;
        }
    }

    float G1 = sum1 / max(kernelSum1, 1e-5);
    float G2 = sum2 / max(kernelSum2, 1e-5);
    float D = G1 - tau * G2;

    if (thresholding) {
        if (useTanh) {
            D = (D >= threshold) ? 1.0 : (1.0 + tanh(phi * (D - threshold)));
        } else {
            D = (D >= threshold) ? 1.0 : 0.0;
        }
    }
    if (invert) {
        D = 1.0 - D;
    }

    vec3 lineArt = vec3(clamp(D, 0.0, 1.0));
    outColor = vec4(mix(src.rgb, lineArt * fragColor.rgb, strength), src.a);
}
