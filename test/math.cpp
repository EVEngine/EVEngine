#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "math/Math.h"
#include "math/Vec2.h"
#include "math/Vec3.h"
#include "math/Mat4.h"

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
