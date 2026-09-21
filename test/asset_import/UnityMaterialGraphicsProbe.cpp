#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include "asset/graphics/CookedMaterial.h"
#include "asset/graphics/EvpackImageLoader.h"
#include "graphics/Canvas.h"
#include "graphics/Graphics.h"
#include "graphics/Light.h"
#include "graphics/Material.h"
#include "graphics/Mesh.h"
#include "graphics/VegetationField.h"
#include "graphics/VegetationRender.h"
#include "image/ImageData.h"

int main(int argc, char** argv) try {
    using namespace eve;
    using namespace eve::graphics;
    if (argc != 3 && argc != 4) {
        std::cerr << "usage: unity_material_graphics_probe <evpack> <asset-ref> [comparison.ppm]\n";
        return 2;
    }
    std::ifstream input(argv[1], std::ios::binary);
    if (!input) return 2;
    std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(input), {}};
    auto                 pack = asset::parseEvpack(bytes);
    auto                 ref  = AssetRef::parse(argv[2]);
    if (!pack || !ref) return 1;
    asset::EvpackResourceReader reader(std::make_shared<const asset::Evpack>(std::move(pack).takeValue()));
    auto*                       gfx = Graphics::create();
    gfx->initHeadless(64, 64);
    const asset::EvpackCapabilities caps{"windows", "x86_64", gfx->getBackendName(), {"rgba8"}, {"spirv-1.6"},
                                         {"high"},  {}};
    auto                            material = asset_graphics::detail::readCookedMaterial(reader, ref.value(), caps);
    if (!material) {
        std::cerr << material.error()->message();
        return 1;
    }
    asset_graphics::GraphicsImageFactoryAdapter factory(*gfx);
    asset_graphics::EvpackImageLoader           loader(reader, factory);
    std::vector<Texture*>                       images;
    for (size_t slot = 0; slot < material.value().images.size(); ++slot) {
        if (!material.value().images[slot]) continue;
        auto image = loader.load(*material.value().images[slot], caps);
        if (!image) {
            std::cerr << image.error()->message();
            return 1;
        }
        material.value().surface.textures[slot].texture = image.value().texture;
        images.push_back(image.value().texture);
    }
    for (size_t slot = 0; slot < material.value().detailImages.size(); ++slot) {
        if (!material.value().detailImages[slot]) continue;
        auto image = loader.load(*material.value().detailImages[slot], caps);
        if (!image) {
            std::cerr << image.error()->message();
            return 1;
        }
        material.value().surface.vegetationDetail.textures[slot].texture = image.value().texture;
        images.push_back(image.value().texture);
    }
    bool vegetationApplied = false;
    if (material.value().vegetationSurface) {
        Material runtime;
        runtime.setTint(material.value().color.r, material.value().color.g, material.value().color.b,
                        material.value().color.a);
        runtime.setMetallic(material.value().metallic);
        runtime.setRoughness(material.value().roughness);
        if (!runtime.setPbrSurface(material.value().surface)) return 1;
        VegetationSample sample{{.08f, .18f, .04f, .7f}, {1, 1, 1, 1}, {0, 0, 0, 0}, {0, 0, 0, 1}};
        if (!applyVegetationSurface(runtime, sample, *material.value().vegetationSurface)) return 1;
        material.value().color     = {runtime.getTintR(), runtime.getTintG(), runtime.getTintB(), runtime.getTintA()};
        material.value().roughness = runtime.getRoughness();
        material.value().surface   = runtime.pbrSurface();
        vegetationApplied          = true;
    }
    auto*          canvas = gfx->newCanvas(64, 64);
    const float    positions[]{-.9f, -.9f, .5f, .9f, -.9f, .5f, .9f, .9f, .5f, -.9f, .9f, .5f};
    const float    normals[]{0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1};
    const float    uv[]{0, 0, 1, 0, 1, 1, 0, 1};
    const uint32_t indices[]{0, 2, 1, 0, 3, 2};
    auto*          mesh = gfx->newMeshFromArrays(positions, normals, uv, 4, indices, 6);
    if (!canvas || !mesh) return 1;
    if (vegetationApplied) {
        std::vector<float> factors{
            .2f, 1.f, 1.f, 0.f, 0.f, .4f, 1.f, 1.f, 1.f, 0.f, .6f, 1.f, 1.f, 1.f, 1.f, .8f, 1.f, 1.f, 0.f, 1.f,
        };
        std::vector<float> highlights(4, .5f);
        if (!mesh->adoptVegetationFactors(std::move(factors)) || !mesh->adoptMotionHighlights(std::move(highlights)))
            return 1;
    }
    gfx->setMesh3DViewProj(glm::mat4(1));
    gfx->setMesh3DView(glm::mat4(1));
    gfx->setMesh3DCameraPos({0, 0, 2});
    Lighting3DPack lighting{};
    lighting.count               = 1;
    lighting.ambient             = {.8f, .8f, .8f, 0};
    lighting.lights[0].posRadius = {.3f, .2f, 1.f, 0};
    lighting.lights[0].color     = {2.f, 2.f, 2.f, 0};
    auto render                  = [&]() {
        const auto& mat = material.value();
        gfx->begin3DFrameToCanvas(canvas);
        gfx->setMesh3DLighting(lighting);
        gfx->setMesh3DMaterial(mat.metallic, mat.roughness);
        gfx->setMesh3DSurface(
            mat.transparent ? SurfaceMode::Transparent : (mat.masked ? SurfaceMode::Masked : SurfaceMode::Opaque),
            mat.blend, mat.doubleSided, true, mat.alphaCutoff, "cutoff");
        if (!gfx->setMesh3DPbrSurface(&mat.surface)) throw std::runtime_error("invalid surface");
        gfx->drawMesh(mesh, glm::mat4(1), nullptr, mat.color);
        if (!gfx->setMesh3DPbrSurface(nullptr)) throw std::runtime_error("surface reset failed");
        gfx->end3DFrameToCanvas();
        return std::unique_ptr<image::ImageData>(canvas->newImageData());
    };
    const bool hasTranslucency                             = material.value().surface.translucency.intensity > 0;
    const bool hasColors                                   = material.value().surface.vegetationColor.fieldColor[3] > 0;
    const bool hasDetail                                   = material.value().surface.vegetationDetail.value > 0;
    const bool hasEmission                                 = material.value().surface.vegetationEmission.enabled;
    auto       lit                                         = render();
    material.value().surface.translucency.intensity        = 0;
    auto withoutTranslucency                               = render();
    material.value().surface.vegetationColor.fieldColor[3] = 0;
    auto withoutColors                                     = render();
    material.value().surface.vegetationDetail.value        = 0;
    auto baseline                                          = render();
    material.value().surface.emissiveStrength              = 0;
    auto withoutEmission                                   = render();
    if (!lit || !withoutTranslucency || !withoutColors || !baseline || !withoutEmission) return 1;
    if (argc == 4) {
        std::ofstream output(argv[3], std::ios::binary);
        if (!output) return 2;
        output << "P6\n128 64\n255\n";
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 128; ++x) {
                const auto pixel = (hasEmission ? (x < 64 ? withoutEmission : baseline) : (x < 64 ? baseline : lit))
                                       ->getPixel(x % 64, y);
                for (const float channel : {pixel.r, pixel.g, pixel.b}) {
                    if (!std::isfinite(channel)) return 1;
                    output.put(static_cast<char>(std::lround(std::clamp(channel, 0.f, 1.f) * 255.f)));
                }
            }
        if (!output) return 2;
    }
    float    maximumDelta = 0, maximumColorDelta = 0, maximumDetailDelta = 0, maximumEmissionDelta = 0;
    unsigned changed = 0, changedColors = 0, changedDetail = 0, changedEmission = 0;
    for (int y = 8; y < 56; ++y)
        for (int x = 8; x < 56; ++x) {
            const auto  a = lit->getPixel(x, y), noTrans = withoutTranslucency->getPixel(x, y);
            const float delta =
                std::max({std::abs(a.r - noTrans.r), std::abs(a.g - noTrans.g), std::abs(a.b - noTrans.b)});
            if (!std::isfinite(delta)) return 1;
            maximumDelta = std::max(maximumDelta, delta);
            changed += delta > .005f;
            const auto  noColors   = withoutColors->getPixel(x, y);
            const float colorDelta = std::max(
                {std::abs(noTrans.r - noColors.r), std::abs(noTrans.g - noColors.g), std::abs(noTrans.b - noColors.b)});
            maximumColorDelta = std::max(maximumColorDelta, colorDelta);
            changedColors += colorDelta > .005f;
            const auto  b = baseline->getPixel(x, y);
            const float detailDelta =
                std::max({std::abs(noColors.r - b.r), std::abs(noColors.g - b.g), std::abs(noColors.b - b.b)});
            maximumDetailDelta = std::max(maximumDetailDelta, detailDelta);
            changedDetail += detailDelta > .005f;
            const auto  noEmission = withoutEmission->getPixel(x, y);
            const float emissionDelta =
                std::max({std::abs(b.r - noEmission.r), std::abs(b.g - noEmission.g), std::abs(b.b - noEmission.b)});
            maximumEmissionDelta = std::max(maximumEmissionDelta, emissionDelta);
            changedEmission += emissionDelta > .005f;
        }
    for (auto* image : images)
        if (!factory.releaseImage(image)) return 1;
    std::cout << "backend=" << gfx->getBackendName() << " textures=" << images.size()
              << " vegetationApplied=" << vegetationApplied << " changedPixels=" << changed
              << " maximumTranslucencyDelta=" << maximumDelta << " changedColorPixels=" << changedColors
              << " maximumColorDelta=" << maximumColorDelta << " changedDetailPixels=" << changedDetail
              << " maximumDetailDelta=" << maximumDetailDelta << " emissionEnabled=" << hasEmission
              << " changedEmissionPixels=" << changedEmission << " maximumEmissionDelta=" << maximumEmissionDelta
              << '\n';
    return (!hasTranslucency || changed > 0) && (!hasColors || changedColors > 0) &&
                   (!hasDetail || changedDetail > 0) && (!hasEmission || changedEmission > 0)
               ? 0
               : 1;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
