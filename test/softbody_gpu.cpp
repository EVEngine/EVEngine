#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "gpgpu/ComputeShader.h"
#include "gpgpu/Gpgpu.h"
#include "gpgpu/GpuBuffer.h"
#include "graphics/Canvas.h"
#include "graphics/Graphics.h"
#include "physics/ClothGPU.h"
#include "physics/Physics.h"
#include "window/Window.h"

#include <SDL2/SDL.h>

#include <cmath>
#include <memory>

using namespace eve::physics;
using namespace eve::gpgpu;

namespace {

bool tryInitGpuWindow() {
    auto *win = eve::window::Window::create();
    auto *gfx = eve::graphics::Graphics::create();
    if (!win || !gfx) return false;
    eve::window::WindowSettings s;
    s.width = 320;
    s.height = 240;
    s.centered = true;
    return win->setWindowSettings(s);
}

std::unique_ptr<ClothGPU> tryMakeCloth(eve::gpgpu::Gpgpu *gpgpu, int cols, int rows,
                                       float spacing, float ox, float oy) {
    try {
        return std::unique_ptr<ClothGPU>(new ClothGPU(gpgpu, cols, rows, spacing, ox, oy));
    } catch (...) {
        return nullptr;  // no compute compiler / backend — skip GPU path
    }
}

}  // namespace

TEST_CASE("softbody.gpu.clothFallsAndPinsHold") {
    if (!tryInitGpuWindow()) return;
    auto *mod = Physics::create();
    std::unique_ptr<ClothGPU> cloth;
    try {
        cloth = std::unique_ptr<ClothGPU>(mod->newClothGPU(8, 6, 10.f, 100.f, 40.f));
    } catch (...) {
        return;  // no shader compiler — skip
    }
    REQUIRE(cloth.get() != nullptr);
    REQUIRE_EQ(cloth->getParticleCount(), 48);
    REQUIRE(cloth->isPinned(0));
    REQUIRE(cloth->isPinned(7));
    const float pinX0 = cloth->getParticleX(0);
    const float pinY0 = cloth->getParticleY(0);

    // Pinned top row holds under gravity.
    for (int i = 0; i < 60; ++i)
        cloth->update(1.f / 60.f);
    REQUIRE(std::fabs(cloth->getParticleX(0) - pinX0) < 0.5f);
    REQUIRE(std::fabs(cloth->getParticleY(0) - pinY0) < 0.5f);

    // Unpin everything → free fall, like the CPU cloth test.
    for (int i = 0; i < cloth->getParticleCount(); ++i)
        cloth->unpin(i);
    const float freeY0 = cloth->getParticleY(20);
    for (int i = 0; i < 45; ++i)
        cloth->update(1.f / 60.f);
    REQUIRE_GT(cloth->getParticleY(20), freeY0 + 20.f);
    for (int i = 0; i < cloth->getParticleCount(); ++i) {
        REQUIRE(std::isfinite(cloth->getParticleX(i)));
        REQUIRE(std::isfinite(cloth->getParticleY(i)));
    }
}

TEST_CASE("softbody.gpu.clothBoundsAndInteract") {
    if (!tryInitGpuWindow()) return;
    auto *gpgpu = Gpgpu::create();
    REQUIRE(gpgpu != nullptr);
    if (!gpgpu->isAvailable()) return;

    std::unique_ptr<ClothGPU> cloth = tryMakeCloth(gpgpu, 6, 6, 12.f, 50.f, 50.f);
    if (!cloth) return;
    REQUIRE_EQ(cloth->getParticleCount(), 36);
    for (int i = 0; i < cloth->getParticleCount(); ++i)
        cloth->unpin(i);
    cloth->setGravity(0.f, 0.f);
    cloth->setBounds(0.f, 0.f, 300.f, 300.f);

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
    for (int i = 0; i < cloth->getParticleCount(); ++i) {
        REQUIRE_GE(cloth->getParticleX(i), -1.f);
        REQUIRE_LE(cloth->getParticleX(i), 301.f);
        REQUIRE_GE(cloth->getParticleY(i), -1.f);
        REQUIRE_LE(cloth->getParticleY(i), 301.f);
    }
}

TEST_CASE("softbody.gpu.clothSelfCollisionSeparates") {
    if (!tryInitGpuWindow()) return;
    auto *gpgpu = Gpgpu::create();
    REQUIRE(gpgpu != nullptr);
    if (!gpgpu->isAvailable()) return;

    auto *mod = Physics::create();
    std::unique_ptr<ClothGPU> cloth;
    try {
        cloth = std::unique_ptr<ClothGPU>(mod->newClothGPU(10, 8, 12.f, 20.f, 20.f));
    } catch (...) {
        return;
    }
    REQUIRE(cloth.get() != nullptr);
    for (int i = 0; i < cloth->getParticleCount(); ++i)
        cloth->unpin(i);
    cloth->setGravity(0.f, 320.f);
    cloth->setBounds(0.f, 0.f, 600.f, 600.f);
    cloth->setParticleSize(3.f);
    cloth->setStiffness(0.9f);
    cloth->setSelfCollision(true);
    REQUIRE(cloth->getSelfCollision());
    const float minDist = cloth->getParticleSize() * 2.f;

    // Alternating wind + pointer repulsion folds the sheet onto itself.
    for (int f = 0; f < 240; ++f) {
        if ((f / 40) % 2 == 0)
            cloth->applyForce(260.f, 0.f);
        else
            cloth->applyForce(-260.f, 0.f);
        cloth->interactAt(300.f + float(f % 60) * 1.5f, 220.f, 90.f, -2600.f);
        cloth->update(1.f / 60.f);
    }

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

TEST_CASE("softbody.gpu.clothLargeSelfCollision") {
    if (!tryInitGpuWindow()) return;
    auto *gpgpu = Gpgpu::create();
    REQUIRE(gpgpu != nullptr);
    if (!gpgpu->isAvailable()) return;

    auto *mod = Physics::create();
    std::unique_ptr<ClothGPU> cloth;
    try {
        cloth = std::unique_ptr<ClothGPU>(mod->newClothGPU(100, 80, 12.f, 20.f, 20.f));
    } catch (...) {
        return;
    }
    REQUIRE(cloth.get() != nullptr);
    REQUIRE_EQ(cloth->getParticleCount(), 8000);
    // Keep the top row pinned so the 8000-particle sheet hangs like a curtain
    // under gravity. This exercises the spatially-hashed self-collision at
    // scale and verifies solver stability, not perfect separation in a pile.
    cloth->setGravity(0.f, 180.f);
    cloth->setBounds(0.f, 0.f, 1200.f, 1000.f);
    cloth->setParticleSize(3.f);
    cloth->setStiffness(0.9f);
    cloth->setIterations(24);
    cloth->setDamping(0.3f);
    cloth->setSelfCollision(true);

    for (int f = 0; f < 120; ++f) {
        cloth->update(1.f / 60.f);
    }

    for (int i = 0; i < 200; ++i) {
        const int idx = (i * 79) % cloth->getParticleCount();
        REQUIRE(std::isfinite(cloth->getParticleX(idx)));
        REQUIRE(std::isfinite(cloth->getParticleY(idx)));
    }
    // The upper half of the hanging sheet stays near its rest column (x = 620)
    // and rest heights — the solver is stable at 8000 particles.
    for (int r = 1; r <= 40; r += 3) {
        const int idx = r * 100 + 50;
        REQUIRE(std::fabs(cloth->getParticleX(idx) - 620.f) < 30.f);
        REQUIRE(std::fabs(cloth->getParticleY(idx) - (20.f + float(r) * 12.f)) < 15.f);
    }
    // Everything stays inside the bounds.
    for (int i = 0; i < 400; ++i) {
        const int idx = (i * 131) % cloth->getParticleCount();
        REQUIRE_GE(cloth->getParticleX(idx), 0.f);
        REQUIRE_LE(cloth->getParticleX(idx), 1200.f);
        REQUIRE_GE(cloth->getParticleY(idx), 0.f);
        REQUIRE_LE(cloth->getParticleY(idx), 1000.f);
    }
}

TEST_CASE("softbody.gpu.renderClothPreview") {
    if (!tryInitGpuWindow()) return;
    auto *gpgpu = Gpgpu::create();
    REQUIRE(gpgpu != nullptr);
    if (!gpgpu->isAvailable()) return;

    auto *gfx = eve::graphics::Graphics::create();
    REQUIRE(gfx != nullptr);
    std::unique_ptr<ClothGPU> cloth = tryMakeCloth(gpgpu, 14, 10, 12.f, 40.f, 30.f);
    if (!cloth) return;
    cloth->setBounds(0.f, 0.f, 320.f, 240.f);
    cloth->setColor(0.78f, 0.84f, 0.98f, 1.f);

    gfx->setBackgroundColorRGBA(0.07f, 0.08f, 0.11f, 1.f);
    for (int frame = 0; frame < 45; ++frame) {
        cloth->applyForce(40.f, 0.f);
        cloth->update(1.f / 60.f);
        gfx->clearScreen();
        cloth->draw(gfx);
        gfx->present();
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) break;
        }
        SDL_Delay(4);
    }
    REQUIRE_GT(cloth->getParticleY(5 * 14 + 7), 40.f);
}
