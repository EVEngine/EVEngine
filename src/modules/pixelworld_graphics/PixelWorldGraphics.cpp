#include "pixelworld_graphics/PixelWorldGraphics.h"

#include "common/Exception.h"
#include "graphics/Graphics.h"
#include "pixelworld/PixelWorld.h"
#include "pixelworld/PixelWorldControl.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <functional>
#include <utility>
#include <vector>

namespace eve::pixelworld_graphics {

namespace {

using Rgba = std::array<std::uint8_t, 4>;

Rgba color(const pixelworld::PixelWorld& world, pixelworld::MaterialId material) noexcept {
    const std::uint32_t rgba = world.materialDisplayRgba(material);
    return {std::uint8_t(rgba >> 24U), std::uint8_t(rgba >> 16U),
            std::uint8_t(rgba >> 8U), std::uint8_t(rgba)};
}

}  // namespace

struct PixelWorldAtlasRenderer::Impl {
    int originX = 0;
    int originY = 0;
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;
    graphics::Texture* texture = nullptr;
    std::uint64_t renderedRevision = 0;
    std::uint64_t totalUploadedChunks = 0;
    std::uint64_t uploadCount = 0;
    bool initialized = false;
};

PixelWorldAtlasRenderer::PixelWorldAtlasRenderer(int originX, int originY, int width, int height)
    : impl_(std::make_unique<Impl>()) {
    if (width <= 0 || height <= 0 || width > 8192 || height > 8192)
        throw eve::Exception("PixelWorld atlas size must be within 1..8192");
    impl_->originX = originX;
    impl_->originY = originY;
    impl_->width = width;
    impl_->height = height;
    impl_->rgba.resize(std::size_t(width) * std::size_t(height) * 4U, 0);
}

PixelWorldAtlasRenderer::~PixelWorldAtlasRenderer() = default;
PixelWorldAtlasRenderer::PixelWorldAtlasRenderer(PixelWorldAtlasRenderer&&) noexcept = default;
PixelWorldAtlasRenderer& PixelWorldAtlasRenderer::operator=(PixelWorldAtlasRenderer&&) noexcept = default;

int PixelWorldAtlasRenderer::sync(pixelworld::PixelWorld* world, graphics::Graphics* graphics) {
    if (!world || !graphics) throw eve::Exception("PixelWorldAtlasRenderer::sync requires world and graphics");

    const std::uint64_t worldRevision = world->revision();
    const bool rolledBack = impl_->initialized && worldRevision < impl_->renderedRevision;
    if (rolledBack) {
        std::fill(impl_->rgba.begin(), impl_->rgba.end(), std::uint8_t(0));
        impl_->renderedRevision = 0;
    }

    auto chunks = world->snapshotChangedChunks(impl_->initialized && !rolledBack
                                                   ? impl_->renderedRevision
                                                   : 0);
    if (impl_->initialized && !rolledBack && chunks.empty()) {
        impl_->renderedRevision = worldRevision;
        return 0;
    }

    struct DirtyRect {
        int x;
        int y;
        int width;
        int height;
    };
    std::vector<DirtyRect> dirtyRects;
    dirtyRects.reserve(chunks.size());
    for (const auto& chunk : chunks) {
        const int chunkOriginX = chunk.x * pixelworld::kPixelChunkSize;
        const int chunkOriginY = chunk.y * pixelworld::kPixelChunkSize;
        const int minX = std::max(chunkOriginX, impl_->originX);
        const int minY = std::max(chunkOriginY, impl_->originY);
        const int maxX = std::min(chunkOriginX + pixelworld::kPixelChunkSize,
                                  impl_->originX + impl_->width);
        const int maxY = std::min(chunkOriginY + pixelworld::kPixelChunkSize,
                                  impl_->originY + impl_->height);
        if (minX >= maxX || minY >= maxY) continue;
        for (int worldY = minY; worldY < maxY; ++worldY) {
            for (int worldX = minX; worldX < maxX; ++worldX) {
                const int chunkX = worldX - chunkOriginX;
                const int chunkY = worldY - chunkOriginY;
                const Rgba pixel = chunk.removed
                                       ? Rgba{0, 0, 0, 0}
                                       : color(*world, chunk.cells[std::size_t(chunkY) *
                                                                      pixelworld::kPixelChunkSize +
                                                                  std::size_t(chunkX)]
                                                           .material);
                const std::size_t atlasOffset =
                    (std::size_t(worldY - impl_->originY) * std::size_t(impl_->width) +
                     std::size_t(worldX - impl_->originX)) * 4U;
                std::copy(pixel.begin(), pixel.end(), impl_->rgba.begin() + std::ptrdiff_t(atlasOffset));
            }
        }
        dirtyRects.push_back({minX - impl_->originX, minY - impl_->originY,
                              maxX - minX, maxY - minY});
    }

    const bool creatingTexture = !impl_->texture;
    if (creatingTexture) {
        impl_->texture = graphics->newTexture(impl_->width, impl_->height, impl_->rgba.data(), false, false);
        graphics->setTextureSampler(impl_->texture, "nearest", "none", 1.0F, 0.0F);
    } else if (rolledBack) {
        graphics->updateTextureRegion(impl_->texture, 0, 0, impl_->width, impl_->height,
                                      impl_->rgba).expect("PixelWorld atlas reset upload failed");
    } else if (!dirtyRects.empty()) {
        const std::size_t atlasStride = std::size_t(impl_->width) * 4U;
        std::vector<graphics::TextureRegionUpload> uploads;
        uploads.reserve(dirtyRects.size());
        for (const DirtyRect rect : dirtyRects) {
            const std::size_t offset =
                std::size_t(rect.y) * atlasStride + std::size_t(rect.x) * 4U;
            uploads.push_back({rect.x, rect.y, rect.width, rect.height,
                               std::span<const std::uint8_t>(impl_->rgba).subspan(offset),
                               atlasStride});
        }
        graphics->updateTextureRegions(impl_->texture, uploads)
            .expect("PixelWorld dirty chunk upload failed");
    }
    impl_->initialized = true;
    impl_->renderedRevision = worldRevision;
    impl_->totalUploadedChunks += dirtyRects.size();
    if (creatingTexture || rolledBack || !dirtyRects.empty()) ++impl_->uploadCount;
    return int(dirtyRects.size());
}

int PixelWorldAtlasRenderer::drawDiagnostics(pixelworld::PixelWorld* world,
                                             graphics::Graphics* graphics, float scale) {
    if (!world || !graphics || scale <= 0.0F)
        throw eve::Exception("PixelWorldAtlasRenderer::drawDiagnostics requires world, graphics and positive scale");
    const int minChunkX = impl_->originX >= 0
                              ? impl_->originX / pixelworld::kPixelChunkSize
                              : -((-impl_->originX + pixelworld::kPixelChunkSize - 1) /
                                  pixelworld::kPixelChunkSize);
    const int minChunkY = impl_->originY >= 0
                              ? impl_->originY / pixelworld::kPixelChunkSize
                              : -((-impl_->originY + pixelworld::kPixelChunkSize - 1) /
                                  pixelworld::kPixelChunkSize);
    const int lastX = impl_->originX + impl_->width - 1;
    const int lastY = impl_->originY + impl_->height - 1;
    const int maxChunkX = lastX >= 0 ? lastX / pixelworld::kPixelChunkSize
                                     : -((-lastX + pixelworld::kPixelChunkSize - 1) /
                                         pixelworld::kPixelChunkSize);
    const int maxChunkY = lastY >= 0 ? lastY / pixelworld::kPixelChunkSize
                                     : -((-lastY + pixelworld::kPixelChunkSize - 1) /
                                         pixelworld::kPixelChunkSize);
    const auto diagnostics = world->chunkDiagnostics({minChunkX, minChunkY, maxChunkX, maxChunkY})
                                 .expect("PixelWorld diagnostic overlay region is bounded");
    for (const auto& chunk : diagnostics) {
        const float x = float(chunk.x * pixelworld::kPixelChunkSize - impl_->originX) * scale;
        const float y = float(chunk.y * pixelworld::kPixelChunkSize - impl_->originY) * scale;
        const float size = float(pixelworld::kPixelChunkSize) * scale;
        const float heat = std::clamp((float(chunk.maximumTemperature) - 20.0F) / 1000.0F,
                                      0.0F, 1.0F);
        const float red = heat;
        const float green = chunk.active ? 0.95F : 0.25F;
        const float blue = chunk.active ? 0.25F : 0.95F;
        graphics->drawSolidRectRGBA(x, y, size, 1.0F, red, green, blue, 0.9F);
        graphics->drawSolidRectRGBA(x, y + size - 1.0F, size, 1.0F, red, green, blue, 0.9F);
        graphics->drawSolidRectRGBA(x, y, 1.0F, size, red, green, blue, 0.9F);
        graphics->drawSolidRectRGBA(x + size - 1.0F, y, 1.0F, size, red, green, blue, 0.9F);
    }

    const auto samples = pixelworld::pixelWorldControlService()
                             .performanceSamples(world->worldLink().world, 60)
                             .expect("live PixelWorld has performance samples");
    constexpr float graphWidth = 124.0F;
    constexpr float graphHeight = 42.0F;
    const float graphX = float(impl_->width) * scale - graphWidth - 8.0F;
    const float graphY = 8.0F;
    graphics->drawSolidRectRGBA(graphX, graphY, graphWidth, graphHeight,
                                0.015F, 0.02F, 0.035F, 0.82F);
    std::uint64_t maximum = 1;
    for (const auto& sample : samples) maximum = std::max(maximum, sample.elapsedMicroseconds);
    const float barWidth = 2.0F;
    const std::size_t visible = std::min<std::size_t>(samples.size(), 60);
    for (std::size_t index = 0; index < visible; ++index) {
        const auto& sample = samples[samples.size() - visible + index];
        const float height = std::max(1.0F, float(sample.elapsedMicroseconds) / float(maximum) *
                                               (graphHeight - 6.0F));
        graphics->drawSolidRectRGBA(graphX + 2.0F + float(index) * barWidth,
                                    graphY + graphHeight - 2.0F - height,
                                    1.0F, height, 0.25F, 0.9F, 1.0F, 0.95F);
    }
    return int(diagnostics.size());
}

graphics::Texture* PixelWorldAtlasRenderer::texture() const noexcept { return impl_->texture; }
int PixelWorldAtlasRenderer::originX() const noexcept { return impl_->originX; }
int PixelWorldAtlasRenderer::originY() const noexcept { return impl_->originY; }
int PixelWorldAtlasRenderer::width() const noexcept { return impl_->width; }
int PixelWorldAtlasRenderer::height() const noexcept { return impl_->height; }
std::uint64_t PixelWorldAtlasRenderer::renderedRevision() const noexcept { return impl_->renderedRevision; }
std::uint64_t PixelWorldAtlasRenderer::totalUploadedChunks() const noexcept { return impl_->totalUploadedChunks; }
std::uint64_t PixelWorldAtlasRenderer::uploadCount() const noexcept { return impl_->uploadCount; }

Module_IMPL(PixelWorldGraphics, new PixelWorldGraphics());

PixelWorldAtlasRenderer* PixelWorldGraphics::newRenderer(int originX, int originY, int width, int height) {
    return new PixelWorldAtlasRenderer(originX, originY, width, height);
}

void PixelWorldGraphics::expose(ssq::Table& table) {
    auto module = table.addClass(name, PixelWorldGraphics::create, false);
    module.addFunc("newRenderer", &PixelWorldGraphics::newRenderer);
    auto renderer = table.addClass<PixelWorldAtlasRenderer>(
        "PixelWorldAtlasRenderer",
        std::function<PixelWorldAtlasRenderer*()>([]() -> PixelWorldAtlasRenderer* { return nullptr; }),
        true);
    renderer.addFunc("sync", &PixelWorldAtlasRenderer::sync);
    renderer.addFunc("drawDiagnostics", &PixelWorldAtlasRenderer::drawDiagnostics);
    renderer.addFunc("getTexture", [](PixelWorldAtlasRenderer* self) { return self->texture(); });
    renderer.addFunc("getOriginX", [](PixelWorldAtlasRenderer* self) { return self->originX(); });
    renderer.addFunc("getOriginY", [](PixelWorldAtlasRenderer* self) { return self->originY(); });
    renderer.addFunc("getWidth", [](PixelWorldAtlasRenderer* self) { return self->width(); });
    renderer.addFunc("getHeight", [](PixelWorldAtlasRenderer* self) { return self->height(); });
    renderer.addFunc("getRenderedRevision", [](PixelWorldAtlasRenderer* self) {
        return std::int64_t(self->renderedRevision());
    });
    renderer.addFunc("getTotalUploadedChunks", [](PixelWorldAtlasRenderer* self) {
        return std::int64_t(self->totalUploadedChunks());
    });
    renderer.addFunc("getUploadCount", [](PixelWorldAtlasRenderer* self) {
        return std::int64_t(self->uploadCount());
    });
}

void PixelWorldGraphics::expose(ssq::Class& cls) {
    cls.addFunc("newRenderer", &PixelWorldGraphics::newRenderer);
}

}  // namespace eve::pixelworld_graphics
