#version 450
// SLG large-map war fog (Bilibili cotton-puff FoW):
//   - dual reverse-scrolling lit cloud COLOR map
//   - mask R = unlock, G = select, B = dissolve
//   - warp mask UV with desaturated cloud noise (disabled inside clear core)
//   - dense sheet → clumps → sparse islands toward unlock (via cover, not alpha wash)
//   - shadow pass: unlocked ground ∩ fog-at-offset ∩ puff silhouette
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

float sampleUnlockSoft(vec2 uv, float softRadius) {
    return (texture(MaskTex, uv).r +
            texture(MaskTex, uv + vec2(softRadius, 0.0)).r +
            texture(MaskTex, uv - vec2(softRadius, 0.0)).r +
            texture(MaskTex, uv + vec2(0.0, softRadius)).r +
            texture(MaskTex, uv - vec2(0.0, softRadius)).r +
            texture(MaskTex, uv + vec2(softRadius, softRadius) * 0.707).r +
            texture(MaskTex, uv + vec2(-softRadius, softRadius) * 0.707).r +
            texture(MaskTex, uv + vec2(softRadius, -softRadius) * 0.707).r +
            texture(MaskTex, uv + vec2(-softRadius, -softRadius) * 0.707).r) *
           (1.0 / 9.0);
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

    vec4 sampleA = texture(MainTex, scrollUV(cloudUv, tileA, speedA, time, 1.0));
    vec4 sampleB = texture(MainTex, scrollUV(cloudUv, tileB, speedB, time, -1.0));
    // Lit cotton albedo + height alpha. Peek-through is gaps between puffs,
    // never a global fogAlpha wash.
    vec3 cloudBase = mix(sampleA.rgb, sampleB.rgb, cloudMix);
    vec3 cloudMul = clamp(sampleA.rgb * sampleB.rgb * 1.12, 0.0, 1.0);
    vec3 cloud = mix(cloudBase, cloudMul, 0.28);
    float noise = luma(cloud);
    // Soft-max coverage: deep fog merges to a sheet; valleys stay open for peek
    // and for the frontier gate to carve sparse islands near unlock.
    float cover = max(sampleA.a, sampleB.a) + sampleA.a * sampleB.a * 0.45;
    cover = clamp(cover, 0.0, 1.0);

    float softRadius = max(edgeSoft * 0.35, 0.008);

    // Hard unwarped clear core — warp must not drag fog into the unlock hole.
    vec2 baseUv = fragUV + fix;
    float clearCore = smoothstep(0.84, 0.96, texture(MaskTex, baseUv).r);
    float warpScale = 1.0 - clearCore;

    float warpNoise = luma(mix(
        texture(MainTex, scrollUV(cloudUv, max(tileA * 0.20, 0.12), speedA * 0.28, time, 1.0)).rgb,
        texture(MainTex, scrollUV(cloudUv, max(tileB * 0.20, 0.12), speedB * 0.28, time, -1.0)).rgb,
        0.5));
    float warpFine = luma(mix(
        texture(MainTex, scrollUV(cloudUv, max(tileA * 0.55, 0.25), speedA * 0.55, time, 1.0)).rgb,
        texture(MainTex, scrollUV(cloudUv, max(tileB * 0.55, 0.25), speedB * 0.55, time, -1.0)).rgb,
        0.5));
    vec2 maskUv = fragUV +
        ((warpNoise - 0.5) * distort + (warpFine - 0.5) * (distort * 0.35)) * warpScale + fix;

    vec4 maskSample = texture(MaskTex, maskUv);
    float selected = maskSample.g;
    float dissolve = maskSample.b;

    // Soft unlock drives approach/frontier. Do NOT multiply fog by (1-unlocked):
    // that creates a muddy alpha wash. Sparseness is cover islands, not fade.
    float unlockedAmt = softenEdge(sampleUnlockSoft(maskUv, softRadius), edgeSoft);
    float fogAmt = 1.0 - clearCore;

    // Frontier peaks mid soft-band; approach rises toward the hole.
    float frontier = 4.0 * unlockedAmt * (1.0 - unlockedAmt);
    frontier = clamp(frontier, 0.0, 1.0);
    frontier = frontier * frontier * (3.0 - 2.0 * frontier);
    frontier *= (1.0 - clearCore);
    float approach = smoothstep(0.05, 0.78, unlockedAmt) * (1.0 - clearCore);
    float breakAmt = clamp(frontier * 0.70 + approach * 0.95, 0.0, 1.0);

    // Sheet always cover-gated so valleys peek; breakAmt raises sparse power
    // → dense sheet → clumps → floating islands → clear.
    float sheet = smoothstep(0.08, 0.42, cover);
    float sparsePow = mix(1.35, 6.2, breakAmt);
    float fogKeep = fogAmt * mix(sheet, pow(max(cover, 1e-3), sparsePow), breakAmt);

    // Extra valley carve on the rim so islands separate cleanly.
    float valley = 1.0 - smoothstep(0.10, 0.50, mix(noise, cover, 0.50));
    fogKeep *= 1.0 - breakAmt * valley * 0.82;
    fogKeep *= 1.0 - approach * (1.0 - smoothstep(0.32, 0.72, cover)) * 0.65;

    float dissolveNoise = luma(texture(MainTex,
        cloudUv * dissolveScale + vec2(time * 0.015, -time * 0.02)).rgb);
    fogKeep *= 1.0 - smoothstep(dissolve - 0.12, dissolve + 0.12, dissolveNoise) * step(1e-4, dissolve);

    // Deep fog stays opaque/bright; breakAmt raises the cover gate → islands only.
    float gate = densityBias + breakAmt * 0.48;
    float density = smoothstep(gate, clamp(gate + densityContrast * mix(1.0, 0.42, breakAmt), 0.0, 1.0),
                               mix(noise, cover, 0.82));
    // Opaque cotton clumps (reference): sparse islands still read as solid white.
    float body = mix(0.94, 1.0, density);

    if (passMode < 0.5) {
        // Drop-shadow of the puff silhouette onto unlocked ground only:
        // sample cloud cover at uv - shadowOff so the cast matches the fog lobes.
        float groundVisible = 1.0 - fogKeep;
        float unlocked = smoothstep(0.38, 0.80, texture(MaskTex, baseUv).r);
        float sUnlock = texture(MaskTex, baseUv + shadowOff).r;
        float overhang = 1.0 - smoothstep(0.48, 0.84, sUnlock);
        vec2 castUv = cloudUv - vec2(shadowOff.x * aspect, shadowOff.y);
        vec4 castA = texture(MainTex, scrollUV(castUv, tileA, speedA, time, 1.0));
        vec4 castB = texture(MainTex, scrollUV(castUv, tileB, speedB, time, -1.0));
        float castCover = clamp(max(castA.a, castB.a) + castA.a * castB.a * 0.45, 0.0, 1.0);
        float puffCast = smoothstep(0.12, 0.52, castCover);
        float a = groundVisible * unlocked * overhang * puffCast *
                  shadowStrength * fogAlpha * fragColor.a;
        outColor = vec4(0.0, 0.0, 0.0, a);
        return;
    }

    // Cool gray underside / milky top — volume like the reference cotton.
    vec3 cool = fogRgb * vec3(0.88, 0.90, 0.95);
    vec3 col = mix(cool * cloud, cloud, clamp(density * 1.02, 0.0, 1.0));
    col = mix(col, fogRgb * 0.98 + cloud * 0.02, 0.03);
    float blink = selected * selectStrength * selectPulse;
    col = mix(col, col * 1.08 + vec3(0.06, 0.10, 0.14), clamp(blink, 0.0, 1.0));
    float a = fogKeep * body * fogAlpha * fragColor.a;
    outColor = vec4(col, a);
}
