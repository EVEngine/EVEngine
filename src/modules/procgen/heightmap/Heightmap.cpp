#include "procgen/heightmap/Heightmap.h"

#include <algorithm>
#include <cmath>

namespace eve::procgen {

Heightmap::Heightmap(int width, int height) { resize(width, height); }

void Heightmap::resize(int width, int height) {
    width_  = width > 0 ? width : 0;
    height_ = height > 0 ? height : 0;
    data_.assign(size_t(width_) * size_t(height_), 0.f);
}

int Heightmap::getWidth() const { return width_; }
int Heightmap::getHeight() const { return height_; }

bool Heightmap::inBounds(int x, int y) const {
    return x >= 0 && y >= 0 && x < width_ && y < height_;
}

void Heightmap::setHeight(int x, int y, float h) {
    if (!inBounds(x, y)) return;
    data_[size_t(y * width_ + x)] = h;
}

float Heightmap::height(int x, int y) const {
    if (!inBounds(x, y)) return 0.f;
    return data_[size_t(y * width_ + x)];
}

namespace {
float lerpHeight(float a, float b, float t) { return a + (b - a) * t; }
}  // namespace

float Heightmap::sampleBilinear(float x, float y) const {
    if (width_ <= 0 || height_ <= 0) return 0.f;
    const float fx = std::clamp(x, 0.f, float(width_ - 1));
    const float fy = std::clamp(y, 0.f, float(height_ - 1));
    const int   x0 = int(std::floor(fx));
    const int   y0 = int(std::floor(fy));
    const int   x1 = std::min(x0 + 1, width_ - 1);
    const int   y1 = std::min(y0 + 1, height_ - 1);
    const float tx  = fx - float(x0);
    const float ty  = fy - float(y0);
    const float top = lerpHeight(data_[size_t(y0 * width_ + x0)], data_[size_t(y0 * width_ + x1)], tx);
    const float bot = lerpHeight(data_[size_t(y1 * width_ + x0)], data_[size_t(y1 * width_ + x1)], tx);
    return lerpHeight(top, bot, ty);
}

float Heightmap::sampleBilinearSeamless(float x, float y) const {
    if (width_ <= 0 || height_ <= 0) return 0.f;
    float px = std::fmod(x, float(width_));
    float py = std::fmod(y, float(height_));
    if (px < 0.f) px += float(width_);
    if (py < 0.f) py += float(height_);
    const int   x0 = int(std::floor(px));
    const int   y0 = int(std::floor(py));
    const int   x1 = (x0 + 1) % width_;
    const int   y1 = (y0 + 1) % height_;
    const float tx  = px - float(x0);
    const float ty  = py - float(y0);
    const float top = lerpHeight(data_[size_t(y0 * width_ + x0)], data_[size_t(y0 * width_ + x1)], tx);
    const float bot = lerpHeight(data_[size_t(y1 * width_ + x0)], data_[size_t(y1 * width_ + x1)], tx);
    return lerpHeight(top, bot, ty);
}

Heightmap Heightmap::generate(const TerrainSampler &sampler, int width, int height) {
    TerrainSampler local = sampler;
    if (local.getWorldWidth() <= 0 || local.getWorldHeight() <= 0) {
        local.setWorldSize(width, height);
    }
    Heightmap hm(width, height);
    const auto fn = local.asFunction();
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            hm.setHeight(x, y, fn(float(x) + 0.5f, float(y) + 0.5f));
        }
    }
    return hm;
}

bool Heightmap::toGrid(Grid2D &out, const TerrainBands &bands) const {
    if (width_ <= 0 || height_ <= 0) return false;
    out.resize(width_, height_);
    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            out.setCell(x, y, int(bands.semanticAt(data_[size_t(y * width_ + x)])));
        }
    }
    return true;
}

}  // namespace eve::procgen
