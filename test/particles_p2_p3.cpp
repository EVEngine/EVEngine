#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/ParticleSdf.h"
#include "graphics/Graphics.h"
#include "particles/ParticleEffect.h"
#include "particles/ParticleEmitter.h"
#include "particles/ParticleEmitterPool.h"
#include "particles/ParticleSystem.h"
#include "particles/Particles.h"
#include "window/Window.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using namespace eve::particles;

namespace {

class CircleSdf final : public eve::IParticleSdfField {
public:
    CircleSdf(float x, float y, float radius) : x_(x), y_(y), radius_(radius) {}

    float sample(float x, float y) const override {
        const float dx = x - x_;
        const float dy = y - y_;
        return std::sqrt(dx * dx + dy * dy) - radius_;
    }

    eve::ParticleSdfGradientStatus gradient(float x, float y, float& outNx, float& outNy) const override {
        const float dx = x - x_;
        const float dy = y - y_;
        const float len = std::sqrt(dx * dx + dy * dy);
        if (len < 1e-5f) {
            outNx = 0.f;
            outNy = 1.f;
            return eve::ParticleSdfGradientStatus::Defined;
        }
        outNx = dx / len;
        outNy = dy / len;
        return eve::ParticleSdfGradientStatus::Defined;
    }

private:
    float x_      = 0.f;
    float y_      = 0.f;
    float radius_ = 1.f;
};

eve::graphics::Texture* makeSoftDisc(eve::graphics::Graphics* gfx, int size) {
    std::vector<std::uint8_t> pixels(std::size_t(size * size) * 4u);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const float nx = (float(x) + 0.5f) / float(size) * 2.f - 1.f;
            const float ny = (float(y) + 0.5f) / float(size) * 2.f - 1.f;
            const float r  = std::sqrt(nx * nx + ny * ny);
            const float a  = std::clamp(1.f - r, 0.f, 1.f);
            auto*       px = &pixels[std::size_t(y * size + x) * 4u];
            const auto  v  = std::uint8_t(a * 255.f);
            px[0] = v;
            px[1] = v;
            px[2] = v;
            px[3] = v;
        }
    }
    return gfx->newTexture(size, size, pixels.data());
}

std::string readFile(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    REQUIRE(input.good());
    std::ostringstream text;
    text << input.rdbuf();
    return text.str();
}

float luma(const Color& color) { return color.r * 0.2126f + color.g * 0.7152f + color.b * 0.0722f; }

}  // namespace

TEST_CASE("particles.pool.acquireRecyclePreservesCapacity") {
    ParticleEmitterPool pool;
    auto first = pool.acquire(128);
    REQUIRE(first.ok());
    ParticleEmitter* a = first.value();
    REQUIRE(a != nullptr);
    CHECK_EQ(a->getBufferSize(), 128);
    a->emit(3);
    CHECK_EQ(a->getCount(), 3);
    REQUIRE(pool.recycle(a).ok());
    CHECK_EQ(pool.idleCount(), 1u);

    auto second = pool.acquire(64);
    REQUIRE(second.ok());
    CHECK_EQ(second.value(), a);
    CHECK_EQ(a->getCount(), 0);
    CHECK_EQ(pool.idleCount(), 0u);
    REQUIRE(pool.recycle(a).ok());
    pool.clear();
    CHECK_EQ(pool.idleCount(), 0u);
}

TEST_CASE("particles.sdf.circleFieldKillsInboundParticles") {
    auto* particles = Particles::create();
    REQUIRE(particles != nullptr);
    auto* emitter = particles->newEmitter(32);
    REQUIRE(emitter != nullptr);
    emitter->setPosition(0.f, 0.f);
    emitter->setEmissionRate(0.f);
    emitter->setParticleLifetime(2.f, 2.f);
    emitter->setSpeed(0.f, 0.f);
    emitter->setCollision("kill", 2.f);
    CircleSdf field(0.f, 0.f, 8.f);
    emitter->setSdfField(&field);
    CHECK(emitter->getSdfField() == &field);
    emitter->setGpuSimulation(true);
    CHECK(!emitter->isGpuFeatureSetSupported());
    CHECK_EQ(emitter->getGpuFallbackReason(), std::string("sdf"));
    emitter->emit(1);
    CHECK_EQ(emitter->getCount(), 1);
    particles->update(1.f / 60.f);
    CHECK_EQ(emitter->getCount(), 0);
    emitter->setSdfField(nullptr);
    emitter->release();
}

TEST_CASE("particles.motionVector.policyRecordedButInactive") {
    auto* emitter = Particles::create()->newEmitter(8);
    REQUIRE(emitter != nullptr);
    emitter->setMotionVectorPolicy("velocity");
    CHECK_EQ(emitter->getMotionVectorPolicy(), std::string("velocity"));
    CHECK(!emitter->isMotionVectorActive());
    emitter->setMotionVectorPolicy("spawn_delta");
    CHECK_EQ(emitter->getMotionVectorPolicy(), std::string("spawn_delta"));
    emitter->setMotionVectorPolicy("bogus");
    CHECK_EQ(emitter->getMotionVectorPolicy(), std::string("none"));
    emitter->release();
}

TEST_CASE("particles.reference.effectPackLoadsAndRenders") {
    auto* window = eve::window::Window::create();
    auto* gfx    = eve::graphics::Graphics::create();
    REQUIRE(window != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings settings;
    settings.width    = 960;
    settings.height   = 540;
    settings.centered = true;
    REQUIRE(window->setWindowSettings(settings));
    gfx->setScreenReadbackEnabled(true);

    auto* texture = makeSoftDisc(gfx, 48);
    REQUIRE(texture != nullptr);

    const std::string root = std::string(EVENGINE_SOURCE_DIR) + "/examples/particle-effects/";
    const char* names[]    = {"fire.effect.json", "smoke.effect.json", "impact.effect.json",
                              "trail.effect.json", "weather.effect.json"};
    std::vector<std::unique_ptr<ParticleEffect>> effects;
    constexpr float centersX[] = {120.f, 300.f, 480.f, 660.f, 840.f};
    for (int i = 0; i < 5; ++i) {
        auto parsed = ParticleEffect::tryFromText(readFile(root + names[i]), root + names[i]);
        REQUIRE(parsed.ok());
        effects.emplace_back(parsed.value());
        CHECK_EQ(effects.back()->getVersion(), 2);
        for (int e = 0; e < effects.back()->getEmitterCount(); ++e) {
            if (auto* emitter = effects.back()->getEmitter(e)) {
                emitter->setTexture(texture);
                emitter->setRandomSeed(20261001 + i * 17 + e);
                emitter->setAutoRandomSeed(false);
            }
        }
        effects.back()->setPosition(centersX[i], 270.f);
        effects.back()->setFloatParameter("intensity", 1.15f);
        effects.back()->start();
        if (std::string(names[i]) == "trail.effect.json") {
            // Distance emission needs motion to spawn ribbon control points.
            for (int step = 0; step < 12; ++step) {
                effects.back()->setPosition(centersX[i] + float(step) * 6.f, 270.f - float(step));
                Particles::create()->update(1.f / 60.f);
            }
        }
    }

    auto* particles = Particles::create();
    for (int frame = 0; frame < 24; ++frame) particles->update(1.f / 60.f);

    gfx->setBackgroundColorRGBA(0.015f, 0.02f, 0.035f, 1.f);
    for (int frame = 0; frame < 2; ++frame) {
        gfx->clearScreen();
        ParticleRenderSystem::render(gfx);
        gfx->present();
    }

    const std::string output = std::string(EVENGINE_TEST_BINARY_DIR) + "/particle_p2_reference_pack.png";
    CHECK(gfx->saveFramePng(output));
    CHECK(std::filesystem::exists(output));
    REQUIRE(std::filesystem::file_size(output) > std::uintmax_t(8000));

    float packPeak = 0.f;
    for (float x : centersX) {
        for (int y = 140; y <= 400; y += 8) {
            for (int sx = int(x) - 70; sx <= int(x) + 70; sx += 8) {
                packPeak = std::max(packPeak, luma(gfx->getPixel(sx, y)));
            }
        }
    }
    REQUIRE_GT(packPeak, 0.08f);

    for (auto& effect : effects) effect->stop();
    window->close();
}
