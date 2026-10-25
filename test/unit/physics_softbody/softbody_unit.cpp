#include "physics/cloth/Cloth.h"
#include "physics/cloth/Cloth3D.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <cmath>

using eve::physics::Cloth;
using eve::physics::Cloth3D;

namespace {

void advance(Cloth& cloth, int frames) {
    for (int i = 0; i < frames; ++i) cloth.update(1.f / 60.f);
}

void advance(Cloth3D& cloth, int frames) {
    for (int i = 0; i < frames; ++i) cloth.update(1.f / 60.f);
}

}  // namespace

TEST_CASE("Cloth2D grid and interaction contract") {
    SUB_CASE("grid layout reports origin spacing and fixed top row") {
        Cloth cloth(4, 3, 10.f, 50.f, 100.f);
        CHECK_EQ(cloth.getCols(), 4);
        CHECK_EQ(cloth.getRows(), 3);
        REQUIRE_EQ(cloth.getParticleCount(), 12);
        CHECK_EQ(cloth.getSpacing(), 10.f);
        CHECK_EQ(cloth.getParticleX(0), 50.f);
        CHECK_EQ(cloth.getParticleY(0), 100.f);
        CHECK_EQ(cloth.getParticleX(3), 80.f);
        CHECK_EQ(cloth.getParticleY(4), 110.f);
        for (int i = 0; i < 4; ++i) CHECK(cloth.isPinned(i));
        CHECK(!cloth.isPinned(4));
    };

    SUB_CASE("gravity moves free particles while pins preserve their position") {
        Cloth       cloth(6, 6, 12.f, 50.f, 50.f);
        const float pinX  = cloth.getParticleX(0);
        const float pinY  = cloth.getParticleY(0);
        const float freeY = cloth.getParticleY(20);
        advance(cloth, 45);
        CHECK(std::fabs(cloth.getParticleX(0) - pinX) < 0.01f);
        CHECK(std::fabs(cloth.getParticleY(0) - pinY) < 0.01f);
        CHECK(cloth.getParticleY(20) > freeY);

        cloth.unpin(0);
        CHECK(!cloth.isPinned(0));
        advance(cloth, 30);
        CHECK(cloth.getParticleY(0) > pinY);
        cloth.pinTopRow();
        CHECK(cloth.isPinned(0));
    };

    SUB_CASE("grabbing moves one particle and release clears interaction state") {
        Cloth       cloth(6, 6, 12.f, 50.f, 50.f);
        const int   index = 21;
        const float x = cloth.getParticleX(index), y = cloth.getParticleY(index);
        CHECK_EQ(cloth.grabAt(-1000.f, -1000.f, 5.f), -1);
        CHECK(!cloth.isGrabbing());
        CHECK_EQ(cloth.grabAt(x, y, 5.f), index);
        CHECK_EQ(cloth.getGrabIndex(), index);
        cloth.moveGrab(x + 40.f, y + 30.f);
        advance(cloth, 10);
        CHECK(std::fabs(cloth.getParticleX(index) - (x + 40.f)) < 1.f);
        CHECK(std::fabs(cloth.getParticleY(index) - (y + 30.f)) < 1.f);
        cloth.releaseGrab();
        CHECK(!cloth.isGrabbing());
        CHECK_EQ(cloth.getGrabIndex(), -1);
    };

    SUB_CASE("interaction force repels unpinned particles from the cursor") {
        Cloth cloth(6, 6, 12.f, 50.f, 50.f);
        for (int i = 0; i < cloth.getParticleCount(); ++i) cloth.unpin(i);
        cloth.setGravity(0.f, 0.f);
        const auto averageDistance = [&] {
            float sum = 0.f;
            for (int i = 0; i < cloth.getParticleCount(); ++i) {
                const float dx = cloth.getParticleX(i) - 86.f;
                const float dy = cloth.getParticleY(i) - 86.f;
                sum += std::sqrt(dx * dx + dy * dy);
            }
            return sum / cloth.getParticleCount();
        };
        const float before = averageDistance();
        for (int i = 0; i < 60; ++i) {
            cloth.interactAt(86.f, 86.f, 90.f, -3600.f);
            cloth.update(1.f / 60.f);
        }
        CHECK(averageDistance() > before + 4.f);
    };

    SUB_CASE("reset restores the constructed grid after deformation") {
        Cloth cloth(4, 3, 10.f, 50.f, 100.f);
        cloth.setParticlePosition(8, 100.f, 200.f);
        CHECK_EQ(cloth.getParticleX(8), 100.f);
        cloth.reset();
        CHECK_EQ(cloth.getParticleX(8), 50.f);
        CHECK_EQ(cloth.getParticleY(8), 120.f);
        CHECK(cloth.isPinned(0));
    };
}

TEST_CASE("Cloth3D grid and interaction contract") {
    SUB_CASE("grid layout is XZ with pinned first row") {
        Cloth3D cloth(4, 3, 0.5f, -1.f, 2.f, 3.f);
        REQUIRE_EQ(cloth.getParticleCount(), 12);
        CHECK_EQ(cloth.getParticleX(0), -1.f);
        CHECK_EQ(cloth.getParticleY(0), 2.f);
        CHECK_EQ(cloth.getParticleZ(0), 3.f);
        CHECK_EQ(cloth.getParticleX(3), 0.5f);
        CHECK_EQ(cloth.getParticleZ(4), 3.5f);
        for (int i = 0; i < 4; ++i) CHECK(cloth.isPinned(i));
        CHECK(!cloth.isPinned(4));
    };

    SUB_CASE("gravity leaves pins fixed and moves free rows downward") {
        Cloth3D     cloth(8, 6, 0.5f, 0.f, 3.f, 0.f);
        const float top = cloth.getParticleY(0);
        advance(cloth, 90);
        CHECK(std::fabs(cloth.getParticleY(0) - top) < 0.01f);
        CHECK(cloth.getParticleY(43) < top - 0.5f);
    };

    SUB_CASE("grab uses spatial radius then follows and releases target") {
        Cloth3D     cloth(6, 6, 0.5f, 0.f, 2.f, 0.f);
        const int   index = 21;
        const float x = cloth.getParticleX(index), y = cloth.getParticleY(index), z = cloth.getParticleZ(index);
        CHECK_EQ(cloth.grabAt(100.f, 100.f, 100.f, 0.1f), -1);
        CHECK_EQ(cloth.grabAt(x, y, z, 0.3f), index);
        cloth.moveGrab(x + 0.4f, y + 0.3f, z + 0.2f);
        advance(cloth, 10);
        CHECK(std::fabs(cloth.getParticleX(index) - x - 0.4f) < 0.02f);
        CHECK(std::fabs(cloth.getParticleY(index) - y - 0.3f) < 0.02f);
        CHECK(std::fabs(cloth.getParticleZ(index) - z - 0.2f) < 0.02f);
        cloth.releaseGrab();
        CHECK(!cloth.isGrabbing());
    };

    SUB_CASE("reset discards runtime displacement without changing origin") {
        Cloth3D cloth(3, 3, 0.5f, -1.f, 2.f, 3.f);
        cloth.setParticlePosition(8, 9.f, 8.f, 7.f);
        cloth.reset();
        CHECK_EQ(cloth.getParticleX(8), 0.f);
        CHECK_EQ(cloth.getParticleY(8), 2.f);
        CHECK_EQ(cloth.getParticleZ(8), 4.f);
    };
}

TEST_CASE("Cloth3D self collision separates unlinked particles only when enabled") {
    for (bool enabled : {false, true}) {
        Cloth3D cloth(4, 4, 0.5f, 0.f, 0.f, 0.f);
        cloth.setGravity(0.f, 0.f, 0.f);
        cloth.setStiffness(0.f);
        cloth.setFoldStiffness(0.f);
        cloth.setDamping(1.f);
        cloth.setSelfCollision(enabled);
        for (int i = 0; i < cloth.getParticleCount(); ++i) cloth.pin(i);
        const int moving = cloth.getParticleCount() - 1;
        cloth.unpin(moving);
        cloth.setParticlePosition(moving, 0.05f, 0.f, 0.f);
        advance(cloth, 240);
        const float x = cloth.getParticleX(moving), y = cloth.getParticleY(moving), z = cloth.getParticleZ(moving);
        const float distance = std::sqrt(x * x + y * y + z * z);
        if (enabled)
            REQUIRE(distance > cloth.getParticleSize());
        else
            CHECK(std::fabs(distance - 0.05f) < 1e-4f);
        CHECK_EQ(cloth.getParticleX(0), 0.f);
    }
}
