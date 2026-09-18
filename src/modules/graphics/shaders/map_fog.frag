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

vec2 hash22(vec2 p) {
    return fract(sin(vec2(dot(p, vec2(127.1, 311.7)),
                          dot(p, vec2(269.5, 183.3)))) * 43758.5453);
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
    // Erode the unlock mask before declaring it fully clear.  A raw mask test
    // clears the whole soft stamp and clips every cloud exactly at the green
    // edge; the eroded core leaves room for puffs to overhang the revealed map.
    float clearCore = smoothstep(0.90, 0.985,
                                 sampleUnlockSoft(baseUv, max(softRadius * 2.6, 0.018)));
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

    // Build one implicit surface from map distance + cloud height. The map mask
    // never multiplies cloud alpha, so it cannot slice a lobe in half. Instead,
    // cloud height pushes the fog boundary outward along the lobe silhouette.
    float unlockedAmt = softenEdge(sampleUnlockSoft(maskUv, softRadius), edgeSoft);
    float wideUnlock = softenEdge(sampleUnlockSoft(maskUv, max(softRadius * 2.4, 0.040)),
                                   min(edgeSoft * 1.35, 0.48));
    float frontier = smoothstep(0.02, 0.20, wideUnlock) *
                     (1.0 - smoothstep(0.985, 1.0, wideUnlock));
    // Full procedural puffs are classified once at their centre. The reveal
    // mask never touches their pixels, so it cannot cut a bite from a lobe.
    const float puffGrid = 9.0;
    vec2 drift = vec2(time * 0.035, time * 0.021);
    vec2 puffPos = floor((cloudUv * puffGrid + drift) * 28.0) / 28.0;
    vec2 puffCell = floor(puffPos);
    float puffField = 0.0;
    float puffShadow = 0.0;
    for (int py = -1; py <= 1; ++py) {
        for (int px = -1; px <= 1; ++px) {
            vec2 cell = puffCell + vec2(px, py);
            vec2 rnd = hash22(cell);
            vec2 centre = cell + 0.18 + rnd * 0.64;
            float radius = 0.62 + hash22(cell + 19.7).x * 0.18;
            vec2 centreCloud = (centre - drift) / puffGrid;
            vec2 centreUv = vec2(centreCloud.x / aspect, centreCloud.y) + fix;
            vec4 centreMask = texture(MaskTex, centreUv);
            // Keep the cluster alive while its Gaussian core contracts. The
            // reveal channel only releases it at the very end of the motion.
            float anchored = 1.0 - smoothstep(0.90, 0.995, centreMask.r);
            float dissolveAtCentre = clamp(centreMask.b, 0.0, 1.0);
            // Raising this Gaussian isosurface removes the low-density rim
            // first, so the cloud contracts organically toward its core.
            float contraction = smoothstep(0.0, 0.82, dissolveAtCentre);
            float cloudThreshold = mix(0.18, 0.92, contraction);
            float clusterScale = mix(1.0, 0.18, contraction);
            vec2 axis = normalize(vec2(rnd.x - 0.5, rnd.y - 0.5) + vec2(0.17, 0.08));
            vec2 side = vec2(-axis.y, axis.x);
            vec2 centres[4] = vec2[4](centre,
                                      centre + axis * radius * 0.62,
                                      centre - axis * radius * 0.55 + side * radius * 0.24,
                                      centre - side * radius * 0.58);
            float radii[4] = float[4](radius, radius * 0.72, radius * 0.66, radius * 0.58);
            float cluster = 0.0;
            float shadowCluster = 0.0;
            for (int ci = 0; ci < 4; ++ci) {
                vec2 lobeCentre = centre + (centres[ci] - centre) * clusterScale;
                vec2 sphereXY = (puffPos - lobeCentre) / max(radii[ci], 0.05);
                float gaussian = exp(-2.2 * dot(sphereXY, sphereXY));
                float lobe = smoothstep(cloudThreshold,
                                        min(cloudThreshold + 0.12, 0.98), gaussian);
                cluster = max(cluster, lobe);
                vec2 shadowXY = (puffPos - vec2(0.0, 0.12) - lobeCentre) /
                                max(radii[ci], 0.05);
                float shadowGaussian = exp(-2.2 * dot(shadowXY, shadowXY));
                shadowCluster = max(shadowCluster,
                    smoothstep(cloudThreshold, min(cloudThreshold + 0.12, 0.98),
                               shadowGaussian));
            }
            // Fade throughout the contraction, not only in its last third.
            // Multiplying the complete cluster field preserves its silhouette:
            // lobes become smaller and paler together instead of being clipped.
            float earlyFade = 1.0 - 0.42 * contraction;
            float finalFade = 1.0 - smoothstep(0.88, 1.0, dissolveAtCentre);
            float alive = earlyFade * finalFade * anchored;
            puffField = max(puffField, cluster * alive);
            puffShadow = max(puffShadow, shadowCluster * alive);
        }
    }
    float bottomEdge = max(puffShadow - puffField, 0.0);
    // Coverage comes only from complete cloud clusters. A continuous backing
    // sheet reads as a white mask beneath the clouds and makes reveals abrupt.
    float fogKeep = max(puffField, bottomEdge);

    float density = smoothstep(densityBias, min(densityBias + densityContrast, 0.98),
                               mix(noise, cover, 0.82));
    // Opaque cotton clumps: transparency belongs in the narrow anti-aliased rim,
    // not across the whole unexplored region.
    float body = mix(0.96, 1.0, density);

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
        outColor = vec4(0.24, 0.29, 0.38, a * 0.42);
        return;
    }

    // Calm pale sheet, sculpted frontier.  This keeps the far unexplored area
    // readable while retaining strong cool undersides on the visible lobes.
    // Painterly three-tone ramp from the reference: no PBR or smooth sphere
    // shading, just white top, pale middle, and a blue-gray lower lip.
    vec3 cloudTop = vec3(0.945, 0.965, 0.965);
    vec3 cloudBottom = vec3(0.737, 0.784, 0.800);
    vec3 sculpted = cloudTop;
    float frontierPuffs = puffField * smoothstep(0.04, 0.46, wideUnlock);
    float sculptAmount = mix(0.04, 1.0, max(frontier, frontierPuffs));
    vec3 col = mix(fogRgb, sculpted, sculptAmount);
    col = mix(col, cloudBottom, clamp(bottomEdge * 0.88, 0.0, 1.0));
    float blink = selected * selectStrength * selectPulse;
    col = mix(col, col * 1.08 + vec3(0.06, 0.10, 0.14), clamp(blink, 0.0, 1.0));
    float a = fogKeep * body * fogAlpha * fragColor.a;
    outColor = vec4(col, a);
}
