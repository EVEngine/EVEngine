// Shared fullscreen helpers for stylize post effects.
// Callers must provide `sampler2D MainTex` before including this file.

#ifndef EVE_STYLIZE_POST_COMMON_GLSL
#define EVE_STYLIZE_POST_COMMON_GLSL

float postLuma(vec3 c) {
    return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

vec2 postWrapUV(vec2 uv) {
    return fract(uv);
}

vec4 postSampleMain(vec2 uv) {
    return texture(MainTex, postWrapUV(uv));
}

vec4 postSampleMainClamped(vec2 uv) {
    return texture(MainTex, clamp(uv, vec2(0.0), vec2(1.0)));
}

#endif  // EVE_STYLIZE_POST_COMMON_GLSL
