#ifndef EVE_PT_SAMPLING
#define EVE_PT_SAMPLING

const float PT_PI = 3.14159265358979323846;
const uint PT_INVALID = 0xffffffffu;

// Integer arithmetic makes a stream reproducible independently of frame timing.
uint ptHash(uint x) {
    x ^= x >> 16u;
    x *= 0x7feb352du;
    x ^= x >> 15u;
    x *= 0x846ca68bu;
    return x ^ (x >> 16u);
}

float ptRandom(inout uint state) {
    state = state * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return float((word ^ (word >> 22u)) >> 8u) * (1.0 / 16777216.0);
}

vec2 ptRandom2(inout uint state) {
    float x = ptRandom(state);
    return vec2(x, ptRandom(state));
}

mat3 ptFrame(vec3 n) {
    vec3 t = normalize(cross(abs(n.z) < 0.999 ? vec3(0, 0, 1) : vec3(0, 1, 0), n));
    return mat3(t, cross(n, t), n);
}

vec3 ptCosineHemisphere(vec2 u) {
    float r = sqrt(u.x);
    float phi = 2.0 * PT_PI * u.y;
    return vec3(r * cos(phi), r * sin(phi), sqrt(max(0.0, 1.0 - u.x)));
}

vec3 ptUniformSphere(vec2 u) {
    float z = 1.0 - 2.0 * u.x;
    float r = sqrt(max(0.0, 1.0 - z * z));
    float phi = 2.0 * PT_PI * u.y;
    return vec3(r * cos(phi), r * sin(phi), z);
}

float ptPowerHeuristic(float a, float b) {
    // Scaling before squaring avoids overflow for small bright area lights.
    float scale = max(a, b);
    if (scale <= 0.0) return 0.0;
    a /= scale;
    b /= scale;
    return a * a / (a * a + b * b);
}

vec3 ptFresnel(vec3 f0, float cosine) {
    return f0 + (1.0 - f0) * pow(clamp(1.0 - cosine, 0.0, 1.0), 5.0);
}

float ptGgxD(float nDotH, float alpha) {
    float a2 = alpha * alpha;
    float d = nDotH * nDotH * (a2 - 1.0) + 1.0;
    return a2 / (PT_PI * d * d);
}

float ptGgxG1(float nDotV, float alpha) {
    float a2 = alpha * alpha;
    return 2.0 * nDotV / (nDotV + sqrt(a2 + (1.0 - a2) * nDotV * nDotV));
}

float ptSpecularProbability(vec3 baseColor, float metallic) {
    vec3 f0 = mix(vec3(0.04), baseColor, metallic);
    float specular = dot(f0, vec3(0.2126, 0.7152, 0.0722));
    float diffuse = (1.0 - metallic) * dot(baseColor, vec3(0.2126, 0.7152, 0.0722));
    return clamp(specular / max(specular + diffuse, 1e-8), 0.1, 0.9);
}

// Returns f * abs(n.wi); pdf includes both lobes even when sampling just one.
vec3 ptEvaluateBsdf(vec3 n, vec3 wo, vec3 wi, vec3 baseColor,
                    float metallic, float roughness, out float pdf) {
    pdf = 0.0;
    float nv = dot(n, wo);
    float nl = dot(n, wi);
    if (nv <= 0.0 || nl <= 0.0) return vec3(0.0);
    vec3 h = normalize(wo + wi);
    float nh = max(dot(n, h), 0.0);
    float vh = max(dot(wo, h), 1e-7);
    float alpha = max(roughness * roughness, 0.001);
    float d = ptGgxD(nh, alpha);
    vec3 f = ptFresnel(mix(vec3(0.04), baseColor, metallic), vh);
    vec3 specular = f * (d * ptGgxG1(nv, alpha) * ptGgxG1(nl, alpha) / (4.0 * nv));
    vec3 diffuse = (1.0 - f) * (1.0 - metallic) * baseColor * (nl / PT_PI);
    float p = ptSpecularProbability(baseColor, metallic);
    pdf = mix(nl / PT_PI, d * nh / (4.0 * vh), p);
    return diffuse + specular;
}

vec3 ptSampleBsdf(vec3 n, vec3 wo, vec3 baseColor, float metallic,
                  float roughness, inout uint rng) {
    float choose = ptRandom(rng);
    vec2 u = ptRandom2(rng);
    mat3 frame = ptFrame(n);
    if (choose >= ptSpecularProbability(baseColor, metallic))
        return frame * ptCosineHemisphere(u);
    float alpha = max(roughness * roughness, 0.001);
    float phi = 2.0 * PT_PI * u.x;
    float cosTheta = sqrt((1.0 - u.y) / (1.0 + (alpha * alpha - 1.0) * u.y));
    float sinTheta = sqrt(max(0.0, 1.0 - cosTheta * cosTheta));
    vec3 h = frame * vec3(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta);
    return reflect(-wo, h);
}

// Scale with coordinate magnitude; all visibility segments use the same offset.
vec3 ptOffset(vec3 p, vec3 geometricNormal, vec3 direction) {
    float magnitude = max(max(abs(p.x), abs(p.y)), abs(p.z));
    float epsilon = max(1e-5, magnitude * 2e-6);
    return p + geometricNormal * (dot(direction, geometricNormal) >= 0.0 ? epsilon : -epsilon);
}

#endif
