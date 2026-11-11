#version 450
// Wave-jitter glitch post-effect.
// Ported from QianMo/X-PostProcessing-Library GlitchWaveJitter.shader (MIT,
// Copyright (C) 2020 QianMo). Unity PostProcess / keyword shell removed;
// Horizontal and Vertical passes collapsed into one frag via `direction`.
// Uses shared snoise() from noise.glsl (Ashima / XNoiseLibrary lineage).
//
// @cost Fullscreen, 2 taps; comparable to glitchAnalog. Near-identity when
//       strength or amount is 0.
//
// Push: 0 strength, 1 amount, 2 frequency, 3 rgbSplit, 4 speed,
//       5 direction (0=H, 1=V), 6 infiniteFreq (0/1), 7 time,
//       8 screenW, 9 screenH

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragUV;
layout(location = 0) out vec4 outColor;
layout(binding = 0) uniform sampler2D MainTex;
layout(push_constant) uniform Externals { float data[32]; } u;

#include "noise.glsl"
#include "post_common.glsl"

void main() {
    float strength = clamp(u.data[0], 0.0, 1.0);
    float amount = clamp(u.data[1], 0.0, 2.0) * strength;
    float frequency = max(u.data[2], 0.0);
    float rgbSplit = max(u.data[3], 0.0);
    float speed = max(u.data[4], 0.0);
    float direction = u.data[5];  // <0.5 horizontal, else vertical
    float infiniteFreq = u.data[6];
    float time = u.data[7];
    float screenW = max(u.data[8], 1.0);
    float screenH = max(u.data[9], 1.0);

    vec4 src = texture(MainTex, fragUV) * fragColor;
    if (strength < 1e-5 || amount < 1e-5) {
        outColor = src;
        return;
    }

    float pulse = (infiniteFreq > 0.5) ? 1.0 : (0.5 + 0.5 * cos(time * frequency));
    float waveStrength = pulse * amount;

    if (direction < 0.5) {
        float uv_y = fragUV.y * screenH;
        float noise_wave_1 = snoise(vec2(uv_y * 0.01, time * speed * 20.0)) * (waveStrength * 32.0);
        float noise_wave_2 = snoise(vec2(uv_y * 0.02, time * speed * 10.0)) * (waveStrength * 4.0);
        float noise_wave_x = noise_wave_1 * noise_wave_2 / screenW;
        float uv_x = fragUV.x + noise_wave_x;
        float rgbSplit_uv_x = (rgbSplit * 50.0 + (20.0 * pulse + 1.0)) * noise_wave_x / screenW;

        vec4 colorG = texture(MainTex, vec2(uv_x, fragUV.y));
        vec4 colorRB = texture(MainTex, vec2(uv_x + rgbSplit_uv_x, fragUV.y));
        outColor = vec4(colorRB.r, colorG.g, colorRB.b, clamp(colorRB.a + colorG.a, 0.0, 1.0)) * fragColor;
    } else {
        float uv_x = fragUV.x * screenW;
        float noise_wave_1 = snoise(vec2(uv_x * 0.01, time * speed * 20.0)) * (waveStrength * 32.0);
        float noise_wave_2 = snoise(vec2(uv_x * 0.02, time * speed * 10.0)) * (waveStrength * 4.0);
        // Preserve original divide-by-_Resolution.x quirk from XPL vertical pass.
        float noise_wave_y = noise_wave_1 * noise_wave_2 / screenW;
        float uv_y = fragUV.y + noise_wave_y;
        float rgbSplit_uv_y = (rgbSplit * 50.0 + (20.0 * pulse + 1.0)) * noise_wave_y / screenH;

        vec4 colorG = texture(MainTex, vec2(fragUV.x, uv_y));
        vec4 colorRB = texture(MainTex, vec2(fragUV.x, uv_y + rgbSplit_uv_y));
        outColor = vec4(colorRB.r, colorG.g, colorRB.b, clamp(colorRB.a + colorG.a, 0.0, 1.0)) * fragColor;
    }
}
