#version 450

// Water surface of the hex map. uv.x is the shore parameter emitted by the
// water mesh builder: 0 in open water, approaching 1 on the shoreline band.

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
    float shore = clamp(vUV.x, 0.0, 1.0);
    vec2 p = vWorldPos.xz;

    // Two crossing wave trains plus fine chop; the shoreline gets shorter waves.
    float wave =
        sin(p.x * 0.42 + time * 1.35) * 0.55 +
        sin(p.y * 0.36 - time * 1.05) * 0.45 +
        sin((p.x + p.y) * 0.85 + time * 2.10) * 0.28 +
        noise(p * 0.9 + time * 0.25) * 0.35;
    float slope = shore * 0.35 + 0.06;
    vec3 N = normalize(vec3(dFdx(wave) * 1.6, 1.0, dFdy(wave) * 1.6) + vNormal * slope);
    N = normalize(mix(N, vNormal, shore * 0.4));

    vec3 V = normalize(vCameraPos - vWorldPos);
    vec3 L = normalize(ubo.lightDirIntensity.xyz);
    vec3 H = normalize(V + L);
    float ndl = max(dot(N, L), 0.0);
    float ndv = max(dot(N, V), 0.001);

    // Depth proxy: open water is deep, the shore band is shallow.
    float depth = 1.0 - shore;
    vec3 deep = vec3(0.012, 0.055, 0.105);
    vec3 shallow = vec3(0.055, 0.30, 0.36);
    vec3 albedo = mix(shallow, deep, smoothstep(0.15, 0.85, depth));

    float fresnel = 0.02 + 0.98 * pow(1.0 - ndv, 5.0);
    vec3 sky = mix(vec3(0.10, 0.20, 0.28), vec3(0.42, 0.62, 0.78), max(N.y, 0.0));
    vec3 color = mix(albedo, sky, fresnel * 0.72);
    color += ubo.lightColor.rgb * pow(max(dot(N, H), 0.0), 96.0) * 0.95;
    color += albedo * ndl * ubo.lightColor.rgb * 0.35;
    color += albedo * ubo.ambient.rgb;

    // Foam rides the shoreline band and breaks up with noise.
    float foamNoise = noise(p * 1.35 + vec2(time * 0.20, -time * 0.16));
    float foam = smoothstep(0.55, 0.98, shore) * smoothstep(0.30, 0.75, foamNoise + shore * 0.35);
    foam += smoothstep(0.90, 1.0, shore) * 0.55;
    color = mix(color, vec3(0.90, 0.96, 0.98), clamp(foam, 0.0, 0.85));

    float cloudField = noise(p * 0.02 + ubo.cloudWind.xy * time * 0.012);
    color *= 1.0 - ubo.cloud.x * smoothstep(ubo.cloudWind.z - 0.15, ubo.cloudWind.z + 0.15, cloudField) * 0.30;
    color = 1.0 - exp(-color * 1.35);
    color = pow(max(color, vec3(0.0)), vec3(1.0 / 2.2));
    float fog = smoothstep(240.0, 520.0, length(vViewPos));
    color = mix(color, vec3(0.075, 0.11, 0.17), fog * 0.55);
    outColor = vec4(color * vTint.rgb, 1.0);
}
