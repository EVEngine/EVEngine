#version 450
// Two independently seeded CPU PBR layers, blended in local object height.
// Existing Mesh3D bindings: albedo=1, normals=2, scalar roughness atlas=6.
layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec2 vUV;
layout(location = 2) in vec4 vTint;
layout(location = 3) in vec3 vWorldPos;
layout(location = 4) in vec3 vCameraPos;
layout(location = 6) in vec3 vLocalPos;
layout(location = 0) out vec4 outColor;
struct Light3D {
    vec4 posRadius;
    vec4 color;
};
layout(set = 0, binding = 0, std140) uniform Frame {
    mat4    mvp;
    mat4    model;
    vec4    lightDirIntensity;
    vec4    lightColor;
    vec4    tint;
    vec4    cameraPos;
    vec4    ambient;
    Light3D lights[8];
}
ubo;
layout(set = 0, binding = 1) uniform sampler2D colorAtlas;
layout(set = 0, binding = 2) uniform sampler2D normalAtlas;
layout(set = 0, binding = 6) uniform sampler2D roughnessAtlas;
layout(push_constant) uniform Externals { float data[32]; }
pc;
const float PI = 3.141592653589793;
float       hash31(vec3 p) {
    p = fract(p * 0.1031);
    p += dot(p, p.yzx + 33.33);
    return fract((p.x + p.y) * p.z);
}
float noise(vec3 p) {
    vec3 i = floor(p), f = fract(p);
    f       = f * f * (3.0 - 2.0 * f);
    float a = mix(hash31(i), hash31(i + vec3(1, 0, 0)), f.x);
    float b = mix(hash31(i + vec3(0, 1, 0)), hash31(i + vec3(1, 1, 0)), f.x);
    float c = mix(hash31(i + vec3(0, 0, 1)), hash31(i + vec3(1, 0, 1)), f.x);
    float d = mix(hash31(i + vec3(0, 1, 1)), hash31(i + vec3(1, 1, 1)), f.x);
    return mix(mix(a, b, f.y), mix(c, d, f.y), f.z);
}
vec2 atlasUv(int layer) {
    // Each square layer has a one-texel wrap gutter on all sides. No mipmaps:
    // bilinear footprints cannot reach the other material or an unrelated row.
    vec2  size = vec2(textureSize(colorAtlas, 0));
    float tile = size.y, content = tile - 2.0;
    return (vec2(float(layer) * tile + 1.0, 1.0) + fract(vUV) * content) / size;
}
vec3 mappedNormal(vec3 n, vec3 tangentNormal) {
    vec3  dp1 = dFdx(vWorldPos), dp2 = dFdy(vWorldPos);
    vec2  duv1 = dFdx(vUV), duv2 = dFdy(vUV);
    float det = duv1.x * duv2.y - duv1.y * duv2.x;
    if (abs(det) < 1e-8) return n;
    vec3 t = (dp1 * duv2.y - dp2 * duv1.y) / det;
    vec3 b = (dp2 * duv1.x - dp1 * duv2.x) / det;
    t -= n * dot(n, t);
    if (dot(t, t) < 1e-10) return n;
    t = normalize(t);
    b -= n * dot(n, b) + t * dot(t, b);
    if (dot(b, b) < 1e-10) return n;
    return normalize(mat3(t, normalize(b), n) * tangentNormal);
}
vec3 lightSurface(vec3 base, vec3 n, vec3 v, vec3 l, vec3 radiance, float roughness) {
    vec3 halfVector = v + l;
    if (dot(halfVector, halfVector) < 1e-8) return vec3(0.0);
    vec3  h  = normalize(halfVector);
    float nl = max(dot(n, l), 0.0), nv = max(dot(n, v), 0.001), nh = max(dot(n, h), 0.0);
    float a = roughness * roughness, a2 = a * a;
    float denominator  = nh * nh * (a2 - 1.0) + 1.0;
    float distribution = a2 / max(PI * denominator * denominator, 1e-6);
    float k            = (roughness + 1.0) * (roughness + 1.0) / 8.0;
    float geometry     = (nl / (nl * (1.0 - k) + k)) * (nv / (nv * (1.0 - k) + k));
    vec3  fresnel      = vec3(0.04) + vec3(0.96) * pow(1.0 - max(dot(h, v), 0.0), 5.0);
    vec3  specular     = distribution * geometry * fresnel / max(4.0 * nl * nv, 0.001);
    return ((1.0 - fresnel) * base / PI + specular) * nl * radiance;
}
void main() {
    float height  = clamp((vLocalPos.y - pc.data[0]) / max(pc.data[1], 1e-5), 0.0, 1.0);
    float width   = max(pc.data[3], 0.001);
    vec3  p       = vLocalPos * max(pc.data[6], 0.01) + vec3(pc.data[7] * 0.013);
    float breakup = 0.7 * noise(p) + 0.3 * noise(p * 2.0 + 7.0);
    float glaze   = smoothstep(pc.data[2] - width, pc.data[2] + width, height + (breakup - 0.5) * pc.data[5]);
    float rim     = smoothstep(pc.data[4] - width * 0.3, pc.data[4] + width * 0.3,
                               height + (noise(p * 1.7) - 0.5) * pc.data[5] * 0.25) *
                clamp(pc.data[10], 0.0, 1.0);
    if (pc.data[9] > 1.5) {
        outColor = vec4(vec3(height), 1.0);
        return;
    }
    if (pc.data[9] > 0.5) {
        outColor = vec4(mix(mix(vec3(0.65, 0.25, 0.08), vec3(0.05, 0.65, 0.9), glaze), vec3(0.7, 0.1, 0.65), rim), 1.0);
        return;
    }
    vec2 baseUv = atlasUv(0), glazeUv = atlasUv(1);
    vec3 base = mix(texture(colorAtlas, baseUv).rgb, texture(colorAtlas, glazeUv).rgb, glaze);
    base *= mix(1.0, 1.0 - clamp(pc.data[8], 0.0, 0.8), rim) * vTint.rgb;
    vec3  normalA       = texture(normalAtlas, baseUv).xyz * 2.0 - 1.0;
    vec3  normalB       = texture(normalAtlas, glazeUv).xyz * 2.0 - 1.0;
    vec3  tangentNormal = normalize(mix(normalA, normalB, glaze));
    float roughness     = mix(texture(roughnessAtlas, baseUv).r, texture(roughnessAtlas, glazeUv).r, glaze);
    roughness           = clamp(mix(roughness, roughness * 0.7, rim), 0.07, 1.0);
    vec3 n              = mappedNormal(normalize(vNormal), tangentNormal);
    vec3 v              = normalize(vCameraPos - vWorldPos);
    vec3 color          = base * ubo.ambient.rgb;
    int  count          = clamp(int(ubo.lightDirIntensity.w + 0.5), 0, 8);
    if (count == 0)
        color += lightSurface(base, n, v, normalize(ubo.lightDirIntensity.xyz), ubo.lightColor.rgb, roughness);
    for (int i = 0; i < count; ++i) {
        vec3  delta       = ubo.lights[i].posRadius.xyz - vWorldPos;
        float radius      = ubo.lights[i].posRadius.w;
        vec3  l           = normalize(radius > 0.0 ? delta : ubo.lights[i].posRadius.xyz);
        float attenuation = radius > 0.0 ? pow(clamp(1.0 - length(delta) / radius, 0.0, 1.0), 2.0) : 1.0;
        color += lightSurface(base, n, v, l, ubo.lights[i].color.rgb * attenuation, roughness);
    }
    // Match the engine material contract: output linear HDR; the shared
    // post-processing/display path performs exposure and output encoding.
    outColor = vec4(max(color, vec3(0.0)), vTint.a);
}
