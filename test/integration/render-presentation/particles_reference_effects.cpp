#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "graphics/Graphics.h"
#include "particles/ParticleEmitter.h"
#include "particles/ParticleSystem.h"
#include "particles/Particles.h"
#include "window/Window.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

using namespace eve::particles;

namespace {

eve::graphics::Texture* makeVfxTexture(eve::graphics::Graphics* gfx, int size, bool ring) {
    std::vector<std::uint8_t> pixels(std::size_t(size * size) * 4u);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const float nx    = (float(x) + 0.5f) / float(size) * 2.f - 1.f;
            const float ny    = (float(y) + 0.5f) / float(size) * 2.f - 1.f;
            const float r     = std::sqrt(nx * nx + ny * ny);
            const float a     = ring ? std::clamp(1.f - std::abs(r - 0.68f) * 13.f, 0.f, 1.f)
                                     : std::clamp(1.f - r, 0.f, 1.f) * std::clamp(1.4f - r, 0.f, 1.f);
            auto*       pixel = &pixels[std::size_t(y * size + x) * 4u];
            const auto  value = std::uint8_t(a * 255.f);
            pixel[0]          = value;
            pixel[1]          = value;
            pixel[2]          = value;
            pixel[3]          = value;
        }
    }
    return gfx->newTexture(size, size, pixels.data());
}

ParticleEmitter* makeLayer(eve::graphics::Texture* texture, float x, float y, int capacity) {
    auto* emitter = Particles::create()->newEmitter(capacity);
    emitter->setTexture(texture);
    emitter->setPosition(x, y);
    emitter->setEmissionRate(0.f);
    emitter->setRandomSeed(20260826 + int(x));
    emitter->setAutoRandomSeed(false);
    return emitter;
}

struct Palette {
    float r;
    float g;
    float b;
};

void makeCompositeBurst(eve::graphics::Texture* glow, eve::graphics::Texture* ring, float x, float y, float direction,
                        const Palette& color, std::vector<ParticleEmitter*>& emitters) {
    auto* core = makeLayer(glow, x, y, 4);
    core->setParticleLifetime(2.f, 2.f);
    core->setParticleSize(42.f, 42.f);
    core->setSpeed(0.f, 0.f);
    core->setBlendMode("additive");
    core->setColorStart(color.r, color.g, color.b, 0.95f);
    core->setColorEnd(color.r, color.g, color.b, 0.95f);
    core->emit(1);
    emitters.push_back(core);

    auto* shockwave = makeLayer(ring, x, y, 4);
    shockwave->setParticleLifetime(2.f, 2.f);
    shockwave->setParticleSize(176.f, 176.f);
    shockwave->setSpeed(0.f, 0.f);
    shockwave->setBlendMode("additive");
    shockwave->setColorStart(color.r, color.g, color.b, 0.82f);
    shockwave->setColorEnd(color.r, color.g, color.b, 0.82f);
    shockwave->emit(1);
    emitters.push_back(shockwave);

    auto* sparks = makeLayer(glow, x, y, 96);
    sparks->setParticleLifetime(0.75f, 1.15f);
    sparks->setParticleSize(7.f, 24.f);
    sparks->setSizes(1.f, 0.15f);
    sparks->setDirection(direction);
    sparks->setSpread(0.72f);
    sparks->setSpeed(150.f, 285.f);
    sparks->setDamping(0.12f);
    sparks->setRenderMode("velocity", 0.075f);
    sparks->setBlendMode("additive");
    sparks->setColorStart(1.f, 0.95f, 0.72f, 1.f);
    sparks->setColorEnd(color.r, color.g, color.b, 0.f);
    sparks->emit(48);
    emitters.push_back(sparks);

    auto* smoke = makeLayer(glow, x - std::cos(direction) * 18.f, y - std::sin(direction) * 18.f, 48);
    smoke->setParticleLifetime(1.2f, 1.9f);
    smoke->setParticleSize(18.f, 18.f);
    smoke->setSizes(0.65f, 1.2f);
    smoke->setSizeVariation(0.35f);
    smoke->setDirection(direction + 3.14159265f);
    smoke->setSpread(1.15f);
    smoke->setSpeed(18.f, 58.f);
    smoke->setDamping(0.3f);
    smoke->setBlendMode("additive");
    smoke->setColorStart(color.r * 0.18f, color.g * 0.16f, color.b * 0.22f, 0.22f);
    smoke->setColorEnd(0.02f, 0.025f, 0.055f, 0.f);
    smoke->emit(12);
    emitters.push_back(smoke);
}

float luma(const Color& color) { return color.r * 0.2126f + color.g * 0.7152f + color.b * 0.0722f; }

}  // namespace

TEST_CASE("particles.reference.compositedBurstsRenderAtMultipleAngles") {
    auto* window = eve::window::Window::create();
    auto* gfx    = eve::graphics::Graphics::create();
    REQUIRE(window != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings settings;
    settings.width    = 960;
    settings.height   = 420;
    settings.centered = true;
    REQUIRE(window->setWindowSettings(settings));
    gfx->setScreenReadbackEnabled(true);

    auto* glow = makeVfxTexture(gfx, 64, false);
    auto* ring = makeVfxTexture(gfx, 64, true);
    REQUIRE(glow != nullptr);
    REQUIRE(ring != nullptr);

    constexpr std::array<float, 3>   centers  = {170.f, 480.f, 790.f};
    constexpr std::array<float, 3>   angles   = {0.f, 0.78539816f, 1.57079633f};
    constexpr std::array<Palette, 3> palettes = {
        Palette{0.08f, 0.78f, 1.f},
        Palette{1.f, 0.28f, 0.06f},
        Palette{0.62f, 0.16f, 1.f},
    };
    std::vector<ParticleEmitter*> emitters;
    for (std::size_t i = 0; i < centers.size(); ++i)
        makeCompositeBurst(glow, ring, centers[i], 210.f, angles[i], palettes[i], emitters);

    for (int frame = 0; frame < 18; ++frame) ParticleSimSystem::update(1.f / 60.f);

    gfx->setBackgroundColorRGBA(0.006f, 0.009f, 0.024f, 1.f);
    for (int frame = 0; frame < 3; ++frame) {
        gfx->clearScreen();
        for (std::size_t i = 0; i < centers.size(); ++i) {
            gfx->drawSolidRect(centers[i] - 142.f, 40.f, 284.f, 340.f, Color(0.f, 0.f, 0.f, 1.f));
            gfx->drawSolidRect(centers[i] - 142.f, 40.f, 284.f, 2.f,
                               Color(palettes[i].r, palettes[i].g, palettes[i].b, 0.8f));
        }
        ParticleRenderSystem::render(gfx);
        gfx->present();
    }

    const std::string output = std::string(EVENGINE_TEST_BINARY_DIR) + "/particle_composite_reference_angles.png";
    CHECK(gfx->saveFramePng(output));
    CHECK(std::filesystem::exists(output));
    REQUIRE(std::filesystem::file_size(output) > std::uintmax_t(12000));

    for (std::size_t i = 0; i < centers.size(); ++i) {
        float peakLuma   = 0.f;
        float peakChroma = 0.f;
        for (int y = 120; y <= 300; y += 4) {
            for (int x = int(centers[i]) - 110; x <= int(centers[i]) + 110; x += 4) {
                const Color sample = gfx->getPixel(x, y);
                peakLuma           = std::max(peakLuma, luma(sample));
                peakChroma         = std::max(
                    peakChroma, std::max({sample.r, sample.g, sample.b}) - std::min({sample.r, sample.g, sample.b}));
            }
        }
        REQUIRE_GT(peakLuma, 0.3f);
        REQUIRE_GT(peakChroma, 0.12f);
    }

    for (auto* emitter : emitters) emitter->release();
    window->close();
}
