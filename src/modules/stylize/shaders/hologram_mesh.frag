#version 450
// Hologram mesh fragment shader (Godot / Sci-Fi community style).
//
// Reuses graphics/mesh3d_toon.vert. Builds a translucent, fresnel-lit
// hologram from: a bright edge (fresnel), horizontal scanlines, a subtle
// time-free glitch band, and value-noise mottling. No time uniform is needed:
// scanlines ride gl_FragCoord and the glitch uses a screen-position hash, so
// the effect is fully static-per-frame and self-animating as the camera moves.
//
// Push constants (declareFloat order in bindEffectMeshUniforms("hologram")):
//   0 colorR  1 colorG  2 colorB
//   3 fresnelStrength  4 scanDensity  5 scanIntensity
//   6 flicker  7 alphaBase  8 glitchAmplitude
// Binding 1 = albedo (optional tint of the hologram body).

layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec2 vUV;
layout(location = 2) in vec4 vTint;
layout(location = 3) in vec3 vLightDir;
layout(location = 4) in vec3 vLightColor;
layout(location = 5) in vec3 vWorldPos;
layout(location = 6) in vec3 vCameraPos;

layout(set = 0, binding = 1) uniform sampler2D albedoSampler;

layout(push_constant) uniform Externals { float data[32]; } u;

layout(location = 0) out vec4 outColor;

float hash11(float p) {
    return fract(sin(p * 127.1) * 43758.5453);
}

void main() {
    vec3 color = vec3(u.data[0], u.data[1], u.data[2]);
    float fresnelStrength = max(u.data[3], 0.0);
    float scanDensity = max(u.data[4], 1.0);
    float scanIntensity = clamp(u.data[5], 0.0, 1.0);
    float flicker = clamp(u.data[6], 0.0, 1.0);
    float alphaBase = clamp(u.data[7], 0.0, 1.0);
    float glitchAmplitude = max(u.data[8], 0.0);

    vec3 N = normalize(vNormal);
    vec3 V = normalize(vCameraPos - vWorldPos);
    float fres = pow(clamp(1.0 - max(dot(N, V), 0.0), 0.0, 1.0), 3.0);

    // Horizontal scanlines (darker lines sweep vertically with the pixel row).
    float row = gl_FragCoord.y * scanDensity;
    float scan = 0.5 + 0.5 * sin(row * 6.2831853);
    scan = mix(1.0, scan, scanIntensity);

    // Glitch: occasional bright band picked from a screen hash.
    float glitchBand = step(0.985, hash11(floor(gl_FragCoord.y * 0.25) + gl_FragCoord.x));
    float glitch = glitchBand * glitchAmplitude;

    // Flicker: slow-ish screen-space mottling.
    float flk = mix(1.0, 0.55 + 0.45 * hash11(floor(gl_FragCoord.x) + gl_FragCoord.y * 1.7),
                    flicker);

    // Body tint from albedo (multiplicative), so a texture reads through.
    vec3 body = color * texture(albedoSampler, vUV).rgb * vTint.rgb;

    float lum = (fres * fresnelStrength + scan + glitch) * flk;
    float alpha = clamp(alphaBase + fres * 0.6, 0.0, 1.0) * flk;

    outColor = vec4(body * lum, alpha);
}