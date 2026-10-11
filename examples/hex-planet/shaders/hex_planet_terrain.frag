#version 450

// Terrain surface of the spherical hex map (examples/hex-planet).
//
// The vertex encoding is exactly the one the planar builder emits, so this shader
// and `hex_map_terrain.frag` share the decode and the palette (see
// HexMapMesh.h / HexTerrainVertexCode):
//   uv.x = layerA + layerB * 8 + layerC * 64
//   uv.y = weightB + weightC * 16
//
// What cannot be shared is everything that quietly assumed a flat map. The planar
// shader reads `vWorldPos.y` as altitude, `vNormal.y` as "up", projects its detail
// noise into XZ and fades the far distance into fog; on a sphere Y spans the whole
// planet, XZ collapses at the poles, the surface normal is the radial direction and
// there is no horizon to fade into. Those four are replaced by radial equivalents.
// The palette and the BRDF below are deliberately the same numbers.

layout(location=0) in vec3 vNormal;
layout(location=1) in vec2 vUV;
layout(location=2) in vec4 vTint;
layout(location=3) in vec3 vWorldPos;
layout(location=4) in vec3 vCameraPos;
layout(location=5) in vec3 vViewPos;

struct Light3D { vec4 posRadius; vec4 color; };
layout(set=0,binding=0,std140) uniform Frame {
    mat4 mvp; mat4 model; vec4 lightDirIntensity; vec4 lightColor; vec4 tint;
    vec4 cameraPos; vec4 ambient; Light3D lights[8]; vec4 texBomb; vec4 parallax;
    mat4 view; vec4 clipInfo; vec4 cloud; vec4 cloudWind;
} ubo;
layout(location=0) out vec4 outColor;

const float PI = 3.14159265359;

float hash21(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}
float noise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash21(i), hash21(i + vec2(1, 0)), f.x),
               mix(hash21(i + vec2(0, 1)), hash21(i + vec2(1, 1)), f.x), f.y);
}
float fbm(vec2 p) {
    float s = 0.0, a = 0.5;
    for (int i = 0; i < 4; ++i) { s += noise(p) * a; p = mat2(1.7, -1.3, 1.3, 1.7) * p; a *= 0.5; }
    return s;
}

/**
 * Detail-noise coordinates of a point on the sphere.
 *
 * Dropping the dominant axis keeps the field roughly isotropic. Projecting into XZ
 * as the planar shader does would collapse to a point at both poles, which reads as
 * a radial smear of stretched noise exactly where the twelve pentagons sit.
 */
vec2 sphereCoords(vec3 dir) {
    vec3 a = abs(dir);
    if (a.y >= a.x && a.y >= a.z) return dir.xz;
    if (a.x >= a.z) return dir.yz;
    return dir.xy;
}

/**
 * Inverts the host's own tonemap.
 *
 * A debug fragment stage that emitted a constant 1.0 arrives in the frame buffer as
 * 224/255, i.e. the host applies a second, Reinhard-style curve `y = x / (x + 0.139)`
 * to whatever this stage returns. This shader authors final sRGB values, so it undoes
 * that curve instead of being silently darkened by it; without this the palette below
 * cannot be tuned at all, because every change is compressed by an invisible stage.
 */
vec3 hostTonemapInverse(vec3 srgb) {
    srgb = clamp(srgb, vec3(0.0), vec3(0.95));
    return (0.139 * srgb) / max(vec3(1.0) - srgb, vec3(0.001));
}

// Unity-parity palette: 0 Sand, 1 Grass, 2 Mud, 3 Stone, 4 Snow.
vec3 terrainAlbedo(int id, vec2 p, float grain, float patchiness) {
    if (id == 0) return mix(vec3(0.74, 0.66, 0.42), vec3(0.88, 0.80, 0.55), grain);
    if (id == 1) return mix(vec3(0.24, 0.45, 0.14), vec3(0.44, 0.62, 0.22), grain * 0.7 + patchiness * 0.3);
    if (id == 2) return mix(vec3(0.31, 0.23, 0.14), vec3(0.45, 0.35, 0.22), grain);
    if (id == 3) return mix(vec3(0.36, 0.36, 0.37), vec3(0.62, 0.61, 0.58), grain);
    return mix(vec3(0.80, 0.86, 0.92), vec3(0.98, 0.99, 1.00), grain * 0.6 + 0.35);
}
float terrainRoughness(int id) {
    if (id == 0) return 0.92;
    if (id == 1) return 0.88;
    if (id == 2) return 0.74;
    if (id == 3) return 0.68;
    return 0.42;
}

void main() {
    float packedIndices = vUV.x;
    float packedWeights = vUV.y;
    int layerA = int(mod(packedIndices, 8.0) + 0.5);
    int layerB = int(mod(floor(packedIndices / 8.0), 8.0) + 0.5);
    int layerC = int(floor(packedIndices / 64.0) + 0.5);
    float weightB = fract(packedWeights);
    float weightC = floor(packedWeights) / 16.0;
    float weightA = max(0.0, 1.0 - weightB - weightC);

    float time = ubo.cloud.z;
    vec3 radial = normalize(vWorldPos);
    vec2 p = sphereCoords(radial) * 14.0;
    float grain = noise(p * 9.0);
    float patchiness = fbm(p * 1.6);

    vec3 albedo = terrainAlbedo(layerA, p, grain, patchiness) * weightA
                + terrainAlbedo(layerB, p + 11.3, grain, patchiness) * weightB
                + terrainAlbedo(layerC, p + 23.9, grain, patchiness) * weightC;
    float roughness = terrainRoughness(layerA) * weightA
                    + terrainRoughness(layerB) * weightB
                    + terrainRoughness(layerC) * weightC;

    // The generator already places the snow line, the rock and the deserts, so the
    // continental scale is in the vertex layers; this only breaks up the flat fills.
    albedo *= mix(0.90, 1.08, fbm(p * 0.8));

    float detail = fbm(p * 6.0);
    vec3 N = normalize(vNormal + vec3(dFdx(detail), 0.0, dFdy(detail)) * 0.22);
    vec3 V = normalize(vCameraPos - vWorldPos);
    vec3 L = normalize(ubo.lightDirIntensity.xyz);
    vec3 H = normalize(V + L);
    float ndl = max(dot(N, L), 0.0);
    float ndv = max(dot(N, V), 0.001);
    float ndh = max(dot(N, H), 0.0);
    float alpha = max(0.03, roughness * roughness), a2 = alpha * alpha;
    float D = a2 / max(PI * pow(ndh * ndh * (a2 - 1.0) + 1.0, 2.0), 0.001);
    float k = pow(roughness + 1.0, 2.0) / 8.0;
    float G = (ndl / (ndl * (1.0 - k) + k)) * (ndv / (ndv * (1.0 - k) + k));
    vec3 F0 = vec3(0.04);
    vec3 F = F0 + (1.0 - F0) * pow(1.0 - max(dot(H, V), 0.0), 5.0);
    vec3 spec = D * G * F / max(4.0 * ndl * ndv, 0.001);
    // 1.5 rather than the planar shader's 2.6: a globe has no distance fog and far less
    // self-occlusion, so the same light energy lands on every visible pixel and the
    // snow and sand fills blow straight through the tonemap.
    vec3 direct = ((1.0 - F) * albedo / PI + spec) * ndl * ubo.lightColor.rgb * 1.5;

    // "Up" is the radial direction here, not world Y: N.y would run from -1 at the
    // south pole to +1 at the north and turn the sky term into a latitude gradient.
    float up = dot(N, radial);
    float sky = max(0.0, up) * 0.16 + 0.06;
    vec3 color = direct + albedo * (ubo.ambient.rgb * 0.55 + vec3(0.10, 0.12, 0.15) * sky);

    // Slope shading so cliffs read as rock even without a texture.
    color *= mix(0.72, 1.0, clamp(up * 1.35 + 0.20, 0.0, 1.0));

    // Cloud banding, sampled on the sphere rather than in world XZ.
    float cloudField = fbm(sphereCoords(radial) * 22.0 + ubo.cloudWind.xy * time * 0.012);
    float cloudMask = smoothstep(ubo.cloudWind.z - 0.15, ubo.cloudWind.z + 0.15, cloudField);
    color *= 1.0 - ubo.cloud.x * cloudMask * 0.30;

    // Reinhard keeps mid-tones saturated instead of clipping to white.
    color = color / (1.0 + color * 1.05);
    color = pow(max(color, vec3(0.0)), vec3(1.0 / 2.2));

    // No distance fog: a planet seen from orbit has no haze band to fade into, and
    // the planar `smoothstep(240, 520, viewDepth)` would erase the whole globe. A
    // limb glow takes its place so the silhouette separates from the starfield.
    float rim = pow(1.0 - clamp(ndv, 0.0, 1.0), 3.0);
    color += vec3(0.10, 0.22, 0.44) * rim * 0.40;

    outColor = vec4(hostTonemapInverse(color * vTint.rgb), 1.0);
}
