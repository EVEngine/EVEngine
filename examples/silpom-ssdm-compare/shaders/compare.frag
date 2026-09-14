#version 450
// Side-by-side POM / SilPOM / SSDM comparison for a planar brick card.
// pc.data[0] mode: 0 = classic POM, 1 = Silhouette POM, 2 = SSDM-style relief.

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

vec2 pomUV(vec2 uv, vec3 viewTS, float scale, float minLayers, float maxLayers) {
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
    float curMapDepth = 1.0 - texture(heightSampler, curUV).r;
    for (int i = 0; i < 64; ++i) {
        if (curDepth >= curMapDepth || float(i) >= layers)
            break;
        curUV -= deltaUV;
        curMapDepth = 1.0 - texture(heightSampler, curUV).r;
        curDepth += layerDepth;
    }
    vec2 prevUV = curUV + deltaUV;
    float after = curMapDepth - curDepth;
    float before = (1.0 - texture(heightSampler, prevUV).r) - (curDepth - layerDepth);
    float denom = after - before;
    float weight = (abs(denom) < 1e-5) ? 0.5 : clamp(after / denom, 0.0, 1.0);
    return mix(curUV, prevUV, weight);
}

float silCoverage(vec2 uv, float padding) {
    vec2 mn = uv - vec2(padding);
    vec2 mx = (1.0 + padding) - uv;
    return (mn.x >= 0.0 && mn.y >= 0.0 && mx.x >= 0.0 && mx.y >= 0.0) ? 1.0 : 0.0;
}

vec4 ssdmSample(vec3 planePos, vec3 planeN, vec3 camPos, vec3 viewDir, float scale,
                float minLayers, float maxLayers) {
    // Keep slab thickness near the POM scale. scale*4 over-extruded the relief so the
    // apparent brick bottoms dug into the floor and lost the depth fight.
    float thickness = max(scale * 1.5, 1e-3);
    vec3 n = normalize(planeN);
    float denom = dot(n, viewDir);
    if (abs(denom) < 1e-5)
        return vec4(0.0);

    vec3 frontPos = planePos + n * thickness;
    float tFront = dot(n, frontPos - camPos) / denom;
    float tBack = dot(n, planePos - camPos) / denom;
    float t0 = max(min(tFront, tBack), 0.0);
    float t1 = max(tFront, tBack);
    if (t1 <= t0)
        return vec4(0.0);

    float layers = clamp(mix(max(maxLayers, 1.0), max(minLayers, 1.0),
                             clamp(abs(dot(n, -viewDir)), 0.0, 1.0)),
                         1.0, 64.0);
    float dt = (t1 - t0) / layers;
    float t = t0;

    mat3 Rt = transpose(mat3(ubo.model));
    vec3 modelOrigin = ubo.model[3].xyz;

    for (int i = 0; i < 64; ++i) {
        if (float(i) >= layers)
            break;
        vec3 p = camPos + viewDir * t;
        float hPlane = dot(n, p - planePos);
        vec3 onPlane = p - n * hPlane;
        vec3 local = Rt * (onPlane - modelOrigin);
        vec2 uv = local.xy * 0.5 + 0.5;
        float height = texture(heightSampler, clamp(uv, 0.0, 1.0)).r;
        float surfaceH = height * thickness;
        if (hPlane <= surfaceH + 1e-4) {
            float cov =
                (uv.x >= 0.0 && uv.y >= 0.0 && uv.x <= 1.0 && uv.y <= 1.0) ? 1.0 : 0.0;
            return vec4(uv, cov, t);
        }
        t += dt;
    }
    return vec4(0.0);
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

    if (mode < 0.5) {
        mat3 TBN = makeTBN(N, vWorldPos, vUV);
        if (length(TBN[0]) > 1e-4) {
            vec3 viewTS = normalize(transpose(TBN) * V);
            uv = pomUV(vUV, viewTS, scale, minLayers, maxLayers);
        }
    } else if (mode < 1.5) {
        mat3 TBN = makeTBN(N, vWorldPos, vUV);
        if (length(TBN[0]) > 1e-4) {
            vec3 viewTS = normalize(transpose(TBN) * V);
            uv = pomUV(vUV, viewTS, scale, minLayers, maxLayers);
        }
        coverage = silCoverage(uv, padding);
    } else {
        // SSDM-style slab march for UVs / silhouette shading.
        // Depth: do NOT trust the projected hit Z (easy to get wrong with mvp*inv(model)
        // under Vulkan ZO). Keep the geometric surface and pull the whole card toward
        // the camera so the grazing floor cannot win inside the panel footprint.
        vec3 planePos = ubo.model[3].xyz;
        vec3 viewDir = normalize(vWorldPos - vCameraPos);
        vec4 hit = ssdmSample(planePos, N, vCameraPos, viewDir, scale, minLayers, maxLayers);

        float h01 = 0.0;
        if (hit.z > 0.5 && hit.w > 0.0) {
            uv = hit.xy;
            vec3 hitPos = vCameraPos + viewDir * hit.w;
            float thickness = max(scale * 1.5, 1e-3);
            h01 = clamp(dot(N, hitPos - planePos) / thickness, 0.0, 1.0);
        } else {
            mat3 TBN = makeTBN(N, vWorldPos, vUV);
            if (length(TBN[0]) > 1e-4) {
                vec3 viewTS = normalize(transpose(TBN) * V);
                uv = pomUV(vUV, viewTS, scale, minLayers, maxLayers);
            }
            h01 = texture(heightSampler, clamp(uv, 0.0, 1.0)).r;
        }
        // Fade relief near the chart bottom so the parallax doesn't look like it
        // continues under the floor plane (the classic "floor covers wall" illusion).
        float bottomFade = smoothstep(0.0, 0.08, uv.y);
        h01 *= bottomFade;
        if (bottomFade < 0.999) {
            // blend UV back toward the geometric chart UV near the base
            uv = mix(vUV, uv, bottomFade);
        }

        // Pull toward camera (smaller ZO depth). Stronger on brick tops.
        fragDepth = clamp(gl_FragCoord.z - (0.002 + h01 * 0.003), 0.0, 1.0);
    }

    if (coverage < 0.5)
        discard;

    gl_FragDepth = fragDepth;

    vec3 albedo = texture(albedoSampler, uv).rgb * vTint.rgb * ubo.tint.rgb;
    if (mode < 0.5)
        albedo *= vec3(1.00, 0.93, 0.88);
    else if (mode < 1.5)
        albedo *= vec3(0.90, 1.00, 0.92);
    else
        albedo *= vec3(0.95, 0.96, 1.02);

    outColor = vec4(shadeLit(albedo, N, V), 1.0);
}
