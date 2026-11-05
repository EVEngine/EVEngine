#version 450
// Screen-space contact shadows: short ray march along the primary light
// direction against GBuffer hardware depth (MainTex.r = Vulkan NDC z).
// Optional NormalTex (binding 1) is world normal * 0.5 + 0.5.
//
// Push (declareMatrix + declareFloat order):
//  0..15 invViewProj
//  16 nearZ  17 farZ
//  18 length  19 thickness  20 intensity  21 stepCount
//  22 lightDirX  23 lightDirY  24 lightDirZ
//  25 texelW  26 texelH  27 hasNormal  28 startBias

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragUV;
layout(location = 0) out vec4 outColor;
layout(binding = 0) uniform sampler2D MainTex;
layout(binding = 1) uniform sampler2D NormalTex;
layout(push_constant) uniform Externals { float data[32]; }
u;

mat4 loadInvVP() {
    return mat4(u.data[0], u.data[1], u.data[2], u.data[3], u.data[4], u.data[5], u.data[6], u.data[7], u.data[8],
                u.data[9], u.data[10], u.data[11], u.data[12], u.data[13], u.data[14], u.data[15]);
}

float ndcToViewZ(float ndcZ, float nearZ, float farZ) {
    return (nearZ * farZ) / max(farZ - ndcZ * (farZ - nearZ), 1e-6);
}

vec3 reconstructWorld(mat4 invVP, vec2 uv, float ndcZ) {
    vec2 ndc  = vec2(uv.x * 2.0 - 1.0, uv.y * 2.0 - 1.0);
    vec4 clip = vec4(ndc, clamp(ndcZ, 0.0, 1.0), 1.0);
    vec4 w    = invVP * clip;
    return w.xyz / max(w.w, 1e-6);
}

float sampleNdcZ(vec2 uv) { return texture(MainTex, clamp(uv, vec2(0.0), vec2(1.0))).r; }

float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

// Returns occlusion in [0,1] for one dithered march, or -1 if no hit.
float marchContact(mat4 viewProj, vec3 origin, vec3 lightDir, float rayLen, float thickness, float nearZ,
                   float farZ, int steps, float dither) {
    float stepSize = rayLen / float(steps);
    float prevT    = 0.0;
    float hitT     = -1.0;
    float hitDelta = 0.0;
    // Keep dither modest: large offsets skip thin floor-contact slabs and
    // reintroduce zebra when adjacent pixels alternate hit/miss.
    float startT = stepSize * (0.35 + dither * 0.25);
    for (int i = 0; i < steps; ++i) {
        float t = startT + stepSize * float(i);
        if (t > rayLen) break;
        vec3 samplePos = origin + lightDir * t;
        vec4 clip      = viewProj * vec4(samplePos, 1.0);
        if (abs(clip.w) < 1e-6) {
            prevT = t;
            continue;
        }
        vec3 ndc = clip.xyz / clip.w;
        vec2 uv  = ndc.xy * 0.5 + 0.5;
        if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) break;

        float sceneNdcZ = sampleNdcZ(uv);
        if (sceneNdcZ >= 0.9999) {
            prevT = t;
            continue;
        }

        float sceneViewZ = ndcToViewZ(sceneNdcZ, nearZ, farZ);
        float rayViewZ   = ndcToViewZ(clamp(ndc.z, 0.0, 1.0), nearZ, farZ);
        float delta      = rayViewZ - sceneViewZ;
        if (delta > 0.0 && delta < thickness) {
            hitT     = t;
            hitDelta = delta;
            float lo = prevT;
            float hi = t;
            for (int r = 0; r < 6; ++r) {
                float mid       = mix(lo, hi, 0.5);
                vec3  midPos    = origin + lightDir * mid;
                vec4  midClip   = viewProj * vec4(midPos, 1.0);
                if (abs(midClip.w) < 1e-6) break;
                vec3  midNdc    = midClip.xyz / midClip.w;
                vec2  midUV     = midNdc.xy * 0.5 + 0.5;
                float midSceneZ = sampleNdcZ(midUV);
                float midSceneV = ndcToViewZ(midSceneZ, nearZ, farZ);
                float midRayV   = ndcToViewZ(clamp(midNdc.z, 0.0, 1.0), nearZ, farZ);
                float midDelta  = midRayV - midSceneV;
                if (midDelta > 0.0 && midDelta < thickness) {
                    hi       = mid;
                    hitT     = mid;
                    hitDelta = midDelta;
                } else {
                    lo = mid;
                }
            }
            break;
        }
        prevT = t;
    }
    if (hitT < 0.0) return -1.0;
    // Quintic-ish soft falloffs — linear smoothstep left hard concentric rings.
    float distU    = clamp(hitT / rayLen, 0.0, 1.0);
    float thickU   = clamp(hitDelta / thickness, 0.0, 1.0);
    float distFade = 1.0 - (distU * distU * (3.0 - 2.0 * distU));
    float soft     = 1.0 - (thickU * thickU * (3.0 - 2.0 * thickU));
    return distFade * soft;
}

float occlusionOrZero(float o) { return o < 0.0 ? 0.0 : o; }

void main() {
    mat4  invVP     = loadInvVP();
    float nearZ     = max(u.data[16], 1e-3);
    float farZ      = max(u.data[17], nearZ + 1e-3);
    float rayLen    = max(u.data[18], 1e-4);
    float thickness = max(u.data[19], 1e-4);
    float intensity = max(u.data[20], 0.0);
    int   steps     = int(clamp(u.data[21], 4.0, 32.0));
    vec3  lightDir  = vec3(u.data[22], u.data[23], u.data[24]);
    float llen      = length(lightDir);
    lightDir        = llen > 1e-3 ? lightDir / llen : vec3(0.0, 1.0, 0.0);
    float texelW    = max(u.data[25], 1e-5);
    float texelH    = max(u.data[26], 1e-5);
    float hasNormal = u.data[27];
    float startBias = max(u.data[28], 0.0);

    float ndcZ = sampleNdcZ(fragUV);
    if (ndcZ >= 0.9999) {
        outColor = vec4(0.0);
        return;
    }

    vec3 pos    = reconstructWorld(invVP, fragUV, ndcZ);
    vec4 eyeH   = invVP * vec4(0.0, 0.0, 0.0, 1.0);
    vec3 camPos = eyeH.xyz / max(eyeH.w, 1e-6);

    vec3 normal;
    if (hasNormal > 0.5) {
        vec3  n   = texture(NormalTex, clamp(fragUV, vec2(0.0), vec2(1.0))).xyz * 2.0 - 1.0;
        float len = length(n);
        normal    = len > 1e-3 ? n / len : vec3(0.0, 1.0, 0.0);
    } else {
        vec3 posR = reconstructWorld(invVP, fragUV + vec2(texelW, 0.0), sampleNdcZ(fragUV + vec2(texelW, 0.0)));
        vec3 posU = reconstructWorld(invVP, fragUV + vec2(0.0, texelH), sampleNdcZ(fragUV + vec2(0.0, texelH)));
        normal    = normalize(cross(posR - pos, posU - pos));
    }
    if (dot(normal, camPos - pos) < 0.0) normal = -normal;

    // Only surfaces facing the light can receive contact shadow from nearby blockers.
    float nDotL = dot(normal, lightDir);
    if (nDotL <= 0.02) {
        outColor = vec4(0.0);
        return;
    }

    mat4 viewProj = inverse(invVP);
    vec3 origin   = pos + normal * startBias + lightDir * startBias;

    // Dual-ray soft: center + one tangent offset (kills zebra without Lavapipe
    // blow-up from a third march on reflection/VT-heavy presets).
    vec3 upRef   = abs(lightDir.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(lightDir, upRef));
    float softR  = rayLen * 0.06;
    float side   = hash12(fragUV * vec2(733.0, 491.0)) < 0.5 ? -1.0 : 1.0;

    float rnd = hash12(fragUV * vec2(1024.0, 768.0) + vec2(17.0, 9.0));
    float o0  = marchContact(viewProj, origin, lightDir, rayLen, thickness, nearZ, farZ, steps, rnd);
    float o1  = marchContact(viewProj, origin + tangent * (softR * side), lightDir, rayLen, thickness, nearZ, farZ,
                            steps, fract(rnd + 0.37));
    float occlusion = (occlusionOrZero(o0) * 0.62) + (occlusionOrZero(o1) * 0.38);

    // Facing-weighted: grazing receivers get less contact darkening.
    occlusion *= smoothstep(0.02, 0.35, nDotL);
    // Mild gamma softens residual step edges without washing out contact tips.
    occlusion = sqrt(clamp(occlusion, 0.0, 1.0));

    float darken = clamp(occlusion * intensity, 0.0, 1.0);
    outColor     = vec4(0.0, 0.0, 0.0, darken);
}
