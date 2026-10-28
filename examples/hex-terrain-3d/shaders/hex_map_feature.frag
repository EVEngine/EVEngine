#version 450

// Structures and decorations of the hex map: city/farm walls, wall towers,
// bridges and the urban / farm / plant / special decorations.
//
// Two mesh streams feed this shader (`HexSurface::Wall` and
// `HexSurface::Feature`); the mesh builders encode which part a vertex belongs
// to in the first texture coordinate:
//
//   0 wall      1 tower     2 bridge
//   3 urban     4 farm      5 plant     6 special
//
// Because the parts are drawn with different materials but share one pipeline,
// the shader picks its albedo/roughness from that code rather than from a
// texture, which keeps the example asset-free.

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

void main() {
    // Codes are written as integer-valued floats; a half step snaps them back.
    int part = int(floor(vUV.x + 0.5));

    vec3  albedo;
    float roughness;
    if (part == 0) {
        // Wall: dressed stone with a coursing pattern so the thin strips read.
        float course = step(0.5, fract(vWorldPos.y * 0.35));
        albedo = mix(vec3(0.46, 0.44, 0.40), vec3(0.58, 0.56, 0.51), course);
        roughness = 0.78;
    } else if (part == 1) {
        // Tower: slightly lighter stone, keeps the silhouette distinct.
        albedo = vec3(0.63, 0.61, 0.56);
        roughness = 0.70;
    } else if (part == 2) {
        // Bridge: timber planks across the channel.
        float plank = step(0.5, fract(vUV.y * 8.0));
        albedo = mix(vec3(0.36, 0.25, 0.15), vec3(0.46, 0.33, 0.20), plank);
        roughness = 0.85;
    } else if (part == 3) {
        // Urban: plastered walls and a tiled roof, split by height.
        albedo = vWorldPos.y > 0.0 ? vec3(0.72, 0.68, 0.60) : vec3(0.55, 0.30, 0.24);
        albedo = mix(vec3(0.55, 0.30, 0.24), vec3(0.78, 0.74, 0.66),
                     step(0.5, fract(vWorldPos.y * 0.5 + 0.5)));
        roughness = 0.72;
    } else if (part == 4) {
        // Farm: tilled rows.
        float row = noise(vWorldPos.xz * 0.9);
        albedo = mix(vec3(0.52, 0.42, 0.22), vec3(0.66, 0.55, 0.30), row);
        roughness = 0.92;
    } else if (part == 5) {
        // Plant: foliage, darker at the base.
        float shade = clamp(vNormal.y * 0.4 + 0.6, 0.0, 1.0);
        albedo = mix(vec3(0.14, 0.30, 0.11), vec3(0.30, 0.52, 0.18), shade);
        roughness = 0.88;
    } else {
        // Special: pale monument stone.
        albedo = mix(vec3(0.62, 0.60, 0.55), vec3(0.80, 0.79, 0.75),
                     step(0.5, fract(vWorldPos.y * 0.28)));
        roughness = 0.60;
    }

    vec3 N = normalize(vNormal);
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

    vec3 direct = ((1.0 - F) * albedo / PI + spec) * ndl * ubo.lightColor.rgb * 2.2;
    float sky = max(0.0, N.y) * 0.18 + 0.10;
    vec3 color = direct + albedo * (ubo.ambient.rgb * 0.60 + vec3(0.10, 0.12, 0.15) * sky);

    float cloudField = noise(vWorldPos.xz * 0.028 + ubo.cloudWind.xy * ubo.cloud.z * 0.012);
    float cloudMask = smoothstep(ubo.cloudWind.z - 0.15, ubo.cloudWind.z + 0.15, cloudField);
    color *= 1.0 - ubo.cloud.x * cloudMask * 0.30;

    color = color / (1.0 + color * 0.72);
    color = pow(max(color, vec3(0.0)), vec3(1.0 / 2.2));
    float fog = smoothstep(240.0, 520.0, length(vViewPos));
    color = mix(color, vec3(0.075, 0.11, 0.17), fog * 0.55);
    outColor = vec4(color * vTint.rgb, 1.0);
}
