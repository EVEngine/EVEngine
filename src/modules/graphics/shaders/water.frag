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
layout(set = 0, binding = 6) uniform sampler2D ssrTex;  // screen-space reflection (optional)

layout(push_constant) uniform Externals { float data[32]; } u;
layout(location = 0) out vec4 outColor;

const float PI = 3.14159265;

float hash(float n) { return fract(sin(n) * 43758.5453123); }
vec2  hash2(int i) { return vec2(hash(float(i) * 7.31), hash(float(i) * 13.17 + 1.0)); }

// Expanding damped-wavelet ripple from a periodic drop (smooth, water-like).
float rippleRing(vec2 uv, int i) {
    float period = max(u.data[7], 1e-3);        // rippleInterval
    float local = mod(u.data[0], period);
    float startPhase = hash(float(i) * 3.7) * period;
    float age = local - startPhase;
    if (age < 0.0) return 0.0;
    float life = period * 0.8;
    if (age > life) return 0.0;
    vec2 center = hash2(i);
    center = mix(vec2(0.5), center, 0.72);      // keep drops near the middle
    float r = length(uv - center);
    float radius = age * 0.22;                  // expanding ring
    float wavelength = 0.10;                    // spacing between crests
    float x = (r - radius) / wavelength;
    // Soft damped wavelet: smooth gaussian envelope, a crest + trough pair.
    float envelope = exp(-(x * x) * 0.9);
    float wave = cos(x * 6.28318);
    float fade = exp(-age * 1.6);               // fade out as it grows
    return u.data[3] * envelope * wave * fade;  // rippleAmp
}

// Water height displacement over UV.
float waterHeight(vec2 uv) {
    float t = u.data[0];
    float ws = u.data[8];
    vec2 edgeDist = min(uv, vec2(1.0) - uv);
    float edgeFactor = 1.0 - clamp(min(edgeDist.x, edgeDist.y) / max(u.data[4], 1e-4), 0.0, 1.0);
    float w = 0.0;
    // Shore-edge waves, strongest at the border and fading inward.
    w += edgeFactor * u.data[2] * (sin((uv.x * ws + t * u.data[1]) * PI * 2.0) +
                                   0.5 * sin((uv.y * ws * 0.7 - t * u.data[1] * 1.3) * PI * 2.0));
    // Fine detail everywhere.
    w += u.data[2] * 0.10 * sin((uv.x * 31.0 + uv.y * 17.0 + t * u.data[1] * 2.0) * PI * 2.0);
    // Occasional middle drop ripples.
    int n = int(u.data[6] + 0.5);
    for (int i = 0; i < 8; ++i) {
        if (i >= n) break;
        w += rippleRing(uv, i);
    }
    return w;
}

void main() {
    // Surface normal from the analytic displacement (finite differences).
    float eps = 1e-3;
    float hL = waterHeight(vUV - vec2(eps, 0.0));
    float hR = waterHeight(vUV + vec2(eps, 0.0));
    float hD = waterHeight(vUV - vec2(0.0, eps));
    float hU = waterHeight(vUV + vec2(0.0, eps));
    vec2 grad = vec2((hR - hL) / (2.0 * eps), (hU - hD) / (2.0 * eps));
    vec3 N = normalize(vec3(-grad.x, 1.0, -grad.y));

    vec3 V = normalize(ubo.cameraPos.xyz - vWorldPos);
    vec3 R = reflect(-V, N);

    // Sky reflection via the env cubemap, blurred more at grazing angles.
    // Clamp LOD to available mip levels so a non-mipmapped env is still safe.
    float ndv = max(dot(V, N), 0.0);
    float maxLod = float(max(textureQueryLevels(env) - 1, 0));
    float lod = min(1.0 + (1.0 - ndv) * 4.0, maxLod);
    vec3 refl = textureLod(env, R, lod).rgb * vec3(u.data[12], u.data[13], u.data[14]);
    float fresnel = 0.02 + 0.98 * pow(1.0 - ndv, 5.0);

    vec3 waterCol = vec3(u.data[9], u.data[10], u.data[11]);
    float reflectAmt = clamp(fresnel * u.data[5] + 0.30, 0.0, 1.0);
    vec3 color = mix(waterCol, refl, reflectAmt);

    // Sun glint highlight.
    vec3 L = normalize(ubo.lightDirIntensity.xyz);
    vec3 H = normalize(V + L);
    float spec = pow(max(dot(N, H), 0.0), 96.0);
    color += ubo.lightColor.rgb * spec * u.data[15];

    // Soft foam where waves meet the edge.
    vec2 edgeDist = min(vUV, vec2(1.0) - vUV);
    float edgeFactor = 1.0 - clamp(min(edgeDist.x, edgeDist.y) / max(u.data[4], 1e-4), 0.0, 1.0);
    color = mix(color, vec3(0.85, 0.93, 1.0), edgeFactor * 0.22);

    // Optional screen-space reflection overlay (bound via the height slot,
    // binding 6). Sample the SSR pass result at this fragment's screen UV and
    // blend it over the env reflection where SSR found a hit (ssr.a > 0).
    if (u.data[18] > 0.5 && u.data[16] > 1.0 && u.data[17] > 1.0) {
        vec2 sUV = vec2(gl_FragCoord.x / u.data[16], gl_FragCoord.y / u.data[17]);
        vec4 ssr = texture(ssrTex, sUV);
        color = mix(color, ssr.rgb, clamp(ssr.a * u.data[19], 0.0, 1.0));
    }

    outColor = vec4(color, 1.0);
}
