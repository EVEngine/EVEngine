#include "math/Math.h"
#include "math/Vec2.h"
#include "math/Vec3.h"
#include "math/Mat4.h"

#include "common/Exception.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/noise.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <string>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace eve::math {
namespace {

float clamp01(float t) { return std::clamp(t, 0.f, 1.f); }

float noiseToUnit(float n) {
    // glm::simplex / perlin is roughly [-1, 1]; map to [0, 1] like LÖVE.
    return clamp01(n * 0.5f + 0.5f);
}

float hash11(float p) {
    p = p - std::floor(p);
    p *= 0.1031f;
    p = p - std::floor(p);
    p *= p + 33.33f;
    p *= p + p;
    return p - std::floor(p);
}

float hash21(float x, float y) {
    glm::vec3 p3 = glm::fract(glm::vec3(x, y, x) * 0.1031f);
    p3 += glm::dot(p3, glm::vec3(p3.y + 33.33f, p3.z + 33.33f, p3.x + 33.33f));
    return glm::fract((p3.x + p3.y) * p3.z);
}

float hash31(float x, float y, float z) {
    glm::vec3 p3 = glm::fract(glm::vec3(x, y, z) * 0.1031f);
    p3 += glm::dot(p3, glm::vec3(p3.y + 33.33f, p3.z + 33.33f, p3.x + 33.33f));
    return glm::fract((p3.x + p3.y) * p3.z);
}

glm::vec2 voronoiPoint(int ix, int iy) {
    float hx = hash21(float(ix), float(iy));
    float hy = hash21(float(ix) + 19.19f, float(iy) + 47.47f);
    return glm::vec2(float(ix) + hx, float(iy) + hy);
}

void voronoiF1F2(float x, float y, float &f1, float &f2) {
    int ix = int(std::floor(x));
    int iy = int(std::floor(y));
    f1 = 1e9f;
    f2 = 1e9f;
    for (int j = -1; j <= 1; ++j) {
        for (int i = -1; i <= 1; ++i) {
            glm::vec2 p = voronoiPoint(ix + i, iy + j);
            float d     = glm::length(glm::vec2(x, y) - p);
            if (d < f1) {
                f2 = f1;
                f1 = d;
            } else if (d < f2) {
                f2 = d;
            }
        }
    }
}

}  // namespace

Module_IMPL(Math, new Math());

Math::Math() : seed_(1), rng_(1) {}

Vec2 *Math::newVec2(float x, float y) { return new Vec2(x, y); }
Vec3 *Math::newVec3(float x, float y, float z) { return new Vec3(x, y, z); }

Mat4 *Math::newMat4() { return new Mat4(); }

Mat4 *Math::newMat4Translation(float x, float y, float z) {
    auto *m = new Mat4();
    m->translate(x, y, z);
    return m;
}

Mat4 *Math::newMat4Scale(float sx, float sy, float sz) {
    auto *m = new Mat4();
    m->scale(sx, sy, sz);
    return m;
}

Mat4 *Math::newMat4RotationZ(float radians) {
    auto *m = new Mat4();
    m->rotateZ(radians);
    return m;
}

float Math::clamp(float x, float lo, float hi) const {
    if (lo > hi) std::swap(lo, hi);
    return std::clamp(x, lo, hi);
}

float Math::lerp(float a, float b, float t) const { return a + (b - a) * t; }

float Math::smoothstep(float edge0, float edge1, float x) const {
    if (edge0 == edge1) return x < edge0 ? 0.f : 1.f;
    float t = clamp01((x - edge0) / (edge1 - edge0));
    return t * t * (3.f - 2.f * t);
}

float Math::remap(float x, float inMin, float inMax, float outMin, float outMax) const {
    if (inMin == inMax) return outMin;
    float t = (x - inMin) / (inMax - inMin);
    return outMin + (outMax - outMin) * t;
}

float Math::degToRad(float deg) const { return deg * float(M_PI) / 180.f; }
float Math::radToDeg(float rad) const { return rad * 180.f / float(M_PI); }

float Math::sign(float x) const {
    if (x > 0.f) return 1.f;
    if (x < 0.f) return -1.f;
    return 0.f;
}

float Math::fract(float x) const { return x - std::floor(x); }

float Math::approach(float current, float target, float maxDelta) const {
    float d = target - current;
    if (std::fabs(d) <= maxDelta) return target;
    return current + sign(d) * maxDelta;
}

float Math::wrap(float x, float lo, float hi) const {
    if (lo == hi) return lo;
    if (lo > hi) std::swap(lo, hi);
    float range = hi - lo;
    float t     = std::fmod(x - lo, range);
    if (t < 0.f) t += range;
    return lo + t;
}

float Math::pingPong(float t, float length) const {
    if (length <= 0.f) return 0.f;
    t = wrap(t, 0.f, length * 2.f);
    return length - std::fabs(t - length);
}

float Math::inverseLerp(float a, float b, float x) const {
    if (a == b) return 0.f;
    return (x - a) / (b - a);
}

float Math::smootherstep(float edge0, float edge1, float x) const {
    if (edge0 == edge1) return x < edge0 ? 0.f : 1.f;
    float t = clamp01((x - edge0) / (edge1 - edge0));
    return t * t * t * (t * (t * 6.f - 15.f) + 10.f);
}

float Math::bias(float t, float b) const {
    t = clamp01(t);
    if (b <= 0.f) return 0.f;
    if (b >= 1.f) return 1.f;
    return t / ((1.f / b - 2.f) * (1.f - t) + 1.f);
}

float Math::gain(float t, float g) const {
    t = clamp01(t);
    if (t < 0.5f) return bias(t * 2.f, g) * 0.5f;
    return bias(t * 2.f - 1.f, 1.f - g) * 0.5f + 0.5f;
}

float Math::ease(float t, const std::string &kind) const {
    t = clamp01(t);
    if (kind == "linear" || kind.empty()) return t;
    if (kind == "inQuad") return t * t;
    if (kind == "outQuad") return 1.f - (1.f - t) * (1.f - t);
    if (kind == "inOutQuad")
        return t < 0.5f ? 2.f * t * t : 1.f - std::pow(-2.f * t + 2.f, 2.f) * 0.5f;
    if (kind == "inCubic") return t * t * t;
    if (kind == "outCubic") return 1.f - std::pow(1.f - t, 3.f);
    if (kind == "inOutCubic")
        return t < 0.5f ? 4.f * t * t * t : 1.f - std::pow(-2.f * t + 2.f, 3.f) * 0.5f;
    if (kind == "inSine") return 1.f - std::cos(t * float(M_PI) * 0.5f);
    if (kind == "outSine") return std::sin(t * float(M_PI) * 0.5f);
    if (kind == "inOutSine") return -(std::cos(float(M_PI) * t) - 1.f) * 0.5f;
    if (kind == "inExpo") return t <= 0.f ? 0.f : std::pow(2.f, 10.f * t - 10.f);
    if (kind == "outExpo") return t >= 1.f ? 1.f : 1.f - std::pow(2.f, -10.f * t);
    if (kind == "inOutExpo") {
        if (t <= 0.f) return 0.f;
        if (t >= 1.f) return 1.f;
        return t < 0.5f ? std::pow(2.f, 20.f * t - 10.f) * 0.5f
                        : (2.f - std::pow(2.f, -20.f * t + 10.f)) * 0.5f;
    }
    throw Exception("Math.ease: unknown kind '%s'", kind.c_str());
}

float Math::step(float edge, float x) const { return x < edge ? 0.f : 1.f; }

float Math::quantize(float x, float stepSize) const {
    if (stepSize == 0.f) return x;
    return std::floor(x / stepSize) * stepSize;
}

float Math::snap(float x, float grid) const {
    if (grid == 0.f) return x;
    return std::round(x / grid) * grid;
}

float Math::length2(float x, float y) const { return std::sqrt(x * x + y * y); }
float Math::length3(float x, float y, float z) const {
    return std::sqrt(x * x + y * y + z * z);
}

float Math::distance2(float x1, float y1, float x2, float y2) const {
    return length2(x2 - x1, y2 - y1);
}

float Math::distance3(float x1, float y1, float z1, float x2, float y2, float z2) const {
    return length3(x2 - x1, y2 - y1, z2 - z1);
}

float Math::dot2(float x1, float y1, float x2, float y2) const { return x1 * x2 + y1 * y2; }
float Math::dot3(float x1, float y1, float z1, float x2, float y2, float z2) const {
    return x1 * x2 + y1 * y2 + z1 * z2;
}

float Math::cross2(float x1, float y1, float x2, float y2) const { return x1 * y2 - y1 * x2; }
float Math::angle2(float x, float y) const { return std::atan2(y, x); }

float Math::angleBetween2(float x1, float y1, float x2, float y2) const {
    return std::atan2(y2, x2) - std::atan2(y1, x1);
}

float Math::lerpAngle(float a, float b, float t) const {
    float twoPi = float(M_PI) * 2.f;
    float diff  = std::fmod(b - a + float(M_PI), twoPi);
    if (diff < 0.f) diff += twoPi;
    diff -= float(M_PI);
    return a + diff * t;
}

float Math::normalize2X(float x, float y) const {
    float len = length2(x, y);
    return len > 0.f ? x / len : 0.f;
}
float Math::normalize2Y(float x, float y) const {
    float len = length2(x, y);
    return len > 0.f ? y / len : 0.f;
}
float Math::normalize3X(float x, float y, float z) const {
    float len = length3(x, y, z);
    return len > 0.f ? x / len : 0.f;
}
float Math::normalize3Y(float x, float y, float z) const {
    float len = length3(x, y, z);
    return len > 0.f ? y / len : 0.f;
}
float Math::normalize3Z(float x, float y, float z) const {
    float len = length3(x, y, z);
    return len > 0.f ? z / len : 0.f;
}

float Math::rotate2X(float x, float y, float radians) const {
    float c = std::cos(radians), s = std::sin(radians);
    return x * c - y * s;
}
float Math::rotate2Y(float x, float y, float radians) const {
    float c = std::cos(radians), s = std::sin(radians);
    return x * s + y * c;
}

float Math::polarX(float radius, float radians) const { return radius * std::cos(radians); }
float Math::polarY(float radius, float radians) const { return radius * std::sin(radians); }
float Math::cartesianRadius(float x, float y) const { return length2(x, y); }
float Math::cartesianAngle(float x, float y) const { return angle2(x, y); }

bool Math::pointInCircle(float px, float py, float cx, float cy, float radius) const {
    return distance2(px, py, cx, cy) <= radius;
}

bool Math::pointInRect(float px, float py, float rx, float ry, float rw, float rh) const {
    return px >= rx && py >= ry && px <= rx + rw && py <= ry + rh;
}

float Math::bilinear(float v00, float v10, float v01, float v11, float u, float v) const {
    float a = lerp(v00, v10, u);
    float b = lerp(v01, v11, u);
    return lerp(a, b, v);
}

void Math::setRandomSeed(uint32_t seed) {
    seed_ = seed == 0 ? 1u : seed;
    rng_.seed(seed_);
}

void Math::setRandomSeedFromTime() {
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(
                  std::chrono::steady_clock::now().time_since_epoch())
                  .count();
    setRandomSeed(static_cast<uint32_t>(us) ^ 0xA5A5A5A5u);
}

uint32_t Math::getRandomSeed() const { return seed_; }

float Math::random() {
    std::uniform_real_distribution<float> dist(0.f, 1.f);
    return dist(rng_);
}

float Math::randomRange(float min, float max) {
    if (min > max) std::swap(min, max);
    std::uniform_real_distribution<float> dist(min, max);
    return dist(rng_);
}

int Math::randomInt(int min, int maxInclusive) {
    if (min > maxInclusive) std::swap(min, maxInclusive);
    std::uniform_int_distribution<int> dist(min, maxInclusive);
    return dist(rng_);
}

float Math::randomGaussian(float mean, float stddev) {
    std::normal_distribution<float> dist(mean, stddev);
    return dist(rng_);
}

float Math::hash1(float x) const { return hash11(x); }
float Math::hash2(float x, float y) const { return hash21(x, y); }
float Math::hash3(float x, float y, float z) const { return hash31(x, y, z); }

float Math::noise1(float x) const { return noiseToUnit(glm::simplex(glm::vec2(x, 0.f))); }
float Math::noise2(float x, float y) const { return noiseToUnit(glm::simplex(glm::vec2(x, y))); }
float Math::noise3(float x, float y, float z) const {
    return noiseToUnit(glm::simplex(glm::vec3(x, y, z)));
}

float Math::perlin2(float x, float y) const { return noiseToUnit(glm::perlin(glm::vec2(x, y))); }
float Math::perlin3(float x, float y, float z) const {
    return noiseToUnit(glm::perlin(glm::vec3(x, y, z)));
}

float Math::fbm2(float x, float y, int octaves, float lacunarity, float gain) const {
    if (octaves < 1) octaves = 1;
    if (octaves > 16) octaves = 16;
    float sum = 0.f, amp = 1.f, freq = 1.f, norm = 0.f;
    for (int i = 0; i < octaves; ++i) {
        sum += noise2(x * freq, y * freq) * amp;
        norm += amp;
        amp *= gain;
        freq *= lacunarity;
    }
    return norm > 0.f ? sum / norm : 0.f;
}

float Math::fbm3(float x, float y, float z, int octaves, float lacunarity, float gain) const {
    if (octaves < 1) octaves = 1;
    if (octaves > 16) octaves = 16;
    float sum = 0.f, amp = 1.f, freq = 1.f, norm = 0.f;
    for (int i = 0; i < octaves; ++i) {
        sum += noise3(x * freq, y * freq, z * freq) * amp;
        norm += amp;
        amp *= gain;
        freq *= lacunarity;
    }
    return norm > 0.f ? sum / norm : 0.f;
}

float Math::ridged2(float x, float y, int octaves, float lacunarity, float gain) const {
    if (octaves < 1) octaves = 1;
    if (octaves > 16) octaves = 16;
    float sum = 0.f, amp = 0.5f, freq = 1.f, prev = 1.f;
    for (int i = 0; i < octaves; ++i) {
        float n = noise2(x * freq, y * freq);
        n       = 1.f - std::fabs(n * 2.f - 1.f);
        n       = n * n;
        sum += n * amp * prev;
        prev = n;
        freq *= lacunarity;
        amp *= gain;
    }
    return clamp01(sum);
}

float Math::ridged3(float x, float y, float z, int octaves, float lacunarity, float gain) const {
    if (octaves < 1) octaves = 1;
    if (octaves > 16) octaves = 16;
    float sum = 0.f, amp = 0.5f, freq = 1.f, prev = 1.f;
    for (int i = 0; i < octaves; ++i) {
        float n = noise3(x * freq, y * freq, z * freq);
        n       = 1.f - std::fabs(n * 2.f - 1.f);
        n       = n * n;
        sum += n * amp * prev;
        prev = n;
        freq *= lacunarity;
        amp *= gain;
    }
    return clamp01(sum);
}

float Math::turbulence2(float x, float y, int octaves, float lacunarity, float gain) const {
    if (octaves < 1) octaves = 1;
    if (octaves > 16) octaves = 16;
    float sum = 0.f, amp = 1.f, freq = 1.f, norm = 0.f;
    for (int i = 0; i < octaves; ++i) {
        sum += std::fabs(noise2(x * freq, y * freq) * 2.f - 1.f) * amp;
        norm += amp;
        amp *= gain;
        freq *= lacunarity;
    }
    return norm > 0.f ? clamp01(sum / norm) : 0.f;
}

float Math::voronoi2(float x, float y) const {
    float f1, f2;
    voronoiF1F2(x, y, f1, f2);
    return f1;
}

float Math::voronoiEdge2(float x, float y) const {
    float f1, f2;
    voronoiF1F2(x, y, f1, f2);
    return f2 - f1;
}

float Math::warpNoise2(float x, float y, float warpAmp) const {
    float wx = noise2(x, y) - 0.5f;
    float wy = noise2(x + 5.2f, y + 1.3f) - 0.5f;
    return noise2(x + wx * warpAmp, y + wy * warpAmp);
}

float Math::bezierQuadratic(float t, float p0, float p1, float p2) const {
    t       = clamp01(t);
    float u = 1.f - t;
    return u * u * p0 + 2.f * u * t * p1 + t * t * p2;
}

float Math::bezierCubic(float t, float p0, float p1, float p2, float p3) const {
    t        = clamp01(t);
    float u  = 1.f - t;
    float uu = u * u;
    float tt = t * t;
    return uu * u * p0 + 3.f * uu * t * p1 + 3.f * u * tt * p2 + tt * t * p3;
}

float Math::bezierQuadratic2X(float t, float x0, float /*y0*/, float x1, float /*y1*/, float x2,
                              float /*y2*/) const {
    return bezierQuadratic(t, x0, x1, x2);
}

float Math::bezierQuadratic2Y(float t, float /*x0*/, float y0, float /*x1*/, float y1, float /*x2*/,
                              float y2) const {
    return bezierQuadratic(t, y0, y1, y2);
}

float Math::bezierCubic2X(float t, float x0, float /*y0*/, float x1, float /*y1*/, float x2,
                          float /*y2*/, float x3, float /*y3*/) const {
    return bezierCubic(t, x0, x1, x2, x3);
}

float Math::bezierCubic2Y(float t, float /*x0*/, float y0, float /*x1*/, float y1, float /*x2*/,
                          float y2, float /*x3*/, float y3) const {
    return bezierCubic(t, y0, y1, y2, y3);
}

void Math::expose(ssq::Table &table) {
    auto cls = table.addClass(name, Math::create, false);
    expose(cls);

    auto v2 = table.addClass<Vec2>(
        "Vec2", std::function<Vec2 *()>([]() -> Vec2 * { return nullptr; }), true);
    v2.addFunc("getX", &Vec2::getX);
    v2.addFunc("getY", &Vec2::getY);
    v2.addFunc("setX", &Vec2::setX);
    v2.addFunc("setY", &Vec2::setY);
    v2.addFunc("set", &Vec2::set);
    v2.addFunc("length", &Vec2::length);
    v2.addFunc("lengthSquared", &Vec2::lengthSquared);
    v2.addFunc("normalize", &Vec2::normalize);
    v2.addFunc("normalized", &Vec2::normalized);
    v2.addFunc("dot", &Vec2::dot);
    v2.addFunc("cross", &Vec2::cross);
    v2.addFunc("distanceTo", &Vec2::distanceTo);
    v2.addFunc("angle", &Vec2::angle);
    v2.addFunc("add", &Vec2::add);
    v2.addFunc("sub", &Vec2::sub);
    v2.addFunc("scale", &Vec2::scale);
    v2.addFunc("lerpTo", &Vec2::lerpTo);
    v2.addFunc("clone", &Vec2::clone);

    auto v3 = table.addClass<Vec3>(
        "Vec3", std::function<Vec3 *()>([]() -> Vec3 * { return nullptr; }), true);
    v3.addFunc("getX", &Vec3::getX);
    v3.addFunc("getY", &Vec3::getY);
    v3.addFunc("getZ", &Vec3::getZ);
    v3.addFunc("setX", &Vec3::setX);
    v3.addFunc("setY", &Vec3::setY);
    v3.addFunc("setZ", &Vec3::setZ);
    v3.addFunc("set", &Vec3::set);
    v3.addFunc("length", &Vec3::length);
    v3.addFunc("lengthSquared", &Vec3::lengthSquared);
    v3.addFunc("normalize", &Vec3::normalize);
    v3.addFunc("normalized", &Vec3::normalized);
    v3.addFunc("dot", &Vec3::dot);
    v3.addFunc("cross", &Vec3::cross);
    v3.addFunc("distanceTo", &Vec3::distanceTo);
    v3.addFunc("add", &Vec3::add);
    v3.addFunc("sub", &Vec3::sub);
    v3.addFunc("scale", &Vec3::scale);
    v3.addFunc("lerpTo", &Vec3::lerpTo);
    v3.addFunc("clone", &Vec3::clone);

    auto m4 = table.addClass<Mat4>(
        "Mat4", std::function<Mat4 *()>([]() -> Mat4 * { return nullptr; }), true);
    m4.addFunc("identity", &Mat4::identity);
    m4.addFunc("translate", &Mat4::translate);
    m4.addFunc("rotateX", &Mat4::rotateX);
    m4.addFunc("rotateY", &Mat4::rotateY);
    m4.addFunc("rotateZ", &Mat4::rotateZ);
    m4.addFunc("scale", &Mat4::scale);
    m4.addFunc("multiply", &Mat4::multiply);
    m4.addFunc("multiplied", &Mat4::multiplied);
    m4.addFunc("transformVec3", &Mat4::transformVec3);
    m4.addFunc("transformPoint2", &Mat4::transformPoint2);
    m4.addFunc("get", &Mat4::get);
    m4.addFunc("set", &Mat4::set);
    m4.addFunc("clone", &Mat4::clone);
}

void Math::expose(ssq::Class &cls) {
    cls.addFunc("getName", &Math::getName);
    cls.addFunc("newVec2", &Math::newVec2);
    cls.addFunc("newVec3", &Math::newVec3);
    cls.addFunc("newMat4", &Math::newMat4);
    cls.addFunc("newMat4Translation", &Math::newMat4Translation);
    cls.addFunc("newMat4Scale", &Math::newMat4Scale);
    cls.addFunc("newMat4RotationZ", &Math::newMat4RotationZ);

    cls.addFunc("clamp", &Math::clamp);
    cls.addFunc("lerp", &Math::lerp);
    cls.addFunc("smoothstep", &Math::smoothstep);
    cls.addFunc("remap", &Math::remap);
    cls.addFunc("degToRad", &Math::degToRad);
    cls.addFunc("radToDeg", &Math::radToDeg);
    cls.addFunc("sign", &Math::sign);
    cls.addFunc("fract", &Math::fract);
    cls.addFunc("approach", &Math::approach);
    cls.addFunc("wrap", &Math::wrap);
    cls.addFunc("pingPong", &Math::pingPong);
    cls.addFunc("inverseLerp", &Math::inverseLerp);
    cls.addFunc("smootherstep", &Math::smootherstep);
    cls.addFunc("bias", &Math::bias);
    cls.addFunc("gain", &Math::gain);
    cls.addFunc("ease", &Math::ease);
    cls.addFunc("step", &Math::step);
    cls.addFunc("quantize", &Math::quantize);
    cls.addFunc("snap", &Math::snap);

    cls.addFunc("length2", &Math::length2);
    cls.addFunc("length3", &Math::length3);
    cls.addFunc("distance2", &Math::distance2);
    cls.addFunc("distance3", &Math::distance3);
    cls.addFunc("dot2", &Math::dot2);
    cls.addFunc("dot3", &Math::dot3);
    cls.addFunc("cross2", &Math::cross2);
    cls.addFunc("angle2", &Math::angle2);
    cls.addFunc("angleBetween2", &Math::angleBetween2);
    cls.addFunc("lerpAngle", &Math::lerpAngle);
    cls.addFunc("normalize2X", &Math::normalize2X);
    cls.addFunc("normalize2Y", &Math::normalize2Y);
    cls.addFunc("normalize3X", &Math::normalize3X);
    cls.addFunc("normalize3Y", &Math::normalize3Y);
    cls.addFunc("normalize3Z", &Math::normalize3Z);
    cls.addFunc("rotate2X", &Math::rotate2X);
    cls.addFunc("rotate2Y", &Math::rotate2Y);
    cls.addFunc("polarX", &Math::polarX);
    cls.addFunc("polarY", &Math::polarY);
    cls.addFunc("cartesianRadius", &Math::cartesianRadius);
    cls.addFunc("cartesianAngle", &Math::cartesianAngle);
    cls.addFunc("pointInCircle", &Math::pointInCircle);
    cls.addFunc("pointInRect", &Math::pointInRect);
    cls.addFunc("bilinear", &Math::bilinear);

    cls.addFunc("setRandomSeed", &Math::setRandomSeed);
    cls.addFunc("setRandomSeedFromTime", &Math::setRandomSeedFromTime);
    cls.addFunc("getRandomSeed", &Math::getRandomSeed);
    cls.addFunc("random", &Math::random);
    cls.addFunc("randomRange", &Math::randomRange);
    cls.addFunc("randomInt", &Math::randomInt);
    cls.addFunc("randomGaussian", &Math::randomGaussian);

    cls.addFunc("hash1", &Math::hash1);
    cls.addFunc("hash2", &Math::hash2);
    cls.addFunc("hash3", &Math::hash3);

    cls.addFunc("noise1", &Math::noise1);
    cls.addFunc("noise2", &Math::noise2);
    cls.addFunc("noise3", &Math::noise3);
    cls.addFunc("perlin2", &Math::perlin2);
    cls.addFunc("perlin3", &Math::perlin3);
    cls.addFunc("fbm2", &Math::fbm2);
    cls.addFunc("fbm3", &Math::fbm3);
    cls.addFunc("ridged2", &Math::ridged2);
    cls.addFunc("ridged3", &Math::ridged3);
    cls.addFunc("turbulence2", &Math::turbulence2);
    cls.addFunc("voronoi2", &Math::voronoi2);
    cls.addFunc("voronoiEdge2", &Math::voronoiEdge2);
    cls.addFunc("warpNoise2", &Math::warpNoise2);

    cls.addFunc("bezierQuadratic", &Math::bezierQuadratic);
    cls.addFunc("bezierCubic", &Math::bezierCubic);
    cls.addFunc("bezierQuadratic2X", &Math::bezierQuadratic2X);
    cls.addFunc("bezierQuadratic2Y", &Math::bezierQuadratic2Y);
    cls.addFunc("bezierCubic2X", &Math::bezierCubic2X);
    cls.addFunc("bezierCubic2Y", &Math::bezierCubic2Y);
}

}  // namespace eve::math
