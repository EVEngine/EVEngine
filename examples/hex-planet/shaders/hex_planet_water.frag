#version 450

// Ocean surface of the spherical hex map (examples/hex-planet).
//
// Shares the planar water contract for its texture coordinates -- `vUV.x` is the
// shore parameter, 0 in open ocean and 1 where the water ends against land -- but
// takes its wave coordinates from the sphere rather than from world XZ, for the
// same reason the terrain shader does: XZ collapses at the poles.

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

vec2 sphereCoords(vec3 dir) {
    vec3 a = abs(dir);
    if (a.y >= a.x && a.y >= a.z) return dir.xz;
    if (a.x >= a.z) return dir.yz;
    return dir.xy;
}

/** Inverts the host's second tonemap stage; see the terrain shader for the measurement. */
vec3 hostTonemapInverse(vec3 srgb) {
    srgb = clamp(srgb, vec3(0.0), vec3(0.95));
    return (0.139 * srgb) / max(vec3(1.0) - srgb, vec3(0.001));
}

void main() {
    float time = ubo.cloud.z;
    vec3 radial = normalize(vWorldPos);
    vec2 p = sphereCoords(radial) * 26.0;

    // Two travelling wave trains plus fbm chop: a flat normal makes an ocean read as
    // a painted disc from orbit, where the only clue to liquid is the specular lobe.
    float wave = sin(p.x * 0.55 + p.y * 0.35 - time * 1.05) * 0.45
               + sin((p.x + p.y) * 0.85 + time * 2.10) * 0.28
               + fbm(p * 1.4 + time * 0.05) * 0.32;

    float shore = clamp(vUV.x, 0.0, 1.0);
    vec3 deep = vec3(0.012, 0.045, 0.095);
    vec3 shallow = vec3(0.040, 0.170, 0.240);
    vec3 albedo = mix(deep, shallow, shore * 0.85);

    // Sea ice. Without it the polar oceans stay dark navy under a grazing light angle
    // and the caps read as holes rather than ice.
    float ice = smoothstep(0.80, 0.94, abs(radial.y));
    albedo = mix(albedo, vec3(0.74, 0.80, 0.87), ice);

    vec3 N = normalize(vNormal + vec3(dFdx(wave), 0.0, dFdy(wave)) * 0.55 + radial * wave * 0.035);
    vec3 V = normalize(vCameraPos - vWorldPos);
    vec3 L = normalize(ubo.lightDirIntensity.xyz);
    vec3 H = normalize(V + L);

    float ndl = max(dot(N, L), 0.0);
    float ndv = max(dot(N, V), 0.001);
    float ndh = max(dot(N, H), 0.0);

    // Tight, strong highlight: the sun glint is what sells the surface as water.
    float shininess = mix(64.0, 900.0, 1.0 - fbm(p * 0.6));
    float spec = pow(ndh, shininess) * 0.40;
    vec3 F = vec3(0.02) + vec3(0.98) * pow(1.0 - max(dot(H, V), 0.0), 5.0);

    vec3 color = albedo * (ubo.ambient.rgb * 0.55 + ubo.lightColor.rgb * ndl * 0.72);
    color += F * spec * ubo.lightColor.rgb * 3.0;

    // Fresnel on the limb: shallow grazing angles turn the ocean into a mirror of the
    // sky, which is also what keeps the far hemisphere from reading as flat navy.
    float fresnel = pow(1.0 - clamp(ndv, 0.0, 1.0), 4.0);
    color = mix(color, vec3(0.22, 0.42, 0.68), fresnel * 0.70);

    color = color / (1.0 + color * 1.05);
    color = pow(max(color, vec3(0.0)), vec3(1.0 / 2.2));

    outColor = vec4(hostTonemapInverse(color * vTint.rgb), 1.0);
}
