#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "graphics/AmbientOcclusion.h"
#include "graphics/AntiAliasing.h"
#include "graphics/Canvas.h"
#include "graphics/DrawItem2D.h"
#include "graphics/Font.h"
#include "graphics/GBuffer.h"
#include "graphics/GlobalIllumination.h"
#include "graphics/Graphics.h"
#include "graphics/Grass.h"
#include "graphics/Light.h"
#include "graphics/Material.h"
#include "graphics/Mesh.h"
#include "graphics/Outline.h"
#include "graphics/Quad.h"
#include "graphics/RenderControl.h"
#include "graphics/ScreenSpaceReflection.h"
#include "graphics/Shader.h"
#include "graphics/Texture.h"
#include "graphics/Volumetric.h"
#include "graphics/Water.h"
#include "graphics/Waterfall.h"
#include "physics/Body.h"
#include "physics/Body3D.h"
#include "physics/Cloth.h"
#include "physics/Cloth3D.h"
#include "physics/Fluid.h"
#include "physics/Physics.h"
#include "physics/World.h"
#include "physics/World3D.h"
#include "window/Window.h"

#include <SDL2/SDL.h>

#include <cmath>
#include <memory>

using namespace eve::physics;
using namespace eve::graphics;

TEST_CASE("softbody.cloth.fallsAndPinsHold") {
    auto *mod = Physics::create();
    std::unique_ptr<Cloth> cloth(mod->newCloth(8, 6, 10.f, 100.f, 40.f));
    REQUIRE(cloth.get() != nullptr);
    CHECK_EQ(cloth->getCols(), 8);
    CHECK_EQ(cloth->getRows(), 6);
    CHECK_EQ(cloth->getParticleCount(), 48);
    CHECK(cloth->isPinned(0));
    CHECK(cloth->isPinned(7));

    // Pinned top row holds under gravity.
    const float pinY0 = cloth->getParticleY(0);
    const float pinX0 = cloth->getParticleX(0);
    cloth->setBounds(0.f, 0.f, 800.f, 600.f);
    for (int i = 0; i < 60; ++i)
        cloth->update(1.f / 60.f);
    CHECK(std::fabs(cloth->getParticleY(0) - pinY0) < 0.5f);
    CHECK(std::fabs(cloth->getParticleX(0) - pinX0) < 0.5f);

    // Free-fall: unpin everything and verify gravity moves particles down.
    for (int i = 0; i < cloth->getParticleCount(); ++i)
        cloth->unpin(i);
    const float freeY0 = cloth->getParticleY(20);
    for (int i = 0; i < 45; ++i)
        cloth->update(1.f / 60.f);
    CHECK_GT(cloth->getParticleY(20), freeY0 + 20.f);
}

TEST_CASE("softbody.cloth.grabMovesParticle") {
    auto *mod = Physics::create();
    std::unique_ptr<Cloth> cloth(mod->newCloth(6, 6, 12.f, 50.f, 50.f));
    // Unpin a mid particle so it can be grabbed.
    const int idx = 3 * 6 + 3;
    cloth->unpin(idx);
    const float x = cloth->getParticleX(idx);
    const float y = cloth->getParticleY(idx);

    int grabbed = cloth->grabAt(x, y, 20.f);
    CHECK_EQ(grabbed, idx);
    CHECK(cloth->isGrabbing());
    cloth->moveGrab(x + 40.f, y + 30.f);
    for (int i = 0; i < 10; ++i)
        cloth->update(1.f / 60.f);

    CHECK(std::fabs(cloth->getParticleX(idx) - (x + 40.f)) < 1.f);
    CHECK(std::fabs(cloth->getParticleY(idx) - (y + 30.f)) < 1.f);
    cloth->releaseGrab();
    CHECK(!cloth->isGrabbing());
}

TEST_CASE("softbody.cloth.selfCollisionSeparates") {
    auto *mod = Physics::create();
    std::unique_ptr<Cloth> cloth(mod->newCloth(10, 8, 12.f, 20.f, 20.f));
    cloth->setGravity(0.f, 320.f);
    cloth->setBounds(0.f, 0.f, 600.f, 600.f);
    cloth->setParticleSize(3.f);
    cloth->setStiffness(0.9f);
    cloth->setMaxFoldAngle(120.f);
    const float minDist = cloth->getParticleSize() * 2.f;
    for (int f = 0; f < 240; ++f) {
        // Alternating wind + pointer repulsion folds the sheet onto itself.
        if ((f / 40) % 2 == 0)
            cloth->applyForce(260.f, 0.f);
        else
            cloth->applyForce(-260.f, 0.f);
        cloth->interactAt(300.f + float(f % 60) * 1.5f, 220.f, 90.f, -2600.f);
        cloth->update(1.f / 60.f);
    }
    // Non-adjacent particles must never interpenetrate beyond solver slack.
    float minSeen = 1e9f;
    for (int i = 0; i < cloth->getParticleCount(); ++i) {
        for (int j = i + 1; j < cloth->getParticleCount(); ++j) {
            const float dx = cloth->getParticleX(j) - cloth->getParticleX(i);
            const float dy = cloth->getParticleY(j) - cloth->getParticleY(i);
            minSeen = std::min(minSeen, std::sqrt(dx * dx + dy * dy));
        }
    }
    REQUIRE_GT(minSeen, minDist * 0.5f);
}

TEST_CASE("softbody.cloth.foldAngleLimited") {
    auto *mod = Physics::create();
    std::unique_ptr<Cloth> cloth(mod->newCloth(3, 3, 10.f, 0.f, 0.f));
    cloth->setGravity(0.f, 0.f);
    cloth->setMaxFoldAngle(30.f);
    cloth->setFoldStiffness(1.f);
    cloth->setDamping(0.2f);
    cloth->setSelfCollision(false);  // isolate the fold limit from proximity
    // Pin the left/right columns and fold the middle column sharply upward.
    for (int r = 0; r < 3; ++r) {
        cloth->pin(r * 3);
        cloth->pin(r * 3 + 2);
        cloth->unpin(r * 3 + 1);
        cloth->setParticlePosition(r * 3, 0.f, 0.f);
        cloth->setParticlePosition(r * 3 + 1, 10.f, -14.f);
        cloth->setParticlePosition(r * 3 + 2, 20.f, 0.f);
    }
    for (int i = 0; i < 120; ++i)
        cloth->update(1.f / 60.f);
    // The sharp -14 fold must be pushed back inside the 30° cone around the
    // straight line (|y| <= 10*tan(15°) ≈ 2.7, plus solver slack).
    REQUIRE_GT(cloth->getParticleY(4), -5.f);
    REQUIRE_LT(cloth->getParticleY(4), 5.f);
    REQUIRE_GT(cloth->getParticleX(4), 9.5f);
    REQUIRE_LT(cloth->getParticleX(4), 11.0f);
}

TEST_CASE("softbody.cloth.collidesWithWorld") {
    auto *mod = Physics::create();
    std::unique_ptr<World> world(mod->newWorld(0.f, 0.f, true));
    auto *ground = world->newBody("static", 400.f, 390.f);
    REQUIRE(ground != nullptr);
    ground->newRectangleFixture(800.f, 20.f);

    std::unique_ptr<Cloth> cloth(mod->newCloth(6, 6, 10.f, 300.f, 60.f));
    for (int i = 0; i < cloth->getParticleCount(); ++i)
        cloth->unpin(i);
    cloth->setGravity(0.f, 600.f);
    cloth->setCollideWorld(world.get());
    const float surfaceY = 400.f;
    for (int i = 0; i < 120; ++i)
        cloth->update(1.f / 60.f);
    for (int i = 0; i < cloth->getParticleCount(); ++i)
        REQUIRE_LE(cloth->getParticleY(i), surfaceY + cloth->getParticleSize() + 1.f);
    // Sanity: the sheet actually fell onto the box top.
    REQUIRE_GT(cloth->getParticleY(5 * 6 + 3), 300.f);
}

TEST_CASE("softbody.cloth.interactAtRepels") {
    auto *mod = Physics::create();
    std::unique_ptr<Cloth> cloth(mod->newCloth(6, 6, 12.f, 50.f, 50.f));
    for (int i = 0; i < cloth->getParticleCount(); ++i)
        cloth->unpin(i);
    cloth->setGravity(0.f, 0.f);
    float avg0 = 0.f;
    for (int i = 0; i < cloth->getParticleCount(); ++i) {
        const float dx = cloth->getParticleX(i) - 86.f;
        const float dy = cloth->getParticleY(i) - 86.f;
        avg0 += std::sqrt(dx * dx + dy * dy);
    }
    avg0 /= float(cloth->getParticleCount());
    for (int i = 0; i < 60; ++i) {
        cloth->interactAt(86.f, 86.f, 90.f, -3600.f);
        cloth->update(1.f / 60.f);
    }
    float avg1 = 0.f;
    for (int i = 0; i < cloth->getParticleCount(); ++i) {
        const float dx = cloth->getParticleX(i) - 86.f;
        const float dy = cloth->getParticleY(i) - 86.f;
        avg1 += std::sqrt(dx * dx + dy * dy);
    }
    avg1 /= float(cloth->getParticleCount());
    REQUIRE_GT(avg1, avg0 + 4.f);
}

TEST_CASE("softbody.cloth.momentumExchangeScalesWithMass") {
    auto *mod = Physics::create();
    const auto runCase = [&](float density) {
        std::unique_ptr<World> world(mod->newWorld(0.f, 0.f, true));
        auto *box = world->newBody("dynamic", 400.f, 120.f);
        REQUIRE(box != nullptr);
        box->newRectangleFixture(40.f, 40.f, density, 0.2f, 0.f);
        std::unique_ptr<Cloth> cloth(mod->newCloth(6, 4, 10.f, 340.f, 20.f));
        for (int i = 0; i < cloth->getParticleCount(); ++i)
            cloth->unpin(i);
        cloth->setGravity(0.f, 600.f);
        cloth->setCollideWorld(world.get());
        for (int i = 0; i < 120; ++i) {
            world->update(1.f / 60.f);
            cloth->update(1.f / 60.f);
        }
        return box->getLinearVelocityY();
    };

    const float vLight = runCase(0.05f);
    const float vHeavy = runCase(5.f);
    // Same cloth impact must push the light body much harder than the heavy one.
    REQUIRE_GT(vLight, 30.f);
    REQUIRE_GT(vLight, vHeavy + 20.f);
}

TEST_CASE("softbody.cloth3d.pinsHoldUnderGravity") {
    auto *mod = Physics::create();
    std::unique_ptr<Cloth3D> cloth(mod->newCloth3D(8, 6, 0.5f, 0.f, 3.f, 0.f));
    REQUIRE(cloth.get() != nullptr);
    REQUIRE_EQ(cloth->getParticleCount(), 48);
    REQUIRE(cloth->isPinned(0));
    REQUIRE(cloth->isPinned(7));
    const float y0 = cloth->getParticleY(0);
    for (int i = 0; i < 90; ++i)
        cloth->update(1.f / 60.f);
    REQUIRE(std::fabs(cloth->getParticleY(0) - y0) < 0.01f);
    REQUIRE_LT(cloth->getParticleY(5 * 8 + 3), y0 - 0.5f);
}

TEST_CASE("softbody.cloth3d.grabMovesParticle") {
    auto *mod = Physics::create();
    std::unique_ptr<Cloth3D> cloth(mod->newCloth3D(6, 6, 0.5f, 0.f, 2.f, 0.f));
    const int idx = 3 * 6 + 3;
    cloth->unpin(idx);
    const float x = cloth->getParticleX(idx);
    const float y = cloth->getParticleY(idx);
    const float z = cloth->getParticleZ(idx);
    int grabbed = cloth->grabAt(x, y, z, 0.3f);
    REQUIRE_EQ(grabbed, idx);
    REQUIRE(cloth->isGrabbing());
    cloth->moveGrab(x + 0.4f, y + 0.3f, z + 0.2f);
    for (int i = 0; i < 10; ++i)
        cloth->update(1.f / 60.f);
    REQUIRE(std::fabs(cloth->getParticleX(idx) - (x + 0.4f)) < 0.02f);
    REQUIRE(std::fabs(cloth->getParticleY(idx) - (y + 0.3f)) < 0.02f);
    REQUIRE(std::fabs(cloth->getParticleZ(idx) - (z + 0.2f)) < 0.02f);
    cloth->releaseGrab();
    REQUIRE(!cloth->isGrabbing());
}

TEST_CASE("softbody.cloth3d.selfCollisionSeparates") {
    auto *mod = Physics::create();
    std::unique_ptr<Cloth3D> cloth(mod->newCloth3D(10, 8, 0.5f, -1.f, 2.f, -1.f));
    cloth->setGravity(0.f, -2.5f, 0.f);
    cloth->setBounds(-1.f, -2.f, -1.f, 6.f, 6.f, 6.f);
    cloth->setStiffness(0.9f);
    cloth->setMaxFoldAngle(130.f);
    const float minDist = cloth->getParticleSize() * 2.f;
    for (int f = 0; f < 240; ++f) {
        if ((f / 40) % 2 == 0)
            cloth->applyForce(2.f, 0.f, 0.f);
        else
            cloth->applyForce(-2.f, 0.f, 0.f);
        cloth->interactAt(float(f % 60) * 0.04f, 1.2f, 0.f, 90.f, -30.f);
        cloth->update(1.f / 60.f);
    }
    float minSeen = 1e9f;
    for (int i = 0; i < cloth->getParticleCount(); ++i) {
        for (int j = i + 1; j < cloth->getParticleCount(); ++j) {
            const float dx = cloth->getParticleX(j) - cloth->getParticleX(i);
            const float dy = cloth->getParticleY(j) - cloth->getParticleY(i);
            const float dz = cloth->getParticleZ(j) - cloth->getParticleZ(i);
            minSeen = std::min(minSeen, std::sqrt(dx * dx + dy * dy + dz * dz));
        }
    }
    REQUIRE_GT(minSeen, minDist * 0.5f);
}

TEST_CASE("softbody.cloth3d.foldAngleLimited") {
    auto *mod = Physics::create();
    std::unique_ptr<Cloth3D> cloth(mod->newCloth3D(3, 3, 0.5f, 0.f, 2.f, 0.f));
    cloth->setGravity(0.f, 0.f, 0.f);
    cloth->setMaxFoldAngle(30.f);
    cloth->setFoldStiffness(1.f);
    cloth->setSelfCollision(false);  // isolate the fold limit from proximity
    for (int c = 0; c < 3; ++c) {
        cloth->setParticlePosition(c, float(c) * 0.5f, 2.4f, 0.f);
        cloth->setParticlePosition(3 + c, float(c) * 0.5f, 1.6f, 0.f);
        cloth->setParticlePosition(6 + c, float(c) * 0.5f, 2.4f, 0.f);
    }
    for (int i = 0; i < 120; ++i)
        cloth->update(1.f / 60.f);
    // The folded middle row should be pushed back toward the flat pose (y = 2.4).
    REQUIRE_GT(cloth->getParticleY(4), 2.0f);
}

TEST_CASE("softbody.cloth3d.collidesWithWorld3D") {
    auto *mod = Physics::create();
    std::unique_ptr<World3D> world3(mod->newWorld3D(0.f, -9.8f, 0.f, true));
    auto *ground = world3->newBody("static", 0.f, -0.5f, 0.f);
    REQUIRE(ground != nullptr);
    ground->newBoxShape(8.f, 1.f, 8.f);

    std::unique_ptr<Cloth3D> cloth(mod->newCloth3D(8, 6, 0.5f, 0.f, 4.f, 0.f));
    for (int i = 0; i < cloth->getParticleCount(); ++i)
        cloth->unpin(i);
    cloth->setGravity(0.f, -6.f, 0.f);
    cloth->setCollideWorld(world3.get());
    const float surfaceY = 0.f;
    for (int i = 0; i < 180; ++i)
        cloth->update(1.f / 60.f);
    for (int i = 0; i < cloth->getParticleCount(); ++i)
        REQUIRE_GE(cloth->getParticleY(i), surfaceY - cloth->getParticleSize() - 0.05f);
    // Sanity: the sheet fell from y = 4 toward the ground.
    REQUIRE_LT(cloth->getParticleY(5 * 8 + 3), 3.5f);
}

TEST_CASE("softbody.cloth3d.momentumExchangeScalesWithMass") {
    auto *mod = Physics::create();
    const auto runCase = [&](float density) {
        std::unique_ptr<World3D> world3(mod->newWorld3D(0.f, 0.f, 0.f, true));
        auto *box = world3->newBody("dynamic", 0.f, 1.2f, 0.f);
        REQUIRE(box != nullptr);
        box->newBoxShape(1.f, 0.8f, 1.f, density, 0.2f, 0.f);
        std::unique_ptr<Cloth3D> cloth(mod->newCloth3D(6, 4, 0.4f, -0.6f, 3.f, -0.6f));
        for (int i = 0; i < cloth->getParticleCount(); ++i)
            cloth->unpin(i);
        cloth->setGravity(0.f, -5.f, 0.f);
        cloth->setCollideWorld(world3.get());
        for (int i = 0; i < 180; ++i) {
            world3->update(1.f / 60.f);
            cloth->update(1.f / 60.f);
        }
        return box->getLinearVelocityY();
    };

    const float vLight = runCase(0.05f);
    const float vHeavy = runCase(5.f);
    // +Y up: the light box is pushed downward noticeably, the heavy one barely.
    REQUIRE_LT(vLight, -0.2f);
    REQUIRE_LT(vLight, vHeavy - 0.1f);
}

namespace {

float testClosestPointTriangle(float ax, float ay, float az, float bx, float by, float bz,
                               float cx, float cy, float cz, float px, float py, float pz,
                               float &qx, float &qy, float &qz) {
    // Mirrors Cloth3D's Ericson closest-point-on-triangle (test-only).
    const float abx = bx - ax, aby = by - ay, abz = bz - az;
    const float acx = cx - ax, acy = cy - ay, acz = cz - az;
    const float apx = px - ax, apy = py - ay, apz = pz - az;
    const float d1 = abx * apx + aby * apy + abz * apz;
    const float d2 = acx * apx + acy * apy + acz * apz;
    if (d1 <= 0.f && d2 <= 0.f) {
        qx = ax; qy = ay; qz = az;
        return std::sqrt((px - ax) * (px - ax) + (py - ay) * (py - ay) +
                         (pz - az) * (pz - az));
    }
    const float bpx = px - bx, bpy = py - by, bpz = pz - bz;
    const float d3 = abx * bpx + aby * bpy + abz * bpz;
    const float d4 = acx * bpx + acy * bpy + acz * bpz;
    if (d3 >= 0.f && d4 <= d3) {
        qx = bx; qy = by; qz = bz;
        return std::sqrt((px - bx) * (px - bx) + (py - by) * (py - by) +
                         (pz - bz) * (pz - bz));
    }
    const float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.f && d1 >= 0.f && d3 <= 0.f) {
        const float t = d1 / (d1 - d3);
        qx = ax + abx * t; qy = ay + aby * t; qz = az + abz * t;
        return std::sqrt((px - qx) * (px - qx) + (py - qy) * (py - qy) +
                         (pz - qz) * (pz - qz));
    }
    const float cpx = px - cx, cpy = py - cy, cpz = pz - cz;
    const float d5 = abx * cpx + aby * cpy + abz * cpz;
    const float d6 = acx * cpx + acy * cpy + acz * cpz;
    if (d6 >= 0.f && d5 <= d6) {
        qx = cx; qy = cy; qz = cz;
        return std::sqrt((px - cx) * (px - cx) + (py - cy) * (py - cy) +
                         (pz - cz) * (pz - cz));
    }
    const float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.f && d2 >= 0.f && d6 <= 0.f) {
        const float t = d2 / (d2 - d6);
        qx = ax + acx * t; qy = ay + acy * t; qz = az + acz * t;
        return std::sqrt((px - qx) * (px - qx) + (py - qy) * (py - qy) +
                         (pz - qz) * (pz - qz));
    }
    const float va = d3 * d6 - d5 * d4;
    if (va <= 0.f && (d4 - d3) >= 0.f && (d5 - d6) >= 0.f) {
        const float t = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        qx = bx + (cx - bx) * t; qy = by + (cy - by) * t; qz = bz + (cz - bz) * t;
        return std::sqrt((px - qx) * (px - qx) + (py - qy) * (py - qy) +
                         (pz - qz) * (pz - qz));
    }
    const float denom = 1.f / (va + vb + vc);
    const float v = vb * denom;
    const float w = vc * denom;
    qx = ax + abx * v + acx * w;
    qy = ay + aby * v + acy * w;
    qz = az + abz * v + acz * w;
    return std::sqrt((px - qx) * (px - qx) + (py - qy) * (py - qy) +
                     (pz - qz) * (pz - qz));
}

}  // namespace

TEST_CASE("softbody.cloth3d.triangleSelfCollision") {
    auto *mod = Physics::create();
    std::unique_ptr<Cloth3D> cloth(mod->newCloth3D(10, 8, 0.5f, -2.f, 2.f, -2.f));
    cloth->setGravity(0.f, -3.f, 0.f);
    cloth->setBounds(-2.f, -3.f, -2.f, 8.f, 8.f, 8.f);
    cloth->setStiffness(0.5f);
    // Let the sheet fold onto itself: no fold resistance, only self-collision.
    cloth->setMaxFoldAngle(175.f);
    cloth->setFoldStiffness(0.f);
    cloth->setParticleSize(0.12f);
    const float thickness = cloth->getParticleSize() * 2.f;

    for (int f = 0; f < 240; ++f) {
        if ((f / 40) % 2 == 0)
            cloth->applyForce(1.5f, 0.f, 0.f);
        else
            cloth->applyForce(-1.5f, 0.f, 0.f);
        // Pinch the sheet upward through itself: strong attraction above the sheet.
        cloth->interactAt(0.f, 3.2f, 0.f, 3.5f, 80.f);
        cloth->update(1.f / 60.f);
    }

    // Rebuild the quad triangles (same winding as Cloth3D) and verify that
    // non-adjacent triangles keep at least half the cloth thickness apart.
    const int cols = cloth->getCols();
    const int rows = cloth->getRows();
    std::vector<int> triVerts;
    auto pushTri = [&](int a, int b, int c) {
        triVerts.push_back(a);
        triVerts.push_back(b);
        triVerts.push_back(c);
    };
    for (int r = 0; r + 1 < rows; ++r) {
        for (int c = 0; c + 1 < cols; ++c) {
            const int a = r * cols + c;
            const int b = r * cols + c + 1;
            const int cc = (r + 1) * cols + c + 1;
            const int d = (r + 1) * cols + c;
            pushTri(a, d, b);
            pushTri(d, cc, b);
        }
    }
    const int triCount = static_cast<int>(triVerts.size()) / 3;
    auto sharesVertex = [&](int t0, int t1) {
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                if (triVerts[t0 * 3 + i] == triVerts[t1 * 3 + j]) return true;
        return false;
    };
    const auto vx = [&](int vi) { return cloth->getParticleX(vi); };
    const auto vy = [&](int vi) { return cloth->getParticleY(vi); };
    const auto vz = [&](int vi) { return cloth->getParticleZ(vi); };

    float minTriDist = 1e9f;
    for (int t0 = 0; t0 < triCount; ++t0) {
        for (int t1 = t0 + 1; t1 < triCount; ++t1) {
            if (sharesVertex(t0, t1)) continue;
            for (int side = 0; side < 2; ++side) {
                const int ta = side == 0 ? t0 : t1;
                const int tb = side == 0 ? t1 : t0;
                for (int k = 0; k < 3; ++k) {
                    float qx, qy, qz;
                    const float d = testClosestPointTriangle(
                        vx(triVerts[tb * 3 + 0]), vy(triVerts[tb * 3 + 0]),
                        vz(triVerts[tb * 3 + 0]), vx(triVerts[tb * 3 + 1]),
                        vy(triVerts[tb * 3 + 1]), vz(triVerts[tb * 3 + 1]),
                        vx(triVerts[tb * 3 + 2]), vy(triVerts[tb * 3 + 2]),
                        vz(triVerts[tb * 3 + 2]), vx(triVerts[ta * 3 + k]),
                        vy(triVerts[ta * 3 + k]), vz(triVerts[ta * 3 + k]), qx, qy, qz);
                    minTriDist = std::min(minTriDist, d);
                }
            }
        }
    }
    REQUIRE_GT(minTriDist, thickness * 0.5f);
}

TEST_CASE("softbody.fluid.emitAndSettle") {
    auto *mod = Physics::create();
    std::unique_ptr<Fluid> fluid(mod->newFluid(256));
    REQUIRE(fluid.get() != nullptr);
    fluid->setBounds(0.f, 0.f, 400.f, 400.f);
    fluid->setGravity(0.f, 900.f);
    int added = fluid->emit(200.f, 80.f, 40, 0.f, 0.f);
    CHECK_EQ(added, 40);
    CHECK_EQ(fluid->getParticleCount(), 40);

    float y0 = 0.f;
    for (int i = 0; i < fluid->getParticleCount(); ++i)
        y0 += fluid->getParticleY(i);
    y0 /= float(fluid->getParticleCount());

    for (int i = 0; i < 120; ++i)
        fluid->update(1.f / 60.f);

    float y1 = 0.f;
    for (int i = 0; i < fluid->getParticleCount(); ++i)
        y1 += fluid->getParticleY(i);
    y1 /= float(fluid->getParticleCount());

    CHECK_GT(y1, y0 + 10.f);
    // Stay inside container.
    for (int i = 0; i < fluid->getParticleCount(); ++i) {
        CHECK_GE(fluid->getParticleX(i), -1.f);
        CHECK_LE(fluid->getParticleX(i), 401.f);
        CHECK_GE(fluid->getParticleY(i), -1.f);
        CHECK_LE(fluid->getParticleY(i), 401.f);
    }
}

TEST_CASE("softbody.fluid.interactRepels") {
    auto *mod = Physics::create();
    std::unique_ptr<Fluid> fluid(mod->newFluid(128));
    fluid->setBounds(0.f, 0.f, 300.f, 300.f);
    fluid->setGravity(0.f, 0.f);
    fluid->emit(150.f, 150.f, 25, 0.f, 0.f);

    float avgDist0 = 0.f;
    for (int i = 0; i < fluid->getParticleCount(); ++i) {
        const float dx = fluid->getParticleX(i) - 150.f;
        const float dy = fluid->getParticleY(i) - 150.f;
        avgDist0 += std::sqrt(dx * dx + dy * dy);
    }
    avgDist0 /= float(fluid->getParticleCount());

    for (int i = 0; i < 45; ++i) {
        fluid->interactAt(150.f, 150.f, 80.f, -4000.f);
        fluid->update(1.f / 60.f);
    }

    float avgDist1 = 0.f;
    for (int i = 0; i < fluid->getParticleCount(); ++i) {
        const float dx = fluid->getParticleX(i) - 150.f;
        const float dy = fluid->getParticleY(i) - 150.f;
        avgDist1 += std::sqrt(dx * dx + dy * dy);
    }
    avgDist1 /= float(fluid->getParticleCount());
    CHECK_GT(avgDist1, avgDist0 + 2.f);
}

TEST_CASE("softbody.render.clothAndFluidPreview") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings s;
    s.width = 720;
    s.height = 420;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));

    auto *mod = Physics::create();
    std::unique_ptr<Cloth> cloth(mod->newCloth(14, 10, 12.f, 40.f, 30.f));
    cloth->setBounds(0.f, 0.f, 720.f, 420.f);
    cloth->setColor(0.78f, 0.84f, 0.98f, 1.f);

    std::unique_ptr<Fluid> fluid(mod->newFluid(400));
    fluid->setBounds(360.f, 40.f, 320.f, 340.f);
    fluid->setGravity(0.f, 980.f);
    fluid->setColor(0.2f, 0.55f, 0.95f, 0.9f);
    fluid->emit(520.f, 90.f, 120, 0.f, 40.f);

    gfx->setBackgroundColorRGBA(0.07f, 0.08f, 0.11f, 1.f);
    for (int frame = 0; frame < 90; ++frame) {
        if (frame == 30)
            cloth->grabAt(cloth->getParticleX(5 * 14 + 7), cloth->getParticleY(5 * 14 + 7), 30.f);
        if (frame >= 30 && frame < 60)
            cloth->moveGrab(180.f + float(frame - 30) * 2.f, 220.f);
        if (frame == 60)
            cloth->releaseGrab();
        if ((frame % 15) == 0)
            fluid->emit(500.f, 70.f, 8, 20.f, 60.f);
        fluid->interactAt(520.f, 200.f, 70.f, -2500.f);

        cloth->update(1.f / 60.f);
        fluid->update(1.f / 60.f);

        gfx->clearScreen();
        // Fluid tank outline.
        gfx->drawSolidRectRGBA(360.f, 40.f, 320.f, 340.f, 0.12f, 0.14f, 0.18f, 1.f);
        cloth->draw(gfx);
        fluid->draw(gfx);
        gfx->present();

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) break;
        }
        SDL_Delay(16);
    }

    CHECK_GT(cloth->getParticleY(5 * 14 + 7), 40.f);
    CHECK_GT(fluid->getParticleCount(), 100);
    win->close();
}

TEST_CASE("softbody.render.cloth3DPreview") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings s;
    s.width = 720;
    s.height = 420;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));

    auto *mod = Physics::create();
    std::unique_ptr<Cloth3D> cloth(mod->newCloth3D(14, 10, 0.4f, -1.5f, 2.5f, -1.f));
    cloth->setColor(0.78f, 0.84f, 0.98f, 1.f);
    cloth->setFoldStiffness(0.7f);

    gfx->setBackgroundColorRGBA(0.07f, 0.08f, 0.11f, 1.f);
    for (int frame = 0; frame < 45; ++frame) {
        if (frame == 15)
            cloth->grabAt(cloth->getParticleX(5 * 14 + 7), cloth->getParticleY(5 * 14 + 7),
                          cloth->getParticleZ(5 * 14 + 7), 0.4f);
        if (frame >= 15 && frame < 30)
            cloth->moveGrab(0.8f, 1.2f, -0.4f);
        if (frame == 30)
            cloth->releaseGrab();
        cloth->applyForce(0.8f, 0.f, 0.f);
        cloth->update(1.f / 60.f);

        gfx->begin3DFrame();
        cloth->draw(gfx);
        gfx->present();

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) break;
        }
        SDL_Delay(8);
    }

    REQUIRE_LT(cloth->getParticleY(5 * 14 + 7), 2.2f);
    win->close();
}
