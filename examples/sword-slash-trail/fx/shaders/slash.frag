#version 450
// Textured sword-slash ribbon. UV.x = arc progress (tail→head), UV.y = root→tip.
// Native mesh vertex interface — no private camera model.
layout(location=0) in vec3 vNormal;
layout(location=1) in vec2 vUV;
layout(location=2) in vec4 vTint;
layout(location=3) in vec3 vWorldPos;
layout(location=4) in vec3 vCameraPos;
layout(location=5) in vec3 vViewPos;
layout(set=0,binding=1) uniform sampler2D slashMask;
layout(push_constant) uniform Params { float p[32]; } u;
// time, scroll, base RGB, edge RGB, intensity
layout(location=0) out vec4 outColor;

float hash21(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

void main() {
    vec2 uv = vec2(fract(vUV.x - u.p[0] * u.p[1] * 0.15), vUV.y);
    float mask = texture(slashMask, uv).a;
    float across = abs(vUV.y - 0.5);
    float softEdge = smoothstep(0.52, 0.18, across);
    float core = 1.0 - smoothstep(0.0, 0.22, across);
    float head = 1.0 - smoothstep(0.78, 1.0, vUV.x);
    float tail = smoothstep(0.0, 0.12, vUV.x);
    float streak = max(0.0, sin((vUV.x * 42.0 - u.p[0] * 18.0) + vUV.y * 6.0));
    float grain = hash21(uv * vec2(64.0, 16.0) + u.p[0]) * 0.18;
    vec3 base = vec3(u.p[2], u.p[3], u.p[4]);
    vec3 edge = vec3(u.p[5], u.p[6], u.p[7]);
    float intensity = max(u.p[8], 0.0);
    vec3 color = mix(edge, base, core);
    color += edge * streak * 0.35;
    color *= 1.0 + grain;
    float alpha = mask * softEdge * head * tail * vTint.a * intensity;
    if (alpha < 0.02) discard;
    // Invert the renderer's display curve so authored brights stay hot under additive blend.
    color = clamp(color, vec3(0.0), vec3(0.98));
    color = 0.139 * color / max(vec3(1.0) - color, vec3(0.001));
    outColor = vec4(color * vTint.rgb, alpha);
}
