#version 450
// Neon Sobel edge post-effect.
// Ported from QianMo/X-PostProcessing-Library EdgeDetectionSobelNeonV2.shader
// (MIT License, Copyright (C) 2020 QianMo). Unity PostProcess shell removed.
// RGB Sobel gradients drive colorful neon edges (V2 path, not luma-only V1).
//
// @cost Fullscreen, 8 taps + center; comparable to chromatic. Near-identity when
//       strength is 0.
//
// Push: 0 strength, 1 edgeWidth, 2 neonFade, 3 brightness, 4 backgroundFade,
//       5 bgR, 6 bgG, 7 bgB, 8 screenW, 9 screenH

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragUV;
layout(location = 0) out vec4 outColor;
layout(binding = 0) uniform sampler2D MainTex;
layout(push_constant) uniform Externals { float data[32]; } u;

vec3 sobelRgb(float stepx, float stepy, vec2 center) {
    vec3 topLeft = texture(MainTex, center + vec2(-stepx, stepy)).rgb;
    vec3 midLeft = texture(MainTex, center + vec2(-stepx, 0.0)).rgb;
    vec3 bottomLeft = texture(MainTex, center + vec2(-stepx, -stepy)).rgb;
    vec3 midTop = texture(MainTex, center + vec2(0.0, stepy)).rgb;
    vec3 midBottom = texture(MainTex, center + vec2(0.0, -stepy)).rgb;
    vec3 topRight = texture(MainTex, center + vec2(stepx, stepy)).rgb;
    vec3 midRight = texture(MainTex, center + vec2(stepx, 0.0)).rgb;
    vec3 bottomRight = texture(MainTex, center + vec2(stepx, -stepy)).rgb;

    vec3 Gx = topLeft + 2.0 * midLeft + bottomLeft - topRight - 2.0 * midRight - bottomRight;
    vec3 Gy = -topLeft - 2.0 * midTop - topRight + bottomLeft + 2.0 * midBottom + bottomRight;
    return sqrt(Gx * Gx + Gy * Gy);
}

void main() {
    float strength = clamp(u.data[0], 0.0, 1.0);
    float edgeWidth = clamp(u.data[1], 0.05, 5.0);
    float neonFade = clamp(u.data[2], 0.1, 1.0);
    float brightness = clamp(u.data[3], 0.2, 2.0);
    float backgroundFade = clamp(u.data[4], 0.0, 1.0);
    vec3 bgColor = clamp(vec3(u.data[5], u.data[6], u.data[7]), 0.0, 1.0);
    float screenW = max(u.data[8], 1.0);
    float screenH = max(u.data[9], 1.0);

    vec4 sceneColor = texture(MainTex, fragUV) * fragColor;
    if (strength < 1e-5) {
        outColor = sceneColor;
        return;
    }

    vec3 sobelGradient = sobelRgb(edgeWidth / screenW, edgeWidth / screenH, fragUV);
    vec3 background = mix(bgColor, sceneColor.rgb, backgroundFade);
    vec3 edgeColor = mix(background, sobelGradient, neonFade) * brightness;

    outColor = vec4(mix(sceneColor.rgb, edgeColor, strength), sceneColor.a);
}
