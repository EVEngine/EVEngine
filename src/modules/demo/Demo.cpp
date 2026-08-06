#include "demo/Demo.h"

#include "common/Exception.h"
#include "graphics/Graphics.h"
#include "graphics/Texture.h"
#include "sound/SoundData.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace eve::demo {

Module_IMPL(Demo, new Demo());

Demo::Demo() = default;
Demo::~Demo() = default;

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = kPi * 2.f;
constexpr int kGenRate = 22050;

void writeS16(std::vector<uint8_t> &pcm, int sampleIndex, float sample) {
    if (sample > 1.f) sample = 1.f;
    if (sample < -1.f) sample = -1.f;
    const int16_t v = static_cast<int16_t>(sample * 32767.f);
    const size_t off = static_cast<size_t>(sampleIndex) * 2u;
    if (off + 1 >= pcm.size()) return;
    pcm[off] = static_cast<uint8_t>(v & 0xff);
    pcm[off + 1] = static_cast<uint8_t>((v >> 8) & 0xff);
}

float noteFreq(int semitoneFromA4) {
    return 440.f * std::pow(2.f, semitoneFromA4 / 12.f);
}

sound::SoundData *makeMusic() {
    const int samples = kGenRate * 4;
    std::vector<uint8_t> pcm(static_cast<size_t>(samples) * 2u, 0);
    const int pattern[] = {-12, -9, -5, -2, 0, -2, -5, -9};
    const int beatSamples = kGenRate / 4;
    for (int i = 0; i < samples; ++i) {
        const int beat = (i / beatSamples) % 8;
        const float freq = noteFreq(pattern[beat]);
        const float t = static_cast<float>(i) / static_cast<float>(kGenRate);
        const float phase = t * freq;
        float s = (std::fmod(phase, 1.f) < 0.5f) ? 0.18f : -0.18f;
        if (beat == 0 || beat == 4) {
            const float bf = noteFreq(pattern[0] - 12);
            s += 0.12f * std::sin(t * bf * 6.2831853f);
        }
        const float local = static_cast<float>(i % beatSamples) / static_cast<float>(beatSamples);
        const float env = (local < 0.08f) ? (local / 0.08f) : (1.f - (local - 0.08f) * 0.35f);
        writeS16(pcm, i, s * std::max(0.05f, env));
    }
    return new sound::SoundData(std::move(pcm), kGenRate, 16, 1);
}

sound::SoundData *makeShoot() {
    const int samples = static_cast<int>(kGenRate * 0.12f);
    std::vector<uint8_t> pcm(static_cast<size_t>(samples) * 2u, 0);
    for (int i = 0; i < samples; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(kGenRate);
        const float env = 1.f - t / 0.12f;
        const float freq = 880.f + 1200.f * env;
        writeS16(pcm, i, env * 0.35f * std::sin(t * freq * 6.2831853f));
    }
    return new sound::SoundData(std::move(pcm), kGenRate, 16, 1);
}

sound::SoundData *makeExplode() {
    const int samples = static_cast<int>(kGenRate * 0.35f);
    std::vector<uint8_t> pcm(static_cast<size_t>(samples) * 2u, 0);
    uint32_t rng = 0xC0FFEEu;
    for (int i = 0; i < samples; ++i) {
        rng = rng * 1664525u + 1013904223u;
        const float noise = (static_cast<float>(rng & 0xffffu) / 32768.f) - 1.f;
        const float t = static_cast<float>(i) / static_cast<float>(kGenRate);
        const float env = std::exp(-t * 8.f);
        const float boom = std::sin(t * 90.f * 6.2831853f) * std::exp(-t * 6.f);
        writeS16(pcm, i, env * (0.45f * noise + 0.35f * boom));
    }
    return new sound::SoundData(std::move(pcm), kGenRate, 16, 1);
}

sound::SoundData *makeHit() {
    const int samples = static_cast<int>(kGenRate * 0.08f);
    std::vector<uint8_t> pcm(static_cast<size_t>(samples) * 2u, 0);
    for (int i = 0; i < samples; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(kGenRate);
        const float env = 1.f - t / 0.08f;
        writeS16(pcm, i, env * 0.3f * std::sin(t * 220.f * 6.2831853f));
    }
    return new sound::SoundData(std::move(pcm), kGenRate, 16, 1);
}

float hash3(int x, int y, int z) {
    uint32_t n = uint32_t(x) * 374761393u + uint32_t(y) * 668265263u + uint32_t(z) * 2246822519u;
    n = (n ^ (n >> 13u)) * 1274126177u;
    return float(n & 0xffffu) / 65535.f;
}

float valueNoise3(float x, float y, float z) {
    const int x0 = int(std::floor(x));
    const int y0 = int(std::floor(y));
    const int z0 = int(std::floor(z));
    const float fx = x - float(x0);
    const float fy = y - float(y0);
    const float fz = z - float(z0);
    const float sx = fx * fx * (3.f - 2.f * fx);
    const float sy = fy * fy * (3.f - 2.f * fy);
    const float sz = fz * fz * (3.f - 2.f * fz);

    const float n000 = hash3(x0, y0, z0);
    const float n100 = hash3(x0 + 1, y0, z0);
    const float n010 = hash3(x0, y0 + 1, z0);
    const float n110 = hash3(x0 + 1, y0 + 1, z0);
    const float n001 = hash3(x0, y0, z0 + 1);
    const float n101 = hash3(x0 + 1, y0, z0 + 1);
    const float n011 = hash3(x0, y0 + 1, z0 + 1);
    const float n111 = hash3(x0 + 1, y0 + 1, z0 + 1);

    const float x00 = n000 + (n100 - n000) * sx;
    const float x10 = n010 + (n110 - n010) * sx;
    const float x01 = n001 + (n101 - n001) * sx;
    const float x11 = n011 + (n111 - n011) * sx;
    const float yz0 = x00 + (x10 - x00) * sy;
    const float yz1 = x01 + (x11 - x01) * sy;
    return yz0 + (yz1 - yz0) * sz;
}

float fbm3(float x, float y, float z) {
    float v = 0.f;
    float amp = 0.5f;
    float freq = 1.f;
    for (int i = 0; i < 5; ++i) {
        v += amp * valueNoise3(x * freq, y * freq, z * freq);
        freq *= 2.f;
        amp *= 0.5f;
    }
    return v;
}

float planetNoise(float u, float v, float freq) {
    const float phi = v * kPi;
    const float theta = u * kTwoPi;
    const float sp = std::sin(phi);
    const float px = sp * std::cos(theta) * freq;
    const float py = std::cos(phi) * freq;
    const float pz = sp * std::sin(theta) * freq;
    return fbm3(px, py, pz);
}

}  // namespace

sound::SoundData *Demo::newSound(const std::string &kind) {
    if (kind == "music") return makeMusic();
    if (kind == "shoot") return makeShoot();
    if (kind == "explode") return makeExplode();
    if (kind == "hit") return makeHit();
    throw eve::Exception("Demo.newSound: unknown kind (expected music/shoot/explode/hit)");
}

graphics::Texture *Demo::newPlanetTexture(graphics::Graphics *gfx) {
    if (!gfx) throw eve::Exception("Demo.newPlanetTexture: null Graphics");
    constexpr int W = 512;
    constexpr int H = 256;
    std::vector<uint8_t> px(size_t(W) * size_t(H) * 4u);
    for (int y = 0; y < H; ++y) {
        const float v = (float(y) + 0.5f) / float(H);
        const float lat = (v - 0.5f) * 2.f;
        for (int x = 0; x < W; ++x) {
            const float u = (float(x) + 0.5f) / float(W);
            float n = planetNoise(u, v, 3.0f);
            n = n * 0.82f + 0.18f * planetNoise(u + 0.13f, v, 7.0f);

            uint8_t r, g, b;
            const float absLat = std::abs(lat);
            if (absLat > 0.72f) {
                const float ice = (absLat - 0.72f) / 0.28f;
                const float grain = planetNoise(u, v, 14.f);
                r = uint8_t(160 + 50 * ice + 20 * grain);
                g = uint8_t(185 + 40 * ice + 15 * grain);
                b = uint8_t(210 + 35 * ice);
            } else if (n > 0.52f) {
                const float elev = (n - 0.52f) / 0.48f;
                r = uint8_t(40 + 80 * elev);
                g = uint8_t(110 + 70 * elev);
                b = uint8_t(45 + 40 * elev);
            } else {
                const float depth = n / 0.52f;
                r = uint8_t(10 + 25 * depth);
                g = uint8_t(40 + 70 * depth);
                b = uint8_t(110 + 90 * depth);
            }

            const float clouds = planetNoise(u + 0.27f, v, 5.5f);
            if (clouds > 0.62f && absLat < 0.68f) {
                const float c = (clouds - 0.62f) / 0.38f;
                r = uint8_t(r * (1.f - 0.45f * c) + 220 * 0.45f * c);
                g = uint8_t(g * (1.f - 0.45f * c) + 228 * 0.45f * c);
                b = uint8_t(b * (1.f - 0.45f * c) + 240 * 0.45f * c);
            }

            const size_t i = (size_t(y) * size_t(W) + size_t(x)) * 4u;
            px[i + 0] = r;
            px[i + 1] = g;
            px[i + 2] = b;
            px[i + 3] = 255;
        }
    }
    return gfx->newTexture(W, H, px.data(), true, false);
}

void Demo::expose(ssq::Table &table) {
    auto cls = table.addClass(name, Demo::create, false);
    expose(cls);
}

void Demo::expose(ssq::Class &cls) {
    cls.addFunc("getName", &Demo::getName);
    cls.addFunc("newSound", &Demo::newSound);
    cls.addFunc("newPlanetTexture", &Demo::newPlanetTexture);
}

}  // namespace eve::demo
