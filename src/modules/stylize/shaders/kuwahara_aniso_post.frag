#version 450
// Anisotropic Kuwahara painterly post-effect (fused single pass).
// Ported from GarrettGunnell/Post-Processing Assets/Kuwahara Filter/
// AnisotropicKuwahara.shader (MIT, Copyright (C) Acerola / Garrett Gunnell).
// Unity multi-pass TFM pipeline (structure tensor → blurX → blurY+eigen →
// filter with _TFM) collapsed into one frag: local Sobel tensor + 5-tap cross
// blur of the tensor field, then the polynomial-weight anisotropic filter.
// Full ping-pong TFM needs a second sample binding; deferred.
//
// @cost Fullscreen, very heavy: ~5 Sobels + O(kernel^2) anisotropic taps
//       (kernel diameter capped at 8). Prefer modest kernelSize; near-identity
//       when strength is 0.
//
// Push: 0 strength, 1 kernelSize (diameter), 2 sharpness, 3 hardness,
//       4 alpha, 5 zeroCrossing, 6 zeta (0 → auto 2/kernelSize),
//       7 texelW, 8 texelH

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragUV;
layout(location = 0) out vec4 outColor;
layout(binding = 0) uniform sampler2D MainTex;
layout(push_constant) uniform Externals { float data[32]; } u;

const float kPi = 3.14159265358979323846;
const int kN = 8;
const int kMaxRadius = 4;  // diameter 8
const int kMaxExtent = kMaxRadius * 2;

float gaussian1d(float sigma, float pos) {
    return (1.0 / sqrt(2.0 * kPi * sigma * sigma)) * exp(-(pos * pos) / (2.0 * sigma * sigma));
}

// Structure tensor components (Jxx, Jyy, Jxy) via Sobel.
vec3 structureTensorAt(vec2 uv, vec2 texel) {
    vec3 Sx =
        (1.0 * texture(MainTex, uv + vec2(-texel.x, -texel.y)).rgb +
         2.0 * texture(MainTex, uv + vec2(-texel.x, 0.0)).rgb +
         1.0 * texture(MainTex, uv + vec2(-texel.x, texel.y)).rgb +
         -1.0 * texture(MainTex, uv + vec2(texel.x, -texel.y)).rgb +
         -2.0 * texture(MainTex, uv + vec2(texel.x, 0.0)).rgb +
         -1.0 * texture(MainTex, uv + vec2(texel.x, texel.y)).rgb) /
        4.0;
    vec3 Sy =
        (1.0 * texture(MainTex, uv + vec2(-texel.x, -texel.y)).rgb +
         2.0 * texture(MainTex, uv + vec2(0.0, -texel.y)).rgb +
         1.0 * texture(MainTex, uv + vec2(texel.x, -texel.y)).rgb +
         -1.0 * texture(MainTex, uv + vec2(-texel.x, texel.y)).rgb +
         -2.0 * texture(MainTex, uv + vec2(0.0, texel.y)).rgb +
         -1.0 * texture(MainTex, uv + vec2(texel.x, texel.y)).rgb) /
        4.0;
    return vec3(dot(Sx, Sx), dot(Sy, Sy), dot(Sx, Sy));
}

// 5-tap cross blur of the structure-tensor field, then eigen → (tx, ty, phi, A).
vec4 encodeTfm(vec2 uv, vec2 texel) {
    vec3 g = structureTensorAt(uv, texel) * gaussian1d(2.0, 0.0);
    float kernelSum = gaussian1d(2.0, 0.0);
    for (int i = 1; i <= 2; ++i) {
        float w = gaussian1d(2.0, float(i));
        g += structureTensorAt(uv + vec2(float(i), 0.0) * texel, texel) * w;
        g += structureTensorAt(uv - vec2(float(i), 0.0) * texel, texel) * w;
        g += structureTensorAt(uv + vec2(0.0, float(i)) * texel, texel) * w;
        g += structureTensorAt(uv - vec2(0.0, float(i)) * texel, texel) * w;
        kernelSum += 4.0 * w;
    }
    g /= max(kernelSum, 1e-5);

    float disc = sqrt(max(g.y * g.y - 2.0 * g.x * g.y + g.x * g.x + 4.0 * g.z * g.z, 0.0));
    float lambda1 = 0.5 * (g.y + g.x + disc);
    float lambda2 = 0.5 * (g.y + g.x - disc);

    vec2 v = vec2(lambda1 - g.x, -g.z);
    vec2 t = length(v) > 0.0 ? normalize(v) : vec2(0.0, 1.0);
    float phi = -atan(t.y, t.x);
    float A = (lambda1 + lambda2 > 0.0) ? (lambda1 - lambda2) / (lambda1 + lambda2) : 0.0;
    return vec4(t, phi, A);
}

void main() {
    float strength = clamp(u.data[0], 0.0, 1.0);
    float kernelSizeF = clamp(u.data[1], 2.0, float(kMaxRadius * 2));
    float sharpness = clamp(u.data[2], 1.0, 18.0);
    float hardness = clamp(u.data[3], 1.0, 100.0);
    float alpha = clamp(u.data[4], 0.01, 2.0);
    float zeroCrossing = clamp(u.data[5], 0.01, 2.0);
    float zetaParam = u.data[6];
    float texelW = max(u.data[7], 0.00001);
    float texelH = max(u.data[8], 0.00001);
    vec2 texel = vec2(texelW, texelH);

    vec4 src = texture(MainTex, fragUV) * fragColor;
    if (strength < 1e-5) {
        outColor = src;
        return;
    }

    int kernelSize = int(floor(kernelSizeF + 0.5));
    kernelSize = clamp(kernelSize, 2, kMaxRadius * 2);
    int kernelRadius = max(kernelSize / 2, 1);
    float zeta = zetaParam > 1e-5 ? clamp(zetaParam, 0.01, 3.0) : (2.0 / float(kernelSize));

    vec4 t = encodeTfm(fragUV, texel);
    float a = float(kernelRadius) * clamp((alpha + t.w) / alpha, 0.1, 2.0);
    float b = float(kernelRadius) * clamp(alpha / (alpha + t.w), 0.1, 2.0);

    float cosPhi = cos(t.z);
    float sinPhi = sin(t.z);
    // Column-major rotation matching HLSL row layout {c,-s; s,c}.
    mat2 R = mat2(vec2(cosPhi, sinPhi), vec2(-sinPhi, cosPhi));
    mat2 S = mat2(vec2(0.5 / a, 0.0), vec2(0.0, 0.5 / b));
    mat2 SR = S * R;

    int maxX = int(sqrt(a * a * cosPhi * cosPhi + b * b * sinPhi * sinPhi));
    int maxY = int(sqrt(a * a * sinPhi * sinPhi + b * b * cosPhi * cosPhi));
    maxX = clamp(maxX, 0, kMaxExtent);
    maxY = clamp(maxY, 0, kMaxExtent);

    float sinZeroCross = sin(zeroCrossing);
    float eta = (zeta + cos(zeroCrossing)) / max(sinZeroCross * sinZeroCross, 1e-5);

    vec4 m[kN];
    vec3 s[kN];
    for (int k = 0; k < kN; ++k) {
        m[k] = vec4(0.0);
        s[k] = vec3(0.0);
    }

    for (int y = -kMaxExtent; y <= kMaxExtent; ++y) {
        if (y < -maxY || y > maxY) continue;
        for (int x = -kMaxExtent; x <= kMaxExtent; ++x) {
            if (x < -maxX || x > maxX) continue;
            vec2 v = SR * vec2(float(x), float(y));
            if (dot(v, v) > 0.25) continue;

            vec3 c = clamp(texture(MainTex, fragUV + vec2(float(x), float(y)) * texel).rgb, 0.0, 1.0);
            float sum = 0.0;
            float w[kN];

            float vxx = zeta - eta * v.x * v.x;
            float vyy = zeta - eta * v.y * v.y;
            float z;
            z = max(0.0, v.y + vxx);
            w[0] = z * z;
            sum += w[0];
            z = max(0.0, -v.x + vyy);
            w[2] = z * z;
            sum += w[2];
            z = max(0.0, -v.y + vxx);
            w[4] = z * z;
            sum += w[4];
            z = max(0.0, v.x + vyy);
            w[6] = z * z;
            sum += w[6];

            v = (sqrt(2.0) * 0.5) * vec2(v.x - v.y, v.x + v.y);
            vxx = zeta - eta * v.x * v.x;
            vyy = zeta - eta * v.y * v.y;
            z = max(0.0, v.y + vxx);
            w[1] = z * z;
            sum += w[1];
            z = max(0.0, -v.x + vyy);
            w[3] = z * z;
            sum += w[3];
            z = max(0.0, -v.y + vxx);
            w[5] = z * z;
            sum += w[5];
            z = max(0.0, v.x + vyy);
            w[7] = z * z;
            sum += w[7];

            float g = exp(-3.125 * dot(v, v)) / max(sum, 1e-5);
            for (int k = 0; k < kN; ++k) {
                float wk = w[k] * g;
                m[k] += vec4(c * wk, wk);
                s[k] += c * c * wk;
            }
        }
    }

    vec4 outputAccum = vec4(0.0);
    for (int k = 0; k < kN; ++k) {
        float mw = max(m[k].w, 1e-5);
        m[k].rgb /= mw;
        s[k] = abs(s[k] / mw - m[k].rgb * m[k].rgb);
        float sigma2 = s[k].r + s[k].g + s[k].b;
        float wk = 1.0 / (1.0 + pow(hardness * 1000.0 * sigma2, 0.5 * sharpness));
        outputAccum += vec4(m[k].rgb * wk, wk);
    }

    vec3 filtered = clamp(outputAccum.rgb / max(outputAccum.w, 1e-5), 0.0, 1.0);
    outColor = vec4(mix(src.rgb, filtered * fragColor.rgb, strength), src.a);
}
