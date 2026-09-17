#version 450
// Full SilPOM vs full SSDM on planar extruded cards.
//
// pc.data[0] mode:
//   0 = classic POM  — parallax only; geometric silhouette unchanged
//   1 = SilPOM       — same solid planar heightfield march as SSDM + jagged
//                      brick-only limb on sides / narrow front rim + FragDepth
//                      (steep-POM+horizon discard punched 镂空 and could not
//                      grow silhouette past the mesh on these cards)
//   2 = SSDM         — planar heightfield march + FragDepth; side-face misses
//                      discard so brick caps form the extruded outline
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
    // 5-tap blur softens texel cliffs so the heightfield has slope the march can hit.
    vec2 texel = 1.0 / vec2(textureSize(heightSampler, 0));
    vec2 c = clamp(uv, 0.0, 1.0);
    float h = texture(heightSampler, c).r * 2.0;
    h += texture(heightSampler, clamp(c + vec2(texel.x, 0.0), 0.0, 1.0)).r;
    h += texture(heightSampler, clamp(c - vec2(texel.x, 0.0), 0.0, 1.0)).r;
    h += texture(heightSampler, clamp(c + vec2(0.0, texel.y), 0.0, 1.0)).r;
    h += texture(heightSampler, clamp(c - vec2(0.0, texel.y), 0.0, 1.0)).r;
    return h * (1.0 / 6.0);
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

float softCoverage(vec2 uv, float feather) {
    float f = max(feather, 1e-4);
    float cx = min(smoothstep(0.0, f, uv.x), smoothstep(0.0, f, 1.0 - uv.x));
    float cy = min(smoothstep(0.0, f, uv.y), smoothstep(0.0, f, 1.0 - uv.y));
    return cx * cy;
}

float horizonTrim(float height01, float ndotv, float strength) {
    float t = clamp(1.0 - abs(ndotv) / 0.35, 0.0, 1.0);
    float threshold = clamp(pow(t, 1.5) * strength, 0.0, 1.0);
    return (height01 >= threshold + 0.02) ? 1.0 : 0.0;
}

vec3 heightNormalTS(vec2 uv, float scale) {
    vec2 texel = 1.0 / vec2(textureSize(heightSampler, 0));
    float hL = heightAt(uv + vec2(-texel.x, 0.0));
    float hR = heightAt(uv + vec2( texel.x, 0.0));
    float hD = heightAt(uv + vec2(0.0, -texel.y));
    float hU = heightAt(uv + vec2(0.0,  texel.y));
    return normalize(vec3((hL - hR) * scale * 10.0,
                          (hD - hU) * scale * 10.0,
                          1.0));
}

// Steep POM + linear refine. Returns xy=UV, z=hitDepth[0..1], w=found.
vec4 pomHit(vec2 uv, vec3 viewTS, float scale, float minLayers, float maxLayers) {
    if (scale < 1e-5)
        return vec4(uv, 0.0, 1.0);
    float layers = clamp(mix(max(maxLayers, 1.0), max(minLayers, 1.0),
                             clamp(abs(viewTS.z), 0.0, 1.0)),
                         1.0, 64.0);
    float layerDepth = 1.0 / layers;
    float vz = max(abs(viewTS.z), 0.08);
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
        curUV -= deltaUV;
        curMap = 1.0 - heightAt(curUV);
        curDepth += layerDepth;
    }
    vec2 prevUV = curUV + deltaUV;
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
    float vz = max(lightTS.z, 0.08);
    vec2 deltaUV = ((lightTS.xy / vz) * scale) / layers;
    vec2 curUV = hitUV;
    float curDepth = hitDepth;
    for (int i = 0; i < 32; ++i) {
        if (float(i) >= layers || curDepth <= 0.0)
            break;
        curUV += deltaUV;
        curDepth -= layerDepth;
        if ((1.0 - heightAt(curUV)) < curDepth - 0.01)
            return 0.2;
    }
    return 1.0;
}

// Geometric heightfield march through the local-space slab.
// Surface is z = height(uv). Returns xy=UV, z=1 on hit (caller applies soft
// coverage — packing it into z caused false misses / 镂空 at chart borders),
// w=hitZ.
vec4 ssdmMarchLocal(vec3 camLocal, vec3 dirLocal, float minLayers, float maxLayers) {
    if (abs(dirLocal.z) < 1e-5)
        return vec4(0.0);
    float t0 = (1.0 - camLocal.z) / dirLocal.z;
    float t1 = (0.0 - camLocal.z) / dirLocal.z;
    float tEnter = max(min(t0, t1), 0.0);
    float tExit = max(t0, t1);
    if (tExit <= tEnter)
        return vec4(0.0);

    // Extra layers + distance-to-surface steps reduce cliff tunneling.
    float layers = clamp(mix(max(maxLayers, 1.0), max(minLayers, 1.0),
                             clamp(abs(dirLocal.z), 0.0, 1.0)),
                         1.0, 64.0) * 1.5;
    layers = clamp(layers, 16.0, 96.0);
    float dtBase = (tExit - tEnter) / layers;
    float t = tEnter;
    float prevD = 1e5;
    float prevT = tEnter;
    for (int i = 0; i < 96; ++i) {
        if (t > tExit)
            break;
        vec3 p = camLocal + dirLocal * t;
        vec2 uv = uvFromLocal(p);
        float d = p.z - heightAt(uv); // >0 above surface
        if (d <= 1e-3 || (prevD > 0.0 && d <= 0.0)) {
            float tA = prevT;
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
            vec2 uvh = uvFromLocal(ph);
            if (uvh.x < -0.02 || uvh.y < -0.02 || uvh.x > 1.02 || uvh.y > 1.02)
                return vec4(0.0);
            return vec4(uvh, 1.0, clamp(ph.z, 0.0, 1.0));
        }
        float stepT = dtBase;
        if (d > 0.0 && d < 0.20)
            stepT = min(dtBase, max(dtBase * 0.2, d * 0.35 / max(abs(dirLocal.z), 0.08)));
        prevD = d;
        prevT = t;
        t += stepT;
    }
    return vec4(0.0);
}

// Front-face miss fill: keep mortar solid (no interior 镂空). Do NOT use this
// on slab sides — filling sides flattens the extruded brick silhouette.
void chartFillFront(vec3 Nw, float scale, float minLayers, float maxLayers,
                    vec3 lightTS, inout vec2 uv, inout vec3 N, inout float coverage,
                    inout float fragDepth, inout float shadow) {
    uv = clamp(vUV, 0.0, 1.0);
    float h = heightAt(uv);
    coverage = 1.0;
    fragDepth = fragDepthFromLocal(vec3(uv.x * 2.0 - 1.0, uv.y * 2.0 - 1.0, h));
    N = normalize(mat3(ubo.model) * heightNormalTS(uv, scale));
    shadow = selfShadow(uv, 1.0 - h, lightTS, scale, minLayers, maxLayers);
}

bool isFrontFace() {
    // Use the mesh attribute (before view-facing flip) so side faces stay sides.
    vec3 frontW = normalize(mat3(ubo.model) * vec3(0.0, 0.0, 1.0));
    return abs(dot(normalize(vNormal), frontW)) > 0.55;
}

vec3 shadeLit(vec3 albedo, vec3 N, vec3 V, float shadow) {
    vec3 L = normalize(ubo.lightDirIntensity.xyz);
    float ndl = max(dot(N, L), 0.0) * shadow;
    float ndv = max(dot(N, V), 0.0);
    float rim = pow(1.0 - ndv, 3.0) * 0.06;
    return albedo * (ubo.ambient.rgb + ubo.lightColor.rgb * ndl) + vec3(rim);
}

void main() {
    float mode = pc.data[0];
    float scale = max(pc.data[1], 0.0);
    float minLayers = max(pc.data[2], 1.0);
    float maxLayers = max(pc.data[3], minLayers);
    float feather = max(pc.data[4], 0.0);
    float horizon = max(pc.data[5], 0.0);

    vec3 Nw = normalize(vNormal);
    vec3 V = normalize(vCameraPos - vWorldPos);
    if (dot(Nw, V) < 0.0)
        Nw = -Nw;

    // Silhouette modes: drop the slab back face so the card does not read as a
    // hollow sandwich; sides + front rim create the extruded brick outline.
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
    float coverage = 1.0;
    float fragDepth = gl_FragCoord.z;
    float shadow = 1.0;

    if (mode < 0.5) {
        // Classic POM — parallax inside the chart, silhouette stays geometric.
        vec4 hit = pomHit(vUV, viewTS, scale, minLayers, maxLayers);
        uv = hit.xy;
        N = normalize(TBN * heightNormalTS(uv, scale));
        vec3 local = localFromWorld(vWorldPos);
        local.z = mix(local.z, heightAt(uv), 0.85);
        fragDepth = fragDepthFromLocal(local);
        shadow = selfShadow(uv, hit.z, lightTS, scale, minLayers, maxLayers);
    } else {
        // SilPOM (mode 1) and planar SSDM (mode 2) share the solid heightfield
        // march. SilPOM only adds a jagged brick-only limb on true side faces
        // (raw mesh normal), so the silhouette pops without punching front mortar.
        vec3 camL = localFromWorld(vCameraPos);
        vec3 fragL = localFromWorld(vWorldPos);
        vec3 dirL = normalize(fragL - camL);
        vec4 hit = ssdmMarchLocal(camL, dirL, minLayers, maxLayers);
        bool front = isFrontFace();
        if (hit.z < 0.5) {
            if (!front)
                discard;
            chartFillFront(Nw, scale, minLayers, maxLayers, lightTS,
                           uv, N, coverage, fragDepth, shadow);
        } else {
            uv = clamp(hit.xy, 0.0, 1.0);
            float h = heightAt(uv);
            if (!front) {
                // Thickness silhouette.
                // SilPOM: brick caps only (jagged). SSDM: full continuous profile.
                if (mode < 1.5 && h < 0.30)
                    discard;
            }
            // No front-face height discard — that re-opens mortar 镂空 at grazing.
            // Edge pop comes from side-face hits + FragDepth on the front.
            coverage = 1.0;
            vec3 hitLocal = vec3(uv.x * 2.0 - 1.0, uv.y * 2.0 - 1.0, hit.w);
            fragDepth = fragDepthFromLocal(hitLocal);
            N = normalize(mat3(ubo.model) * heightNormalTS(uv, scale));
            shadow = selfShadow(uv, 1.0 - hit.w, lightTS, scale, minLayers,
                                maxLayers);
        }
    }

    // RH_ZO: only pull toward the camera so relief never punches holes in the floor.
    gl_FragDepth = min(gl_FragCoord.z, fragDepth);

    vec3 albedo = texture(albedoSampler, clamp(uv, 0.0, 1.0)).rgb * vTint.rgb * ubo.tint.rgb;
    
    if (mode < 0.5)
        albedo *= vec3(1.00, 0.94, 0.88);
    else if (mode < 1.5)
        albedo *= vec3(0.90, 1.00, 0.92);
    else
        albedo *= vec3(0.94, 0.96, 1.04);
    outColor = vec4(shadeLit(albedo, N, V, shadow) * max(coverage, 0.05), 1.0);
}
