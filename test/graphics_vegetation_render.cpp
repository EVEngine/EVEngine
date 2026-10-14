#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include "asset/CanonicalMesh.h"
#include "asset/graphics/VegetationAsset.h"
#include "filesystem/FileData.h"
#include "graphics/Canvas.h"
#include "graphics/Graphics.h"
#include "graphics/Light.h"
#include "graphics/Material.h"
#include "graphics/Mesh.h"
#include "graphics/RenderSystem.h"
#include "graphics/RenderSystem3D.h"
#include "graphics/VegetationField.h"
#include "graphics/VegetationMaskPacking.h"
#include "graphics/VegetationMotion.h"
#include "graphics/VegetationRender.h"
#include "image/Image.h"
#include "image/ImageData.h"
#include "window/Window.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve::graphics;

TEST_CASE("graphics.vegetation.render_mesh_update_and_season") {
    auto*                       gfx    = Graphics::create();
    auto*                       window = eve::window::Window::create();
    eve::window::WindowSettings settings;
    settings.width  = 800;
    settings.height = 500;
    REQUIRE(window->setWindowSettings(settings));
    gfx->setScreenReadbackEnabled(true);
    gfx->setBackgroundColor(Color(0.06f, 0.09f, 0.13f, 1.f));
    auto* camera = Camera3D::createCamera();
    camera->setEye(4, 3, 6);
    camera->setTarget(0, 0.6f, 0);
    camera->setAmbient(0.45f, 0.45f, 0.45f);
    auto* sun = Light3D::createLight("dir");
    sun->setDirection(0.4f, 1.f, 0.5f);
    sun->setColor(1, 0.96f, 0.9f, 1.5f);
    VegetationField   field;
    VegetationGlobals globals;
    globals.motion.z = 1.f;
    REQUIRE(field.replace(globals, {}).ok());
    std::vector<VegetationVertex> rest;
    std::vector<float>            uv;
    std::vector<uint32_t>         indices;
    for (unsigned row = 0; row < 12; ++row)
        for (unsigned col = 0; col < 16; ++col) {
            const glm::vec3 pivot(float(col) * 0.22f - 1.65f, 0.f, float(row) * 0.23f - 1.3f);
            const float     height = 0.7f + 0.04f * float((col * 7 + row * 13) % 9);
            const auto      start  = uint32_t(rest.size());
            for (unsigned level = 0; level < 4; ++level) {
                const float t     = float(level) / 3.f;
                const float width = 0.055f * (1.f - t) + 0.003f;
                for (unsigned side = 0; side < 2; ++side) {
                    VegetationVertex v;
                    v.pivot     = pivot;
                    v.position  = pivot + glm::vec3(side ? width : -width, t * height, 0);
                    v.normal    = {0, 0, 1};
                    v.tangent   = glm::vec4(1, 0, 0, 1);
                    v.bending   = t * t;
                    v.branch    = t;
                    v.flutter   = t;
                    v.variation = float((col + row * 16) % 101) / 100.f;
                    rest.push_back(v);
                    uv.insert(uv.end(), {float(side), t});
                }
                if (level < 3) {
                    const auto a = start + level * 2;
                    indices.insert(indices.end(), {a, a + 1, a + 3, a, a + 3, a + 2});
                }
            }
        }
    eve::asset::CanonicalMeshData canonical;
    canonical.indices      = indices;
    canonical.texcoords[0] = uv;
    for (const auto* name : {"COLOR_0", "_UNITY_UV0", "_UNITY_UV1", "_UNITY_UV3", "TANGENT"})
        canonical.attributes[name].components = 4;
    for (size_t i = 0; i < rest.size(); ++i) {
        const auto& v = rest[i];
        canonical.positions.insert(canonical.positions.end(), {v.position.x, v.position.y, v.position.z});
        canonical.normals.insert(canonical.normals.end(), {v.normal.x, v.normal.y, v.normal.z});
        auto append = [&](const char* name, std::initializer_list<float> values) {
            auto& out = canonical.attributes[name].values;
            out.insert(out.end(), values);
        };
        const auto masks = std::floor(v.branch * 2047.f) * 2048.f + std::floor(v.flutter * 2047.f);
        append("COLOR_0", {v.variation, v.occlusion, v.detail, v.bending});
        append("TANGENT", {v.tangent->x, v.tangent->y, v.tangent->z, v.tangent->w});
        append("_UNITY_UV0", {uv[i * 2], 1.f - uv[i * 2 + 1], masks, 40980.f});
        append("_UNITY_UV1", {uv[i * 2], 1.f - uv[i * 2 + 1], 0, 0});
        append("_UNITY_UV3", {v.pivot.x, -v.pivot.z, v.pivot.y, 0});
    }
    auto asset = eve::asset_graphics::VegetationAsset::fromCanonical(canonical);
    REQUIRE(asset.ok());
    canonical    = {};  // Runtime deformation owns its rest stream independently of import staging.
    auto created = asset.value()->createMesh(*gfx, field, {});
    REQUIRE(created.ok());
    auto* mesh            = created.value();
    auto  initialGeometry = asset.value()->evaluate(field, {});
    REQUIRE(initialGeometry.ok());
    REQUIRE(std::vector<float>(mesh->motionHighlights().begin(), mesh->motionHighlights().end()) ==
            initialGeometry.value().motionHighlights);
    REQUIRE(std::vector<float>(mesh->vegetationFactors().begin(), mesh->vegetationFactors().end()) ==
            initialGeometry.value().vegetationFactors);
    REQUIRE(mesh->hasImportedTangents());
    const auto initialTangents = mesh->importedTangents();
    auto*      plant           = Renderable3D::create();
    plant->setMesh(mesh);
    plant->setCamera(camera);
    Material          material;
    VegetationSurface surface;
    surface.albedo    = {0.18f, 0.65f, 0.06f, 1.f};
    surface.roughness = 1.f;
    VegetationMask sourceMask{2, 1, {glm::vec4(0, .4f, .2f, .8f), glm::vec4(0, 1, .8f, .2f)}};
    auto           orm = packVegetationOrm(sourceMask, .7f, .8f);
    REQUIRE(orm.ok());
    uint8_t packedMask[8];
    for (size_t i = 0; i < 2; ++i)
        for (size_t c = 0; c < 4; ++c)
            packedMask[i * 4 + c] = uint8_t(std::lround(orm.value().pixels[i][int(c)] * 255.f));
    auto* maskTexture = gfx->newTexture(2, 1, packedMask);
    REQUIRE(maskTexture != nullptr);
    auto          pbr = material.pbrSurface();
    const uint8_t normalPixel[]{180, 128, 255, 255};
    auto*         normalTexture = gfx->newTexture(1, 1, normalPixel);
    REQUIRE(normalTexture != nullptr);
    pbr.normalMode                                          = PbrNormalMode::VegetationRG;
    pbr.textures[size_t(PbrTextureSlot::Normal)].texture    = normalTexture;
    pbr.textures[size_t(PbrTextureSlot::Normal)].srgbDecode = false;
    for (auto slot : {PbrTextureSlot::MetallicRoughness, PbrTextureSlot::Occlusion}) {
        auto& binding      = pbr.textures[size_t(slot)];
        binding.texture    = maskTexture;
        binding.srgbDecode = false;
    }
    REQUIRE(material.setPbrSurface(pbr).ok());
    auto sample = field.sample({0, 0, 0});
    REQUIRE(sample.ok());
    REQUIRE(applyVegetationSurface(material, sample.value(), surface).ok());
    plant->setMaterial(&material);
    VegetationMotion motion;
    motion.time   = 0.7;
    motion.camera = {4, 3, 6};
    REQUIRE(asset.value()->updateMesh(*gfx, *mesh, field, motion).ok());
    auto updatedGeometry = asset.value()->evaluate(field, motion);
    REQUIRE(updatedGeometry.ok());
    REQUIRE(std::vector<float>(mesh->motionHighlights().begin(), mesh->motionHighlights().end()) ==
            updatedGeometry.value().motionHighlights);
    REQUIRE(std::vector<float>(mesh->vegetationFactors().begin(), mesh->vegetationFactors().end()) ==
            updatedGeometry.value().vegetationFactors);
    REQUIRE(mesh->importedTangents() != initialTangents);
    REQUIRE(mesh->hasBounds());
    for (int frame = 0; frame < 4; ++frame) {
        RenderSystem3D::render(*gfx);
        RenderSystem::render(*gfx);
    }
    [[maybe_unused]] auto*                 image = eve::image::Image::create();
    std::unique_ptr<eve::image::ImageData> screenshot(gfx->newImageData());
    REQUIRE(screenshot != nullptr);
    int green = 0;
    for (int y = 0; y < settings.height; y += 2)
        for (int x = 0; x < settings.width; x += 2) {
            auto pixel = screenshot->getPixel(x, y);
            if (pixel.g > pixel.r + 0.04f && pixel.g > pixel.b + 0.04f) ++green;
        }
    CHECK(green > 1000);
    const auto path = std::filesystem::path(EVENGINE_TEST_BINARY_DIR) / "out/vegetation.png";
    std::filesystem::create_directories(path.parent_path());
    std::unique_ptr<eve::filesystem::FileData> png(
        screenshot->encode(medialoader::FormatHandler::ENCODED_PNG, "vegetation.png", false));
    REQUIRE(png != nullptr);
    std::ofstream output(path, std::ios::binary);
    output.write(static_cast<const char*>(png->getData()), std::streamsize(png->getSize()));
    REQUIRE(output.good());
    const auto withMask                                                     = material.pbrSurface();
    auto       withoutMask                                                  = withMask;
    withoutMask.textures[size_t(PbrTextureSlot::MetallicRoughness)].texture = nullptr;
    withoutMask.textures[size_t(PbrTextureSlot::Occlusion)].texture         = nullptr;
    REQUIRE(material.setPbrSurface(withoutMask).ok());
    for (int frame = 0; frame < 4; ++frame) {
        RenderSystem3D::render(*gfx);
        RenderSystem::render(*gfx);
    }
    std::unique_ptr<eve::image::ImageData> plainFrame(gfx->newImageData());
    REQUIRE(plainFrame != nullptr);
    int maskChangedPixels = 0;
    for (int y = 0; y < settings.height; y += 2)
        for (int x = 0; x < settings.width; x += 2) {
            const auto a = screenshot->getPixel(x, y), b = plainFrame->getPixel(x, y);
            if (std::abs(a.r - b.r) + std::abs(a.g - b.g) + std::abs(a.b - b.b) > .01f) ++maskChangedPixels;
        }
    CHECK(maskChangedPixels > 100);
    auto dual               = withMask;
    dual.colorMaskEnabled   = true;
    dual.colorMaskSecondary = {.7f, .02f, .01f};
    dual.colorMaskMin       = .2f;
    dual.colorMaskMax       = .8f;
    REQUIRE(material.setPbrSurface(dual).ok());
    for (int frame = 0; frame < 4; ++frame) {
        RenderSystem3D::render(*gfx);
        RenderSystem::render(*gfx);
    }
    std::unique_ptr<eve::image::ImageData> dualFrame(gfx->newImageData());
    REQUIRE(dualFrame != nullptr);
    int secondaryPixels = 0;
    for (int y = 0; y < settings.height; y += 2)
        for (int x = 0; x < settings.width; x += 2) {
            const auto pixel = dualFrame->getPixel(x, y);
            if (pixel.r > pixel.g + .04f && pixel.r > pixel.b + .04f) ++secondaryPixels;
        }
    CHECK(secondaryPixels > 100);
    std::unique_ptr<eve::filesystem::FileData> dualPng(
        dualFrame->encode(medialoader::FormatHandler::ENCODED_PNG, "vegetation-dual-color.png", false));
    REQUIRE(dualPng != nullptr);
    std::ofstream dualOutput(path.parent_path() / "vegetation-dual-color.png", std::ios::binary);
    dualOutput.write(static_cast<const char*>(dualPng->getData()), std::streamsize(dualPng->getSize()));
    REQUIRE(dualOutput.good());
    REQUIRE(material.setPbrSurface(withMask).ok());
    VegetationElement autumn;
    autumn.extents    = {10, 10, 10};
    autumn.seasonal   = true;
    autumn.seasons[3] = {0.8f, 0.08f, 0.015f, 1.f};
    globals.season    = 3.f;
    REQUIRE(field.replace(globals, std::span(&autumn, 1)).ok());
    auto changed = field.sample({0, 0, 0});
    REQUIRE(changed.ok());
    REQUIRE(applyVegetationSurface(material, changed.value(), surface).ok());
    motion.time = 2.3;
    REQUIRE(asset.value()->updateMesh(*gfx, *mesh, field, motion).ok());
    for (int frame = 0; frame < 4; ++frame) {
        RenderSystem3D::render(*gfx);
        RenderSystem::render(*gfx);
    }
    std::unique_ptr<eve::image::ImageData> autumnFrame(gfx->newImageData());
    REQUIRE(autumnFrame != nullptr);
    int red = 0;
    for (int y = 0; y < settings.height; y += 2)
        for (int x = 0; x < settings.width; x += 2) {
            const auto pixel = autumnFrame->getPixel(x, y);
            if (pixel.r > pixel.g + 0.04f && pixel.r > pixel.b + 0.04f) ++red;
        }
    CHECK(red > 1000);
    std::unique_ptr<eve::filesystem::FileData> autumnPng(
        autumnFrame->encode(medialoader::FormatHandler::ENCODED_PNG, "vegetation-autumn.png", false));
    REQUIRE(autumnPng != nullptr);
    std::ofstream autumnOutput(path.parent_path() / "vegetation-autumn.png", std::ios::binary);
    autumnOutput.write(static_cast<const char*>(autumnPng->getData()), std::streamsize(autumnPng->getSize()));
    REQUIRE(autumnOutput.good());
    plant->setMaterial(nullptr);
    plant->setVisible(false);
}
