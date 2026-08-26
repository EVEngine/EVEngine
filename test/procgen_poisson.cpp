#include "procgen/PointSet.h"
#include "procgen/Procgen.h"

#include <cmath>
#include <vector>

#include <zeroerr.hpp>

using namespace eve::procgen;

TEST_CASE("procgen.poisson.deterministicAndSeeded") {
    Procgen proc;

    PointSet* a = proc.poissonDisk(40, 30, 2.5f, 99, 300);
    PointSet* b = proc.poissonDisk(40, 30, 2.5f, 99, 300);
    PointSet* c = proc.poissonDisk(40, 30, 2.5f, 100, 300);
    REQUIRE(bool(a));
    REQUIRE(bool(b));
    REQUIRE(bool(c));
    CHECK_EQ(a->getCount(), b->getCount());
    CHECK(a->getCount() > 0);
    for (int i = 0; i < a->getCount(); ++i) {
        CHECK_EQ(a->getX(i), b->getX(i));
        CHECK_EQ(a->getZ(i), b->getZ(i));
    }

    delete c;
    delete b;
    delete a;
}

TEST_CASE("procgen.poisson.respectsMinSpacing") {
    Procgen  proc;
    PointSet* points = proc.poissonDisk(50, 50, 2.f, 7, 400);
    REQUIRE(bool(points));
    CHECK(points->getCount() > 1);

    const float minDist = 1.9f;  // allow a little float slack
    for (int i = 0; i < points->getCount(); ++i) {
        for (int j = i + 1; j < points->getCount(); ++j) {
            const float dx = points->getX(i) - points->getX(j);
            const float dz = points->getZ(i) - points->getZ(j);
            const float d  = std::sqrt(dx * dx + dz * dz);
            CHECK(d >= minDist);
        }
    }
    delete points;
}

TEST_CASE("procgen.poisson.maxPointsCapsOutput") {
    Procgen  proc;
    PointSet* capped = proc.poissonDisk(100, 100, 0.5f, 3, 50);
    REQUIRE(bool(capped));
    CHECK(capped->getCount() <= 50);
    delete capped;

    PointSet* tiny = proc.poissonDisk(0, 0, 1.f, 3, 100);
    REQUIRE(bool(tiny));
    CHECK_EQ(tiny->getCount(), 0);
    delete tiny;
}

TEST_CASE("procgen.poisson.allPointsInsideArea") {
    Procgen  proc;
    PointSet* points = proc.poissonDisk(20, 20, 1.5f, 11, 200);
    REQUIRE(bool(points));
    for (int i = 0; i < points->getCount(); ++i) {
        CHECK(points->getX(i) >= 0.f);
        CHECK(points->getX(i) <= 20.f);
        CHECK(points->getZ(i) >= 0.f);
        CHECK(points->getZ(i) <= 20.f);
        CHECK_EQ(points->getY(i), 0.f);
    }
    delete points;
}