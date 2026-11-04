#version 450
// Analog video glitch post-effect.
// Ported from keijiro/KinoGlitch@master Assets/Kino/Glitch/Shader/AnalogGlitch.shader
// (MIT License, Copyright (C) 2015 Keijiro Takahashi). Unity Blit/C# shell removed;
// public 0..1 knobs remapped inside the shader the same way AnalogGlitch.cs did.
//
// @cost Fullscreen, 1–2 taps; comparable to chromatic/grain. Zero when all knobs
//       and strength are 0 (near-identity early out).
//
// Push: 0 strength, 1 scanLineJitter, 2 verticalJump, 3 horizontalShake,
//       4 colorDrift, 5 time

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragUV;
layout(location = 0) out vec4 outColor;
layout(binding = 0) uniform sampler2D MainTex;
layout(push_constant) uniform Externals { float data[32]; } u;

#include "noise.glsl"
#include "post_common.glsl"

void main() {
    float strength = clamp(u.data[0], 0.0, 1.0);
    float scanLineJitter = clamp(u.data[1], 0.0, 1.0) * strength;
    float verticalJump = clamp(u.data[2], 0.0, 1.0) * strength;
    float horizontalShake = clamp(u.data[3], 0.0, 1.0) * strength;
    float colorDrift = clamp(u.data[4], 0.0, 1.0) * strength;
    float time = u.data[5];

    vec4 src = texture(MainTex, fragUV) * fragColor;
    if (strength < 1e-5 ||
        (scanLineJitter < 1e-5 && verticalJump < 1e-5 && horizontalShake < 1e-5 &&
         colorDrift < 1e-5)) {
        outColor = src;
        return;
    }

    // Match AnalogGlitch.cs remapping from artistic 0..1 knobs to shader units.
    float slThresh = clamp(1.0 - scanLineJitter * 1.2, 0.0, 1.0);
    float slDisp = 0.002 + pow(scanLineJitter, 3.0) * 0.05;
    float jumpTime = time * verticalJump * 11.3;
    float shakeAmt = horizontalShake * 0.2;
    float driftAmt = colorDrift * 0.04;
    float driftTime = time * 606.11;

    float uCoord = fragUV.x;
    float vCoord = fragUV.y;

    float jitter = nrand(vCoord, time) * 2.0 - 1.0;
    jitter *= step(slThresh, abs(jitter)) * slDisp;

    float jump = mix(vCoord, fract(vCoord + jumpTime), verticalJump);
    float shake = (nrand(time, 2.0) - 0.5) * shakeAmt;
    float drift = sin(jump + driftTime) * driftAmt;

    vec4 src1 = postSampleMain(vec2(uCoord + jitter + shake, jump));
    vec4 src2 = postSampleMain(vec2(uCoord + jitter + shake + drift, jump));

    outColor = vec4(src1.r, src2.g, src1.b, src.a);
}
