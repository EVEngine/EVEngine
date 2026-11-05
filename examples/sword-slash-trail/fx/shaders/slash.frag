#version 450
// Textured sword-slash ribbon. UV.x = arc progress (tail→head), UV.y = root→tip.
// Albedo may be a procedural alpha mask or a CC0 crescent slash PNG (RGB+A).
layout(location=0) in vec3 vNormal;
layout(location=1) in vec2 vUV;
layout(location=2) in vec4 vTint;
layout(location=3) in vec3 vWorldPos;
layout(location=4) in vec3 vCameraPos;
layout(location=5) in vec3 vViewPos;
layout(set=0,binding=1) uniform sampler2D slashMask;
layout(push_constant) uniform Params { float p[32]; } u;
// time, scroll, base RGB, edge RGB, intensity, useTexColor
layout(location=0) out vec4 outColor;

float hash21(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

void main() {
    // Crescent slash sheets are authored left=tail / right=head; scroll along U.
    vec2 uv = vec2(fract(vUV.x - u.p[0] * u.p[1] * 0.12), vUV.y);
    vec4 tex = texture(slashMask, uv);
    float texLum = max(tex.a, max(tex.r, max(tex.g, tex.b)));
    float fromRoot = smoothstep(0.0, 0.08, vUV.y);
    float tipWeight = smoothstep(0.22, 1.0, vUV.y);
    float head = 1.0 - 0.10 * smoothstep(0.94, 1.0, vUV.x);
    float tail = smoothstep(0.0, 0.12, vUV.x);
    float streak = max(0.0, sin((vUV.x * 48.0 - u.p[0] * 20.0) + vUV.y * 4.0));
    float grain = hash21(uv * vec2(64.0, 16.0) + u.p[0]) * 0.12;
    vec3 base = vec3(u.p[2], u.p[3], u.p[4]);
    vec3 edge = vec3(u.p[5], u.p[6], u.p[7]);
    float intensity = max(u.p[8], 0.0);
    float useTexColor = u.p[9];
    vec3 procedural = mix(edge * 0.5, base, tipWeight);
    procedural += edge * streak * (0.2 + 0.7 * tipWeight);
    vec3 texColor = max(tex.rgb, vec3(texLum));
    vec3 color = mix(procedural, mix(procedural, texColor, 0.92), clamp(useTexColor, 0.0, 1.0));
    color *= 1.0 + grain;
    float body = mix(0.45, 1.2, tipWeight) * fromRoot;
    float alpha = max(texLum, tipWeight * 0.55) * body * head * tail * vTint.a * intensity;
    if (alpha < 0.015) discard;
    color = clamp(color, vec3(0.0), vec3(0.98));
    color = 0.139 * color / max(vec3(1.0) - color, vec3(0.001));
    outColor = vec4(color * vTint.rgb, alpha);
}
