#version 450

// River channel of the hex map. uv.x runs along the channel, uv.y across it.

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
    float time = ubo.cloud.z;
    float along = vUV.x * 6.0;
    float across = clamp(vUV.y, 0.0, 1.0);
    vec2 p = vWorldPos.xz;

    // Water flows downstream: ripples travel with +uv.x.
    float ripple =
        sin((p.x * 0.55 + p.y * 0.35) - time * 2.6) * 0.45 +
        sin((p.x * 1.30 - p.y * 0.90) - time * 4.1) * 0.25 +
        noise(p * 1.9 - vec2(time * 0.9, 0.0)) * 0.35;
    float bank = smoothstep(0.0, 0.35, across) * smoothstep(1.0, 0.65, across);
    vec3 N = normalize(vec3(dFdx(ripple), 1.0, dFdy(ripple)) * 1.8 + vNormal);
    N = normalize(mix(vNormal, N, 0.35 + 0.55 * bank));

    vec3 V = normalize(vCameraPos - vWorldPos);
    vec3 L = normalize(ubo.lightDirIntensity.xyz);
    vec3 H = normalize(V + L);
    float ndv = max(dot(N, V), 0.001);

    vec3 albedo = mix(vec3(0.05, 0.20, 0.26), vec3(0.10, 0.40, 0.44), bank);
    float fresnel = 0.03 + 0.97 * pow(1.0 - ndv, 4.0);
    vec3 color = mix(albedo, vec3(0.32, 0.52, 0.62), fresnel * 0.55);
    color += ubo.lightColor.rgb * pow(max(dot(N, H), 0.0), 72.0) * 0.70;
    color += albedo * ubo.ambient.rgb;

    // Wet banks darken and desaturate towards the channel edges.
    color *= mix(0.72, 1.10, bank);
    color += vec3(0.55, 0.72, 0.70) * pow(max(dot(N, H), 0.0), 28.0) * (1.0 - bank) * 0.35;

    color = 1.0 - exp(-color * 1.3);
    color = pow(max(color, vec3(0.0)), vec3(1.0 / 2.2));
    float fog = smoothstep(240.0, 520.0, length(vViewPos));
    color = mix(color, vec3(0.075, 0.11, 0.17), fog * 0.55);
    outColor = vec4(color * vTint.rgb, 1.0);
}
