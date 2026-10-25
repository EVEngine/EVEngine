// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "origins": [
    "math.cpp",
    "math_smooth_max.cpp",
    "math_steering.cpp"
  ]
}
*/
// clang-format on

#include "zeroerr/assert.h"

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_math)
#include "PrivateTestScope.h"
#include <cmath>
#include <memory>
#include "common/Exception.h"
#include "math/Mat4.h"
#include "math/Math.h"
#include "math/Vec2.h"
#include "math/Vec3.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// math.cpp
namespace test_math {
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

using namespace eve::math;

TEST_CASE("math.scalar.clampLerp") {
    auto *m = Math::create();
    CHECK_EQ(m->clamp(5.f, 0.f, 3.f), 3.f);
    CHECK_EQ(m->clamp(-1.f, 0.f, 3.f), 0.f);
    CHECK(std::fabs(m->lerp(0.f, 10.f, 0.5f) - 5.f) < 1e-5f);
    CHECK(std::fabs(m->smoothstep(0.f, 1.f, 0.5f) - 0.5f) < 1e-5f);
    CHECK(std::fabs(m->degToRad(180.f) - float(M_PI)) < 1e-5f);
    CHECK(std::fabs(m->radToDeg(float(M_PI)) - 180.f) < 1e-4f);
    CHECK_EQ(m->sign(-2.f), -1.f);
    CHECK(std::fabs(m->fract(3.25f) - 0.25f) < 1e-5f);
    CHECK(std::fabs(m->approach(0.f, 10.f, 3.f) - 3.f) < 1e-5f);
    CHECK(std::fabs(m->wrap(5.f, 0.f, 4.f) - 1.f) < 1e-5f);
}

TEST_CASE("math.geometry.vec2ops") {
    auto *m = Math::create();
    CHECK(std::fabs(m->length2(3.f, 4.f) - 5.f) < 1e-5f);
    CHECK(std::fabs(m->distance2(0.f, 0.f, 3.f, 4.f) - 5.f) < 1e-5f);
    CHECK(std::fabs(m->dot2(1.f, 0.f, 0.f, 1.f)) < 1e-6f);
    CHECK(std::fabs(m->normalize2X(3.f, 4.f) - 0.6f) < 1e-5f);
    CHECK(std::fabs(m->normalize2Y(3.f, 4.f) - 0.8f) < 1e-5f);
    CHECK(m->pointInCircle(1.f, 1.f, 0.f, 0.f, 2.f));
    CHECK(!m->pointInRect(5.f, 5.f, 0.f, 0.f, 2.f, 2.f));
}

TEST_CASE("math.geometry.pickAndOverlap2d") {
    auto *m = Math::create();
    CHECK(m->circlesOverlap(0.f, 0.f, 1.f, 1.5f, 0.f, 1.f));
    CHECK(!m->circlesOverlap(0.f, 0.f, 1.f, 3.f, 0.f, 1.f));
    CHECK(m->rectsOverlap(0.f, 0.f, 2.f, 2.f, 1.f, 1.f, 2.f, 2.f));
    CHECK(!m->rectsOverlap(0.f, 0.f, 1.f, 1.f, 2.f, 2.f, 1.f, 1.f));
    CHECK(m->circleRectOverlap(0.f, 0.f, 1.f, 0.5f, -0.5f, 2.f, 1.f));
    CHECK(m->segmentsIntersect(0.f, 0.f, 2.f, 2.f, 0.f, 2.f, 2.f, 0.f));
    CHECK(!m->segmentsIntersect(0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 1.f, 1.f));

    float t = m->raycastCircle2(0.f, 0.f, 1.f, 0.f, 5.f, 0.f, 1.f);
    CHECK(std::fabs(t - 4.f) < 1e-4f);
    CHECK(m->raycastCircle2(0.f, 0.f, -1.f, 0.f, 5.f, 0.f, 1.f) < 0.f);

    t = m->raycastRect2(0.f, 0.f, 1.f, 0.f, 2.f, -1.f, 2.f, 2.f);
    CHECK(std::fabs(t - 2.f) < 1e-4f);
    CHECK(std::fabs(m->closestPointOnSegment2X(1.f, 1.f, 0.f, 0.f, 2.f, 0.f) - 1.f) < 1e-5f);
    CHECK(std::fabs(m->closestPointOnSegment2Y(1.f, 1.f, 0.f, 0.f, 2.f, 0.f)) < 1e-5f);
}

TEST_CASE("math.geometry.pickAndOverlap3d") {
    auto *m = Math::create();
    CHECK(m->pointInSphere(0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f));
    CHECK(!m->pointInSphere(2.f, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f));
    CHECK(m->pointInBox(0.f, 0.f, 0.f, -1.f, -1.f, -1.f, 1.f, 1.f, 1.f));
    CHECK(m->spheresOverlap(0.f, 0.f, 0.f, 1.f, 1.5f, 0.f, 0.f, 1.f));
    CHECK(m->boxesOverlap(0.f, 0.f, 0.f, 1.f, 1.f, 1.f, 0.5f, 0.5f, 0.5f, 2.f, 2.f, 2.f));
    CHECK(!m->boxesOverlap(0.f, 0.f, 0.f, 1.f, 1.f, 1.f, 2.f, 2.f, 2.f, 3.f, 3.f, 3.f));

    float t = m->raycastSphere(0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 5.f, 0.f, 0.f, 1.f);
    CHECK(std::fabs(t - 4.f) < 1e-4f);
    t = m->raycastBox(0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 2.f, -1.f, -1.f, 4.f, 1.f, 1.f);
    CHECK(std::fabs(t - 2.f) < 1e-4f);
    t = m->raycastPlane(0.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 5.f, 0.f, 0.f, 1.f);
    CHECK(std::fabs(t - 5.f) < 1e-4f);
    CHECK(m->raycastPlane(0.f, 0.f, 0.f, 0.f, 0.f, -1.f, 0.f, 0.f, 5.f, 0.f, 0.f, 1.f) < 0.f);
    CHECK(std::fabs(m->closestPointOnSegment3Z(0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 0.f, 2.f) - 1.f) < 1e-5f);
}

TEST_CASE("math.vec2.vec3.classes") {
    auto                 *m = Math::create();
    std::unique_ptr<Vec2> a(m->newVec2(3.f, 4.f));
    CHECK(std::fabs(a->length() - 5.f) < 1e-5f);
    a->normalize();
    CHECK(std::fabs(a->length() - 1.f) < 1e-5f);

    std::unique_ptr<Vec3> b(m->newVec3(1.f, 0.f, 0.f));
    std::unique_ptr<Vec3> c(m->newVec3(0.f, 1.f, 0.f));
    std::unique_ptr<Vec3> cr(b->cross(c.get()));
    CHECK(std::fabs(cr->getZ() - 1.f) < 1e-5f);
}

TEST_CASE("math.mat4.transform") {
    auto                 *m = Math::create();
    std::unique_ptr<Mat4> mat(m->newMat4Translation(10.f, 20.f, 0.f));
    std::unique_ptr<Vec2> p(m->newVec2(1.f, 2.f));
    std::unique_ptr<Vec2> out(mat->transformPoint2(p.get()));
    CHECK(std::fabs(out->getX() - 11.f) < 1e-4f);
    CHECK(std::fabs(out->getY() - 22.f) < 1e-4f);
    CHECK(std::fabs(mat->get(12) - 10.f) < 1e-5f);  // translation x in column-major
}

TEST_CASE("math.random.deterministic") {
    auto *m = Math::create();
    m->setRandomSeed(42);
    float a = m->random();
    float b = m->randomRange(10.f, 20.f);
    int   c = m->randomInt(1, 6);
    m->setRandomSeed(42);
    CHECK(std::fabs(m->random() - a) < 1e-7f);
    CHECK(std::fabs(m->randomRange(10.f, 20.f) - b) < 1e-7f);
    CHECK_EQ(m->randomInt(1, 6), c);
    CHECK_GE(c, 1);
    CHECK_LE(c, 6);
}

TEST_CASE("math.noise.range") {
    auto *m = Math::create();
    for (int i = 0; i < 20; ++i) {
        float n = m->noise2(float(i) * 0.1f, float(i) * 0.07f);
        CHECK_GE(n, 0.f);
        CHECK_LE(n, 1.f);
    }
    float n0 = m->noise2(1.5f, 2.5f);
    float n1 = m->noise2(1.5f, 2.5f);
    CHECK(std::fabs(n0 - n1) < 1e-6f);
}

TEST_CASE("math.bezier.endpoints") {
    auto *m = Math::create();
    CHECK(std::fabs(m->bezierQuadratic(0.f, 0.f, 5.f, 10.f) - 0.f) < 1e-5f);
    CHECK(std::fabs(m->bezierQuadratic(1.f, 0.f, 5.f, 10.f) - 10.f) < 1e-5f);
    CHECK(std::fabs(m->bezierCubic(0.f, 0.f, 1.f, 2.f, 3.f) - 0.f) < 1e-5f);
    CHECK(std::fabs(m->bezierCubic(1.f, 0.f, 1.f, 2.f, 3.f) - 3.f) < 1e-5f);
    CHECK(std::fabs(m->bezierCubic2X(0.5f, 0.f, 0.f, 0.f, 10.f, 10.f, 10.f, 10.f, 0.f) -
                    m->bezierCubic(0.5f, 0.f, 0.f, 10.f, 10.f)) < 1e-5f);
}

TEST_CASE("math.procgen.hashFbmVoronoi") {
    auto *m  = Math::create();
    float h0 = m->hash2(3.f, 7.f);
    float h1 = m->hash2(3.f, 7.f);
    CHECK(std::fabs(h0 - h1) < 1e-6f);
    CHECK_GE(h0, 0.f);
    CHECK_LE(h0, 1.f);
    CHECK(std::fabs(m->hash2(3.f, 7.f) - m->hash2(3.1f, 7.f)) > 1e-4f);

    float f = m->fbm2(0.2f, 0.3f, 4, 2.f, 0.5f);
    CHECK_GE(f, 0.f);
    CHECK_LE(f, 1.f);

    float r = m->ridged2(1.1f, 2.2f, 3, 2.f, 0.5f);
    CHECK_GE(r, 0.f);
    CHECK_LE(r, 1.f);

    float v = m->voronoi2(1.3f, 2.7f);
    CHECK_GE(v, 0.f);
    float e = m->voronoiEdge2(1.3f, 2.7f);
    CHECK_GE(e, 0.f);

    float w = m->warpNoise2(0.5f, 0.5f, 2.f);
    CHECK_GE(w, 0.f);
    CHECK_LE(w, 1.f);
}

TEST_CASE("math.procgen.easeBiasSnap") {
    auto *m = Math::create();
    CHECK(std::fabs(m->inverseLerp(0.f, 10.f, 5.f) - 0.5f) < 1e-5f);
    CHECK(std::fabs(m->ease(0.5f, "linear") - 0.5f) < 1e-5f);
    CHECK(m->ease(0.5f, "inQuad") < 0.5f);
    CHECK(m->ease(0.5f, "outQuad") > 0.5f);
    CHECK(std::fabs(m->snap(13.f, 5.f) - 15.f) < 1e-5f);
    CHECK(std::fabs(m->quantize(13.f, 5.f) - 10.f) < 1e-5f);
    CHECK(std::fabs(m->rotate2X(1.f, 0.f, float(M_PI) * 0.5f)) < 1e-5f);
    CHECK(std::fabs(m->rotate2Y(1.f, 0.f, float(M_PI) * 0.5f) - 1.f) < 1e-5f);
    CHECK(std::fabs(m->bilinear(0.f, 1.f, 0.f, 1.f, 0.5f, 0.f) - 0.5f) < 1e-5f);
}

TEST_CASE("math.scalar.extras") {
    auto *m = Math::create();
    CHECK(std::fabs(m->remap(5.f, 0.f, 10.f, 0.f, 100.f) - 50.f) < 1e-4f);
    CHECK(std::fabs(m->remap(0.f, 0.f, 0.f, 3.f, 4.f) - 3.f) < 1e-5f);
    CHECK(std::fabs(m->pingPong(3.5f, 2.f) - 0.5f) < 1e-4f);
    CHECK(std::fabs(m->pingPong(3.f, 0.f)) < 1e-6f);
    CHECK(std::fabs(m->smootherstep(0.f, 1.f, 0.5f) - 0.5f) < 1e-4f);
    CHECK(std::fabs(m->bias(0.5f, 0.f)) < 1e-5f);
    CHECK(std::fabs(m->bias(0.5f, 1.f) - 1.f) < 1e-5f);
    CHECK(std::fabs(m->gain(0.25f, 0.f)) < 1e-5f);
    CHECK(std::fabs(m->gain(0.5f, 1.f) - 0.5f) < 1e-5f);
    CHECK(std::fabs(m->ease(0.f, "inQuad")) < 1e-5f);
    CHECK(std::fabs(m->ease(1.f, "outQuad") - 1.f) < 1e-5f);
    CHECK(std::fabs(m->ease(0.5f, "inOutCubic") - 0.5f) < 1e-5f);
    CHECK(std::fabs(m->ease(0.5f, "inOutSine") - 0.5f) < 1e-5f);
    CHECK(std::fabs(m->ease(0.3f, "outExpo") - (1.f - std::pow(2.f, -10.f * 0.3f))) < 1e-5f);
    CHECK(std::fabs(m->ease(0.f, "inExpo")) < 1e-5f);
    CHECK(std::fabs(m->ease(1.f, "outExpo") - 1.f) < 1e-5f);
    bool threw = false;
    try {
        m->ease(0.5f, "nope");
    } catch (const eve::Exception &) {
        threw = true;
    }
    CHECK(threw);
    CHECK_EQ(m->step(0.5f, 0.1f), 0.f);
    CHECK_EQ(m->step(0.5f, 0.9f), 1.f);
    CHECK(std::fabs(m->quantize(1.3f, 0.f) - 1.3f) < 1e-5f);
    CHECK(std::fabs(m->snap(1.3f, 0.f) - 1.3f) < 1e-5f);
    // lerpAngle is radian-based and takes the shortest wrap-around path.
    CHECK(std::fabs(m->lerpAngle(0.f, float(M_PI) * 0.5f, 0.5f) - float(M_PI) * 0.25f) < 1e-3f);
}

TEST_CASE("math.geometry.extras") {
    auto *m = Math::create();
    CHECK(std::fabs(m->length3(1.f, 2.f, 2.f) - 3.f) < 1e-5f);
    CHECK(std::fabs(m->distance3(0.f, 0.f, 0.f, 1.f, 2.f, 2.f) - 3.f) < 1e-5f);
    CHECK(std::fabs(m->dot3(1.f, 0.f, 0.f, 0.f, 1.f, 0.f)) < 1e-6f);
    CHECK(std::fabs(m->cross2(1.f, 0.f, 0.f, 1.f) - 1.f) < 1e-6f);
    CHECK(std::fabs(m->angle2(0.f, 1.f) - float(M_PI) * 0.5f) < 1e-5f);
    CHECK(std::fabs(m->angleBetween2(0.f, 1.f, 1.f, 0.f) + float(M_PI) * 0.5f) < 1e-5f);
    CHECK(std::fabs(m->normalize3X(0.f, 3.f, 4.f)) < 1e-5f);
    CHECK(std::fabs(m->normalize3Y(0.f, 3.f, 4.f) - 0.6f) < 1e-5f);
    CHECK(std::fabs(m->normalize3Z(0.f, 3.f, 4.f) - 0.8f) < 1e-5f);
    CHECK(std::fabs(m->normalize3X(0.f, 0.f, 0.f)) < 1e-6f);
    CHECK(std::fabs(m->polarX(2.f, 0.f) - 2.f) < 1e-5f);
    CHECK(std::fabs(m->polarY(2.f, float(M_PI) * 0.5f) - 2.f) < 1e-5f);
    CHECK(std::fabs(m->cartesianRadius(3.f, 4.f) - 5.f) < 1e-5f);
    CHECK(std::fabs(m->cartesianAngle(0.f, 1.f) - float(M_PI) * 0.5f) < 1e-5f);
    CHECK(std::fabs(m->bilinear(0.f, 10.f, 20.f, 30.f, 0.5f, 0.5f) - 15.f) < 1e-5f);

    CHECK(m->pointInSphere(0.f, 0.f, 0.f, 1.f, 1.f, 1.f, 2.f));
    CHECK(!m->pointInSphere(3.f, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f));
    CHECK(!m->pointInSphere(0.f, 0.f, 0.f, 0.f, 0.f, 0.f, -1.f));
    CHECK(m->pointInBox(1.f, 1.f, 1.f, 0.f, 0.f, 0.f, 2.f, 2.f, 2.f));
    CHECK(!m->pointInBox(3.f, 1.f, 1.f, 0.f, 0.f, 0.f, 2.f, 2.f, 2.f));
    CHECK(m->spheresOverlap(0.f, 0.f, 0.f, 1.f, 1.5f, 0.f, 0.f, 1.f));
    CHECK(!m->spheresOverlap(0.f, 0.f, 0.f, 1.f, 3.f, 0.f, 0.f, 1.f));
    CHECK(!m->spheresOverlap(0.f, 0.f, 0.f, -1.f, 0.f, 0.f, 0.f, 1.f));
    CHECK(m->boxesOverlap(0.f, 0.f, 0.f, 2.f, 2.f, 2.f, 1.f, 1.f, 1.f, 3.f, 3.f, 3.f));
    CHECK(!m->boxesOverlap(0.f, 0.f, 0.f, 1.f, 1.f, 1.f, 2.f, 2.f, 2.f, 3.f, 3.f, 3.f));

    CHECK(std::fabs(m->closestPointOnSegment2X(0.f, 0.f, 1.f, 0.f, 3.f, 0.f) - 1.f) < 1e-5f);
    CHECK(std::fabs(m->closestPointOnSegment2Y(0.f, 0.f, 1.f, 0.f, 3.f, 0.f)) < 1e-5f);
    CHECK(std::fabs(m->closestPointOnSegment3X(5.f, 5.f, 5.f, 1.f, 0.f, 0.f, 2.f, 0.f, 0.f) - 2.f) < 1e-5f);
    CHECK(std::fabs(m->closestPointOnSegment3Y(5.f, 5.f, 5.f, 1.f, 0.f, 0.f, 2.f, 0.f, 0.f)) < 1e-5f);
    CHECK(std::fabs(m->closestPointOnSegment3Z(5.f, 5.f, 5.f, 1.f, 0.f, 0.f, 2.f, 0.f, 0.f)) < 1e-5f);

    CHECK(std::fabs(m->raycastSphere(0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 5.f, 0.f, 0.f, 1.f) - 4.f) < 1e-4f);
    CHECK(m->raycastSphere(0.f, 0.f, 0.f, -1.f, 0.f, 0.f, 5.f, 0.f, 0.f, 1.f) < 0.f);
    CHECK(m->raycastSphere(0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 5.f, 0.f, 0.f, 1.f) < 0.f);
    CHECK(std::fabs(m->raycastBox(0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 2.f, -1.f, -1.f, 4.f, 1.f, 1.f) - 2.f) < 1e-4f);
    CHECK(m->raycastBox(0.f, 0.f, 0.f, -1.f, 0.f, 0.f, 2.f, -1.f, -1.f, 4.f, 1.f, 1.f) < 0.f);
    CHECK(std::fabs(m->raycastPlane(0.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 5.f, 0.f, 0.f, 1.f) - 5.f) < 1e-4f);
    CHECK(m->raycastPlane(0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 5.f, 0.f, 0.f, 1.f) < 0.f);
    CHECK(m->raycastRect2(0.f, 0.f, 1.f, 0.f, 5.f, -1.f, 2.f, 2.f) > 0.f);
    CHECK(m->raycastRect2(0.f, 0.f, -1.f, 0.f, 5.f, -1.f, 2.f, 2.f) < 0.f);
    CHECK(m->raycastCircle2(0.f, 0.f, 1.f, 1.f, 5.f, 0.f, 1.f) < 0.f);  // ray misses the circle
}

TEST_CASE("math.randomAndHashDeterministic") {
    auto *m = Math::create();
    m->setRandomSeed(12345u);
    CHECK_EQ(m->getRandomSeed(), 12345u);
    const float r1 = m->random();
    CHECK(r1 >= 0.f);
    CHECK(r1 < 1.f);
    m->setRandomSeed(12345u);
    CHECK(std::fabs(m->random() - r1) < 1e-6f);
    const float rr = m->randomRange(10.f, 20.f);
    CHECK(rr >= 10.f);
    CHECK(rr <= 20.f);
    const int ri = m->randomInt(5, 7);
    CHECK(ri >= 5);
    CHECK(ri <= 7);
    const float g = m->randomGaussian(0.f, 1.f);
    CHECK(std::isfinite(g));
    m->setRandomSeedFromTime();
    CHECK(m->getRandomSeed() != 0u);

    CHECK(std::fabs(m->hash1(1.f) - m->hash1(1.f)) < 1e-6f);
    const float h1 = m->hash1(1.f);
    CHECK(h1 >= 0.f);
    CHECK(h1 <= 1.f);
    CHECK(std::fabs(m->hash2(1.f, 2.f) - m->hash2(1.f, 2.f)) < 1e-6f);
    const float h3 = m->hash3(1.f, 2.f, 3.f);
    CHECK(h3 >= 0.f);
    CHECK(h3 <= 1.f);
    const float n1 = m->noise1(0.5f);
    CHECK(n1 >= 0.f);
    CHECK(n1 <= 1.f);
    const float n2 = m->noise2(0.25f, 0.75f);
    CHECK(n2 >= 0.f);
    CHECK(n2 <= 1.f);
    const float n3 = m->noise3(0.1f, 0.2f, 0.3f);
    CHECK(n3 >= 0.f);
    CHECK(n3 <= 1.f);
    const float p2 = m->perlin2(0.5f, 0.5f);
    CHECK(p2 >= 0.f);
    CHECK(p2 <= 1.f);
    const float p3 = m->perlin3(0.5f, 0.5f, 0.5f);
    CHECK(p3 >= 0.f);
    CHECK(p3 <= 1.f);
    const float f2 = m->fbm2(0.3f, 0.7f);
    CHECK(f2 >= 0.f);
    CHECK(f2 <= 1.f);
    const float f3 = m->fbm3(0.3f, 0.7f, 0.2f);
    CHECK(f3 >= 0.f);
    CHECK(f3 <= 1.f);
    const float rd2 = m->ridged2(0.3f, 0.7f);
    CHECK(rd2 >= 0.f);
    CHECK(rd2 <= 1.f);
    const float rd3 = m->ridged3(0.3f, 0.7f, 0.2f);
    CHECK(rd3 >= 0.f);
    CHECK(rd3 <= 1.f);
    const float tb = m->turbulence2(0.3f, 0.7f);
    CHECK(tb >= 0.f);
    CHECK(tb <= 1.f);
    const float v2 = m->voronoi2(0.3f, 0.7f);
    CHECK(v2 >= 0.f);
    CHECK(v2 <= 1.6f);
    const float ve = m->voronoiEdge2(0.3f, 0.7f);
    CHECK(ve >= 0.f);
    const float wn = m->warpNoise2(0.3f, 0.7f);
    CHECK(wn >= 0.f);
    CHECK(wn <= 1.f);
}

TEST_CASE("math.bezier.endpointProperties") {
    auto *m = Math::create();
    CHECK(std::fabs(m->bezierQuadratic(0.f, 1.f, 5.f, 9.f) - 1.f) < 1e-5f);
    CHECK(std::fabs(m->bezierQuadratic(1.f, 1.f, 5.f, 9.f) - 9.f) < 1e-5f);
    CHECK(std::fabs(m->bezierQuadratic(0.5f, 1.f, 5.f, 9.f) - 5.f) < 1e-4f);
    CHECK(std::fabs(m->bezierCubic(0.f, 0.f, 3.f, 6.f, 9.f)) < 1e-5f);
    CHECK(std::fabs(m->bezierCubic(1.f, 0.f, 3.f, 6.f, 9.f) - 9.f) < 1e-5f);
    CHECK(std::fabs(m->bezierQuadratic2X(0.f, 0.f, 0.f, 5.f, 5.f, 10.f, 10.f)) < 1e-5f);
    CHECK(std::fabs(m->bezierQuadratic2X(1.f, 0.f, 0.f, 5.f, 5.f, 10.f, 10.f) - 10.f) < 1e-5f);
    CHECK(std::fabs(m->bezierQuadratic2Y(0.f, 0.f, 0.f, 5.f, 5.f, 10.f, 10.f)) < 1e-5f);
    CHECK(std::fabs(m->bezierQuadratic2Y(1.f, 0.f, 0.f, 5.f, 5.f, 10.f, 10.f) - 10.f) < 1e-5f);
    CHECK(std::fabs(m->bezierCubic2X(0.f, 0.f, 0.f, 1.f, 1.f, 2.f, 2.f, 3.f, 3.f)) < 1e-5f);
    CHECK(std::fabs(m->bezierCubic2X(1.f, 0.f, 0.f, 1.f, 1.f, 2.f, 2.f, 3.f, 3.f) - 3.f) < 1e-5f);
    CHECK(std::fabs(m->bezierCubic2Y(0.5f, 0.f, 0.f, 1.f, 1.f, 2.f, 2.f, 3.f, 3.f) - 1.5f) < 1e-4f);
}
}  // namespace test_math
#undef M_PI
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_math_smooth_max) || \
    defined(EVE_TEST_ORIGIN_math_steering)
#include "PrivateTestScope.h"
#include <array>
#include <cmath>
#include <limits>
#include <memory>
#include "common/SmoothMax.h"
#include "math/Math.h"
#include "math/Steering.h"
#include "math/Vec2.h"
#include "math/Vec3.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// Responsibility: math_interpolation_steering
namespace test_math_interpolation_steering {
// math_smooth_max.cpp
namespace {
namespace math_smooth_max {
using eve::math::smoothMax;

TEST_CASE("math.smoothMax.valuesAndRange") {
    CHECK_EQ(smoothMax(10.f, 10.f, 4.f), 11.f);
    CHECK_EQ(smoothMax(-10.f, -10.f, 4.f), -9.f);
    CHECK_EQ(smoothMax(3.f, 7.f, 0.f), 7.f);
    CHECK_EQ(smoothMax(3.f, 7.f, 4.f), 7.f);
    CHECK_EQ(smoothMax(3.f, 8.f, 4.f), 8.f);
    for (int i = -100; i <= 100; ++i) {
        const float a     = static_cast<float>(i) * 0.1f;
        const float value = smoothMax(a, 2.f, 4.f);
        CHECK_EQ(value, smoothMax(2.f, a, 4.f));
        CHECK(value >= std::max(a, 2.f));
        CHECK(value <= std::max(a, 2.f) + 1.f);
        CHECK(smoothMax(a + 0.01f, 2.f, 4.f) >= value);
        CHECK(std::abs(smoothMax(a + 8.f, 10.f, 4.f) - value - 8.f) < 2e-6f);
    }
}

TEST_CASE("math.smoothMax.continuousSlope") {
    constexpr float delta = 0.001f;
    for (const float x : {-4.f, 0.f, 4.f}) {
        const float center = smoothMax(x, 0.f, 4.f);
        const float left   = (center - smoothMax(x - delta, 0.f, 4.f)) / delta;
        const float right  = (smoothMax(x + delta, 0.f, 4.f) - center) / delta;
        CHECK(std::abs(left - right) < 0.002f);
        CHECK(std::abs(left - (x + 4.f) / 8.f) < 0.002f);
    }
}

TEST_CASE("math.smoothMax.mountainFootprint") {
    // A user-authored paraboloid falls below the terrain before its footprint ends.
    for (int i = -100; i <= 100; ++i) {
        const float x        = static_cast<float>(i);
        const float terrain  = 3.f + std::sin(x * 0.05f);
        const float mountain = 25.f - 0.01f * x * x;
        const float height   = smoothMax(terrain, mountain, 4.f);
        CHECK(height >= terrain);
        if (std::abs(x) >= 60.f) CHECK_EQ(height, terrain);
        if (std::abs(x) <= 20.f) CHECK_EQ(height, mountain);
    }
}

TEST_CASE("math.smoothMax.extremeFiniteInputs") {
    const float largest = std::numeric_limits<float>::max();
    CHECK_EQ(smoothMax(largest, -largest, largest), largest);
    CHECK_EQ(smoothMax(-largest, largest, largest), largest);
    CHECK_EQ(smoothMax(0.f, 0.f, largest), largest * 0.25f);
    CHECK(std::isinf(smoothMax(largest, largest, largest)));
    CHECK_EQ(smoothMax(0.f, 1.f, std::numeric_limits<float>::min()), 1.f);
}
}  // namespace math_smooth_max
}  // namespace

// math_steering.cpp
namespace {
namespace math_steering {
namespace steering = eve::math::steering;

TEST_CASE("math.steering2.seekFleeAndArrive") {
    const auto seek = steering::seek(steering::Vector2{}, {3.f, 4.f}, 10.f);
    CHECK(std::abs(seek.x - 6.f) < .001f);
    CHECK(std::abs(seek.y - 8.f) < .001f);

    const auto flee = steering::flee(steering::Vector2{}, {3.f, 4.f}, 5.f);
    CHECK(flee.x < 0.f);
    CHECK(flee.y < 0.f);
    CHECK_EQ(steering::arrive(steering::Vector2{}, {.1f, 0.f}, 5.f, 4.f, .5f).x, 0.f);
}

TEST_CASE("math.steering3.seekFleeAndArrive") {
    const auto seek = steering::seek(steering::Vector3{}, {2.f, 3.f, 6.f}, 7.f);
    CHECK(std::abs(seek.x - 2.f) < .001f);
    CHECK(std::abs(seek.y - 3.f) < .001f);
    CHECK(std::abs(seek.z - 6.f) < .001f);

    const auto flee = steering::flee(steering::Vector3{}, {0.f, 0.f, 2.f}, 3.f);
    CHECK_EQ(flee.x, 0.f);
    CHECK_EQ(flee.y, 0.f);
    CHECK_EQ(flee.z, -3.f);
    CHECK_EQ(steering::arrive(steering::Vector3{}, {0.f, .1f, 0.f}, 5.f, 4.f, .5f).y, 0.f);
}

TEST_CASE("math.steering2.pathSeparationAndAvoidance") {
    const std::array points{steering::Vector2{0.f, 0.f}, steering::Vector2{5.f, 0.f}, steering::Vector2{10.f, 0.f}};
    CHECK_EQ(steering::pathTarget({}, points, 0, .2f), 1);

    const std::array neighbors{steering::Vector2{1.f, 0.f}, steering::Vector2{-3.f, 0.f}};
    const auto       separated = steering::separation({}, neighbors, 2.f, 4.f);
    CHECK(separated.x < 0.f);

    const auto avoided =
        steering::avoid(steering::Vector2{}, steering::Vector2{1.f, 0.f}, steering::Vector2{2.f, 0.f}, 1.f, 2.f, 3.f);
    CHECK_EQ(avoided.x, -3.f);
    CHECK_EQ(avoided.y, 0.f);
    const auto stationaryFallback =
        steering::avoid(steering::Vector2{}, steering::Vector2{}, steering::Vector2{}, 1.f, 2.f, 3.f);
    CHECK_EQ(stationaryFallback.x, 3.f);
    CHECK_EQ(stationaryFallback.y, 0.f);
}

TEST_CASE("math.steering3.pathSeparationAndAvoidance") {
    const std::array points{steering::Vector3{0.f, 0.f, 0.f}, steering::Vector3{0.f, 0.f, 5.f},
                            steering::Vector3{0.f, 0.f, 10.f}};
    CHECK_EQ(steering::pathTarget({}, points, 0, .2f), 1);

    const std::array neighbors{steering::Vector3{0.f, 0.f, 1.f}, steering::Vector3{0.f, 0.f, -3.f}};
    const auto       separated = steering::separation({}, neighbors, 2.f, 4.f);
    CHECK(separated.z < 0.f);

    const auto avoided = steering::avoid({}, {0.f, 0.f, 1.f}, {0.f, 0.f, 2.f}, 1.f, 2.f, 3.f);
    CHECK_EQ(avoided.x, 0.f);
    CHECK_EQ(avoided.y, 0.f);
    CHECK_EQ(avoided.z, -3.f);
    const auto stationaryFallback =
        steering::avoid(steering::Vector3{}, steering::Vector3{}, steering::Vector3{}, 1.f, 2.f, 3.f);
    CHECK_EQ(stationaryFallback.x, 3.f);
    CHECK_EQ(stationaryFallback.y, 0.f);
    CHECK_EQ(stationaryFallback.z, 0.f);
}

TEST_CASE("math.steering.invalidInputsReturnNeutral") {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    CHECK_EQ(steering::seek(steering::Vector2{}, {1.f, 1.f}, -1.f).x, 0.f);
    CHECK_EQ(steering::seek(steering::Vector3{}, {1.f, nan, 1.f}, 1.f).z, 0.f);
    CHECK_EQ(steering::arrive(steering::Vector2{}, {1.f, 1.f}, 1.f, 0.f, 2.f).x, 0.f);
    CHECK_EQ(steering::pathTarget(steering::Vector3{}, {}, 0, 1.f), -1);

    const std::array invalidCurrent2{steering::Vector2{nan, 0.f}, steering::Vector2{1.f, 0.f}};
    const std::array invalidNext2{steering::Vector2{0.f, 0.f}, steering::Vector2{nan, 0.f}};
    const std::array invalidCurrent3{steering::Vector3{0.f, nan, 0.f}, steering::Vector3{1.f, 0.f, 0.f}};
    const std::array invalidNext3{steering::Vector3{0.f, 0.f, 0.f}, steering::Vector3{0.f, 0.f, nan}};
    CHECK_EQ(steering::pathTarget(steering::Vector2{}, invalidCurrent2, 0, 1.f), -1);
    CHECK_EQ(steering::pathTarget(steering::Vector2{}, invalidNext2, 0, 1.f), -1);
    CHECK_EQ(steering::pathTarget(steering::Vector3{}, invalidCurrent3, 0, 1.f), -1);
    CHECK_EQ(steering::pathTarget(steering::Vector3{}, invalidNext3, 0, 1.f), -1);
}

TEST_CASE("math.steering.extremeFiniteCoordinatesRemainSteerable") {
    const float extent = std::numeric_limits<float>::max();

    const auto seek2   = steering::seek(steering::Vector2{-extent, 0.f}, {extent, 0.f}, 7.f);
    const auto flee2   = steering::flee(steering::Vector2{-extent, 0.f}, {extent, 0.f}, 7.f);
    const auto arrive2 = steering::arrive(steering::Vector2{-extent, 0.f}, {extent, 0.f}, 7.f, 4.f, 1.f);
    CHECK_EQ(seek2.x, 7.f);
    CHECK_EQ(flee2.x, -7.f);
    CHECK_EQ(arrive2.x, 7.f);

    const auto seek3   = steering::seek(steering::Vector3{-extent, 0.f, 0.f}, {extent, 0.f, 0.f}, 9.f);
    const auto flee3   = steering::flee(steering::Vector3{-extent, 0.f, 0.f}, {extent, 0.f, 0.f}, 9.f);
    const auto arrive3 = steering::arrive(steering::Vector3{-extent, 0.f, 0.f}, {extent, 0.f, 0.f}, 9.f, 4.f, 1.f);
    CHECK_EQ(seek3.x, 9.f);
    CHECK_EQ(flee3.x, -9.f);
    CHECK_EQ(arrive3.x, 9.f);
}

TEST_CASE("math.steering.scriptAdaptersExposeBothDimensions") {
    eve::math::Math                  math;
    std::unique_ptr<eve::math::Vec2> seek2(math.steeringSeek2(0.f, 0.f, 3.f, 4.f, 10.f));
    std::unique_ptr<eve::math::Vec3> seek3(math.steeringSeek3(0.f, 0.f, 0.f, 2.f, 3.f, 6.f, 7.f));
    CHECK(std::abs(seek2->getX() - 6.f) < .001f);
    CHECK(std::abs(seek3->getZ() - 6.f) < .001f);

    std::unique_ptr<eve::math::Vec3> separated(math.steeringSeparation3(0.f, 0.f, 0.f, "0:0:1,0:0:-3", 2.f, 4.f));
    CHECK(separated->getZ() < 0.f);
    CHECK_EQ(math.steeringPathTarget3(0.f, 0.f, 0.f, "0:0:0,0:0:5", 0, .2f), 1);
    CHECK_EQ(math.steeringPathTarget2(0.f, 0.f, "0:0:5,5:0", 0, .2f), 0);
    CHECK_EQ(math.steeringPathTarget3(0.f, 0.f, 0.f, "0:0:0junk,0:0:5", 0, .2f), 0);
}
}  // namespace math_steering
}  // namespace
}  // namespace test_math_interpolation_steering
#endif
