#version 450
// Side-by-side POM / SilPOM / SSDM for cylinders (and other charted surfaces).
// pc.data[0] mode: 0 = classic POM, 1 = Silhouette POM,
// 2 = SSDM-style (POM shading + FragDepth; no UV-bound discard).
//
// Cylinder charts wrap in U (seam at 0/1) and are open in V (top/bottom limbs).
// POM/SSDM wrap U when sampling so the seam stays continuous; SilPOM does not —
// it discards when the displaced UV leaves [0,1]^2, which opens the seam and
// bites the top/bottom silhouette.

layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec2 vUV;
layout(location = 2) in vec4 vTint;
layout(location = 3) in vec3 vWorldPos;
layout(location = 4) in vec3 vCameraPos;
layout(location = 5) in vec3 vViewPos;
layout(location = 0) out vec4 outColor;

struct Light3D { vec4 posRadius; vec4 color; };

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
    vec4 cloud;
    vec4 cloudWind;
    vec4 virtualTexture;
    vec4 virtualAtlas;
    vec4 bindlessEnv;
    vec4 envProbeCenter;
    vec4 envProbeExtent;
    vec4 skinInfo;
    vec4 reflectionProbeCenter[2];
    vec4 reflectionProbeExtent[2];
} ubo;

layout(set = 0, binding = 1) uniform sampler2D albedoSampler;
layout(set = 0, binding = 6) uniform sampler2D heightSampler;

layout(push_constant) uniform Externals { float data[32]; } pc;

mat3 makeTBN(vec3 N, vec3 worldPos, vec2 uv) {
    vec3 n = normalize(N);
    vec3 dp1 = dFdx(worldPos);
    vec3 dp2 = dFdy(worldPos);
    vec2 duv1 = dFdx(uv);
    vec2 duv2 = dFdy(uv);
    float det = duv1.x * duv2.y - duv2.x * duv1.y;
    if (abs(det) < 1e-8)
        return mat3(vec3(0.0), vec3(0.0), n);
    vec3 T = normalize(cross(dp2, n) * duv1.x + cross(n, dp1) * duv2.x);
    vec3 B = normalize(cross(dp2, n) * duv1.y + cross(n, dp1) * duv2.y);
    if (length(T) < 1e-4 || length(B) < 1e-4)
        return mat3(vec3(0.0), vec3(0.0), n);
    return mat3(T, B, n);
}

float heightAt(vec2 uv, bool wrapU) {
    vec2 s = wrapU ? vec2(fract(uv.x), clamp(uv.y, 0.0, 1.0))
                   : clamp(uv, 0.0, 1.0);
    return texture(heightSampler, s).r;
}

vec2 pomUV(vec2 uv, vec3 viewTS, float scale, float minLayers, float maxLayers,
           bool wrapU) {
    if (scale < 1e-5)
        return uv;
    float layers = mix(max(maxLayers, 1.0), max(minLayers, 1.0),
                       clamp(abs(viewTS.z), 0.0, 1.0));
    layers = clamp(layers, 1.0, 64.0);
    float layerDepth = 1.0 / layers;
    float vz = max(abs(viewTS.z), 0.08);
    vec2 deltaUV = ((viewTS.xy / vz) * scale) / layers;
    vec2 curUV = uv;
    float curDepth = 0.0;
    float curMapDepth = 1.0 - heightAt(curUV, wrapU);
    for (int i = 0; i < 64; ++i) {
        if (curDepth >= curMapDepth || float(i) >= layers)
            break;
        curUV -= deltaUV;
        curMapDepth = 1.0 - heightAt(curUV, wrapU);
        curDepth += layerDepth;
    }
    vec2 prevUV = curUV + deltaUV;
    float after = curMapDepth - curDepth;
    float before = (1.0 - heightAt(prevUV, wrapU)) - (curDepth - layerDepth);
    float denom = after - before;
    float weight = (abs(denom) < 1e-5) ? 0.5 : clamp(after / denom, 0.0, 1.0);
    vec2 hit = mix(curUV, prevUV, weight);
    // Keep unwrapped hit for SilPOM coverage; wrap only for continuous sampling.
    return hit;
}

float silCoverage(vec2 uv, float padding) {
    vec2 mn = uv - vec2(padding);
    vec2 mx = (1.0 + padding) - uv;
    return (mn.x >= 0.0 && mn.y >= 0.0 && mx.x >= 0.0 && mx.y >= 0.0) ? 1.0 : 0.0;
}

vec2 sampleUV(vec2 uv, bool wrapU) {
    if (wrapU)
        return vec2(fract(uv.x), clamp(uv.y, 0.0, 1.0));
    return clamp(uv, 0.0, 1.0);
}

vec3 shadeLit(vec3 albedo, vec3 N, vec3 V) {
    vec3 L = normalize(ubo.lightDirIntensity.xyz);
    float ndl = max(dot(N, L), 0.0);
    float ndv = max(dot(N, V), 0.0);
    vec3 ambient = ubo.ambient.rgb;
    vec3 diffuse = ubo.lightColor.rgb * ndl;
    float rim = pow(1.0 - ndv, 3.0) * 0.08;
    return albedo * (ambient + diffuse) + vec3(rim);
}

void main() {
    float mode = pc.data[0];
    float scale = max(pc.data[1], 0.0);
    float minLayers = max(pc.data[2], 1.0);
    float maxLayers = max(pc.data[3], minLayers);
    float padding = max(pc.data[4], 0.0);

    vec3 N = normalize(vNormal);
    vec3 V = normalize(vCameraPos - vWorldPos);
    if (dot(N, V) < 0.0)
        N = -N;

    vec2 uv = vUV;
    float coverage = 1.0;
    // Vulkan RH_ZO: NDC Z is already [0,1]. Always write FragDepth (any-path rule).
    float fragDepth = gl_FragCoord.z;
    bool wrapU = true;

    if (mode < 0.5) {
        mat3 TBN = makeTBN(N, vWorldPos, vUV);
        if (length(TBN[0]) > 1e-4) {
            vec3 viewTS = normalize(transpose(TBN) * V);
            uv = pomUV(vUV, viewTS, scale, minLayers, maxLayers, true);
        }
        wrapU = true;
    } else if (mode < 1.5) {
        // SilPOM: no U wrap — leaving the chart (seam or top/bottom) discards.
        mat3 TBN = makeTBN(N, vWorldPos, vUV);
        if (length(TBN[0]) > 1e-4) {
            vec3 viewTS = normalize(transpose(TBN) * V);
            uv = pomUV(vUV, viewTS, scale, minLayers, maxLayers, false);
        }
        coverage = silCoverage(uv, padding);
        wrapU = false;
    } else {
        // SSDM-style on cylinders: wrap U like POM so the seam stays filled,
        // never discard on chart exits, and pull FragDepth toward the camera.
        mat3 TBN = makeTBN(N, vWorldPos, vUV);
        if (length(TBN[0]) > 1e-4) {
            vec3 viewTS = normalize(transpose(TBN) * V);
            uv = pomUV(vUV, viewTS, scale, minLayers, maxLayers, true);
        }
        // Soften near chart bottom so relief does not look like it continues
        // under the floor. Still sample / shade — do not discard the contact.
        float bottomFade = smoothstep(0.0, 0.08, vUV.y);
        uv = mix(vUV, uv, bottomFade);
        float h01 = heightAt(uv, true) * bottomFade;

        // Pull toward camera (smaller ZO depth). Stronger on brick tops.
        fragDepth = clamp(gl_FragCoord.z - (0.002 + h01 * 0.004), 0.0, 1.0);
        wrapU = true;
    }

    if (coverage < 0.5)
        discard;

    gl_FragDepth = fragDepth;

    vec3 albedo = texture(albedoSampler, sampleUV(uv, wrapU)).rgb * vTint.rgb * ubo.tint.rgb;
    if (mode < 0.5)
        albedo *= vec3(1.00, 0.93, 0.88);
    else if (mode < 1.5)
        albedo *= vec3(0.90, 1.00, 0.92);
    else
        albedo *= vec3(0.95, 0.96, 1.02);

    outColor = vec4(shadeLit(albedo, N, V), 1.0);
}
