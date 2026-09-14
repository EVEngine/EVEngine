#include "avatar/VrmSurface.h"
#include <bit>
#include <cstring>
#include <stdexcept>
#include "common/Module.h"
#include "filesystem/FileData.h"
#include "graphics/Graphics.h"
#include "graphics/Material.h"
#include "graphics/Shader.h"
#include "graphics/Texture.h"
#include "graphics/shaders/mesh3d_mtoon_frag_spv.inc"
#include "graphics/shaders/mesh3d_mtoon_vert_spv.inc"
#include "image/ImageData.h"

namespace eve::avatar {
VrmSurface::VrmSurface() = default;
VrmSurface::~VrmSurface() {
    if (auto* gfx = ModuleManager::getInstance<graphics::Graphics>("Graphics"); gfx && !providerLifetime.expired()) {
        if (gfx->releaseTexture(metadata)) delete metadata;
        if (gfx->releaseShader(shader)) delete shader;
    }
}
graphics::Texture& buildVrmAtlas(graphics::Graphics& gfx, const VrmDocument& d, std::vector<VrmAtlasRect>& rects) {
    std::vector<std::unique_ptr<image::ImageData>> decoded;
    int                                            width = 4096, height = 1, x = 0, y = 0, row = 0;
    for (const auto& bytes : d.images) {
        filesystem::FileData file("embedded", bytes.size());
        std::memcpy(file.getData(), bytes.data(), bytes.size());
        auto img = std::make_unique<image::ImageData>(&file);
        if (img->getWidth() > width) width = img->getWidth();
        decoded.push_back(std::move(img));
    }
    for (const auto& img : decoded) {
        const int w = img->getWidth(), h = img->getHeight();
        if (x + w > width) {
            x = 0;
            y += row;
            row = 0;
        }
        rects.push_back({x, y, w, h});
        x += w;
        row    = std::max(row, h);
        height = std::max(height, y + h);
    }
    if (width > 16384 || height > 16384) throw std::runtime_error("VRM image atlas exceeds supported dimensions");
    image::ImageData atlas(width, height, "RGBA8");
    for (size_t i = 0; i < decoded.size(); ++i)
        atlas.paste(decoded[i].get(), rects[i].x, rects[i].y, 0, 0, rects[i].w, rects[i].h);
    return *gfx.newTexture(&atlas);
}
std::unique_ptr<VrmSurface> buildVrmSurface(graphics::Graphics& gfx, const VrmMaterial& data, graphics::Texture& atlas,
                                            const std::vector<VrmAtlasRect>& rects) {
    if (gfx.getBackendName() != "vulkan") throw std::runtime_error("VRM MToon requires the Vulkan graphics provider");
    auto s              = std::make_unique<VrmSurface>();
    s->providerLifetime = gfx.resourceLifetime();
    s->base = s->current = data;
    std::array<std::array<float, 4>, 32> values{};
    values[0] = {data.shift, data.toony, data.gi, data.normalScale};
    values[1] = {data.rimMix, data.rimPower, data.rimLift, data.outlineWidth};
    values[2] = {data.outlineMix,
                 data.outlineMode == "worldCoordinates"    ? 1.f
                 : data.outlineMode == "screenCoordinates" ? 2.f
                                                           : 0.f,
                 data.alphaMode == "MASK"    ? 1.f
                 : data.alphaMode == "BLEND" ? 2.f
                                             : 0.f,
                 data.cutoff};
    values[3] = {data.scrollX, data.scrollY, data.rotationSpeed, data.shiftTextureScale};
    values[4] = {data.mtoon ? 1.f : 0.f, data.doubleSided ? 1.f : 0.f, data.textures[5].image >= 0 ? 1.f : 0.f,
                 data.emissionStrength};
    for (int t = 0; t < 9; ++t) {
        const auto& slot = data.textures[t];
        if (slot.image >= 0) {
            const auto& r     = rects.at(slot.image);
            values[5 + t * 3] = {float(r.x), float(r.y), float(r.w), float(r.h)};
        }
        values[6 + t * 3] = {slot.offset[0], slot.offset[1], slot.scale[0], slot.scale[1]};
        values[7 + t * 3] = {slot.rotation, float(slot.wrapS), float(slot.wrapT), slot.image >= 0 ? 1.f : 0.f};
    }
    // Encode IEEE floats losslessly into RGBA8 texels; every backend already supports this storage.
    std::array<uint8_t, 512> packed{};
    for (size_t i = 0; i < 32; ++i)
        for (size_t c = 0; c < 4; ++c) {
            uint32_t bits = std::bit_cast<uint32_t>(values[i][c]);
            for (size_t b = 0; b < 4; ++b) packed[(i * 4 + c) * 4 + b] = uint8_t(bits >> (8 * b));
        }
    s->metadata = gfx.newTexture(128, 1, packed.data(), false, false);
    s->shader   = gfx.newMeshShaderFromSpv({mesh3d_mtoon_vert_spv, mesh3d_mtoon_vert_spv + mesh3d_mtoon_vert_spv_count},
                                           {mesh3d_mtoon_frag_spv, mesh3d_mtoon_frag_spv + mesh3d_mtoon_frag_spv_count});
    auto configured = gfx.configureMeshShaderSurface(
        *s->shader, data.alphaMode == "BLEND" ? graphics::BlendMode::Alpha : graphics::BlendMode::Opaque,
        data.alphaMode != "BLEND" || data.zWrite, true);
    if (!configured.ok()) throw std::runtime_error(configured.status().describe());
    for (int i = 0; i < 25; ++i) s->shader->declareFloat("vrm" + std::to_string(i));
    auto material = [&]() {
        auto m = std::make_unique<graphics::Material>();
        m->setShadingModel("custom");
        m->setShader(s->shader);
        m->setAlbedoTexture(&atlas);
        m->setHeightTexture(s->metadata);
        m->setSurfaceMode(data.alphaMode == "BLEND" ? "transparent" : data.alphaMode == "MASK" ? "masked" : "opaque");
        m->setSortPriority(data.queue);
        m->setAlphaCutoff(data.cutoff);
        m->setDepthWrite(data.alphaMode != "BLEND" || data.zWrite);
        return m;
    };
    s->material = material();
    if (data.outlineMode != "none" && data.outlineWidth > 0) s->outlineMaterial = material();
    s->update(0);
    return s;
}
void VrmSurface::update(float time) {
    std::array<float, 25> v{};
    size_t                n      = 0;
    auto                  append = [&](const auto& values) {
        for (float x : values) v[n++] = x;
    };
    append(current.color);
    append(current.shade);
    append(current.emission);
    append(current.matcap);
    append(current.rim);
    append(current.outline);
    append(uvScale);
    append(uvOffset);
    v[n++] = time;
    for (int i = 0; i < 25; ++i) {
        material->setFloat("vrm" + std::to_string(i), v[i]);
        if (outlineMaterial) outlineMaterial->setFloat("vrm" + std::to_string(i), i == 24 ? 1.f : v[i]);
    }
}
}  // namespace eve::avatar
