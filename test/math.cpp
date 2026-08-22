#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "math/Math.h"
#include "math/Vec2.h"
#include "math/Vec3.h"
#include "math/Mat4.h"
#include "common/Exception.h"

#include <cmath>
#include <memory>

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
    CHECK(std::fabs(m->closestPointOnSegment3Z(0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 0.f, 2.f) - 1.f) <
          1e-5f);
}

TEST_CASE("math.vec2.vec3.classes") {
    auto *m = Math::create();
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
    auto *m = Math::create();
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
    auto *m = Math::create();
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
