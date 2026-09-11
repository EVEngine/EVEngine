#version 450
// SLG / large-map war-fog overlay (dual scrolling cloud + mask bridge).
// MainTex = cloud/color noise (repeat). MaskTex = gameplay bridge:
//   R = unlocked (1 clear / 0 fogged)
//   G = selected region (blink)
//   B = dissolve threshold (0 full fog .. 1 fully dissolved)
// Push floats:
//  0 time
//  1 tileA  2 tileB
//  3 speedA 4 speedB
//  5 distort
//  6 fixX   7 fixY
//  8..10 fog RGB
//  11 fogAlpha
//  12 shadowOffX 13 shadowOffY
//  14 shadowStrength
//  15 passMode (0 shadow, 1 main)
//  16 edgeSoft
//  17 selectStrength
//  18 selectPulse
//  19 dissolveScale
//  20 cloudMix

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragUV;
layout(location = 0) out vec4 outColor;
layout(binding = 0) uniform sampler2D MainTex;
layout(binding = 1) uniform sampler2D MaskTex;
layout(push_constant) uniform Externals { float data[32]; } u;

vec2 scrollUV(vec2 uv, float tile, float speed, float time, float dir) {
    return uv * max(tile, 1e-4) + vec2(dir * speed * time, dir * speed * time * 0.73);
}

float luma(vec3 c) {
    return dot(c, vec3(0.299, 0.587, 0.114));
}

void main() {
    float time = u.data[0];
    float tileA = u.data[1];
    float tileB = u.data[2];
    float speedA = u.data[3];
    float speedB = u.data[4];
    float distort = u.data[5];
    vec2 fix = vec2(u.data[6], u.data[7]);
    vec3 fogRgb = vec3(u.data[8], u.data[9], u.data[10]);
    float fogAlpha = clamp(u.data[11], 0.0, 1.0);
    vec2 shadowOff = vec2(u.data[12], u.data[13]);
    float shadowStrength = clamp(u.data[14], 0.0, 1.0);
    float passMode = u.data[15];
    float edgeSoft = max(u.data[16], 1e-4);
    float selectStrength = max(u.data[17], 0.0);
    float selectPulse = clamp(u.data[18], 0.0, 1.0);
    float dissolveScale = max(u.data[19], 0.25);
    float cloudMix = clamp(u.data[20], 0.0, 1.0);

    vec3 cloudA = texture(MainTex, scrollUV(fragUV, tileA, speedA, time, 1.0)).rgb;
    vec3 cloudB = texture(MainTex, scrollUV(fragUV, tileB, speedB, time, -1.0)).rgb;
    vec3 cloud = mix(cloudA, cloudB, cloudMix);
    float noise = luma(cloud);

    // Soften mask edges by warping mask UVs with desaturated dual-cloud noise.
    vec2 maskUv = fragUV + (noise - 0.5) * distort + fix;
    vec4 maskSample = texture(MaskTex, maskUv);
    float unlocked = maskSample.r;
    float selected = maskSample.g;
    float dissolve = maskSample.b;

    float fogKeep = 1.0 - smoothstep(0.5 - edgeSoft, 0.5 + edgeSoft, unlocked);

    // Dissolve: as B rises, pixels whose noise is below B clear first.
    float dissolveNoise = luma(texture(MainTex, fragUV * dissolveScale + vec2(time * 0.02, -time * 0.015)).rgb);
    fogKeep *= step(dissolve, dissolveNoise + 1e-4);

    if (passMode < 0.5) {
        vec2 sUv = maskUv + shadowOff;
        vec4 sMask = texture(MaskTex, sUv);
        float sFog = 1.0 - smoothstep(0.5 - edgeSoft, 0.5 + edgeSoft, sMask.r);
        float sDissolve = luma(texture(MainTex, (fragUV + shadowOff) * dissolveScale).rgb);
        sFog *= step(sMask.b, sDissolve + 1e-4);
        float a = sFog * shadowStrength * fogAlpha * fragColor.a;
        outColor = vec4(0.0, 0.0, 0.0, a);
        return;
    }

    vec3 col = fogRgb * mix(vec3(0.55), cloud, 0.85);
    float blink = selected * selectStrength * selectPulse;
    col = mix(col, col * 1.35 + vec3(0.18, 0.22, 0.28), clamp(blink, 0.0, 1.0));
    float a = fogKeep * fogAlpha * fragColor.a;
    outColor = vec4(col, a);
}
