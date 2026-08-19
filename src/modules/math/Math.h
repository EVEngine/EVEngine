#pragma once

#include "common/Module.h"

#include <cstdint>
#include <random>
#include <string>

namespace eve::math {

class Vec2;
class Vec3;
class Mat4;

/**
 * Math module — glm-backed vectors/matrices, noise, bezier, random.
 * Script: `math <- eve.Math();`
 *
 * No overloads: distinct names (length2/length3, noise1/noise2/…).
 */
class Math : public Module {
public:
    Module_REG(Math);
    Math();
    ~Math() override = default;

    Vec2 *newVec2(float x = 0.f, float y = 0.f);
    Vec3 *newVec3(float x = 0.f, float y = 0.f, float z = 0.f);
    Mat4 *newMat4();
    Mat4 *newMat4Translation(float x, float y, float z);
    Mat4 *newMat4Scale(float sx, float sy, float sz);
    Mat4 *newMat4RotationZ(float radians);

    // --- scalar ---
    float clamp(float x, float lo, float hi) const;
    float lerp(float a, float b, float t) const;
    float smoothstep(float edge0, float edge1, float x) const;
    float remap(float x, float inMin, float inMax, float outMin, float outMax) const;
    float degToRad(float deg) const;
    float radToDeg(float rad) const;
    float sign(float x) const;
    float fract(float x) const;
    float approach(float current, float target, float maxDelta) const;
    float wrap(float x, float lo, float hi) const;
    float pingPong(float t, float length) const;

    /** Inverse of lerp: t such that lerp(a,b,t) ≈ x. */
    float inverseLerp(float a, float b, float x) const;
    float smootherstep(float edge0, float edge1, float x) const;
    /** Schlick bias/gain — shape [0,1] distributions (procgen falloff). */
    float bias(float t, float b) const;
    float gain(float t, float g) const;
    /**
     * Easing on [0,1]. kind:
     * "linear"|"inQuad"|"outQuad"|"inOutQuad"|"inCubic"|"outCubic"|"inOutCubic"|
     * "inSine"|"outSine"|"inOutSine"|"inExpo"|"outExpo"|"inOutExpo"
     */
    float ease(float t, const std::string &kind) const;
    float step(float edge, float x) const;
    float quantize(float x, float stepSize) const;
    float snap(float x, float grid) const;

    // --- geometry (float components) ---
    float length2(float x, float y) const;
    float length3(float x, float y, float z) const;
    float distance2(float x1, float y1, float x2, float y2) const;
    float distance3(float x1, float y1, float z1, float x2, float y2, float z2) const;
    float dot2(float x1, float y1, float x2, float y2) const;
    float dot3(float x1, float y1, float z1, float x2, float y2, float z2) const;
    float cross2(float x1, float y1, float x2, float y2) const;
    float angle2(float x, float y) const;
    float angleBetween2(float x1, float y1, float x2, float y2) const;
    float lerpAngle(float a, float b, float t) const;
    float normalize2X(float x, float y) const;
    float normalize2Y(float x, float y) const;
    float normalize3X(float x, float y, float z) const;
    float normalize3Y(float x, float y, float z) const;
    float normalize3Z(float x, float y, float z) const;

    /** Rotate (x,y) by radians around origin. */
    float rotate2X(float x, float y, float radians) const;
    float rotate2Y(float x, float y, float radians) const;
    float polarX(float radius, float radians) const;
    float polarY(float radius, float radians) const;
    float cartesianRadius(float x, float y) const;
    float cartesianAngle(float x, float y) const;

    // --- 2D hit / overlap / ray (picking & collision detection) ---
    bool pointInCircle(float px, float py, float cx, float cy, float radius) const;
    bool pointInRect(float px, float py, float rx, float ry, float rw, float rh) const;
    bool circlesOverlap(float x1, float y1, float r1, float x2, float y2, float r2) const;
    bool rectsOverlap(float x1, float y1, float w1, float h1, float x2, float y2, float w2,
                      float h2) const;
    bool circleRectOverlap(float cx, float cy, float radius, float rx, float ry, float rw,
                           float rh) const;
    /** True if segments AB and CD intersect (including endpoints). */
    bool segmentsIntersect(float ax, float ay, float bx, float by, float cx, float cy, float dx,
                           float dy) const;
    /**
     * Ray vs circle. Hit point = (ox,oy) + t*(dx,dy). Returns t >= 0 on hit, else -1.
     * Direction need not be unit; for a segment use dir = B-A and accept t in [0,1].
     */
    float raycastCircle2(float ox, float oy, float dx, float dy, float cx, float cy,
                         float radius) const;
    /** Ray vs axis-aligned rect (x,y,w,h). Returns parametric t >= 0, else -1. */
    float raycastRect2(float ox, float oy, float dx, float dy, float rx, float ry, float rw,
                       float rh) const;
    float closestPointOnSegment2X(float px, float py, float ax, float ay, float bx, float by) const;
    float closestPointOnSegment2Y(float px, float py, float ax, float ay, float bx, float by) const;

    // --- 3D hit / overlap / ray ---
    bool pointInSphere(float px, float py, float pz, float cx, float cy, float cz,
                       float radius) const;
    /** Inclusive AABB test against [min,max] on each axis. */
    bool pointInBox(float px, float py, float pz, float minX, float minY, float minZ, float maxX,
                    float maxY, float maxZ) const;
    bool spheresOverlap(float x1, float y1, float z1, float r1, float x2, float y2, float z2,
                        float r2) const;
    bool boxesOverlap(float minAx, float minAy, float minAz, float maxAx, float maxAy, float maxAz,
                      float minBx, float minBy, float minBz, float maxBx, float maxBy,
                      float maxBz) const;
    float raycastSphere(float ox, float oy, float oz, float dx, float dy, float dz, float cx,
                        float cy, float cz, float radius) const;
    float raycastBox(float ox, float oy, float oz, float dx, float dy, float dz, float minX,
                     float minY, float minZ, float maxX, float maxY, float maxZ) const;
    /**
     * Ray vs infinite plane through (px,py,pz) with normal (nx,ny,nz).
     * Returns parametric t, or -1 if parallel / behind ray origin.
     */
    float raycastPlane(float ox, float oy, float oz, float dx, float dy, float dz, float px,
                       float py, float pz, float nx, float ny, float nz) const;
    float closestPointOnSegment3X(float px, float py, float pz, float ax, float ay, float az,
                                  float bx, float by, float bz) const;
    float closestPointOnSegment3Y(float px, float py, float pz, float ax, float ay, float az,
                                  float bx, float by, float bz) const;
    float closestPointOnSegment3Z(float px, float py, float pz, float ax, float ay, float az,
                                  float bx, float by, float bz) const;

    /** Bilinear sample of 4 corners (v00,v10,v01,v11) with u,v in [0,1]. */
    float bilinear(float v00, float v10, float v01, float v11, float u, float v) const;

    // --- random ---
    void     setRandomSeed(uint32_t seed);
    void     setRandomSeedFromTime();
    uint32_t getRandomSeed() const;
    float    random();
    float    randomRange(float min, float max);
    int      randomInt(int min, int maxInclusive);
    /** Box-Muller Gaussian (mean, stddev). */
    float    randomGaussian(float mean, float stddev);

    // --- deterministic hash [0,1] (grid / WFC / tile seeds) ---
    float hash1(float x) const;
    float hash2(float x, float y) const;
    float hash3(float x, float y, float z) const;

    // --- noise [0, 1] ---
    float noise1(float x) const;
    float noise2(float x, float y) const;
    float noise3(float x, float y, float z) const;
    float perlin2(float x, float y) const;
    float perlin3(float x, float y, float z) const;

    /**
     * Fractal Brownian Motion / ridged / turbulence.
     * octaves >= 1; lacunarity ~2; gain/persistence ~0.5.
     */
    float fbm2(float x, float y, int octaves = 4, float lacunarity = 2.f,
               float gain = 0.5f) const;
    float fbm3(float x, float y, float z, int octaves = 4, float lacunarity = 2.f,
               float gain = 0.5f) const;
    float ridged2(float x, float y, int octaves = 4, float lacunarity = 2.f,
                  float gain = 0.5f) const;
    float ridged3(float x, float y, float z, int octaves = 4, float lacunarity = 2.f,
                  float gain = 0.5f) const;
    float turbulence2(float x, float y, int octaves = 4, float lacunarity = 2.f,
                      float gain = 0.5f) const;

    /**
     * Worley / Voronoi F1 distance in [0, ~1.5] (cell size 1).
     * voronoiEdge2 = F2 - F1 (cell borders).
     */
    float voronoi2(float x, float y) const;
    float voronoiEdge2(float x, float y) const;

    /**
     * Domain warp: sample noise at (x,y) + warpAmp * (noise-0.5).
     * Useful for organic terrain / caves.
     */
    float warpNoise2(float x, float y, float warpAmp = 1.f) const;

    // --- bezier ---
    float bezierQuadratic(float t, float p0, float p1, float p2) const;
    float bezierCubic(float t, float p0, float p1, float p2, float p3) const;
    float bezierQuadratic2X(float t, float x0, float y0, float x1, float y1, float x2,
                            float y2) const;
    float bezierQuadratic2Y(float t, float x0, float y0, float x1, float y1, float x2,
                            float y2) const;
    float bezierCubic2X(float t, float x0, float y0, float x1, float y1, float x2, float y2,
                        float x3, float y3) const;
    float bezierCubic2Y(float t, float x0, float y0, float x1, float y1, float x2, float y2,
                        float x3, float y3) const;

private:
    uint32_t     seed_ = 1;
    std::mt19937 rng_;
};

}  // namespace eve::math
