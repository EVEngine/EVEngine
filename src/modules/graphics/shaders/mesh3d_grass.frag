#version 450
// t3ssel8r-style grass fragment: 4-frame sway atlas + shadow-mapped two-tone green.
// Push: [0] time  [1] frameDuration  [2] width  [3] height
//       [4] alphaCutoff  [5] alwaysDark
//       [6..8] lightGreen  [9..11] darkGreen  [12] frameCount

layout(location = 0) in vec2 vUV;
layout(location = 1) in vec3 vWorldPos;
layout(location = 2) in vec3 vViewPos;
layout(location = 3) in vec3 vRootPos;
layout(location = 4) in float vInstanceId;
layout(location = 5) in vec4 vTint;
layout(location = 6) in float vAlwaysDark;

struct Light3D {
    vec4 posRadius;
    vec4 color;
};

layout(set = 0, binding = 0, std140) uniform Frame {
    mat4 mvp;
    mat4 model;
    vec4 lightDirIntensity;
    vec4 lightColor;
    vec4 tint;
    vec4 cameraPos;
    vec4 ambient;
    Light3D lights[8];
    vec4 texBomb;
    vec4 parallax;
    mat4 view;
    vec4 clipInfo;
} ubo;

layout(set = 0, binding = 1) uniform sampler2D albedoSampler;

layout(set = 0, binding = 4, std140) uniform ShadowFrame {
    mat4 lightVP[3];
    vec4 splits;
    vec4 bias;
    vec4 cascadeBias;
    vec4 cascadeTexel;
} shadow;

layout(set = 0, binding = 5) uniform sampler2DArrayShadow shadowMap;

layout(push_constant) uniform Externals {
    float data[32];
} u;

layout(location = 0) out vec4 outColor;

float sampleShadowCascade(vec3 worldPos, int cascade, float ndcBias) {
    vec4 lightClip = shadow.lightVP[cascade] * vec4(worldPos, 1.0);
    vec3 ndc = lightClip.xyz / max(lightClip.w, 1e-6);
    vec2 uv = ndc.xy * 0.5 + 0.5;
    float depth = ndc.z;
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 || depth < 0.0 || depth > 1.0)
        return 1.0;

    vec2 texel = 1.0 / vec2(textureSize(shadowMap, 0).xy);
    const vec2 kRotOffsets[9] = vec2[9](
        vec2(-0.5, -0.5), vec2(0.0, -0.6), vec2(0.5, -0.5),
        vec2(-0.6, 0.0), vec2(0.0, 0.0),  vec2(0.6, 0.0),
        vec2(-0.5, 0.5), vec2(0.0, 0.6),  vec2(0.5, 0.5)
    );
    float sum = 0.0;
    float zref = depth - ndcBias;
    for (int i = 0; i < 9; ++i) {
        vec2 s = uv + kRotOffsets[i] * texel;
        if (s.x < 0.0 || s.x > 1.0 || s.y < 0.0 || s.y > 1.0)
            continue;
        sum += texture(shadowMap, vec4(s, float(cascade), zref));
    }
    return sum / 9.0;
}

float cascadeNdcBias(int cascade) {
    float b = cascade == 0 ? shadow.cascadeBias.x
                           : (cascade == 1 ? shadow.cascadeBias.y : shadow.cascadeBias.z);
    return b > 1e-8 ? b : shadow.bias.x;
}

float sampleGrassShadow(vec3 worldPos, float viewDepth) {
    if (shadow.bias.y < 0.5 || shadow.bias.z < 0.5 || shadow.splits.w < 1e-4)
        return 1.0;

    int cascade = 2;
    if (viewDepth < shadow.splits.x) cascade = 0;
    else if (viewDepth < shadow.splits.y) cascade = 1;

    float vis = sampleShadowCascade(worldPos, cascade, cascadeNdcBias(cascade));
    vis = mix(1.0, vis, clamp(shadow.splits.w, 0.0, 1.0));
    return vis;
}

void main() {
    float frames = max(u.data[12], 1.0);
    float duration = max(u.data[1], 1e-4);
    float hash = fract(sin(vInstanceId * 12.9898) * 43758.5453);
    float t = u.data[0] / duration + hash * frames;
    int frame = int(mod(floor(t), frames));

    // inUV.y = 0 at the root (bottom). Vulkan/image v=0 is the top row.
    vec2 atlasUV = vec2((vUV.x + float(frame)) / frames, 1.0 - vUV.y);
    vec4 mask = texture(albedoSampler, atlasUV) * vTint;
    if (mask.a < max(u.data[4], 0.01))
        discard;

    float viewDepth = max(-vViewPos.z, 0.0);
    float vis = sampleGrassShadow(vRootPos, viewDepth);
    if (u.data[5] > 0.5 || vAlwaysDark > 0.5)
        vis = 0.0;

    vec3 lightGreen = vec3(u.data[6], u.data[7], u.data[8]);
    vec3 darkGreen = vec3(u.data[9], u.data[10], u.data[11]);
    vec3 grassCol = mix(darkGreen, lightGreen, vis);
    outColor = vec4(grassCol * mask.rgb, mask.a);
}
