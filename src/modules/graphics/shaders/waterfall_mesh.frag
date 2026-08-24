#version 450
layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec2 vUV;
layout(location = 2) in vec4 vTint;
layout(location = 3) in vec3 vWorldPos;
layout(location = 4) in vec3 vCameraPos;
layout(location = 5) in vec3 vViewPos;

struct Light3D { vec4 posRadius; vec4 color; };
layout(set = 0, binding = 0, std140) uniform Frame {
    mat4 mvp;
    mat4 model;
    vec4 lightDirIntensity;
    vec4 lightColor;
    vec4 tint;
    vec4 cameraPos;
    vec4 ambient;
    Light3D lights[8];
    vec4 texBomb;
    vec4 parallax;
    mat4 view;
    vec4 clipInfo;
} ubo;

layout(set = 0, binding = 1) uniform sampler2D albedo;
layout(set = 0, binding = 3) uniform samplerCube env;

layout(push_constant) uniform Externals { float data[32]; } u;
layout(location = 0) out vec4 outColor;

const float PI = 3.14159265;

// Classic pseudo-random value noise.
float hash(float n) { return fract(sin(n) * 43758.5453123); }
float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 s = f * f * (3.0 - 2.0 * f);
    float a = hash(i.x + i.y * 57.0);
    float b = hash(i.x + 1.0 + i.y * 57.0);
    float c = hash(i.x + (i.y + 1.0) * 57.0);
    float d = hash(i.x + 1.0 + (i.y + 1.0) * 57.0);
    return mix(mix(a, b, s.x), mix(c, d, s.x), s.y);
}

// 2D fractal noise, driven by a downward-scrolling coordinate so the water
// always flows toward the pool at the bottom.
float flowNoise(vec2 uv, float t) {
    float speed = u.data[1];            // flowSpeed
    vec2 q = vec2(uv.x * 3.0, uv.y * 4.0 + t * speed);
    float n = 0.0;
    float amp = 0.5;
    float freq = 1.0;
    for (int o = 0; o < 4; ++o) {
        n += amp * noise(q * freq + float(o) * 13.7);
        freq *= 2.1;
        amp *= 0.5;
    }
    return n;
}

// Vertical flowing streaks: elongated in the fall direction by streakScale.
float streaks(vec2 uv, float t) {
    float scale = u.data[4];            // streakScale
    int n = int(u.data[3] + 0.5);       // streakCount
    float s = 0.0;
    for (int i = 0; i < 6; ++i) {
        if (i >= n) break;
        float x = fract(hash(float(i) * 3.1) + uv.x * 0.35) - 0.5;
        float phase = hash(float(i) * 9.7) * 10.0;
        float y = (uv.y - 0.5 + t * u.data[1] * 0.6) * scale + phase;
        float dx = exp(-abs(x) * 3.5);
        float dy = exp(-abs(y) * 1.5);
        s += dx * dy;
    }
    return s;
}

// Turbulent white-water crest intensity over the sheet (streaks + noise).
float cascade(vec2 uv, float t) {
    float turb = u.data[2];             // turbulence
    float n = flowNoise(uv, t);
    float st = streaks(uv, t);
    // Bands that slide downward, wider near the bottom like a churning fall.
    float band = 0.5 + 0.5 * sin((uv.y * 22.0 + t * u.data[1] * 3.0) * PI);
    float v = n * 0.6 + st * (0.5 + turb * 0.5) + band * 0.2;
    return clamp(v * turb, 0.0, 1.0);
}

void main() {
    float t = u.data[0];

    // Surface normal from the analytic displacement (finite differences on a
    // function of the unscrolled coordinate so streaks read as geometry bumps).
    vec2 p0 = vUV;
    float eps = 1e-3;
    vec2 pL = p0 - vec2(eps, 0.0);
    vec2 pR = p0 + vec2(eps, 0.0);
    vec2 pD = p0 - vec2(0.0, eps);
    vec2 pU = p0 + vec2(0.0, eps);
    float h  = cascade(p0, t);
    float hL = cascade(pL, t);
    float hR = cascade(pR, t);
    float hD = cascade(pD, t);
    float hU = cascade(pU, t);
    vec2 grad = vec2((hR - hL) / (2.0 * eps), (hU - hD) / (2.0 * eps));
    vec3 N = normalize(vec3(-grad.x, -grad.y, 1.0));   // plane faces +Z

    vec3 V = normalize(ubo.cameraPos.xyz - vWorldPos);
    vec3 R = reflect(-V, N);

    // Sky reflection via the env cubemap, Fresnel-weighted.
    float ndv = max(dot(V, N), 0.0);
    float lod = 1.0 + (1.0 - ndv) * 3.0;
    vec3 refl = textureLod(env, R, lod).rgb;
    float fresnel = 0.02 + 0.98 * pow(1.0 - ndv, 5.0);

    vec3 waterCol = vec3(u.data[10], u.data[11], u.data[12]);
    float reflectAmt = clamp(fresnel * u.data[8] + 0.05, 0.0, 1.0);
    vec3 color = mix(waterCol, refl, reflectAmt);

    // Sun glint highlight.
    vec3 L = normalize(ubo.lightDirIntensity.xyz);
    vec3 H = normalize(V + L);
    float spec = pow(max(dot(N, H), 0.0), 96.0);
    color += ubo.lightColor.rgb * spec * u.data[9];

    // White-water foam: crests in the body plus foam bands at the top lip and
    // the splash pool at the bottom.
    float foam = cascade(p0, t) * u.data[7];
    float top = u.data[5];                              // topFoam
    float bottom = u.data[6];                           // bottomFoam
    float topBand = clamp((p0.y - (1.0 - top)) / max(top, 1e-4), 0.0, 1.0);
    float bottomBand = clamp((bottom - p0.y) / max(bottom, 1e-4), 0.0, 1.0);
    float edge = clamp(topBand + bottomBand, 0.0, 1.0);
    float foamy = clamp(foam + edge * 0.9, 0.0, 1.0);
    vec3 foamCol = vec3(0.90, 0.95, 1.0);
    color = mix(color, foamCol, foamy);

    // Fade to slightly transparent at the very bottom so it melts into the pool.
    outColor = vec4(color, 1.0 - bottomBand * 0.5);
}
