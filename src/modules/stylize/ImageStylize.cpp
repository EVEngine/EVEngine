#include "stylize/ImageStylize.h"

#include "common/Exception.h"
#include "image/ImageData.h"

#include <algorithm>
#include <cmath>

namespace eve::stylize {
namespace {

using eve::image::ImageData;
using medialoader::Colorf;

float clamp01(float x) { return std::max(0.f, std::min(1.f, x)); }
float mixf(float a, float b, float t) { return a + (b - a) * t; }
float luma(const Colorf &c) { return c.r * 0.299f + c.g * 0.587f + c.b * 0.114f; }

float hash21(int x, int y) {
    float n = std::sin(float(x) * 127.1f + float(y) * 311.7f) * 43758.5453f;
    return n - std::floor(n);
}

Colorf sample(const ImageData *img, int x, int y) {
    x = std::max(0, std::min(img->getWidth() - 1, x));
    y = std::max(0, std::min(img->getHeight() - 1, y));
    return img->getPixel(x, y);
}

float sobelAt(const ImageData *img, int x, int y) {
    auto L = [&](int dx, int dy) { return luma(sample(img, x + dx, y + dy)); };
    float gx = -L(-1, -1) - 2.f * L(-1, 0) - L(-1, 1) + L(1, -1) + 2.f * L(1, 0) + L(1, 1);
    float gy = -L(-1, -1) - 2.f * L(0, -1) - L(1, -1) + L(-1, 1) + 2.f * L(0, 1) + L(1, 1);
    return std::sqrt(gx * gx + gy * gy);
}

ImageData *ensureRgba8Clone(ImageData *src) {
    if (!src) throw eve::Exception("processImageCpu: null ImageData");
    if (src->getFormat() != "RGBA8")
        throw eve::Exception("processImageCpu: only RGBA8 supported (got %s)",
                             src->getFormat().c_str());
    return new ImageData(*src);
}

void processCartoon(ImageData *src, ImageData *dst) {
    const int w = src->getWidth();
    const int h = src->getHeight();
    constexpr float bands = 4.f;
    constexpr float posterize = 6.f;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            Colorf c = src->getPixel(x, y);
            float Y = luma(c);
            float cel = std::floor(Y * bands) / std::max(bands - 1.f, 1.f);
            float scale = (Y > 1e-4f) ? (cel / Y) : 1.f;
            c.r = clamp01(std::floor(c.r * scale * posterize + 0.5f) / posterize);
            c.g = clamp01(std::floor(c.g * scale * posterize + 0.5f) / posterize);
            c.b = clamp01(std::floor(c.b * scale * posterize + 0.5f) / posterize);
            float edge = sobelAt(src, x, y);
            float line = clamp01((edge - 0.35f) / 0.08f);
            c.r = mixf(c.r, 0.05f, line);
            c.g = mixf(c.g, 0.04f, line);
            c.b = mixf(c.b, 0.08f, line);
            dst->setPixel(x, y, c);
        }
    }
}

void processWatercolor(ImageData *src, ImageData *dst) {
    const int w = src->getWidth();
    const int h = src->getHeight();
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            Colorf acc{0, 0, 0, 0};
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    Colorf s = sample(src, x + dx, y + dy);
                    acc.r += s.r;
                    acc.g += s.g;
                    acc.b += s.b;
                    acc.a += s.a;
                }
            }
            acc.r /= 9.f;
            acc.g /= 9.f;
            acc.b /= 9.f;
            acc.a /= 9.f;
            Colorf sharp = src->getPixel(x, y);
            Colorf c;
            c.r = mixf(sharp.r, acc.r, 0.65f);
            c.g = mixf(sharp.g, acc.g, 0.65f);
            c.b = mixf(sharp.b, acc.b, 0.65f);
            c.a = sharp.a;
            float edge = sobelAt(src, x, y);
            float darken = 1.f - clamp01(edge * 1.4f) * 0.85f;
            c.r *= darken;
            c.g *= darken;
            c.b *= darken;
            float grain = mixf(0.85f, 1.15f, hash21(x, y));
            c.r = clamp01(c.r * grain);
            c.g = clamp01(c.g * grain * 0.98f);
            c.b = clamp01(c.b * grain * 0.94f);
            dst->setPixel(x, y, c);
        }
    }
}

void processInk(ImageData *src, ImageData *dst) {
    const int w = src->getWidth();
    const int h = src->getHeight();
    constexpr float levels = 5.f;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float Y = std::pow(clamp01(luma(src->getPixel(x, y))), 1.35f);
            float tone = std::floor(Y * levels) / std::max(levels - 1.f, 1.f);
            float edge = sobelAt(src, x, y);
            float ink = clamp01((1.f - tone) * 0.85f + clamp01((edge - 0.28f) / 0.15f));
            ink = clamp01(ink + (hash21(x * 3, y * 5) - 0.5f) * 0.08f);
            Colorf c;
            c.r = mixf(0.93f, 0.05f, ink);
            c.g = mixf(0.90f, 0.05f, ink);
            c.b = mixf(0.82f, 0.07f, ink);
            c.a = src->getPixel(x, y).a;
            dst->setPixel(x, y, c);
        }
    }
}

void processPixel(ImageData *src, ImageData *dst) {
    const int w = src->getWidth();
    const int h = src->getHeight();
    constexpr int pixelSize = 4;
    constexpr float steps = 8.f;
    static const int bayer[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            int sx = (x / pixelSize) * pixelSize + pixelSize / 2;
            int sy = (y / pixelSize) * pixelSize + pixelSize / 2;
            Colorf c = sample(src, sx, sy);
            int bi = (x & 3) + (y & 3) * 4;
            float d = (float(bayer[bi]) / 16.f - 0.5f) * 0.35f / steps;
            c.r = clamp01(std::floor(clamp01(c.r + d) * steps) / std::max(steps - 1.f, 1.f));
            c.g = clamp01(std::floor(clamp01(c.g + d) * steps) / std::max(steps - 1.f, 1.f));
            c.b = clamp01(std::floor(clamp01(c.b + d) * steps) / std::max(steps - 1.f, 1.f));
            dst->setPixel(x, y, c);
        }
    }
}

}  // namespace

image::ImageData *processImageCpu(image::ImageData *src, const std::string &style) {
    ImageData *dst = ensureRgba8Clone(src);
    try {
        if (style == "cartoon")
            processCartoon(src, dst);
        else if (style == "watercolor")
            processWatercolor(src, dst);
        else if (style == "ink")
            processInk(src, dst);
        else if (style == "pixel")
            processPixel(src, dst);
        else {
            delete dst;
            throw eve::Exception("processImageCpu: unknown style '%s'", style.c_str());
        }
    } catch (...) {
        delete dst;
        throw;
    }
    return dst;
}

}  // namespace eve::stylize
