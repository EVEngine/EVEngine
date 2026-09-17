#version 450
// Full SilPOM vs full SSDM on planar extruded cards.
//
// pc.data[0] mode:
//   0 = classic POM  — parallax only; geometric silhouette unchanged
//   1 = SilPOM       — POM shading on the front; side faces raymarch and
//                      keep brick caps only (jagged limb). Front depth stays
//                      geometric so cliff FragDepth cannot open 镂空 shells.
//   2 = SSDM         — model-space heightfield march through the extruded
//                      slab; side misses discard → continuous outline.
//                      Front keeps geometric depth for the same reason.
//
// Height.r: 1 = raised toward the card normal (+Z in model space).
// Card mesh is a slab with local XY in [-1,1] → UV, local Z in [0,1]
// (Z=1 is the front face). Physical thickness comes from model scale.z.

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

float heightAt(vec2 uv) {
    return texture(heightSampler, clamp(uv, 0.0, 1.0)).r;
}

vec3 localFromWorld(vec3 worldPos) {
    return (inverse(ubo.model) * vec4(worldPos, 1.0)).xyz;
}

vec2 uvFromLocal(vec3 local) {
    return local.xy * 0.5 + 0.5;
}

float fragDepthFromLocal(vec3 hitLocal) {
    vec4 clip = ubo.mvp * vec4(hitLocal, 1.0);
    return clamp(clip.z / max(clip.w, 1e-6), 0.0, 1.0);
}

vec3 heightNormalTS(vec2 uv, float scale) {
    vec2 texel = 1.0 / vec2(textureSize(heightSampler, 0));
    float hL = heightAt(uv + vec2(-texel.x, 0.0));
    float hR = heightAt(uv + vec2( texel.x, 0.0));
    float hD = heightAt(uv + vec2(0.0, -texel.y));
    float hU = heightAt(uv + vec2(0.0,  texel.y));
    return normalize(vec3((hL - hR) * scale * 5.0,
                          (hD - hU) * scale * 5.0,
                          1.0));
}

// Steep POM + linear refine. Returns xy=UV, z=hitDepth[0..1], w=found.
// UV is always clamped — walk-off discard shredded the left card at grazing.
vec4 pomHit(vec2 uv, vec3 viewTS, float scale, float minLayers, float maxLayers) {
    if (scale < 1e-5)
        return vec4(uv, 0.0, 1.0);
    float layers = clamp(mix(max(maxLayers, 1.0), max(minLayers, 1.0),
                             clamp(abs(viewTS.z), 0.0, 1.0)),
                         1.0, 64.0);
    float layerDepth = 1.0 / layers;
    // Floor |vz| higher at grazing to limit runaway UV steps (zebra stripes).
    float vz = max(abs(viewTS.z), 0.18);
    vec2 deltaUV = ((viewTS.xy / vz) * scale) / layers;
    vec2 curUV = uv;
    float curDepth = 0.0;
    float curMap = 1.0 - heightAt(curUV);
    bool found = false;
    for (int i = 0; i < 64; ++i) {
        if (float(i) >= layers)
            break;
        if (curDepth >= curMap) {
            found = true;
            break;
        }
        curUV = clamp(curUV - deltaUV, 0.0, 1.0);
        curMap = 1.0 - heightAt(curUV);
        curDepth += layerDepth;
    }
    vec2 prevUV = clamp(curUV + deltaUV, 0.0, 1.0);
    float after = curMap - curDepth;
    float before = (1.0 - heightAt(prevUV)) - (curDepth - layerDepth);
    float denom = after - before;
    float w = (abs(denom) < 1e-5) ? 0.5 : clamp(after / denom, 0.0, 1.0);
    return vec4(mix(curUV, prevUV, w),
                clamp(mix(curDepth, curDepth - layerDepth, w), 0.0, 1.0),
                found ? 1.0 : 0.0);
}

float selfShadow(vec2 hitUV, float hitDepth, vec3 lightTS, float scale,
                 float minLayers, float maxLayers) {
    if (scale < 1e-5 || lightTS.z <= 0.0)
        return 1.0;
    float layers = clamp(mix(max(maxLayers, 1.0), max(minLayers, 1.0),
                             clamp(lightTS.z, 0.0, 1.0)),
                         1.0, 32.0);
    float layerDepth = 1.0 / layers;
    float vz = max(lightTS.z, 0.12);
    vec2 deltaUV = ((lightTS.xy / vz) * scale) / layers;
    vec2 curUV = hitUV;
    float curDepth = hitDepth;
    for (int i = 0; i < 32; ++i) {
        if (float(i) >= layers || curDepth <= 0.0)
            break;
        curUV = clamp(curUV + deltaUV, 0.0, 1.0);
        curDepth -= layerDepth;
        if ((1.0 - heightAt(curUV)) < curDepth - 0.015)
            return 0.35;
    }
    return 1.0;
}

// Geometric heightfield march. Returns xy=UV, z=1 on hit, w=hitZ.
vec4 ssdmMarchLocal(vec3 camLocal, vec3 dirLocal, float minLayers, float maxLayers) {
    if (abs(dirLocal.z) < 1e-5)
        return vec4(0.0);
    float t0 = (1.0 - camLocal.z) / dirLocal.z;
    float t1 = (0.0 - camLocal.z) / dirLocal.z;
    float tEnter = max(min(t0, t1), 0.0);
    float tExit = max(t0, t1);
    if (tExit <= tEnter)
        return vec4(0.0);

    float layers = clamp(mix(max(maxLayers, 1.0), max(minLayers, 1.0),
                             clamp(abs(dirLocal.z), 0.0, 1.0)),
                         1.0, 64.0);
    float dt = (tExit - tEnter) / layers;
    float t = tEnter;
    for (int i = 0; i < 64; ++i) {
        if (float(i) >= layers)
            break;
        vec3 p = camLocal + dirLocal * t;
        vec2 uv = uvFromLocal(p);
        if (uv.x < -0.02 || uv.y < -0.02 || uv.x > 1.02 || uv.y > 1.02) {
            t += dt;
            continue;
        }
        if (p.z <= heightAt(clamp(uv, 0.0, 1.0)) + 1e-3) {
            float tA = max(t - dt, tEnter);
            float tB = t;
            for (int r = 0; r < 6; ++r) {
                float tm = 0.5 * (tA + tB);
                vec3 pm = camLocal + dirLocal * tm;
                if (pm.z <= heightAt(uvFromLocal(pm)) + 1e-3)
                    tB = tm;
                else
                    tA = tm;
            }
            vec3 ph = camLocal + dirLocal * tB;
            vec2 uvh = clamp(uvFromLocal(ph), 0.0, 1.0);
            return vec4(uvh, 1.0, clamp(ph.z, 0.0, 1.0));
        }
        t += dt;
    }
    return vec4(0.0);
}

bool isFrontFace() {
    vec3 frontW = normalize(mat3(ubo.model) * vec3(0.0, 0.0, 1.0));
    return abs(dot(normalize(vNormal), frontW)) > 0.55;
}

vec3 shadeLit(vec3 albedo, vec3 N, vec3 V, float shadow) {
    vec3 L = normalize(ubo.lightDirIntensity.xyz);
    float ndl = max(dot(N, L), 0.0) * shadow;
    float ndv = max(dot(N, V), 0.0);
    float rim = pow(1.0 - ndv, 3.0) * 0.05;
    return albedo * (ubo.ambient.rgb + ubo.lightColor.rgb * ndl) + vec3(rim);
}

void main() {
    float mode = pc.data[0];
    float scale = max(pc.data[1], 0.0);
    float minLayers = max(pc.data[2], 1.0);
    float maxLayers = max(pc.data[3], minLayers);

    vec3 Nw = normalize(vNormal);
    vec3 V = normalize(vCameraPos - vWorldPos);
    if (dot(Nw, V) < 0.0)
        Nw = -Nw;

    if (mode >= 0.5) {
        vec3 frontW = normalize(mat3(ubo.model) * vec3(0.0, 0.0, 1.0));
        if (dot(normalize(vNormal), frontW) < -0.35)
            discard;
    }

    mat3 TBN = makeTBN(Nw, vWorldPos, vUV);
    vec3 viewTS = length(TBN[0]) > 1e-4
                      ? normalize(transpose(TBN) * V)
                      : vec3(0.0, 0.0, 1.0);
    vec3 lightTS = length(TBN[0]) > 1e-4
                       ? normalize(transpose(TBN) * normalize(ubo.lightDirIntensity.xyz))
                       : vec3(0.0, 0.0, 1.0);

    vec2 uv = vUV;
    vec3 N = Nw;
    float fragDepth = gl_FragCoord.z;
    float shadow = 1.0;
    bool writeReliefDepth = false;

    if (mode < 0.5) {
        // Classic POM — parallax inside the chart, silhouette stays geometric.
        // Fade parallax at grazing angles so hard cliffs do not zebra-stripe.
        float facing = clamp(abs(viewTS.z), 0.0, 1.0);
        float pomAmt = smoothstep(0.12, 0.42, facing);
        vec4 hit = pomHit(vUV, viewTS, scale * pomAmt, minLayers, maxLayers);
        uv = clamp(mix(vUV, hit.xy, pomAmt), 0.0, 1.0);
        vec3 nTS = heightNormalTS(uv, scale);
        // Flatten normals when facing is low — cliff dFdx noise = zebra.
        nTS = normalize(mix(vec3(0.0, 0.0, 1.0), nTS, pomAmt));
        N = normalize(TBN * nTS);
        shadow = mix(1.0, selfShadow(uv, hit.z, lightTS, scale * pomAmt,
                                     minLayers, maxLayers), 0.35 * pomAmt);
    } else if (mode < 1.5) {
        // SilPOM: POM on the front (parallax look), side-face heightfield for
        // jagged brick-cap limb. Never horizon-trim the front (that = 镂空).
        bool front = isFrontFace();
        if (front) {
            float facing = clamp(abs(viewTS.z), 0.0, 1.0);
            float pomAmt = smoothstep(0.10, 0.38, facing);
            vec4 hit = pomHit(vUV, viewTS, scale * pomAmt, minLayers, maxLayers);
            uv = clamp(mix(vUV, hit.xy, pomAmt), 0.0, 1.0);
            vec3 nTS = heightNormalTS(uv, scale);
            nTS = normalize(mix(vec3(0.0, 0.0, 1.0), nTS, pomAmt));
            N = normalize(TBN * nTS);
            shadow = mix(1.0, selfShadow(uv, hit.z, lightTS, scale * pomAmt,
                                         minLayers, maxLayers), 0.45 * pomAmt);
        } else {
            vec3 camL = localFromWorld(vCameraPos);
            vec3 fragL = localFromWorld(vWorldPos);
            vec3 dirL = normalize(fragL - camL);
            vec4 hit = ssdmMarchLocal(camL, dirL, minLayers, maxLayers);
            if (hit.z < 0.5)
                discard;
            uv = clamp(hit.xy, 0.0, 1.0);
            float h = heightAt(uv);
            if (h < 0.45)
                discard;
            vec3 hitLocal = vec3(uv.x * 2.0 - 1.0, uv.y * 2.0 - 1.0, hit.w);
            fragDepth = fragDepthFromLocal(hitLocal);
            writeReliefDepth = true;
            N = normalize(mat3(ubo.model) * heightNormalTS(uv, scale));
            shadow = mix(1.0, selfShadow(uv, 1.0 - hit.w, lightTS, scale,
                                         minLayers, maxLayers), 0.5);
        }
    } else {
        // Planar SSDM — geometric heightfield march on every face.
        vec3 camL = localFromWorld(vCameraPos);
        vec3 fragL = localFromWorld(vWorldPos);
        vec3 dirL = normalize(fragL - camL);
        vec4 hit = ssdmMarchLocal(camL, dirL, minLayers, maxLayers);
        bool front = isFrontFace();
        if (hit.z < 0.5) {
            if (!front)
                discard;
            uv = clamp(vUV, 0.0, 1.0);
            N = normalize(mat3(ubo.model) * heightNormalTS(uv, scale));
            shadow = 1.0;
        } else {
            uv = clamp(hit.xy, 0.0, 1.0);
            N = normalize(mat3(ubo.model) * heightNormalTS(uv, scale));
            shadow = mix(1.0, selfShadow(uv, 1.0 - hit.w, lightTS, scale,
                                         minLayers, maxLayers), 0.5);
            if (!front) {
                vec3 hitLocal = vec3(uv.x * 2.0 - 1.0, uv.y * 2.0 - 1.0, hit.w);
                fragDepth = fragDepthFromLocal(hitLocal);
                writeReliefDepth = true;
            }
        }
    }

    if (writeReliefDepth)
        gl_FragDepth = min(gl_FragCoord.z, fragDepth);
    else
        gl_FragDepth = gl_FragCoord.z;

    vec3 albedo = texture(albedoSampler, clamp(uv, 0.0, 1.0)).rgb * vTint.rgb * ubo.tint.rgb;
    if (mode < 0.5)
        albedo *= vec3(1.00, 0.94, 0.88);
    else if (mode < 1.5)
        albedo *= vec3(0.90, 1.00, 0.92);
    else
        albedo *= vec3(0.94, 0.96, 1.04);

    outColor = vec4(shadeLit(albedo, N, V, shadow), 1.0);
}
