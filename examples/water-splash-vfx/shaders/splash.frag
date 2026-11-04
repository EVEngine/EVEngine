#version 450
// Semi-transparent stylized splash on clean clay-sheet UVs.
// UV.v = 0 at the ground, 1 at the open lip. Noise carves the jagged water edge
// and mottles the white foam band; above the cut the sheet is transparent so
// nested crowns and the animation read clearly.

layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec2 vUV;
layout(location = 2) in vec4 vTint;
layout(location = 3) in vec3 vWorldPos;
layout(location = 4) in vec3 vCameraPos;
layout(location = 5) in vec3 vViewPos;

layout(set = 0, binding = 1) uniform sampler2D albedoSampler;
layout(push_constant) uniform Params { float p[32]; } u;
// 0 time, 1 scroll, 2-4 body RGB, 5-7 foam RGB,
// 8 jagged, 9 foamAmount, 10 opacity, 11 softEdge

layout(location = 0) out vec4 outColor;

float hash21(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

float valueNoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    float a = hash21(i);
    float b = hash21(i + vec2(1.0, 0.0));
    float c = hash21(i + vec2(0.0, 1.0));
    float d = hash21(i + vec2(1.0, 1.0));
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

float fbm(vec2 p) {
    float v = 0.0;
    float a = 0.5;
    for (int i = 0; i < 5; ++i) {
        v += a * valueNoise(p);
        p = p * 2.07 + vec2(17.1, 9.3);
        a *= 0.5;
    }
    return v;
}

void main() {
    float time = u.p[0];
    float scroll = u.p[1];
    vec3 body = vec3(u.p[2], u.p[3], u.p[4]);
    vec3 foam = vec3(u.p[5], u.p[6], u.p[7]);
    float jagged = clamp(u.p[8], 0.0, 1.5);
    float foamAmt = clamp(u.p[9], 0.0, 1.5);
    float opacity = clamp(u.p[10], 0.05, 1.0);
    float soft = mix(0.02, 0.12, clamp(u.p[11], 0.0, 1.0));

    // Scroll the water mask upward so rising / falling reads on the fixed sheet.
    float v = clamp(vUV.y - scroll * 0.15 - time * 0.02, -0.2, 1.4);
    float ang = vUV.x;

    // High-frequency angular + vertical noise for a torn water lip (not a clean ring).
    float n1 = fbm(vec2(ang * 18.0, v * 3.0 + time * 0.35));
    float n2 = fbm(vec2(ang * 41.0 + 3.1, v * 7.0 - time * 0.55));
    float n3 = valueNoise(vec2(ang * 73.0, v * 11.0 + time));
    float edgeNoise = (n1 * 0.55 + n2 * 0.30 + n3 * 0.15) * 2.0 - 1.0;

    // Cut height: water fills the lower sheet; above the noisy cut is transparent.
    float cut = 0.58 + jagged * 0.30 * edgeNoise;
    float waterMask = 1.0 - smoothstep(cut - soft, cut + soft * 0.35, v);
    if (waterMask < 0.012) discard;

    // Foam gathers irregularly under the cut — mottled blue/white boundary.
    float foamNoise = fbm(vec2(ang * 28.0 + time * 0.2, v * 9.0));
    float foamBand = smoothstep(cut - (0.10 + 0.14 * foamAmt), cut - soft * 0.5, v) *
                     (1.0 - smoothstep(cut - soft * 0.5, cut + soft * 0.25, v));
    foamBand *= foamAmt * (0.25 + 0.75 * foamNoise);

    // Vertical streaks break the solid body into watery ribbons.
    float streak = fbm(vec2(ang * 22.0, v * 2.5 + time * 0.4));
    float bodyShade = mix(0.82, 1.08, streak);

    vec3 color = mix(body * bodyShade, foam, clamp(foamBand, 0.0, 1.0));

    // Bottom more solid, lip more see-through so nested crowns composite.
    float bodyAlpha = mix(0.92, 0.42, smoothstep(0.05, cut, max(v, 0.0)));
    float alpha = waterMask * bodyAlpha * opacity * vTint.a;
    if (alpha < 0.02) discard;

    // Match engine display curve used by other example mesh shaders.
    color = clamp(color, vec3(0.0), vec3(0.98));
    color = 0.139 * color / max(vec3(1.0) - color, vec3(0.001));
    outColor = vec4(color, alpha);
}
