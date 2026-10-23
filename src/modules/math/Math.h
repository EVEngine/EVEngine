#pragma once
#include "common/Export.h"


#include "common/Module.h"

#include <cstdint>
#include <random>
#include <string>

namespace eve::math {

class Vec2;
class Vec3;
class Mat4;

/**
 * @brief Math module — glm-backed vectors/matrices, noise, bezier, random.
 * Script: `math <- eve.Math();`
 *
 * No overloads: distinct names (length2/length3, noise1/noise2/…).
 */
class EVENGINE_API_FOUNDATION Math : public Module {
public:
    Module_REG(Math);
    /** @brief Initializes RNG and math helpers. */
    Math();
    /** @brief Releases module state; caller-owned Vec/Mat pointers are not freed. */
    ~Math() override = default;

    /** @brief Allocates a Vec2. @ownership Caller deletes. */
    Vec2 *newVec2(float x = 0.f, float y = 0.f);
    /** @brief Allocates a Vec3. @ownership Caller deletes. */
    Vec3 *newVec3(float x = 0.f, float y = 0.f, float z = 0.f);
    /** @brief Allocates an identity Mat4. @ownership Caller deletes. */
    Mat4 *newMat4();
    /** @brief Allocates a translation Mat4. @ownership Caller deletes. */
    Mat4 *newMat4Translation(float x, float y, float z);
    /** @brief Allocates a scale Mat4. @ownership Caller deletes. */
    Mat4 *newMat4Scale(float sx, float sy, float sz);
    /** @brief Allocates a Z-rotation Mat4 (radians). @ownership Caller deletes. */
    Mat4 *newMat4RotationZ(float radians);

    // --- scalar ---
    /** @brief Clamps x into [lo, hi]. */
    float clamp(float x, float lo, float hi) const;
    /** @brief Linear interpolation between a and b by t. */
    float lerp(float a, float b, float t) const;
    /** @brief Hermite smoothstep between edge0 and edge1. */
    float smoothstep(float edge0, float edge1, float x) const;
    /**
     * @brief C1 smooth maximum of two finite heights; see common/SmoothMax.h.
     * @param a First height in shared terrain units.
     * @param b Second height in shared terrain units.
     * @param width Finite nonnegative height band; zero gives max(a,b).
     * @return Blended height; at a==b adds width/4, on float overflow returns +infinity.
     * @thread Stateless; no retained data, callbacks, or RNG. Constant work per sample.
     */
    [[nodiscard]] float smoothMax(float a, float b, float width) const;
    /** @brief Maps x from [inMin,inMax] into [outMin,outMax]. */
    float remap(float x, float inMin, float inMax, float outMin, float outMax) const;
    /** @brief Degrees to radians. */
    float degToRad(float deg) const;
    /** @brief Radians to degrees. */
    float radToDeg(float rad) const;
    /** @brief Sign of x (-1, 0, or 1). */
    float sign(float x) const;
    /** @brief Fractional part of x. */
    float fract(float x) const;
    /** @brief Moves current toward target by at most maxDelta. */
    float approach(float current, float target, float maxDelta) const;
    /** @brief Wraps x into [lo, hi). */
    float wrap(float x, float lo, float hi) const;
    /** @brief Triangle-wave ping-pong of t over [0, length]. */
    float pingPong(float t, float length) const;

    /** @brief Linear interpolation between a and b by t. */
    /** @brief Inverse of lerp: t such that lerp(a,b,t) ≈ x. */
    float inverseLerp(float a, float b, float x) const;
    /** @brief Ken Perlin smootherstep between edge0 and edge1. */
    float smootherstep(float edge0, float edge1, float x) const;
    /** @brief Schlick bias/gain — shape [0,1] distributions (procgen falloff). */
    float bias(float t, float b) const;
    /** @brief Schlick gain shaping of t in [0,1]. */
    float gain(float t, float g) const;
    /**
     * @brief Easing on [0,1]. kind:
     * "linear"|"inQuad"|"outQuad"|"inOutQuad"|"inCubic"|"outCubic"|"inOutCubic"|
     * "inSine"|"outSine"|"inOutSine"|"inExpo"|"outExpo"|"inOutExpo"
     */
    float ease(float t, const std::string &kind) const;
    /** @brief Heaviside step: 0 if x < edge else 1. */
    float step(float edge, float x) const;
    /** @brief Quantizes x to multiples of stepSize. */
    float quantize(float x, float stepSize) const;
    /** @brief Snaps x to the nearest grid multiple. */
    float snap(float x, float grid) const;

    // --- geometry (float components) ---
    /** @brief 2D vector length from components. */
    float length2(float x, float y) const;
    /** @brief 3D vector length from components. */
    float length3(float x, float y, float z) const;
    /** @brief 2D distance between two points. */
    float distance2(float x1, float y1, float x2, float y2) const;
    /** @brief 3D distance between two points. */
    float distance3(float x1, float y1, float z1, float x2, float y2, float z2) const;
    /** @brief 2D dot product of two vectors. */
    float dot2(float x1, float y1, float x2, float y2) const;
    /** @brief 3D dot product of two vectors. */
    float dot3(float x1, float y1, float z1, float x2, float y2, float z2) const;
    /** @brief 2D cross product (scalar z-component). */
    float cross2(float x1, float y1, float x2, float y2) const;
    /** @brief Atan2 angle of (x,y) in radians. */
    float angle2(float x, float y) const;
    /** @brief Signed angle from first vector to second. */
    float angleBetween2(float x1, float y1, float x2, float y2) const;
    /** @brief Shortest-path angle interpolation. */
    float lerpAngle(float a, float b, float t) const;
    /** @brief X of the normalized 2D vector (zero-safe). */
    float normalize2X(float x, float y) const;
    /** @brief Y of the normalized 2D vector (zero-safe). */
    float normalize2Y(float x, float y) const;
    /** @brief X of the normalized 3D vector (zero-safe). */
    float normalize3X(float x, float y, float z) const;
    /** @brief Y of the normalized 3D vector (zero-safe). */
    float normalize3Y(float x, float y, float z) const;
    /** @brief Z of the normalized 3D vector (zero-safe). */
    float normalize3Z(float x, float y, float z) const;

    // --- steering (stateless vector calculations) ---
    /** @brief Creates a 2D seek velocity. @ownership owned @lifetime Caller deletes the result. */
    Vec2 *steeringSeek2(float x, float y, float targetX, float targetY, float maxSpeed) const;
    /** @brief Creates a 3D seek velocity. @ownership owned @lifetime Caller deletes the result. */
    Vec3 *steeringSeek3(float x, float y, float z, float targetX, float targetY, float targetZ,
                        float maxSpeed) const;
    /** @brief Creates a 2D flee velocity. @ownership owned @lifetime Caller deletes the result. */
    Vec2 *steeringFlee2(float x, float y, float targetX, float targetY, float maxSpeed) const;
    /** @brief Creates a 3D flee velocity. @ownership owned @lifetime Caller deletes the result. */
    Vec3 *steeringFlee3(float x, float y, float z, float targetX, float targetY, float targetZ,
                        float maxSpeed) const;
    /** @brief Creates a 2D arrive velocity. @ownership owned @lifetime Caller deletes the result. */
    Vec2 *steeringArrive2(float x, float y, float targetX, float targetY, float maxSpeed,
                          float slowRadius, float stopRadius) const;
    /** @brief Creates a 3D arrive velocity. @ownership owned @lifetime Caller deletes the result. */
    Vec3 *steeringArrive3(float x, float y, float z, float targetX, float targetY, float targetZ,
                          float maxSpeed, float slowRadius, float stopRadius) const;
    /** @brief Creates 2D separation acceleration from CSV. @ownership owned @lifetime Caller deletes the result. */
    Vec2 *steeringSeparation2(float x, float y, const std::string &neighbors, float radius,
                              float maxAcceleration) const;
    /** @brief Creates 3D separation acceleration from CSV. @ownership owned @lifetime Caller deletes the result. */
    Vec3 *steeringSeparation3(float x, float y, float z, const std::string &neighbors,
                              float radius, float maxAcceleration) const;
    /** @brief Selects a 2D path point from CSV x:y points, or -1 for no valid points. */
    int steeringPathTarget2(float x, float y, const std::string &points, int current,
                            float tolerance) const;
    /** @brief Selects a 3D path point from CSV x:y:z points, or -1 for no valid points. */
    int steeringPathTarget3(float x, float y, float z, const std::string &points, int current,
                            float tolerance) const;
    /** @brief Creates 2D avoidance acceleration. @ownership owned @lifetime Caller deletes the result. */
    Vec2 *steeringAvoid2(float x, float y, float velocityX, float velocityY, float obstacleX,
                         float obstacleY, float obstacleRadius, float lookAhead,
                         float maxAcceleration) const;
    /** @brief Creates 3D avoidance acceleration. @ownership owned @lifetime Caller deletes the result. */
    Vec3 *steeringAvoid3(float x, float y, float z, float velocityX, float velocityY,
                         float velocityZ, float obstacleX, float obstacleY, float obstacleZ,
                         float obstacleRadius, float lookAhead, float maxAcceleration) const;

    /** @brief Rotate (x,y) by radians around origin. */
    float rotate2X(float x, float y, float radians) const;
    /** @brief Y after rotating (x,y) by radians about the origin. */
    float rotate2Y(float x, float y, float radians) const;
    /** @brief X of polar (radius, radians). */
    float polarX(float radius, float radians) const;
    /** @brief Y of polar (radius, radians). */
    float polarY(float radius, float radians) const;
    /** @brief Length of (x,y). */
    float cartesianRadius(float x, float y) const;
    /** @brief Atan2 angle of (x,y). */
    float cartesianAngle(float x, float y) const;

    // --- 2D hit / overlap / ray (picking & collision detection) ---
    /** @brief True if point is inside or on the circle. */
    bool pointInCircle(float px, float py, float cx, float cy, float radius) const;
    /** @brief True if point is inside or on the axis-aligned rect. */
    bool pointInRect(float px, float py, float rx, float ry, float rw, float rh) const;
    /** @brief True if two circles overlap. */
    bool circlesOverlap(float x1, float y1, float r1, float x2, float y2, float r2) const;
    /** @brief True if two axis-aligned rects overlap. */
    bool rectsOverlap(float x1, float y1, float w1, float h1, float x2, float y2, float w2,
                      float h2) const;
    /** @brief True if circle and axis-aligned rect overlap. */
    bool circleRectOverlap(float cx, float cy, float radius, float rx, float ry, float rw,
                           float rh) const;
    /** @brief True if segments AB and CD intersect (including endpoints). */
    bool segmentsIntersect(float ax, float ay, float bx, float by, float cx, float cy, float dx,
                           float dy) const;
    /**
     * @brief Ray vs circle. Hit point = (ox,oy) + t*(dx,dy). Returns t >= 0 on hit, else -1.
     * Direction need not be unit; for a segment use dir = B-A and accept t in [0,1].
     */
    float raycastCircle2(float ox, float oy, float dx, float dy, float cx, float cy,
                         float radius) const;
    /** @brief Ray vs axis-aligned rect (x,y,w,h). Returns parametric t >= 0, else -1. */
    float raycastRect2(float ox, float oy, float dx, float dy, float rx, float ry, float rw,
                       float rh) const;
    /** @brief X of the closest point on segment AB to P. */
    float closestPointOnSegment2X(float px, float py, float ax, float ay, float bx, float by) const;
    /** @brief Y of the closest point on segment AB to P. */
    float closestPointOnSegment2Y(float px, float py, float ax, float ay, float bx, float by) const;

    // --- 3D hit / overlap / ray ---
    /** @brief True if point is inside or on the sphere. */
    bool pointInSphere(float px, float py, float pz, float cx, float cy, float cz,
                       float radius) const;
    /** @brief Inclusive AABB test against [min,max] on each axis. */
    bool pointInBox(float px, float py, float pz, float minX, float minY, float minZ, float maxX,
                    float maxY, float maxZ) const;
    /** @brief True if two spheres overlap. */
    bool spheresOverlap(float x1, float y1, float z1, float r1, float x2, float y2, float z2,
                        float r2) const;
    /** @brief True if two AABBs overlap. */
    bool boxesOverlap(float minAx, float minAy, float minAz, float maxAx, float maxAy, float maxAz,
                      float minBx, float minBy, float minBz, float maxBx, float maxBy,
                      float maxBz) const;
    /** @brief Ray vs sphere; parametric t >= 0 on hit, else -1. */
    float raycastSphere(float ox, float oy, float oz, float dx, float dy, float dz, float cx,
                        float cy, float cz, float radius) const;
    /** @brief Ray vs AABB; parametric t >= 0 on hit, else -1. */
    float raycastBox(float ox, float oy, float oz, float dx, float dy, float dz, float minX,
                     float minY, float minZ, float maxX, float maxY, float maxZ) const;
    /**
     * @brief Ray vs infinite plane through (px,py,pz) with normal (nx,ny,nz).
     * Returns parametric t, or -1 if parallel / behind ray origin.
     */
    float raycastPlane(float ox, float oy, float oz, float dx, float dy, float dz, float px,
                       float py, float pz, float nx, float ny, float nz) const;
    /** @brief X of the closest point on segment AB to P. */
    float closestPointOnSegment3X(float px, float py, float pz, float ax, float ay, float az,
                                  float bx, float by, float bz) const;
    /** @brief Y of the closest point on segment AB to P. */
    float closestPointOnSegment3Y(float px, float py, float pz, float ax, float ay, float az,
                                  float bx, float by, float bz) const;
    /** @brief Z of the closest point on segment AB to P. */
    float closestPointOnSegment3Z(float px, float py, float pz, float ax, float ay, float az,
                                  float bx, float by, float bz) const;

    /** @brief Bilinear sample of 4 corners (v00,v10,v01,v11) with u,v in [0,1]. */
    float bilinear(float v00, float v10, float v01, float v11, float u, float v) const;

    // --- random ---
    /** @brief Sets the module RNG seed. */
    void     setRandomSeed(uint32_t seed);
    /** @brief Seeds the RNG from the system clock. */
    void     setRandomSeedFromTime();
    /** @brief Current RNG seed value. */
    uint32_t getRandomSeed() const;
    /** @brief Uniform float in [0, 1). */
    float    random();
    /** @brief Uniform float in [min, max). */
    float    randomRange(float min, float max);
    /** @brief Uniform inclusive integer in [min, maxInclusive]. */
    int      randomInt(int min, int maxInclusive);
    /** @brief Box-Muller Gaussian (mean, stddev). */
    float    randomGaussian(float mean, float stddev);

    // --- deterministic hash [0,1] (grid / WFC / tile seeds) ---
    /** @brief Deterministic 1D hash mapped to [0,1]. */
    float hash1(float x) const;
    /** @brief Deterministic 2D hash mapped to [0,1]. */
    float hash2(float x, float y) const;
    /** @brief Deterministic 3D hash mapped to [0,1]. */
    float hash3(float x, float y, float z) const;

    // --- noise [0, 1] ---
    /** @brief Value noise in 1D, range [0,1]. */
    float noise1(float x) const;
    /** @brief Value noise in 2D, range [0,1]. */
    float noise2(float x, float y) const;
    /** @brief Value noise in 3D, range [0,1]. */
    float noise3(float x, float y, float z) const;
    /** @brief Perlin noise in 2D, range [0,1]. */
    float perlin2(float x, float y) const;
    /** @brief Perlin noise in 3D, range [0,1]. */
    float perlin3(float x, float y, float z) const;

    /**
     * @brief Fractal Brownian Motion / ridged / turbulence.
     * octaves >= 1; lacunarity ~2; gain/persistence ~0.5.
     */
    float fbm2(float x, float y, int octaves = 4, float lacunarity = 2.f,
               float gain = 0.5f) const;
    /** @brief 3D fractal Brownian motion. */
    float fbm3(float x, float y, float z, int octaves = 4, float lacunarity = 2.f,
               float gain = 0.5f) const;
    /** @brief 2D ridged multifractal noise. */
    float ridged2(float x, float y, int octaves = 4, float lacunarity = 2.f,
                  float gain = 0.5f) const;
    /** @brief 3D ridged multifractal noise. */
    float ridged3(float x, float y, float z, int octaves = 4, float lacunarity = 2.f,
                  float gain = 0.5f) const;
    /** @brief 2D turbulence (absolute fBm). */
    float turbulence2(float x, float y, int octaves = 4, float lacunarity = 2.f,
                      float gain = 0.5f) const;

    /**
     * @brief Worley / Voronoi F1 distance in [0, ~1.5] (cell size 1).
     * voronoiEdge2 = F2 - F1 (cell borders).
     */
    float voronoi2(float x, float y) const;
    /** @brief 2D Worley F2-F1 edge distance. */
    float voronoiEdge2(float x, float y) const;

    /**
     * @brief Domain warp: sample noise at (x,y) + warpAmp * (noise-0.5).
     * Useful for organic terrain / caves.
     */
    float warpNoise2(float x, float y, float warpAmp = 1.f) const;

    // --- bezier ---
    /** @brief 1D quadratic Bezier at t in [0,1]. */
    float bezierQuadratic(float t, float p0, float p1, float p2) const;
    /** @brief 1D cubic Bezier at t in [0,1]. */
    float bezierCubic(float t, float p0, float p1, float p2, float p3) const;
    /** @brief X of a 2D quadratic Bezier at t. */
    float bezierQuadratic2X(float t, float x0, float y0, float x1, float y1, float x2,
                            float y2) const;
    /** @brief Y of a 2D quadratic Bezier at t. */
    float bezierQuadratic2Y(float t, float x0, float y0, float x1, float y1, float x2,
                            float y2) const;
    /** @brief X of a 2D cubic Bezier at t. */
    float bezierCubic2X(float t, float x0, float y0, float x1, float y1, float x2, float y2,
                        float x3, float y3) const;
    /** @brief Y of a 2D cubic Bezier at t. */
    float bezierCubic2Y(float t, float x0, float y0, float x1, float y1, float x2, float y2,
                        float x3, float y3) const;

private:
    uint32_t     seed_ = 1;
    std::mt19937 rng_;
};

}  // namespace eve::math
