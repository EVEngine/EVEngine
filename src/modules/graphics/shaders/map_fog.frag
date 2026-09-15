#version 450
// SLG large-map war fog (Bilibili article approach):
//   - dual reverse-scrolling cloud COLOR map (different tiling / speed)
//   - mask R = unlock, G = select, B = dissolve
//   - warp mask UV with desaturated cloud noise + fix offsets
//   - wispy unlock edges (cloud-shaped transition band)
//   - shadow pass before main for volume
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

float softenEdge(float v, float soft) {
    float lo = clamp(0.5 - soft, 0.0, 1.0);
    float hi = clamp(0.5 + soft, 0.0, 1.0);
    float t = smoothstep(lo, hi, v);
    return pow(t, mix(1.0, 0.50, clamp(soft * 3.0, 0.0, 1.0)));
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
    float densityContrast = max(u.data[21], 0.05);
    float densityBias = clamp(u.data[22], 0.0, 0.9);
    float aspect = max(u.data[23], 1e-4);

    vec2 cloudUv = vec2(fragUV.x * aspect, fragUV.y);

    vec3 sampleA = texture(MainTex, scrollUV(cloudUv, tileA, speedA, time, 1.0)).rgb;
    vec3 sampleB = texture(MainTex, scrollUV(cloudUv, tileB, speedB, time, -1.0)).rgb;
    // Dual reverse scroll (article): keep color-map structure — average + multiply.
    // Heavy screen-blend washed lit clouds to flat white on Lavapipe/soft maps.
    vec3 cloudBase = mix(sampleA, sampleB, cloudMix);
    vec3 cloudMul = clamp(sampleA * sampleB * 1.25, 0.0, 1.0);
    vec3 cloud = mix(cloudBase, cloudMul, 0.42);
    float noise = luma(cloud);

    // Low-frequency warp (article: desaturate cloud color map as mask UV noise).
    float warpNoise = luma(mix(
        texture(MainTex, scrollUV(cloudUv, max(tileA * 0.20, 0.12), speedA * 0.28, time, 1.0)).rgb,
        texture(MainTex, scrollUV(cloudUv, max(tileB * 0.20, 0.12), speedB * 0.28, time, -1.0)).rgb,
        0.5));
    // Second octave warp for irregular cloudy rims.
    float warpFine = luma(mix(
        texture(MainTex, scrollUV(cloudUv, max(tileA * 0.55, 0.25), speedA * 0.55, time, 1.0)).rgb,
        texture(MainTex, scrollUV(cloudUv, max(tileB * 0.55, 0.25), speedB * 0.55, time, -1.0)).rgb,
        0.5));
    vec2 maskUv = fragUV + (warpNoise - 0.5) * distort + (warpFine - 0.5) * (distort * 0.35) + fix;

    vec4 maskSample = texture(MaskTex, maskUv);
    float unlocked = maskSample.r;
    float selected = maskSample.g;
    float dissolve = maskSample.b;

    float softRadius = max(edgeSoft * 0.40, 0.008);
    float unlockedSoft =
        (unlocked +
         texture(MaskTex, maskUv + vec2(softRadius, 0.0)).r +
         texture(MaskTex, maskUv - vec2(softRadius, 0.0)).r +
         texture(MaskTex, maskUv + vec2(0.0, softRadius)).r +
         texture(MaskTex, maskUv - vec2(0.0, softRadius)).r +
         texture(MaskTex, maskUv + vec2(softRadius, softRadius) * 0.707).r +
         texture(MaskTex, maskUv + vec2(-softRadius, softRadius) * 0.707).r +
         texture(MaskTex, maskUv + vec2(softRadius, -softRadius) * 0.707).r +
         texture(MaskTex, maskUv + vec2(-softRadius, -softRadius) * 0.707).r) *
        (1.0 / 9.0);
    float fogKeep = 1.0 - softenEdge(unlockedSoft, edgeSoft);

    // Bubbly rim: carve unlock edge in the valleys BETWEEN cotton puffs
    // (reference FoW has discrete circular lobes + pinholes at the frontier).
    float edgeBand = 4.0 * fogKeep * (1.0 - fogKeep);
    float valley = 1.0 - smoothstep(0.18, 0.55, noise);
    fogKeep *= 1.0 - edgeBand * valley * 1.15;
    // Extra pinholes near the rim where puff coverage is thin.
    fogKeep *= 1.0 - edgeBand * (1.0 - smoothstep(0.35, 0.70, noise)) * 0.35;

    float dissolveNoise = luma(texture(MainTex,
        cloudUv * dissolveScale + vec2(time * 0.015, -time * 0.02)).rgb);
    fogKeep *= 1.0 - smoothstep(dissolve - 0.12, dissolve + 0.12, dissolveNoise) * step(1e-4, dissolve);

    // Puff luminance → coverage. Valleys stay see-through; peaks stay milky.
    float density = smoothstep(densityBias, clamp(densityBias + densityContrast, 0.0, 1.0), noise);
    // Interior porosity so terrain peeks through inter-puff gaps (reference look).
    float porosity = valley * valley * 0.38;
    // Toward the unlock rim, lean harder on density so coverage thins out
    // gradually (sparse frontier puffs, terrain shows between them).
    float rimThin = mix(1.0, density * density, edgeBand * 1.05);
    // Partial alpha throughout — never a solid paper plate.
    float body = mix(0.04, 0.72, density);
    body *= (1.0 - porosity) * rimThin;

    if (passMode < 0.5) {
        // Cast shadow onto the unlocked terrain: hole ∩ offset-fog, shaped by
        // the same puff density so the shadow silhouette matches the cotton rim.
        vec2 sUv = maskUv + shadowOff;
        vec4 sMask = texture(MaskTex, sUv);
        float sUnlocked =
            (sMask.r +
             texture(MaskTex, sUv + vec2(softRadius, 0.0)).r +
             texture(MaskTex, sUv - vec2(softRadius, 0.0)).r +
             texture(MaskTex, sUv + vec2(0.0, softRadius)).r +
             texture(MaskTex, sUv - vec2(0.0, softRadius)).r) *
            0.2;
        float hole = softenEdge(unlockedSoft, edgeSoft);
        float sFog = 1.0 - softenEdge(sUnlocked, edgeSoft);
        float sDissolveNoise = luma(texture(MainTex, (cloudUv + shadowOff * vec2(aspect, 1.0)) * dissolveScale).rgb);
        sFog *= 1.0 - smoothstep(sMask.b - 0.12, sMask.b + 0.12, sDissolveNoise) * step(1e-4, sMask.b);
        float a = hole * sFog * mix(0.40, 0.95, density) * shadowStrength * fogAlpha * fragColor.a;
        outColor = vec4(0.0, 0.0, 0.0, a);
        return;
    }

    // Keep puff albedo dominant — near-white peaks, cool-gray self-shadow valleys.
    vec3 col = mix(cloud, fogRgb * cloud, 0.10);
    col = mix(col, fogRgb, 0.03);
    col += cloud * (density * 0.08);
    float blink = selected * selectStrength * selectPulse;
    col = mix(col, col * 1.12 + vec3(0.10, 0.14, 0.22), clamp(blink, 0.0, 1.0));
    float a = fogKeep * body * fogAlpha * fragColor.a;
    outColor = vec4(col, a);
}
