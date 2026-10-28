#version 450

// Road ribbon of the hex map. uv.x runs along the road, uv.y across it.

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

void main() {
    float time = ubo.cloud.z;
    float across = clamp(vUV.y, 0.0, 1.0);
    vec2 p = vWorldPos.xz;
    float grain = noise(p * 3.4);

    // Two wheel ruts with a lighter crown, like the reference road material.
    float ruts = smoothstep(0.10, 0.22, abs(across - 0.28)) * smoothstep(0.10, 0.22, abs(across - 0.72));
    vec3 albedo = mix(vec3(0.30, 0.25, 0.18), vec3(0.46, 0.40, 0.31), grain);
    albedo = mix(albedo * 0.72, albedo, ruts);
    albedo *= smoothstep(0.0, 0.14, across) * smoothstep(1.0, 0.86, across) * 0.35 + 0.65;

    vec3 N = normalize(vNormal + vec3(dFdx(grain), 0.0, dFdy(grain)) * 0.10);
    vec3 V = normalize(vCameraPos - vWorldPos);
    vec3 L = normalize(ubo.lightDirIntensity.xyz);
    float ndl = max(dot(N, L), 0.0);
    vec3 color = albedo * (ubo.ambient.rgb + ubo.lightColor.rgb * ndl * 1.35);
    color *= mix(0.80, 1.0, clamp(N.y * 1.2 + 0.2, 0.0, 1.0));

    float cloudField = noise(p * 0.02 + ubo.cloudWind.xy * time * 0.012);
    color *= 1.0 - ubo.cloud.x * smoothstep(ubo.cloudWind.z - 0.15, ubo.cloudWind.z + 0.15, cloudField) * 0.30;
    color = 1.0 - exp(-color * 1.25);
    color = pow(max(color, vec3(0.0)), vec3(1.0 / 2.2));
    float fog = smoothstep(240.0, 520.0, length(vViewPos));
    color = mix(color, vec3(0.075, 0.11, 0.17), fog * 0.55);
    outColor = vec4(color * vTint.rgb, 1.0);
}
