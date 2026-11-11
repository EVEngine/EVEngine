#version 450
// Digital / block-damage glitch post-effect.
// Ported from keijiro/KinoGlitch@master Assets/Kino/Glitch/Shader/DigitalGlitch.shader
// (MIT License, Copyright (C) 2015 Keijiro Takahashi). Unity Blit/C# shell removed.
//
// Adaptation for EVEngine StylePass (single MainTex binding):
//   - _NoiseTex 64x32 CPU texture → procedural cell noise (default 64×32),
//     refreshed on a quantized time lattice like DigitalGlitch.UpdateNoiseTexture.
//   - _TrashTex history frames → same-frame MainTex sample with a coarse UV
//     jump that changes on ~13/73-frame-like time steps (no RT history yet).
// Core weights (displacement / frame / color shuffle) match the original frag.
//
// @cost Fullscreen, 1–2 taps; comparable to glitchAnalog. Near-identity when
//       strength or intensity is 0.
//
// Push: 0 strength, 1 intensity, 2 time, 3 cellCountX, 4 cellCountY

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragUV;
layout(location = 0) out vec4 outColor;
layout(binding = 0) uniform sampler2D MainTex;
layout(push_constant) uniform Externals { float data[32]; } u;

#include "noise.glsl"
#include "post_common.glsl"

// Approximate the ARGB noise texel DigitalGlitch writes into a 64×32 atlas:
// runs of identical random colors until a coin-flip refreshes (p≈0.11).
vec4 digitalNoiseCell(vec2 cell, float epoch) {
    float runSeed = floor(nrand(cell.y + epoch * 0.17, cell.x * 0.31 + epoch) * 9.0);
    vec2 key = cell + vec2(runSeed, epoch);
    return vec4(nrand(key.x, key.y + 0.1), nrand(key.x + 3.1, key.y + 1.7),
                nrand(key.x + 5.9, key.y + 4.3), nrand(key.x + 8.2, key.y + 6.1));
}

void main() {
    float strength = clamp(u.data[0], 0.0, 1.0);
    float intensity = clamp(u.data[1], 0.0, 1.0) * strength;
    float time = u.data[2];
    float cellCountX = max(u.data[3], 1.0);
    float cellCountY = max(u.data[4], 1.0);

    vec4 src = texture(MainTex, fragUV) * fragColor;
    if (strength < 1e-5 || intensity < 1e-5) {
        outColor = src;
        return;
    }

    // Noise atlas refresh cadence ≈ DigitalGlitch.Update() random gate.
    float refreshHz = mix(2.0, 18.0, intensity);
    float epoch = floor(time * refreshHz);

    vec2 cell = floor(fragUV * vec2(cellCountX, cellCountY));
    vec4 glitch = digitalNoiseCell(cell, epoch);

    float thresh = 1.001 - intensity * 1.001;
    float w_d = step(thresh, pow(glitch.z, 2.5));  // displacement
    float w_f = step(thresh, pow(glitch.w, 2.5));  // frame / trash mix
    float w_c = step(thresh, pow(glitch.z, 3.5));  // color shuffle

    vec2 uv = postWrapUV(fragUV + glitch.xy * w_d);
    vec4 source = postSampleMain(uv) * fragColor;

    // Trash-frame stand-in: jump UV on quantized "frame" counters.
    float frame = floor(time * 60.0);
    float trashA = floor(frame / 13.0);
    float trashB = floor(frame / 73.0);
    float pickB = step(0.5, nrand(frame, 9.0));
    vec2 trashOff = mix(vec2(nrand(trashA, 1.0), nrand(trashA, 2.0)),
                        vec2(nrand(trashB, 3.0), nrand(trashB, 4.0)), pickB);
    trashOff = (trashOff - 0.5) * 0.35;
    vec3 trash = postSampleMain(postWrapUV(uv + trashOff)).rgb;

    vec3 color = mix(source.rgb, trash, w_f);

    // Shuffle color components (original: color.grb + inverted luma lift).
    vec3 neg = clamp(color.grb + (1.0 - dot(color, vec3(1.0))) * 0.5, 0.0, 1.0);
    color = mix(color, neg, w_c);

    outColor = vec4(color, source.a);
}
