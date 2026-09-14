#version 450
// 3D slash mesh fragment shader (skill-hit ribbon effect).
//
// Reuses graphics/mesh3d_toon.vert for mesh lighting setup. Emulates a
// energy ribbon where UV.x runs from old tail to new head and UV.y spans the
// blade root-to-tip width. Designed for TrailEmitter-generated geometry.
//
// Push constants (declareFloat order in bindEffectMeshUniforms("slash")):
//   0 coreR      1 coreG       2 coreB
//   3 edgeR      4 edgeG       5 edgeB
//   6 intensity  7 width       8 softness
//   9 noiseScale 10 time       11 speed
//   12 flowWarp  13 edgeDistortion
//   14 depthSoftness 15 intersectionWidth 16 intersectionGlow
//   17 refractionStrength
// Binding 1 = albedo (multiplied by vTint from the mesh Frame UBO).
// Binding 7 = opaque scene depth in Vulkan NDC space.

layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec2 vUV;
layout(location = 2) in vec4 vTint;
layout(location = 3) in vec3 vLightDir;
layout(location = 4) in vec3 vLightColor;
layout(location = 5) in vec3 vWorldPos;
layout(location = 6) in vec3 vCameraPos;

layout(set = 0, binding = 1) uniform sampler2D albedoSampler;
layout(set = 0, binding = 7) uniform sampler2D sceneDepth;
layout(set = 0, binding = 18) uniform sampler2D sceneColor;

layout(push_constant) uniform Externals { float data[32]; } u;

layout(location = 0) out vec4 outColor;

float hash21(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash21(i), hash21(i + vec2(1.0, 0.0)), f.x),
               mix(hash21(i + vec2(0.0, 1.0)), hash21(i + vec2(1.0, 1.0)), f.x),
               f.y);
}

float fbm(vec2 p) {
    float sum = 0.0;
    float amp = 0.5;
    float freq = 1.0;
    for (int i = 0; i < 4; ++i) {
        sum += amp * noise(p * freq);
        amp *= 0.5;
        freq *= 2.0;
    }
    return sum;
}

void main() {
    vec3 core = vec3(u.data[0], u.data[1], u.data[2]);
    vec3 edge = vec3(u.data[3], u.data[4], u.data[5]);
    float intensity = max(u.data[6], 0.0);
    float width = clamp(u.data[7], 0.005, 0.3);
    float softness = max(u.data[8], 0.001);
    float noiseScale = max(u.data[9], 0.1);
    float time = max(u.data[10], 0.0);
    float speed = max(u.data[11], 0.0);
    float flowWarp = clamp(u.data[12], 0.0, 2.0);
    float edgeDistortion = clamp(u.data[13], 0.0, 0.5);
    float depthSoftness = max(u.data[14], 0.0001);
    float intersectionWidth = max(u.data[15], 0.0001);
    float intersectionGlow = max(u.data[16], 0.0);
    float refractionStrength = clamp(u.data[17], 0.0, 1.0);

    vec2 flowDomain = vec2(vUV.x * noiseScale - time * speed, vUV.y * 3.0);
    vec2 flow = vec2(fbm(flowDomain), fbm(flowDomain + vec2(19.7, 7.3))) * 2.0 - 1.0;
    vec2 warpedUV = vUV + flow * edgeDistortion * flowWarp * vec2(0.25, 0.5);
    float across = abs(warpedUV.y - 0.5);
    float blade = 1.0 - smoothstep(0.5 - softness, 0.5, across);
    float coreMask = 1.0 - smoothstep(width, width + softness, across);
    float tail = smoothstep(0.0, 0.18, warpedUV.x);
    float head = 1.0 - smoothstep(0.92, 1.0, warpedUV.x);
    float jitter = fbm(flowDomain + flow * flowWarp);
    float energy = blade * tail * head * mix(0.72, 1.15, jitter);

    vec2 depthUV = gl_FragCoord.xy / vec2(textureSize(sceneDepth, 0));
    float sceneZ = texture(sceneDepth, depthUV).r;
    float depthGap = max(sceneZ - gl_FragCoord.z, 0.0);
    float softFade = smoothstep(0.0, depthSoftness, depthGap);
    float intersection = (sceneZ < 0.99999)
                             ? 1.0 - smoothstep(0.0, intersectionWidth, depthGap)
                             : 0.0;
    vec3 glow = mix(edge, core, coreMask);
    vec2 refractionUV = clamp(depthUV + flow * refractionStrength * 0.02, vec2(0.001), vec2(0.999));
    vec3 refracted = texture(sceneColor, refractionUV).rgb;
    vec3 energyColor = (glow + edge * intersection * intersectionGlow) * vTint.rgb * intensity;
    vec3 color = mix(energyColor, refracted, refractionStrength) * energy;
    float alpha = clamp(vTint.a * energy * softFade, 0.0, 1.0);
    outColor = vec4(color, alpha);
}
