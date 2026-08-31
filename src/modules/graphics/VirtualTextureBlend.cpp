#include "graphics/VirtualTextureBlend.h"

#include "image/ImageData.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <unordered_set>

namespace eve::graphics {
namespace {

using Color = image::ImageData::Colorf;

eve::Diagnostic invalid(std::string message) {
    return eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, std::move(message));
}

float wrap01(float value) {
    value -= std::floor(value);
    return value < 0.f ? value + 1.f : value;
}

Color sampleRepeat(const image::ImageData& image, float u, float v) {
    const int   width  = image.getWidth();
    const int   height = image.getHeight();
    const float x      = wrap01(u) * float(width) - 0.5f;
    const float y      = wrap01(v) * float(height) - 0.5f;
    const int   x0     = int(std::floor(x));
    const int   y0     = int(std::floor(y));
    const float tx     = x - float(x0);
    const float ty     = y - float(y0);
    auto        pixel  = [&](int px, int py) {
        px = (px % width + width) % width;
        py = (py % height + height) % height;
        return image.getPixel(px, py);
    };
    const Color a   = pixel(x0, y0);
    const Color b   = pixel(x0 + 1, y0);
    const Color c   = pixel(x0, y0 + 1);
    const Color d   = pixel(x0 + 1, y0 + 1);
    const auto  mix = [tx, ty](float av, float bv, float cv, float dv) {
        return (av + (bv - av) * tx) + ((cv + (dv - cv) * tx) - (av + (bv - av) * tx)) * ty;
    };
    Color out{mix(a.r, b.r, c.r, d.r), mix(a.g, b.g, c.g, d.g), mix(a.b, b.b, c.b, d.b), mix(a.a, b.a, c.a, d.a)};
    return out;
}

std::shared_ptr<const image::ImageData> copyImage(const image::ImageData& source) {
    return std::make_shared<image::ImageData>(source);
}

}  // namespace

eve::Result<std::unique_ptr<VirtualTextureBlend>> VirtualTextureBlend::create(const VirtualTextureBlendConfig& config) {
    if (config.width <= 0 || config.height <= 0)
        return eve::Result<std::unique_ptr<VirtualTextureBlend>>::failure(
            invalid("virtual texture dimensions must be positive"));
    if (config.tileSize < 4 || config.tileSize > 1024)
        return eve::Result<std::unique_ptr<VirtualTextureBlend>>::failure(
            invalid("virtual texture tile size must be in [4, 1024]"));
    if (config.borderSize < 0 || config.borderSize * 2 >= config.tileSize)
        return eve::Result<std::unique_ptr<VirtualTextureBlend>>::failure(
            invalid("virtual texture border must be non-negative and smaller than half a tile"));
    if (config.residentPageCapacity == 0)
        return eve::Result<std::unique_ptr<VirtualTextureBlend>>::failure(
            invalid("virtual texture resident page capacity must be positive"));
    return eve::Result<std::unique_ptr<VirtualTextureBlend>>::success(
        std::unique_ptr<VirtualTextureBlend>(new VirtualTextureBlend(config)));
}

eve::Result<std::uint32_t> VirtualTextureBlend::addLayer(const VirtualTextureBlendLayer& layer) {
    if (!layer.albedo) return eve::Result<std::uint32_t>::failure(invalid("virtual texture layer requires albedo"));
    if (layer.albedo->getWidth() <= 0 || layer.albedo->getHeight() <= 0 || layer.albedo->getFormat() != "RGBA8")
        return eve::Result<std::uint32_t>::failure(invalid("layer albedo must be non-empty RGBA8"));
    if (layer.normal && layer.normal->getFormat() != "RGBA8")
        return eve::Result<std::uint32_t>::failure(invalid("layer normal must be RGBA8"));
    if (layer.weight && layer.weight->getFormat() != "RGBA8")
        return eve::Result<std::uint32_t>::failure(invalid("layer weight must be RGBA8"));
    if (!std::isfinite(layer.uvScale) || layer.uvScale <= 0.f || !std::isfinite(layer.constantWeight) ||
        layer.constantWeight < 0.f || !std::isfinite(layer.heightContrast) || layer.heightContrast < 0.f)
        return eve::Result<std::uint32_t>::failure(invalid("layer blend parameters are invalid"));
    if (layers_.size() >= 16)
        return eve::Result<std::uint32_t>::failure(invalid("virtual texture supports at most 16 layers"));

    VirtualTextureBlendLayer owned = layer;
    owned.albedo                   = copyImage(*layer.albedo);
    if (layer.normal) owned.normal = copyImage(*layer.normal);
    if (layer.weight) owned.weight = copyImage(*layer.weight);
    layers_.push_back(std::move(owned));
    invalidate();
    return eve::Result<std::uint32_t>::success(std::uint32_t(layers_.size() - 1));
}

std::size_t VirtualTextureBlend::PageKeyHash::operator()(const PageKey& key) const noexcept {
    std::size_t seed = std::hash<int>{}(key.x);
    seed ^= std::hash<int>{}(key.y) + 0x9e3779b9u + (seed << 6u) + (seed >> 2u);
    seed ^= std::hash<int>{}(key.mip) + 0x9e3779b9u + (seed << 6u) + (seed >> 2u);
    seed ^= std::hash<int>{}(int(key.attribute)) + 0x9e3779b9u + (seed << 6u) + (seed >> 2u);
    return seed;
}

int VirtualTextureBlend::pageCountX(int mipLevel) const {
    if (mipLevel < 0 || mipLevel > 30) return 0;
    const int width = std::max(1, config_.width >> mipLevel);
    return (width + config_.tileSize - 1) / config_.tileSize;
}

int VirtualTextureBlend::pageCountY(int mipLevel) const {
    if (mipLevel < 0 || mipLevel > 30) return 0;
    const int height = std::max(1, config_.height >> mipLevel);
    return (height + config_.tileSize - 1) / config_.tileSize;
}

eve::Result<std::shared_ptr<const image::ImageData>> VirtualTextureBlend::requestAlbedoPage(int pageX, int pageY,
                                                                                            int mipLevel) {
    return requestPage({pageX, pageY, mipLevel, Attribute::Albedo});
}

eve::Result<std::shared_ptr<const image::ImageData>> VirtualTextureBlend::requestNormalPage(int pageX, int pageY,
                                                                                            int mipLevel) {
    return requestPage({pageX, pageY, mipLevel, Attribute::Normal});
}

eve::Result<std::shared_ptr<const image::ImageData>> VirtualTextureBlend::requestPage(PageKey key) {
    if (layers_.empty())
        return eve::Result<std::shared_ptr<const image::ImageData>>::failure(
            invalid("virtual texture has no material layers"));
    if (key.mip < 0 || key.mip > 30 || key.x < 0 || key.y < 0 || key.x >= pageCountX(key.mip) ||
        key.y >= pageCountY(key.mip))
        return eve::Result<std::shared_ptr<const image::ImageData>>::failure(
            invalid("virtual texture page coordinate is outside the mip"));
    auto found = cache_.find(key);
    if (found != cache_.end()) {
        ++hits_;
        lru_.splice(lru_.begin(), lru_, found->second.lru);
        return eve::Result<std::shared_ptr<const image::ImageData>>::success(found->second.image);
    }

    ++misses_;
    auto page = composePage(key);
    if (cache_.size() == config_.residentPageCapacity) {
        cache_.erase(lru_.back());
        lru_.pop_back();
        ++evictions_;
    }
    lru_.push_front(key);
    cache_.emplace(key, CacheEntry{page, lru_.begin()});
    return eve::Result<std::shared_ptr<const image::ImageData>>::success(std::move(page));
}

std::shared_ptr<const image::ImageData> VirtualTextureBlend::composePage(const PageKey& key) const {
    const int          extent    = config_.tileSize + config_.borderSize * 2;
    auto               page      = std::make_shared<image::ImageData>(extent, extent, "RGBA8");
    const int          mipWidth  = std::max(1, config_.width >> key.mip);
    const int          mipHeight = std::max(1, config_.height >> key.mip);
    std::vector<float> influences(layers_.size());
    std::vector<Color> samples(layers_.size());
    for (int py = 0; py < extent; ++py) {
        for (int px = 0; px < extent; ++px) {
            const int   virtualX = key.x * config_.tileSize + px - config_.borderSize;
            const int   virtualY = key.y * config_.tileSize + py - config_.borderSize;
            const float u        = (float(virtualX) + 0.5f) / float(mipWidth);
            const float v        = (float(virtualY) + 0.5f) / float(mipHeight);
            float       maximum  = 0.f;
            for (std::size_t i = 0; i < layers_.size(); ++i) {
                const auto& layer  = layers_[i];
                const Color albedo = sampleRepeat(*layer.albedo, u * layer.uvScale, v * layer.uvScale);
                const float mask   = layer.weight ? sampleRepeat(*layer.weight, u, v).r : 1.f;
                influences[i]      = layer.constantWeight * mask + albedo.a * layer.heightContrast;
                maximum            = std::max(maximum, influences[i]);
                if (key.attribute == Attribute::Normal && layer.normal)
                    samples[i] = sampleRepeat(*layer.normal, u * layer.uvScale, v * layer.uvScale);
                else if (key.attribute == Attribute::Normal)
                    samples[i] = Color{0.5f, 0.5f, 1.f, 1.f};
                else
                    samples[i] = albedo;
            }
            float           total      = 0.f;
            constexpr float transition = 0.15f;
            for (float& influence : influences) {
                influence = std::max(0.f, influence - maximum + transition);
                total += influence;
            }
            if (total <= 1e-6f) {
                influences.front() = 1.f;
                total              = 1.f;
            }
            Color output{0.f, 0.f, 0.f, 0.f};
            for (std::size_t i = 0; i < samples.size(); ++i) {
                const float weight = influences[i] / total;
                if (key.attribute == Attribute::Normal) {
                    output.r += (samples[i].r * 2.f - 1.f) * weight;
                    output.g += (samples[i].g * 2.f - 1.f) * weight;
                    output.b += (samples[i].b * 2.f - 1.f) * weight;
                } else {
                    output.r += samples[i].r * weight;
                    output.g += samples[i].g * weight;
                    output.b += samples[i].b * weight;
                    output.a += samples[i].a * weight;
                }
            }
            if (key.attribute == Attribute::Normal) {
                const float length  = std::sqrt(output.r * output.r + output.g * output.g + output.b * output.b);
                const float inverse = length > 1e-6f ? 1.f / length : 1.f;
                output              = Color{output.r * inverse * 0.5f + 0.5f, output.g * inverse * 0.5f + 0.5f,
                               output.b * inverse * 0.5f + 0.5f, 1.f};
            }
            page->setPixel(px, py, output);
        }
    }
    return page;
}

eve::Result<std::unique_ptr<image::ImageData>> VirtualTextureBlend::bakeAlbedo() {
    return bakeAttribute(Attribute::Albedo);
}

eve::Result<std::unique_ptr<image::ImageData>> VirtualTextureBlend::bakeNormal() {
    return bakeAttribute(Attribute::Normal);
}

eve::Result<std::unique_ptr<image::ImageData>> VirtualTextureBlend::bakeAttribute(Attribute attribute) {
    if (layers_.empty())
        return eve::Result<std::unique_ptr<image::ImageData>>::failure(
            invalid("virtual texture has no material layers"));
    auto output = std::make_unique<image::ImageData>(config_.width, config_.height, "RGBA8");
    for (int pageY = 0; pageY < pageCountY(); ++pageY) {
        for (int pageX = 0; pageX < pageCountX(); ++pageX) {
            auto pageResult = requestPage({pageX, pageY, 0, attribute});
            if (!pageResult.ok()) return eve::Result<std::unique_ptr<image::ImageData>>::failure(pageResult.status());
            const auto& page       = *pageResult.value();
            const int   copyWidth  = std::min(config_.tileSize, config_.width - pageX * config_.tileSize);
            const int   copyHeight = std::min(config_.tileSize, config_.height - pageY * config_.tileSize);
            output->paste(const_cast<image::ImageData*>(&page), pageX * config_.tileSize, pageY * config_.tileSize,
                          config_.borderSize, config_.borderSize, copyWidth, copyHeight);
        }
    }
    return eve::Result<std::unique_ptr<image::ImageData>>::success(std::move(output));
}

eve::Result<VirtualTextureResidentSet> VirtualTextureBlend::buildResidentSet(
    const std::vector<VirtualTexturePageId>& residentPages) {
    if (layers_.empty())
        return eve::Result<VirtualTextureResidentSet>::failure(invalid("virtual texture has no material layers"));
    if (residentPages.size() > config_.residentPageCapacity)
        return eve::Result<VirtualTextureResidentSet>::failure(
            invalid("resident page selection exceeds physical cache capacity"));

    std::unordered_set<std::uint64_t> unique;
    for (const auto& page : residentPages) {
        if (page.x < 0 || page.y < 0 || page.x >= pageCountX() || page.y >= pageCountY())
            return eve::Result<VirtualTextureResidentSet>::failure(
                invalid("resident page coordinate is outside mip zero"));
        const auto key = (std::uint64_t(std::uint32_t(page.y)) << 32u) | std::uint32_t(page.x);
        if (!unique.insert(key).second)
            return eve::Result<VirtualTextureResidentSet>::failure(
                invalid("resident page selection contains duplicates"));
    }

    const int                 extent    = config_.tileSize + config_.borderSize * 2;
    const std::size_t         slotCount = config_.residentPageCapacity + 1;
    const int                 slotsX    = int(std::ceil(std::sqrt(double(slotCount))));
    const int                 slotsY    = int((slotCount + std::size_t(slotsX) - 1) / std::size_t(slotsX));
    VirtualTextureResidentSet output;
    output.albedoAtlas    = std::make_unique<image::ImageData>(slotsX * extent, slotsY * extent, "RGBA8");
    output.normalAtlas    = std::make_unique<image::ImageData>(slotsX * extent, slotsY * extent, "RGBA8");
    output.pageTable      = std::make_unique<image::ImageData>(pageCountX(), pageCountY(), "RGBA8");
    output.atlasSlotsX    = slotsX;
    output.atlasSlotsY    = slotsY;
    output.pageExtent     = extent;
    output.borderFraction = float(config_.borderSize) / float(extent);
    output.residentPages  = residentPages.size();

    auto albedoFallback = bakeAlbedo();
    if (!albedoFallback.ok()) return eve::Result<VirtualTextureResidentSet>::failure(albedoFallback.status());
    auto normalFallback = bakeNormal();
    if (!normalFallback.ok()) return eve::Result<VirtualTextureResidentSet>::failure(normalFallback.status());

    auto writeFallback = [&](image::ImageData& atlas, const image::ImageData& source) {
        for (int y = 0; y < extent; ++y) {
            for (int x = 0; x < extent; ++x) {
                const float u = (float(x - config_.borderSize) + 0.5f) / float(config_.tileSize);
                const float v = (float(y - config_.borderSize) + 0.5f) / float(config_.tileSize);
                atlas.setPixel(x, y, sampleRepeat(source, u, v));
            }
        }
    };
    writeFallback(*output.albedoAtlas, *albedoFallback.value());
    writeFallback(*output.normalAtlas, *normalFallback.value());

    const Color fallbackEntry{0.5f / float(slotsX), 0.5f / float(slotsY), 0.f, 1.f};
    for (int y = 0; y < pageCountY(); ++y)
        for (int x = 0; x < pageCountX(); ++x) output.pageTable->setPixel(x, y, fallbackEntry);

    for (std::size_t index = 0; index < residentPages.size(); ++index) {
        const int slot   = int(index) + 1;
        const int slotX  = slot % slotsX;
        const int slotY  = slot / slotsX;
        auto      albedo = requestAlbedoPage(residentPages[index].x, residentPages[index].y, 0);
        if (!albedo.ok()) return eve::Result<VirtualTextureResidentSet>::failure(albedo.status());
        auto normal = requestNormalPage(residentPages[index].x, residentPages[index].y, 0);
        if (!normal.ok()) return eve::Result<VirtualTextureResidentSet>::failure(normal.status());
        output.albedoAtlas->paste(const_cast<image::ImageData*>(albedo.value().get()), slotX * extent, slotY * extent,
                                  0, 0, extent, extent);
        output.normalAtlas->paste(const_cast<image::ImageData*>(normal.value().get()), slotX * extent, slotY * extent,
                                  0, 0, extent, extent);
        const Color entry{(float(slotX) + 0.5f) / float(slotsX), (float(slotY) + 0.5f) / float(slotsY), 1.f, 1.f};
        output.pageTable->setPixel(residentPages[index].x, residentPages[index].y, entry);
    }
    return eve::Result<VirtualTextureResidentSet>::success(std::move(output));
}

void VirtualTextureBlend::invalidate() {
    cache_.clear();
    lru_.clear();
}

VirtualTextureBlendStats VirtualTextureBlend::stats() const { return {cache_.size(), hits_, misses_, evictions_}; }

}  // namespace eve::graphics
