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
    // Dual reverse scroll: lit albedo + height alpha from the cotton map.
    // Do NOT lower global fogAlpha — peek-through comes from puff coverage only.
    vec3 cloudBase = mix(sampleA.rgb, sampleB.rgb, cloudMix);
    vec3 cloudMul = clamp(sampleA.rgb * sampleB.rgb * 1.25, 0.0, 1.0);
    vec3 cloud = mix(cloudBase, cloudMul, 0.42);
    float noise = luma(cloud);
    // Soft-max coverage: deep fog merges into a sheet; frontier gate sparsifies
    // only near unlock (valleys between lobes open first).
    float cover = max(sampleA.a, sampleB.a) + sampleA.a * sampleB.a * 0.35;
    cover = clamp(cover, 0.0, 1.0);

    float softRadius = max(edgeSoft * 0.25, 0.006);

    // Unwarped unlock core: hard threshold so soft penumbra / 9-tap blur cannot
    // leak "hole" into fog (that painted dirty shadow smears on the sheet).
    vec2 baseUv = fragUV + fix;
    float clearCore = smoothstep(0.82, 0.96, texture(MaskTex, baseUv).r);
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

    float unlockedAmt = softenEdge(sampleUnlockSoft(maskUv, softRadius), edgeSoft);
    // Force clear wherever the unwarped core says unlocked.
    float fogAmt = (1.0 - unlockedAmt) * (1.0 - clearCore);

    // Frontier peaks in the soft approach band (not inside the hole):
    // dense sheet → broken clumps → sparse islands → clear.
    float frontier = 4.0 * unlockedAmt * (1.0 - unlockedAmt);
    frontier = clamp(frontier, 0.0, 1.0);
    frontier = frontier * frontier * (3.0 - 2.0 * frontier);
    // Suppress frontier once the unwarped core is clear.
    frontier *= (1.0 - clearCore);

    float valley = 1.0 - smoothstep(0.20, 0.58, mix(noise, cover, 0.55));
    fogAmt *= 1.0 - frontier * valley * 0.55;
    fogAmt *= 1.0 - frontier * (1.0 - smoothstep(0.30, 0.68, cover)) * 0.28;

    float sparsePow = mix(1.0, 2.6, frontier);
    float fogKeep = fogAmt * mix(1.0, pow(max(cover, 1e-3), sparsePow), frontier);

    float dissolveNoise = luma(texture(MainTex,
        cloudUv * dissolveScale + vec2(time * 0.015, -time * 0.02)).rgb);
    fogKeep *= 1.0 - smoothstep(dissolve - 0.12, dissolve + 0.12, dissolveNoise) * step(1e-4, dissolve);

    float gate = densityBias + frontier * 0.28;
    float density = smoothstep(gate, clamp(gate + densityContrast * mix(1.0, 0.70, frontier), 0.0, 1.0),
                               mix(noise, cover, 0.72));
    float body = mix(0.28, 1.0, density);
    body *= mix(1.0, density * density, frontier * 0.70);

    if (passMode < 0.5) {
        // Rim contact shadow only: unlocked pixel whose tiny offset neighbor is
        // still fogged. Large offsets painted a dirty ghost of the whole hole.
        float hole = clearCore;
        vec2 rimOff = shadowOff * 0.35;
        float neighbor = texture(MaskTex, baseUv + rimOff).r;
        float overhang = 1.0 - smoothstep(0.55, 0.90, neighbor);
        float a = hole * overhang * shadowStrength * fogAlpha * fragColor.a;
        outColor = vec4(0.0, 0.0, 0.0, a);
        return;
    }

    // Bright cotton albedo — keep peaks milky/white; cool tint only in self-shadow.
    vec3 col = mix(cloud, fogRgb * cloud, 0.08);
    col = mix(col, fogRgb, 0.02);
    col += cloud * (density * 0.10);
    float blink = selected * selectStrength * selectPulse;
    col = mix(col, col * 1.12 + vec3(0.10, 0.14, 0.22), clamp(blink, 0.0, 1.0));
    float a = fogKeep * body * fogAlpha * fragColor.a;
    outColor = vec4(col, a);
}
