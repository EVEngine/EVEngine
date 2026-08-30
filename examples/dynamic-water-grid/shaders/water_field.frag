#version 450

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragUV;
layout(location = 0) out vec4 outColor;
layout(binding = 0) uniform sampler2D MainTex;

layout(push_constant) uniform Externals {
    float data[32];
} u;

void main() {
    vec2 screenSize = vec2(u.data[0], u.data[1]);
    vec2 origin = vec2(u.data[2], u.data[3]);
    vec2 tileSize = vec2(u.data[4], u.data[5]);
    vec2 fieldSize = vec2(u.data[6], u.data[7]);
    float time = u.data[8];
    float wetThreshold = u.data[9];
    float opacity = u.data[10];

    // Inverse of EVEngine's isometric TileProjection. Water samples are stored
    // at logical-grid nodes; the dual-grid shore tiles live between four nodes.
    vec2 screen = floor(fragUV * screenSize) + vec2(0.5);
    float a = (screen.x - origin.x) / max(tileSize.x * 0.5, 0.0001);
    float b = (screen.y - origin.y) / max(tileSize.y * 0.5, 0.0001);
    vec2 grid = vec2((b + a) * 0.5, (b - a) * 0.5);

    if (grid.x < -0.5 || grid.y < -0.5 ||
        grid.x > fieldSize.x - 0.5 || grid.y > fieldSize.y - 0.5) {
        outColor = vec4(0.0);
        return;
    }

    vec2 dataUV = (grid + vec2(0.5)) / fieldSize;
    vec4 field = texture(MainTex, dataUV);
    float depth = field.r;
    vec2 flow = field.gb * 2.0 - 1.0;

    float wave = sin(screen.x * 0.075 + screen.y * 0.031 + time * 2.25 + flow.x * 2.0);
    wave += sin(screen.x * -0.028 + screen.y * 0.093 + time * 1.37 + flow.y * 2.0);
    wave *= 0.5;

    float threshold = wetThreshold + wave * 0.009;
    float coverage = smoothstep(threshold - 0.018, threshold + 0.025, depth);
    if (coverage <= 0.001) {
        outColor = vec4(0.0);
        return;
    }

    float deep = smoothstep(0.10, 0.82, depth);
    vec3 shallowColor = vec3(0.18, 0.55, 0.62);
    vec3 deepColor = vec3(0.035, 0.20, 0.34);
    vec3 waterColor = mix(shallowColor, deepColor, deep);

    float contour = 1.0 - smoothstep(0.015, 0.065, abs(depth - threshold - 0.035));
    float glint = max(0.0, sin(screen.x * 0.16 - screen.y * 0.07 + time * 3.1));
    waterColor += vec3(0.42, 0.31, 0.46) * contour * (0.45 + glint * 0.35);

    float alpha = coverage * opacity * mix(0.56, 0.84, deep);
    outColor = vec4(waterColor * fragColor.rgb, alpha * fragColor.a);
}
