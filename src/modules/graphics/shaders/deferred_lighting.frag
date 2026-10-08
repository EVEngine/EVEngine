#version 450
// Phase C: clustered deferred lighting for Hybrid opaque. Samples the Phase B
// GBuffer MRT and evaluates core metallic-roughness PBR with the same clustered
// light list / primary directional / CSM contract as mesh3d_clustered.frag.

struct Light3D {
    vec4 posRadius;
    vec4 color;
};

layout(set = 0, binding = 0, std140) uniform DeferredFrame {
    mat4 invViewProj;
    mat4 view;
    vec4 lightDir;     // xyz = primary dir toward surface; w = 1 if enabled
    vec4 lightColor;   // rgb = primary; w = envIntensity (reserved)
    vec4 cameraPos;    // xyz = eye
    vec4 ambient;      // rgb ambient; w unused
    vec4 gridInfo;     // tilesX, tilesY, slices, pointCount
    vec4 clipInfo;     // near, far, screenW, screenH
} ubo;

layout(set = 0, binding = 1) uniform sampler2D gbNormal;
layout(set = 0, binding = 2) uniform sampler2D gbDepthColor;
layout(set = 0, binding = 3) uniform sampler2D gbAlbedo;
layout(set = 0, binding = 4) uniform sampler2D gbPbrParams;
layout(set = 0, binding = 5) uniform sampler2D gbEmissive;
layout(set = 0, binding = 6) uniform sampler2D gbHwDepth;

layout(std430, set = 0, binding = 7) readonly buffer LightBuffer {
    Light3D lights[];
};
layout(std430, set = 0, binding = 8) readonly buffer ClusterTable {
    uvec2 clusters[];
};
layout(std430, set = 0, binding = 9) readonly buffer LightIndexBuffer {
    uint lightIndices[];
};

layout(set = 0, binding = 10, std140) uniform ShadowFrame {
    mat4 lightVP[3];
    vec4 splits;
    vec4 bias;
    vec4 cascadeBias;
    vec4 cascadeTexel;
} shadow;

layout(set = 0, binding = 11) uniform sampler2DArrayShadow shadowMap;

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

const float PI = 3.14159265359;

float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float a = max(roughness * roughness, 0.002);
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float denom = (NdotH * NdotH * (a2 - 1.0) + 1.0);
    return a2 / max(PI * denom * denom, 1e-4);
}

float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotV / max(NdotV * (1.0 - k) + k, 1e-4);
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    return GeometrySchlickGGX(max(dot(N, V), 0.0), roughness) *
           GeometrySchlickGGX(max(dot(N, L), 0.0), roughness);
}

vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 shadeLight(vec3 N, vec3 V, vec3 L, vec3 radiance, vec3 albedo, float metallic,
                float roughness, float specularFactor) {
    float NdotL = max(dot(N, L), 0.0);
    if (NdotL <= 0.0) return vec3(0.0);
    vec3 H = normalize(V + L);
    vec3 F0 = mix(vec3(0.04 * specularFactor), albedo, metallic);
    float NDF = DistributionGGX(N, H, roughness);
    float G = GeometrySmith(N, V, L, roughness);
    vec3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);
    vec3 specular = (NDF * G * F) / max(4.0 * max(dot(N, V), 0.0) * NdotL, 1e-4);
    vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
    return (kD * albedo / PI + specular) * radiance * NdotL;
}

float sampleShadowCascade(vec3 worldPos, int cascade, float biasAmt) {
    vec4 lightClip = shadow.lightVP[cascade] * vec4(worldPos, 1.0);
    vec3 ndc = lightClip.xyz / max(lightClip.w, 1e-6);
    vec2 uv = ndc.xy * 0.5 + 0.5;
    float depth = ndc.z;
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 || depth < 0.0 || depth > 1.0)
        return 1.0;
    return texture(shadowMap, vec4(uv, float(cascade), depth - biasAmt));
}

float sampleShadowPCF(vec3 worldPos, vec3 N, float viewDepth, float nDotL) {
    if (shadow.bias.y < 0.5 || shadow.bias.z < 0.5 || shadow.splits.w < 1e-4)
        return 1.0;
    int cascade = 2;
    if (viewDepth < shadow.splits.x) cascade = 0;
    else if (viewDepth < shadow.splits.y) cascade = 1;
    float b = cascade == 0 ? shadow.cascadeBias.x
                           : (cascade == 1 ? shadow.cascadeBias.y : shadow.cascadeBias.z);
    if (b < 1e-8) b = shadow.bias.x;
    b *= mix(0.75, 1.0, clamp(nDotL, 0.0, 1.0));
    float tw = cascade == 0 ? shadow.cascadeTexel.x
                            : (cascade == 1 ? shadow.cascadeTexel.y : shadow.cascadeTexel.z);
    vec3 p = worldPos + N * ((2.0 * max(tw, 1e-6)) / max(nDotL, 0.2));
    float vis = sampleShadowCascade(p, cascade, b);
    return mix(0.04, 1.0, mix(1.0, vis, clamp(shadow.splits.w, 0.0, 1.0)));
}

uint clusterIndex(float viewDepth) {
    int tilesX = int(ubo.gridInfo.x + 0.5);
    int tilesY = int(ubo.gridInfo.y + 0.5);
    int slices = int(ubo.gridInfo.z + 0.5);
    float nearZ = ubo.clipInfo.x;
    float farZ = max(ubo.clipInfo.y, nearZ + 1e-3);
    float screenW = max(ubo.clipInfo.z, 1.0);
    float screenH = max(ubo.clipInfo.w, 1.0);
    int tx = clamp(int(floor(gl_FragCoord.x / screenW * float(tilesX))), 0, tilesX - 1);
    int ty = clamp(int(floor(gl_FragCoord.y / screenH * float(tilesY))), 0, tilesY - 1);
    float depth = max(viewDepth, nearZ);
    int sz = clamp(int(floor((depth - nearZ) / (farZ - nearZ) * float(slices))), 0, slices - 1);
    return uint((sz * tilesY + ty) * tilesX + tx);
}

void main() {
    vec2 uv = vUV;
    float hwZ = texture(gbHwDepth, uv).r;
    // Empty GBuffer pixels clear depth to 1.0 — leave scene color untouched via
    // discard so transparent Forward+ can still composite later. Hybrid opens
    // scene color cleared first; deferred then fills opaque lit pixels.
    if (hwZ >= 0.9999) discard;

    vec4 clip = vec4(uv * 2.0 - 1.0, hwZ, 1.0);
    vec4 worldH = ubo.invViewProj * clip;
    vec3 worldPos = worldH.xyz / max(worldH.w, 1e-6);

    vec3 N = normalize(texture(gbNormal, uv).xyz * 2.0 - 1.0);
    vec3 albedo = texture(gbAlbedo, uv).rgb;
    vec4 pbr = texture(gbPbrParams, uv);
    float metallic = clamp(pbr.r, 0.0, 1.0);
    float roughness = clamp(pbr.g, 0.04, 1.0);
    float occlusion = clamp(pbr.b, 0.0, 1.0);
    float specularFactor = clamp(pbr.a, 0.0, 1.0);
    vec3 emissive = texture(gbEmissive, uv).rgb;

    vec3 V = normalize(ubo.cameraPos.xyz - worldPos);
    if (dot(N, V) < 0.0) N = -N;

    vec3 viewPos = (ubo.view * vec4(worldPos, 1.0)).xyz;
    float viewDepth = max(-viewPos.z, 0.0);

    vec3 Lo = vec3(0.0);
    if (ubo.lightDir.w > 0.5) {
        vec3 L = normalize(ubo.lightDir.xyz);
        float nDotL = max(dot(N, L), 0.0);
        float shadowVis = sampleShadowPCF(worldPos, N, viewDepth, nDotL);
        Lo += shadeLight(N, V, L, ubo.lightColor.rgb, albedo, metallic, roughness, specularFactor) *
              shadowVis;
    }

    uint ci = clusterIndex(viewDepth);
    uvec2 entry = clusters[ci];
    uint count = min(entry.y, 32u);
    for (uint i = 0u; i < count; ++i) {
        uint li = lightIndices[entry.x + i];
        Light3D light = lights[li];
        vec3 toLight = light.posRadius.xyz - worldPos;
        float dist = length(toLight);
        float radius = max(light.posRadius.w, 1e-3);
        if (dist >= radius) continue;
        vec3 L = toLight / max(dist, 1e-4);
        float atten = 1.0 - smoothstep(radius * 0.8, radius, dist);
        atten /= max(dist * dist, 1e-4);
        Lo += shadeLight(N, V, L, light.color.rgb * atten, albedo, metallic, roughness,
                         specularFactor);
    }

    vec3 ambient = ubo.ambient.rgb * albedo * (1.0 - metallic) * occlusion;
    vec3 color = ambient + Lo + emissive;
    outColor = vec4(color, 1.0);
    // Copy GBuffer HW depth into scene color so transparent Forward+ depth-tests
    // against deferred opaques (Hybrid does not re-draw core opaque forward).
    gl_FragDepth = hwZ;
}
