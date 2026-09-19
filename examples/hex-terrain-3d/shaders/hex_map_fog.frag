#version 450

// Fog-of-war overlay of the hex map.
//
// The mesh builder (HexFogMesh.cpp) covers every cell that is not currently
// fully visible with a hexagonal column and encodes the shade in the first
// texture coordinate:
//   uv.x = 0  -> explored before, but no viewer sees it right now (dim)
//   uv.x = 1  -> never explored (opaque black)
//
// The shader outputs final display colours directly: the overlay is an
// unlit UI-like surface, so the lit-surface tone mapping of the terrain shader
// would darken it a second time.

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

void main() {
    float unknown = step(0.5, vUV.x);
    // The renderer tone-maps and gamma-encodes this surface like any other, so
    // these are *linear* values: a mid grey-blue for ground that was explored but
    // is not watched right now, and near black for ground never seen at all.
    vec3 seen    = vec3(0.10, 0.13, 0.20);
    vec3 unseen  = vec3(0.015, 0.02, 0.04);
    vec3 color   = mix(seen, unseen, unknown);

    // Large-scale mottling keeps the flat column tops from reading as plastic.
    float mottle = fbm(vWorldPos.xz * 0.06);
    color *= 0.86 + 0.28 * mottle * (1.0 - unknown * 0.6);

    // A faint lift on the vertical walls so the fog column has a silhouette.
    float wall = 1.0 - abs(normalize(vNormal).y);
    color *= 1.0 - wall * 0.22;

    float cloudField = fbm(vWorldPos.xz * 0.028 + ubo.cloudWind.xy * ubo.cloud.z * 0.012);
    float cloudMask = smoothstep(ubo.cloudWind.z - 0.15, ubo.cloudWind.z + 0.15, cloudField);
    color *= 1.0 - ubo.cloud.x * cloudMask * 0.30;

    float fog = smoothstep(240.0, 520.0, length(vViewPos));
    color = mix(color, vec3(0.075, 0.11, 0.17), fog * 0.55);
    outColor = vec4(color * vTint.rgb, 1.0);
}
