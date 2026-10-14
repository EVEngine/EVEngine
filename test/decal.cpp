#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "Fixtures.h"
#include "RenderImageAudit.h"
#include "common/Capability.h"
#include "common/DecalQuery.h"
#include "decal/Decal.h"
#include "decal/ProceduralDecal.h"
#include "decal/DecalManager.h"
#include "graphics/Graphics.h"
#include "graphics/RenderControl.h"
#include "graphics/RenderSystem.h"
#include "graphics/RenderSystem3D.h"
#include "graphics/Texture.h"
#include "image/ImageData.h"
#include "window/Window.h"

#include <SDL2/SDL.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <vector>

using namespace eve::decal;
using namespace eve::graphics;

namespace {
eve::graphics::Texture *makeSolidTex(eve::graphics::Graphics *gfx, uint8_t r, uint8_t g,
                                     uint8_t b);
eve::graphics::Mesh *makePlane(eve::graphics::Graphics *gfx, float size);
}  // namespace

TEST_CASE("decal.proceduralBakeIsDeterministicAndChannelPacked") {
    auto preset = proceduralDecalPreset("blood-wet", 1729u);
    REQUIRE(preset.ok());
    preset.value().width = 64;
    preset.value().height = 48;
    auto first = bakeProceduralDecal(preset.value());
    auto second = bakeProceduralDecal(preset.value());
    REQUIRE(first.ok());
    REQUIRE(second.ok());
    CHECK_EQ(first.value().albedo, second.value().albedo);
    CHECK_EQ(first.value().normal, second.value().normal);
    CHECK_EQ(first.value().params, second.value().params);
    REQUIRE_EQ(first.value().albedo.size(), static_cast<std::size_t>(64 * 48 * 4));
    bool hasCoverage = false;
    bool hasHeight = false;
    bool hasPerturbedNormal = false;
    for (std::size_t i = 0; i < first.value().albedo.size(); i += 4) {
        hasCoverage = hasCoverage || first.value().albedo[i + 3] > 0;
        hasHeight = hasHeight || first.value().params[i + 3] > 0;
        hasPerturbedNormal = hasPerturbedNormal || first.value().normal[i] != 128 ||
                             first.value().normal[i + 1] != 128;
        CHECK_EQ(first.value().normal[i + 3], first.value().albedo[i + 3]);
    }
    CHECK(hasCoverage);
    CHECK(hasHeight);
    CHECK(hasPerturbedNormal);
}

TEST_CASE("decal.proceduralPresetsCoverReferenceMaterialFamilies") {
    const char* names[] = {"blood-wet", "blood-dried", "damage", "dirt", "rust", "puddle",
                           "paint", "moss", "mold", "lichen"};
    for (const char* name : names) {
        auto preset = proceduralDecalPreset(name, 7u);
        REQUIRE(preset.ok());
        preset.value().width = 16;
        preset.value().height = 16;
        auto baked = bakeProceduralDecal(preset.value());
        REQUIRE(baked.ok());
        CHECK_EQ(baked.value().albedo.size(), static_cast<std::size_t>(16 * 16 * 4));
    }
    auto unknown = proceduralDecalPreset("not-a-preset", 1u);
    CHECK(!unknown.ok());
}

TEST_CASE("decal.proceduralLayerControlsChangeMaterialOutputs") {
    ProceduralDecalRecipe recipe;
    recipe.width = 32;
    recipe.height = 32;
    recipe.seed = 99u;
    recipe.layerA.pattern = DecalPattern::Puddle;
    recipe.layerA.amount = 1.f;
    recipe.layerA.blur = 0.f;
    recipe.layerA.color = {0.8f, 0.1f, 0.05f};
    recipe.layerA.roughness = 0.15f;
    recipe.layerA.metallic = 0.1f;
    recipe.layerA.emissive = 0.35f;
    recipe.layerA.height = 0.2f;
    recipe.layerB.enabled = false;
    auto first = bakeProceduralDecal(recipe);
    REQUIRE(first.ok());

    recipe.layerA.pattern = DecalPattern::Cracks;
    recipe.layerA.rotation = 0.7f;
    recipe.layerA.contrast = 2.2f;
    recipe.layerA.color = {0.05f, 0.2f, 0.9f};
    recipe.layerA.roughness = 0.85f;
    recipe.layerA.metallic = 0.75f;
    recipe.layerA.emissive = 0.9f;
    recipe.layerA.height = 0.8f;
    recipe.layerA.normalStrength = 5.f;
    auto second = bakeProceduralDecal(recipe);
    REQUIRE(second.ok());

    CHECK(first.value().albedo != second.value().albedo);
    CHECK(first.value().normal != second.value().normal);
    CHECK(first.value().params != second.value().params);
    bool hasEmissive = false;
    for (std::size_t index = 2; index < second.value().params.size(); index += 4)
        hasEmissive = hasEmissive || second.value().params[index] > 0;
    CHECK(hasEmissive);
}

TEST_CASE("decal.proceduralImportsReferenceSubstanceMaterialPresets") {
    const auto directory = std::filesystem::path(EVENGINE_SOURCE_DIR) / "test" / "fixtures" /
                           "procedural_decal";
    std::size_t importedCount = 0;
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (entry.path().extension() != ".sbsprs") continue;
        const auto& path = entry.path();
        std::ifstream stream(path, std::ios::binary);
        REQUIRE(stream.good());
        const std::string xml((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
        auto imported = importProceduralDecalSbsprs(xml);
        REQUIRE(imported.ok());
        CHECK(imported.value().width >= 1);
        CHECK(imported.value().height >= 1);
        imported.value().width = 32;
        imported.value().height = 32;
        auto baked = bakeProceduralDecal(imported.value());
        REQUIRE(baked.ok());
        CHECK_EQ(baked.value().albedo.size(), static_cast<std::size_t>(32 * 32 * 4));
        CHECK_EQ(baked.value().normal.size(), baked.value().albedo.size());
        CHECK_EQ(baked.value().params.size(), baked.value().albedo.size());
        ++importedCount;
    }
    CHECK_EQ(importedCount, 40u);

    auto malformed = importProceduralDecalSbsprs("<sbspresets><sbspreset><presetinput identifier=\"a_color\"");
    CHECK(!malformed.ok());
    const auto document = [](const std::string& identifier, const std::string& value) {
        return "<sbspresets><sbspreset><presetinput identifier=\"" + identifier +
               "\" value=\"" + value + "\"/></sbspreset></sbspresets>";
    };
    CHECK(!importProceduralDecalSbsprs(document("a_amount", "not-a-number")).ok());
    CHECK(!importProceduralDecalSbsprs(document("$randomseed", "1.5")).ok());
    CHECK(!importProceduralDecalSbsprs(document("$outputsize", "8,bad")).ok());
    CHECK(!importProceduralDecalSbsprs(document("a_color", "0.1,0.2,0.3,0.4")).ok());
    std::string oversized(1024u * 1024u + 1u, 'x');
    CHECK(!importProceduralDecalSbsprs(oversized).ok());
}

TEST_CASE("decal.renderImportedPresetContactSheet") {
    const auto directory = std::filesystem::path(EVENGINE_SOURCE_DIR) / "test" / "fixtures" /
                           "procedural_decal";
    std::vector<std::filesystem::path> presets;
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (entry.path().extension() == ".sbsprs") presets.push_back(entry.path());
    }
    std::sort(presets.begin(), presets.end());
    REQUIRE_EQ(presets.size(), 40u);

    constexpr int tile = 128;
    constexpr int gap = 4;
    constexpr int columns = 5;
    constexpr int rows = 8;
    constexpr int sheetWidth = columns * tile + (columns + 1) * gap;
    constexpr int sheetHeight = rows * tile + (rows + 1) * gap;
    std::vector<std::uint8_t> sheet(static_cast<std::size_t>(sheetWidth * sheetHeight * 4), 255u);
    for (int y = 0; y < sheetHeight; ++y) {
        for (int x = 0; x < sheetWidth; ++x) {
            const auto index = static_cast<std::size_t>((y * sheetWidth + x) * 4);
            const auto shade = static_cast<std::uint8_t>(((x / 12 + y / 12) & 1) ? 72 : 104);
            sheet[index + 0] = shade;
            sheet[index + 1] = shade;
            sheet[index + 2] = shade;
        }
    }

    const auto output = std::filesystem::path(EVENGINE_TEST_BINARY_DIR) / "out" / "decal" /
                        "imported_presets";
    std::error_code ec;
    std::filesystem::create_directories(output, ec);
    REQUIRE(!ec);
    std::ofstream manifest(output / "contact_sheet_order.txt", std::ios::binary);
    REQUIRE(manifest.good());

    for (std::size_t presetIndex = 0; presetIndex < presets.size(); ++presetIndex) {
        std::ifstream stream(presets[presetIndex], std::ios::binary);
        REQUIRE(stream.good());
        const std::string xml((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
        auto recipe = importProceduralDecalSbsprs(xml);
        REQUIRE(recipe.ok());
        recipe.value().width = tile;
        recipe.value().height = tile;
        auto bake = bakeProceduralDecal(recipe.value());
        REQUIRE(bake.ok());

        eve::image::ImageData image(tile, tile, "RGBA8", bake.value().albedo.data(), false);
        REQUIRE(saveImagePng(image, (output / (presets[presetIndex].stem().string() + ".png")).string()));
        manifest << presetIndex + 1 << "\t" << presets[presetIndex].stem().string() << "\n";

        const int originX = gap + static_cast<int>(presetIndex % columns) * (tile + gap);
        const int originY = gap + static_cast<int>(presetIndex / columns) * (tile + gap);
        for (int y = 0; y < tile; ++y) {
            for (int x = 0; x < tile; ++x) {
                const auto source = static_cast<std::size_t>((y * tile + x) * 4);
                const auto target = static_cast<std::size_t>(((originY + y) * sheetWidth + originX + x) * 4);
                const float alpha = float(bake.value().albedo[source + 3]) / 255.f;
                for (int channel = 0; channel < 3; ++channel) {
                    sheet[target + channel] = static_cast<std::uint8_t>(
                        float(bake.value().albedo[source + channel]) * alpha +
                        float(sheet[target + channel]) * (1.f - alpha));
                }
            }
        }
    }
    eve::image::ImageData contactSheet(sheetWidth, sheetHeight, "RGBA8", sheet.data(), false);
    REQUIRE(saveImagePng(contactSheet, (output / "contact_sheet.png").string()));
    std::printf("procedural decal contact sheet saved: %s\n", output.string().c_str());
}

TEST_CASE("decal.renderImportedPresetsOnLitMaterialSpheres") {
    eve::window::Window *win = nullptr;
    eve::graphics::Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 1280, 800);

    auto *cam = Camera3D::createCamera();
    cam->data()->eyeZ = 6.0f;
    cam->setAmbient(0.075f, 0.08f, 0.095f);
    RenderSystem3D::setDirectionalLight(-0.65f, 0.8f, 0.9f, 1.f, 0.92f, 0.78f);

    auto *background = Renderable3D::create();
    background->meshRenderer()->mesh = makePlane(gfx, 12.f);
    background->meshRenderer()->texture = makeSolidTex(gfx, 28, 32, 42);
    background->meshRenderer()->roughness = 0.9f;
    background->transform()->z = -0.65f;

    static bool sMaterialSphereDrawer = false;
    if (!sMaterialSphereDrawer) {
        sMaterialSphereDrawer = true;
        RenderSystem3D::addDecalExtraDrawer(
            [](eve::graphics::Graphics &g, const Camera3D::Data &camData,
               const glm::mat4 &viewProj, float aspect) {
                DecalManager::inst().drawAll(g, camData.eyeX, camData.eyeY, camData.eyeZ,
                                             viewProj, aspect);
            });
    }
    gfx->getRenderControl()->enable("decal");
    gfx->getRenderControl()->compile();
    gfx->setScreenReadbackEnabled(true);

    const auto directory = std::filesystem::path(EVENGINE_SOURCE_DIR) / "test" / "fixtures" /
                           "procedural_decal";
    std::vector<std::filesystem::path> presets;
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (entry.path().extension() == ".sbsprs") presets.push_back(entry.path());
    }
    std::sort(presets.begin(), presets.end());
    REQUIRE_EQ(presets.size(), 40u);

    auto *sphere = gfx->newMeshSphere(48, 24);
    REQUIRE(sphere != nullptr);
    DecalManager::inst().clearAll();
    constexpr int columns = 8;
    for (std::size_t presetIndex = 0; presetIndex < presets.size(); ++presetIndex) {
        std::ifstream stream(presets[presetIndex], std::ios::binary);
        REQUIRE(stream.good());
        const std::string xml((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
        auto recipe = importProceduralDecalSbsprs(xml);
        REQUIRE(recipe.ok());
        recipe.value().width = 128;
        recipe.value().height = 128;
        auto bake = bakeProceduralDecal(recipe.value());
        REQUIRE(bake.ok());

        const int column = static_cast<int>(presetIndex % columns);
        const int row = static_cast<int>(presetIndex / columns);
        const float x = (float(column) - 3.5f) * 0.9f;
        const float y = (2.f - float(row)) * 0.9f;
        auto *entity = Renderable3D::create();
        entity->meshRenderer()->mesh = sphere;
        entity->meshRenderer()->texture = makeSolidTex(gfx, 185, 185, 190);
        entity->meshRenderer()->roughness = 0.48f;
        entity->setPosition(x, y, 0.f);
        entity->setScale(0.36f, 0.36f, 0.36f);

        const int decal = DecalManager::inst().project(
            x, y, 0.18f, 0.f, 0.f, 1.f,
            gfx->newTexture(128, 128, bake.value().albedo.data()), "material-sphere", 0.78f,
            0.72f, false, static_cast<int>(presetIndex), 0.f, 0.f, 0.f, 1.f, 1.f, 1.f, 1.f);
        REQUIRE(DecalManager::inst().setTextures(
            decal, gfx->newTexture(128, 128, bake.value().normal.data()),
            gfx->newTexture(128, 128, bake.value().params.data())));
        REQUIRE(DecalManager::inst().setProjection(decal, "spherical", 4.f) ==
                DecalProjectionStatus::Applied);
        REQUIRE(DecalManager::inst().setParallax(decal, 0.025f, 8.f, 24.f) ==
                DecalParallaxStatus::Applied);
        REQUIRE(DecalManager::inst().setEdgeFade(decal, 0.025f) == DecalEdgeFadeStatus::Applied);
    }

    for (int frame = 0; frame < 4; ++frame) {
        RenderSystem3D::render(*gfx);
        RenderSystem::render(*gfx);
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) break;
        }
    }
    auto *snapshot = gfx->newImageData();
    REQUIRE(snapshot != nullptr);
    const auto output = std::filesystem::path(EVENGINE_TEST_BINARY_DIR) / "out" / "decal" /
                        "imported_presets";
    std::error_code ec;
    std::filesystem::create_directories(output, ec);
    REQUIRE(!ec);
    REQUIRE(saveImagePng(*snapshot, (output / "lit_material_spheres.png").string()));
    delete snapshot;

    DecalManager::inst().clearAll();
    win->close();
}

TEST_CASE("decal.proceduralRejectsInvalidRecipeWithoutOutput") {
    ProceduralDecalRecipe recipe;
    recipe.width = 0;
    auto invalidSize = bakeProceduralDecal(recipe);
    CHECK(!invalidSize.ok());
    recipe.width = 32;
    recipe.schemaVersion = 99;
    auto invalidVersion = bakeProceduralDecal(recipe);
    CHECK(!invalidVersion.ok());
    recipe.schemaVersion = ProceduralDecalRecipe::kSchemaVersion;
    recipe.layerA.scale = 0.f;
    auto invalidScale = bakeProceduralDecal(recipe);
    CHECK(!invalidScale.ok());
    recipe.layerA.scale = 4097.f;
    CHECK(!bakeProceduralDecal(recipe).ok());
    recipe.layerA.scale = 1.f;
    recipe.layerA.blur = 2.f;
    CHECK(!bakeProceduralDecal(recipe).ok());
    recipe.layerA.blur = 0.f;
    recipe.layerA.normalSoftness = 0.f;
    CHECK(!bakeProceduralDecal(recipe).ok());
    recipe.layerA.normalSoftness = 0.08f;
    recipe.layerA.normalThickness = -1.f;
    CHECK(!bakeProceduralDecal(recipe).ok());
    recipe.layerA.normalThickness = 1.f;
    recipe.layerA.contrast = std::numeric_limits<float>::max();
    CHECK(!bakeProceduralDecal(recipe).ok());
}

TEST_CASE("decal.proceduralFacadeUploadsAllRuntimeChannels") {
    eve::graphics::Graphics *gfx = nullptr;
    openHeadlessGfx(gfx, 32, 32);
    Decal api;
    auto *albedo = api.bakePresetTexture(gfx, "rust", 42u, 32, "albedo");
    auto *normal = api.bakePresetTexture(gfx, "rust", 42u, 32, "normal");
    auto *params = api.bakePresetTexture(gfx, "rust", 42u, 32, "params");
    REQUIRE(albedo != nullptr);
    REQUIRE(normal != nullptr);
    REQUIRE(params != nullptr);
    CHECK_EQ(albedo->getWidth(), 32);
    CHECK_EQ(normal->getHeight(), 32);
    CHECK_EQ(params->getWidth(), 32);
    const auto importedPath = std::filesystem::path(EVENGINE_SOURCE_DIR) / "test" / "fixtures" /
                              "procedural_decal" / "Blood_Wet.sbsprs";
    std::ifstream importedStream(importedPath, std::ios::binary);
    REQUIRE(importedStream.good());
    const std::string importedXml((std::istreambuf_iterator<char>(importedStream)),
                                  std::istreambuf_iterator<char>());
    auto *importedAlbedo = api.bakeSbsprsTexture(gfx, importedXml, 32, "albedo");
    REQUIRE(importedAlbedo != nullptr);
    CHECK_EQ(importedAlbedo->getWidth(), 32);
    CHECK_THROWS(([&] {
        auto *unexpected = api.bakePresetTexture(gfx, "rust", 42u, 32, "unknown");
        (void)unexpected;
    }(), false));
}

TEST_CASE("decal.managerProjectRemove") {
    auto &mgr = DecalManager::inst();
    mgr.clearAll();
    CHECK(mgr.count() == 0);

    const int id = mgr.project(1.f, 2.f, 3.f, 0.f, 1.f, 0.f, nullptr, "blood", 0.5f, 0.15f,
                               false, 0, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f);
    CHECK(id > 0);
    CHECK(mgr.count() == 1);
    CHECK(mgr.instances()[0].size == 0.5f);
    CHECK(mgr.instances()[0].ny == 1.f);

    CHECK(mgr.remove(id));
    CHECK(mgr.count() == 0);
    CHECK(!mgr.remove(id));
}

TEST_CASE("decal.managerLifetimeFadeExpiry") {
    auto &mgr = DecalManager::inst();
    mgr.clearAll();
    mgr.project(0.f, 0.f, 0.f, 0.f, 1.f, 0.f, nullptr, "", 0.5f, 0.15f, false, 0, 0.1f, 1.f,
                0.5f, 0.f, 0.f, 0.f, 0.f);
    mgr.update(0.5f);
    CHECK(mgr.count() == 1);
    CHECK(mgr.instances()[0].age >= 0.5f);
    // age = 0.5 + 1.2 = 1.7 >= lifetime(1) + fadeOut(0.5) -> expired.
    mgr.update(1.2f);
    CHECK(mgr.count() == 0);
}

TEST_CASE("decal.managerPersistentNeverExpires") {
    auto &mgr = DecalManager::inst();
    mgr.clearAll();
    mgr.project(0.f, 0.f, 0.f, 0.f, 1.f, 0.f, nullptr, "", 0.5f, 0.15f, false, 0, 0.f, 0.f, 0.f,
                0.f, 0.f, 0.f, 0.f);
    mgr.update(1000.f);
    CHECK(mgr.count() == 1);
}

TEST_CASE("decal.managerKindQuotaEvictsOldest") {
    auto &mgr = DecalManager::inst();
    mgr.clearAll();
    mgr.setLimit("blood", 2);
    const int first =
        mgr.project(0.f, 0.f, 0.f, 0.f, 1.f, 0.f, nullptr, "blood", 0.5f, 0.15f, false, 0, 0.f,
                    0.f, 0.f, 0.f, 0.f, 0.f, 0.f);
    const int second =
        mgr.project(1.f, 0.f, 0.f, 0.f, 1.f, 0.f, nullptr, "blood", 0.5f, 0.15f, false, 0, 0.f,
                    0.f, 0.f, 0.f, 0.f, 0.f, 0.f);
    const int third =
        mgr.project(2.f, 0.f, 0.f, 0.f, 1.f, 0.f, nullptr, "blood", 0.5f, 0.15f, false, 0, 0.f,
                    0.f, 0.f, 0.f, 0.f, 0.f, 0.f);
    CHECK(mgr.count() == 2);
    // The oldest ("first") was evicted to make room for "third".
    CHECK(!mgr.remove(first));
    CHECK(mgr.remove(second));
    CHECK(mgr.remove(third));
}

TEST_CASE("decal.managerOtherKindNotEvicted") {
    auto &mgr = DecalManager::inst();
    mgr.clearAll();
    mgr.setLimit("blood", 1);
    mgr.project(0.f, 0.f, 0.f, 0.f, 1.f, 0.f, nullptr, "blood", 0.5f, 0.15f, false, 0, 0.f, 0.f,
                0.f, 0.f, 0.f, 0.f, 0.f);
    const int dirt =
        mgr.project(1.f, 0.f, 0.f, 0.f, 1.f, 0.f, nullptr, "dirt", 0.5f, 0.15f, false, 0, 0.f,
                    0.f, 0.f, 0.f, 0.f, 0.f, 0.f);
    CHECK(mgr.count() == 2);
    CHECK(mgr.remove(dirt));
}

TEST_CASE("decal.managerAtlasBlendSetters") {
    auto &mgr = DecalManager::inst();
    mgr.clearAll();
    const int id = mgr.project(0.f, 0.f, 0.f, 0.f, 1.f, 0.f, nullptr, "", 0.5f, 0.15f, false, 0,
                               0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f);
    CHECK(id > 0);
    CHECK(mgr.setUvRect(id, 0.25f, 0.25f, 0.5f, 0.5f));
    CHECK(mgr.instances()[0].uvRect[0] == 0.25f);
    CHECK(mgr.instances()[0].uvRect[3] == 0.5f);

    CHECK(mgr.setBlend(id, "add"));
    CHECK(mgr.instances()[0].blendMode == 1);
    CHECK(mgr.setBlend(id, "over"));
    CHECK(mgr.instances()[0].blendMode == 0);
    CHECK(!mgr.setBlend(id, "bogus"));  // unknown mode rejected

    CHECK(mgr.setProjection(id, "triplanar", 8.f) == DecalProjectionStatus::Applied);
    CHECK(mgr.instances()[0].projectionMode == 1);
    CHECK(mgr.instances()[0].blendSharpness == 8.f);
    CHECK(mgr.setProjection(id, "planar", 4.f) == DecalProjectionStatus::Applied);
    CHECK(mgr.instances()[0].projectionMode == 0);
    CHECK(mgr.setProjection(id, "spherical", 4.f) == DecalProjectionStatus::Applied);
    CHECK(mgr.instances()[0].projectionMode == 2);
    CHECK(mgr.setProjection(id, "world", 4.f) == DecalProjectionStatus::Applied);
    CHECK(mgr.instances()[0].projectionMode == 3);
    CHECK(mgr.setProjection(id, "bogus", 4.f) == DecalProjectionStatus::InvalidMode);
    CHECK(mgr.setProjection(id, "triplanar", 0.f) == DecalProjectionStatus::InvalidSharpness);
    CHECK(mgr.setProjection(99999, "triplanar", 4.f) == DecalProjectionStatus::UnknownId);
    CHECK(mgr.setParallax(id, 0.06f, 8.f, 32.f) == DecalParallaxStatus::Applied);
    CHECK(mgr.instances()[0].parallaxScale == 0.06f);
    CHECK(mgr.instances()[0].parallaxMinLayers == 8.f);
    CHECK(mgr.instances()[0].parallaxMaxLayers == 32.f);
    CHECK(mgr.setParallax(id, -0.1f, 8.f, 32.f) == DecalParallaxStatus::InvalidScale);
    CHECK(mgr.setParallax(id, 0.1f, 32.f, 8.f) == DecalParallaxStatus::InvalidLayers);
    CHECK(mgr.setParallax(99999, 0.1f, 8.f, 32.f) == DecalParallaxStatus::UnknownId);
    CHECK(mgr.setEdgeFade(id, 0.12f) == DecalEdgeFadeStatus::Applied);
    CHECK(mgr.instances()[0].edgeFadeWidth == 0.12f);
    CHECK(mgr.setEdgeFade(id, 0.5f) == DecalEdgeFadeStatus::InvalidWidth);
    CHECK(mgr.setEdgeFade(99999, 0.1f) == DecalEdgeFadeStatus::UnknownId);

    CHECK(mgr.setTextures(id, nullptr, nullptr));
    CHECK(!mgr.setUvRect(99999, 0.f, 0.f, 1.f, 1.f));  // unknown id
}

TEST_CASE("decal.capabilityProvidedToConsumers") {
    // Instantiate the decal module (registers the drawer + capability).
    auto *const decalModule = eve::ModuleManager::requireInstance<eve::decal::Decal>("Decal");
    REQUIRE(decalModule != nullptr);
    auto *q = eve::cap::query<eve::IDecalQuery>();
    REQUIRE(q != nullptr);
    q->clearAll();
    CHECK(q->count() == 0);
    q->setLimit("blood", 1);
    const int id = q->project(0.f, 0.f, 0.f, 0.f, 1.f, 0.f, nullptr, "blood", 0.5f, 0.15f, false,
                              0, 0.f, 0.f, 0.f);
    CHECK(id > 0);
    CHECK(q->count() == 1);
    CHECK(q->remove(id));
    CHECK(q->count() == 0);
}

TEST_CASE("decal.renderControlFeatureGatesPass") {
    eve::graphics::RenderControl rc;
    CHECK(rc.supports("decal"));
    CHECK(!rc.isEnabled("decal"));

    rc.enable("decal");
    CHECK(rc.isEnabled("decal"));
    CHECK(rc.isEnabled("gbuffer"));  // decal implies the G-buffer (depth/normal inputs)

    rc.compile();
    CHECK(rc.hasPass("decal"));
    // Order: shadow -> gbuffer -> decal -> forward -> hair.
    CHECK(rc.getPassName(1) == "gbuffer");
    CHECK(rc.getPassName(2) == "decal");
    CHECK(rc.getPassName(3) == "forward");

    rc.disable("gbuffer");
    CHECK(!rc.isEnabled("decal"));  // dropping the G-buffer drops decals too
}

namespace {

eve::graphics::Texture *makeSolidTex(eve::graphics::Graphics *gfx, uint8_t r, uint8_t g,
                                     uint8_t b) {
    const int size = 16;
    std::vector<uint8_t> px(size_t(size) * size_t(size) * 4u);
    for (size_t i = 0; i < px.size(); i += 4) {
        px[i] = r;
        px[i + 1] = g;
        px[i + 2] = b;
        px[i + 3] = 255;
    }
    return gfx->newTexture(size, size, px.data());
}

eve::graphics::Mesh *makePlane(eve::graphics::Graphics *gfx, float size) {
    const float h = size * 0.5f;
    const std::vector<float> pos = {-h, -h, 0.f, h, -h, 0.f, h, h, 0.f, -h, h, 0.f};
    const std::vector<float> nrm = {0.f, 0.f, 1.f, 0.f, 0.f, 1.f,
                                    0.f, 0.f, 1.f, 0.f, 0.f, 1.f};
    const std::vector<float> uv = {0.f, 0.f, 1.f, 0.f, 1.f, 1.f, 0.f, 1.f};
    const std::vector<uint32_t> idx = {0, 1, 2, 0, 2, 3};
    return gfx->newMeshFromArrays(pos.data(), nrm.data(), uv.data(), 4, idx.data(), 6);
}

}  // namespace

// End-to-end smoke: a red decal projected onto a white plane must change the
// screen-center pixel toward red (the decal layer is written between the
// G-buffer and forward passes, and mesh3d.frag blends it before lighting).
TEST_CASE("decal.gpuProjectBlendsIntoForward") {
    eve::window::Window *win = nullptr;
    eve::graphics::Graphics *gfx = nullptr;
    openGfxWindow(win, gfx);

    auto *mesh = makePlane(gfx, 2.f);
    REQUIRE(mesh != nullptr);

    auto *cam = Camera3D::createCamera();
    cam->data()->eyeZ = 2.6f;

    auto *ent = Renderable3D::create();
    ent->meshRenderer()->mesh = mesh;
    ent->meshRenderer()->texture = makeSolidTex(gfx, 255, 255, 255);
    RenderSystem3D::setDirectionalLight(0.4f, 1.f, 0.3f, 1.f, 1.f, 1.f);

    // Enable the decal pass and feed it from DecalManager through a drawer
    // (same path the Decal script module registers at startup).
    static bool sDrawerRegistered = false;
    if (!sDrawerRegistered) {
        sDrawerRegistered = true;
        RenderSystem3D::addDecalExtraDrawer(
            [](eve::graphics::Graphics &g, const Camera3D::Data &camData,
               const glm::mat4 &viewProj, float aspect) {
                DecalManager::inst().drawAll(g, camData.eyeX, camData.eyeY, camData.eyeZ,
                                             viewProj, aspect);
            });
    }
    gfx->getRenderControl()->enable("decal");
    gfx->getRenderControl()->compile();

    auto *decalTex = makeSolidTex(gfx, 200, 20, 20);
    DecalManager::inst().clearAll();
    DecalManager::inst().project(0.f, 0.f, 0.f, 0.f, 0.f, 1.f, decalTex, "smoke", 1.2f, 0.1f,
                                 false, 0, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f);

    gfx->setScreenReadbackEnabled(true);
    for (int i = 0; i < 3; ++i) {
        RenderSystem3D::render(*gfx);
        RenderSystem::render(*gfx);
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) break;
        }
    }

    const int W = gfx->getWidth();
    const int H = gfx->getHeight();
    const Color center = gfx->getPixel(W / 2, H / 2);
    const Color corner = gfx->getPixel(4, 4);
    // Center (inside the 1.2m decal on a 2m plane) must be red-tinted; the
    // corner stays near-white.
    CHECK_GT(center.r - center.g, 0.1f);
    CHECK_GT(center.r - center.b, 0.1f);
    CHECK_LT(corner.r - corner.g, 0.1f);

    DecalManager::inst().clearAll();
    win->close();
}

// Emissive decals use the additive layer pipeline: a red glow on a dark plane
// must visibly brighten the screen center vs. the unlit corner.
TEST_CASE("decal.gpuEmissiveAdditiveGlow") {
    eve::window::Window *win = nullptr;
    eve::graphics::Graphics *gfx = nullptr;
    openGfxWindow(win, gfx);

    auto *mesh = makePlane(gfx, 2.f);
    REQUIRE(mesh != nullptr);

    auto *cam = Camera3D::createCamera();
    cam->data()->eyeZ = 2.6f;

    auto *ent = Renderable3D::create();
    ent->meshRenderer()->mesh = mesh;
    ent->meshRenderer()->texture = makeSolidTex(gfx, 60, 60, 60);  // dark base
    RenderSystem3D::setDirectionalLight(0.4f, 1.f, 0.3f, 1.f, 1.f, 1.f);

    static bool sGlowDrawer = false;
    if (!sGlowDrawer) {
        sGlowDrawer = true;
        RenderSystem3D::addDecalExtraDrawer(
            [](eve::graphics::Graphics &g, const Camera3D::Data &camData,
               const glm::mat4 &viewProj, float aspect) {
                DecalManager::inst().drawAll(g, camData.eyeX, camData.eyeY, camData.eyeZ,
                                             viewProj, aspect);
            });
    }
    gfx->getRenderControl()->enable("decal");
    gfx->getRenderControl()->compile();

    auto *glowColor = makeSolidTex(gfx, 200, 20, 20);  // red glow color
    auto *glowParams = makeSolidTex(gfx, 0, 0, 255);   // B = emissive intensity
    DecalManager::inst().clearAll();
    const int id = DecalManager::inst().project(0.f, 0.f, 0.f, 0.f, 0.f, 1.f, glowColor, "glow",
                                                1.2f, 0.1f, false, 0, 0.f, 0.f, 0.f, 0.f, 0.f,
                                                0.f, 0.f);
    CHECK(id > 0);
    CHECK(DecalManager::inst().setTextures(id, nullptr, glowParams));
    CHECK(DecalManager::inst().setBlend(id, "add"));
    CHECK(DecalManager::inst().setStrength(id, 0.f, 0.f, 0.f, 1.f));

    gfx->setScreenReadbackEnabled(true);
    for (int i = 0; i < 3; ++i) {
        RenderSystem3D::render(*gfx);
        RenderSystem::render(*gfx);
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) break;
        }
    }

    const int W = gfx->getWidth();
    const int H = gfx->getHeight();
    const Color center = gfx->getPixel(W / 2, H / 2);
    const Color corner = gfx->getPixel(4, 4);
    CHECK_GT(center.r, corner.r + 0.2f);  // emissive red lifts the center
    CHECK_GT(center.r - center.g, 0.1f);  // ... toward red, not white

    DecalManager::inst().clearAll();
    win->close();
}

// Headless verification of the decal pass itself: queue a G-buffer plane + one
// decal, then read the DecalLayer back to CPU. Works without a swapchain, so it
// also runs when the interactive GPU/surface is busy.
TEST_CASE("decal.gpuHeadlessProjectionReadback") {
    eve::graphics::Graphics *gfx = nullptr;
    openHeadlessGfx(gfx, 160, 120);

    auto *mesh = makePlane(gfx, 2.f);
    REQUIRE(mesh != nullptr);
    gfx->getRenderControl()->enable("decal");
    gfx->getRenderControl()->compile();

    gfx->beginGBufferPass(160, 120);
    gfx->drawMeshGBuffer(mesh, glm::mat4(1.f), glm::mat4(1.f), 0.1f, 100.f,
                         makeSolidTex(gfx, 255, 255, 255));
    gfx->endGBufferPass();

    auto *decalTex = makeSolidTex(gfx, 200, 20, 20);
    gfx->beginDecalPass(160, 120);
    gfx->setDecalCamera(glm::mat4(1.f), 0.1f, 100.f);
    // Identity box at the origin covers the screen center.
    gfx->drawDecal(glm::mat4(1.f), decalTex, nullptr, nullptr, nullptr, 1.f, 0.f, 0.f, 0.f, 0.f);
    gfx->endDecalPass();

    auto *img = gfx->readDecalLayerToImageData("albedo");
    REQUIRE(img != nullptr);
    CHECK_EQ(img->getWidth(), 160);
    CHECK_EQ(img->getHeight(), 120);

    const uint8_t *px = static_cast<const uint8_t *>(img->getData());
    auto at = [&](int x, int y) { return px + (size_t(y) * 160u + size_t(x)) * 4u; };
    const uint8_t *center = at(80, 60);
    CHECK_GT(center[0], 150);  // red decal written at the center
    CHECK_LT(center[1], 80);
    const uint8_t *corner = at(4, 4);
    CHECK_LT(corner[0], 20);  // outside the unit box -> layer stays cleared
    delete img;
}

// Triplanar mode paints a wall that classic planar projection discards (facing
// nearly orthogonal to the decal forward). Planar leaves the wall near the
// base albedo color.
TEST_CASE("decal.gpuTriplanarCoversGrazingWall") {
    eve::window::Window *win = nullptr;
    eve::graphics::Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);

    // Vertical wall in XY (normal +Z), viewed from +Z.
    auto *wall = makePlane(gfx, 2.f);
    REQUIRE(wall != nullptr);

    auto *cam = Camera3D::createCamera();
    cam->data()->eyeZ = 2.6f;

    auto *ent = Renderable3D::create();
    ent->meshRenderer()->mesh = wall;
    ent->meshRenderer()->texture = makeSolidTex(gfx, 180, 180, 180);
    RenderSystem3D::setDirectionalLight(0.2f, 0.5f, 1.f, 1.f, 1.f, 1.f);

    static bool sTripDrawer = false;
    if (!sTripDrawer) {
        sTripDrawer = true;
        RenderSystem3D::addDecalExtraDrawer(
            [](eve::graphics::Graphics &g, const Camera3D::Data &camData,
               const glm::mat4 &viewProj, float aspect) {
                DecalManager::inst().drawAll(g, camData.eyeX, camData.eyeY, camData.eyeZ,
                                             viewProj, aspect);
            });
    }
    gfx->getRenderControl()->enable("decal");
    gfx->getRenderControl()->compile();

    auto *decalTex = makeSolidTex(gfx, 200, 20, 20);
    auto &mgr = DecalManager::inst();
    mgr.clearAll();
    // Project along +Y onto a +Z wall — planar culls, triplanar keeps coverage.
    const int id = mgr.project(0.f, 0.f, 0.f, 0.f, 1.f, 0.f, decalTex, "trip", 2.f, 2.f, false,
                               0, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f);
    REQUIRE(id > 0);

    auto centerRedness = [&]() -> float {
        gfx->setScreenReadbackEnabled(true);
        for (int i = 0; i < 3; ++i) {
            RenderSystem3D::render(*gfx);
            RenderSystem::render(*gfx);
            SDL_Event e;
            while (SDL_PollEvent(&e)) {
                if (e.type == SDL_QUIT) break;
            }
        }
        const Color c = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
        return c.r - c.g;
    };

    REQUIRE(mgr.setProjection(id, "planar", 4.f) == DecalProjectionStatus::Applied);
    const float planarScore = centerRedness();

    REQUIRE(mgr.setProjection(id, "triplanar", 4.f) == DecalProjectionStatus::Applied);
    const float triplanarScore = centerRedness();

    REQUIRE(mgr.setProjection(id, "world", 4.f) == DecalProjectionStatus::Applied);
    const float worldScore = centerRedness();

    // Triplanar must shift the wall toward the red decal; planar should not.
    REQUIRE(triplanarScore > 0.12f);
    REQUIRE(triplanarScore > planarScore + 0.08f);
    REQUIRE(worldScore > 0.12f);
    REQUIRE(worldScore > planarScore + 0.08f);

    mgr.clearAll();
    win->close();
}

namespace {

float smoothstep(float e0, float e1, float x) {
    const float t = std::clamp((x - e0) / (e1 - e0), 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}

// Procedural decal art so the gallery needs no external assets.
eve::graphics::Texture *makeChecker(eve::graphics::Graphics *gfx, int size, int cells) {
    std::vector<uint8_t> px(size_t(size) * size_t(size) * 4u);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const bool dark = ((x * cells / size) + (y * cells / size)) % 2 == 0;
            const uint8_t v = dark ? 150 : 205;
            size_t i = (size_t(y) * size_t(size) + size_t(x)) * 4u;
            px[i] = v;
            px[i + 1] = v;
            px[i + 2] = v;
            px[i + 3] = 255;
        }
    }
    return gfx->newTexture(size, size, px.data());
}

eve::graphics::Texture *makeBloodSplat(eve::graphics::Graphics *gfx, int size) {
    std::vector<uint8_t> px(size_t(size) * size_t(size) * 4u);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const float u = (float(x) + 0.5f) / float(size);
            const float v = (float(y) + 0.5f) / float(size);
            const float dx = u - 0.5f;
            const float dy = v - 0.5f;
            const float ang = std::atan2(dy, dx);
            const float r = std::sqrt(dx * dx + dy * dy) * 2.f;
            // Irregular splat edge: three-lobe wobble + inner core.
            const float wob = 0.72f + 0.22f * std::sin(3.f * ang + 1.3f) +
                              0.10f * std::sin(5.f * ang + 4.2f);
            const float a = 1.f - smoothstep(0.30f, wob, r);
            const float core = 1.f - smoothstep(0.0f, 0.34f, r);
            size_t i = (size_t(y) * size_t(size) + size_t(x)) * 4u;
            px[i] = 130;
            px[i + 1] = 14;
            px[i + 2] = 24;
            px[i + 3] = uint8_t(std::clamp((a * 0.55f + core * 0.45f) * 255.f, 0.f, 255.f));
        }
    }
    return gfx->newTexture(size, size, px.data());
}

eve::graphics::Texture *makeDirt(eve::graphics::Graphics *gfx, int size) {
    std::vector<uint8_t> px(size_t(size) * size_t(size) * 4u);
    uint32_t seed = 12345u;
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            seed = seed * 1664525u + 1013904223u;
            const float n = float(seed % 10000u) / 10000.f;
            const float a = n < 0.62f ? 0.25f + 0.35f * n : 0.f;
            size_t i = (size_t(y) * size_t(size) + size_t(x)) * 4u;
            px[i] = 118;
            px[i + 1] = 102;
            px[i + 2] = 82;
            px[i + 3] = uint8_t(a * 255.f);
        }
    }
    return gfx->newTexture(size, size, px.data());
}

eve::graphics::Texture *makeDentNormal(eve::graphics::Graphics *gfx, int size) {
    // Concave dent: tangent-space normal tilts inward toward the center.
    std::vector<uint8_t> px(size_t(size) * size_t(size) * 4u);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const float u = (float(x) + 0.5f) / float(size);
            const float v = (float(y) + 0.5f) / float(size);
            const float dx = (u - 0.5f) * 2.f;
            const float dy = (v - 0.5f) * 2.f;
            const float r = std::sqrt(dx * dx + dy * dy);
            float tilt = 1.6f * std::exp(-r * r * 2.6f);
            // Raised rim just outside the crater.
            tilt -= 0.8f * std::exp(-(r - 0.55f) * (r - 0.55f) * 60.f);
            glm::vec3 n = glm::normalize(glm::vec3(-dx * tilt, -dy * tilt, 1.f));
            size_t i = (size_t(y) * size_t(size) + size_t(x)) * 4u;
            px[i] = uint8_t((n.x * 0.5f + 0.5f) * 255.f);
            px[i + 1] = uint8_t((n.y * 0.5f + 0.5f) * 255.f);
            px[i + 2] = uint8_t((n.z * 0.5f + 0.5f) * 255.f);
            px[i + 3] = 255;
        }
    }
    return gfx->newTexture(size, size, px.data());
}

eve::graphics::Texture *makeDentAlbedo(eve::graphics::Graphics *gfx, int size) {
    std::vector<uint8_t> px(size_t(size) * size_t(size) * 4u);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const float u = (float(x) + 0.5f) / float(size);
            const float v = (float(y) + 0.5f) / float(size);
            const float dx = (u - 0.5f) * 2.f;
            const float dy = (v - 0.5f) * 2.f;
            const float r = std::sqrt(dx * dx + dy * dy);
            const float crater = 1.f - smoothstep(0.f, 0.55f, r);
            const float ring =
                smoothstep(0.42f, 0.55f, r) * (1.f - smoothstep(0.55f, 0.68f, r));
            const uint8_t val =
                uint8_t(std::clamp((68.f + crater * -28.f + ring * 42.f), 0.f, 255.f));
            size_t i = (size_t(y) * size_t(size) + size_t(x)) * 4u;
            px[i] = val;
            px[i + 1] = uint8_t(val * 0.93f);
            px[i + 2] = uint8_t(val * 0.88f);
            px[i + 3] = 255;
        }
    }
    return gfx->newTexture(size, size, px.data());
}

eve::graphics::Texture *makeRoughParams(eve::graphics::Graphics *gfx, int size, float rough) {
    std::vector<uint8_t> px(size_t(size) * size_t(size) * 4u);
    for (size_t i = 0; i < px.size(); i += 4) {
        px[i] = uint8_t(rough * 255.f);  // R = target roughness
        px[i + 1] = 0;                    // G = metallic
        px[i + 2] = 0;                    // B = emissive
        px[i + 3] = 255;
    }
    return gfx->newTexture(size, size, px.data());
}

}  // namespace

// Gallery render: blood / dirt / dent decals projected onto a checker floor,
// saved as PNG for visual review (build/<plat>/test/out/decal/).
TEST_CASE("decal.renderGalleryPng") {
    eve::window::Window *win = nullptr;
    eve::graphics::Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 480, 360);

    auto *mesh = makePlane(gfx, 3.f);
    REQUIRE(mesh != nullptr);
    auto *cam = Camera3D::createCamera();
    cam->data()->eyeY = 1.9f;
    cam->data()->eyeZ = 2.35f;
    auto *ent = Renderable3D::create();
    ent->meshRenderer()->mesh = mesh;
    ent->transform()->pitch = -3.14159265f * 0.5f;  // lay the plane flat (normal +Y)
    ent->meshRenderer()->texture = makeChecker(gfx, 64, 8);
    RenderSystem3D::setDirectionalLight(0.4f, 1.f, 0.3f, 1.f, 1.f, 1.f);

    static bool sGalleryDrawer = false;
    if (!sGalleryDrawer) {
        sGalleryDrawer = true;
        RenderSystem3D::addDecalExtraDrawer(
            [](eve::graphics::Graphics &g, const Camera3D::Data &camData,
               const glm::mat4 &viewProj, float aspect) {
                DecalManager::inst().drawAll(g, camData.eyeX, camData.eyeY, camData.eyeZ,
                                             viewProj, aspect);
            });
    }
    gfx->getRenderControl()->enable("decal");
    gfx->getRenderControl()->compile();

    DecalManager::inst().clearAll();
    auto &mgr = DecalManager::inst();
    auto bloodRecipe = proceduralDecalPreset("blood-wet", 7u);
    auto dirtRecipe = proceduralDecalPreset("dirt", 23u);
    auto damageRecipe = proceduralDecalPreset("damage", 37u);
    REQUIRE(bloodRecipe.ok());
    REQUIRE(dirtRecipe.ok());
    REQUIRE(damageRecipe.ok());
    for (auto *recipe : {&bloodRecipe.value(), &dirtRecipe.value(), &damageRecipe.value()}) {
        recipe->width = 128;
        recipe->height = 128;
    }
    auto bloodBake = bakeProceduralDecal(bloodRecipe.value());
    auto dirtBake = bakeProceduralDecal(dirtRecipe.value());
    auto damageBake = bakeProceduralDecal(damageRecipe.value());
    REQUIRE(bloodBake.ok());
    REQUIRE(dirtBake.ok());
    REQUIRE(damageBake.ok());
    // Blood: dark red splat, wet gloss (roughness down via params R).
    const int blood = mgr.project(-0.85f, 0.01f, 0.15f, 0.f, 1.f, 0.f,
                                  gfx->newTexture(128, 128, bloodBake.value().albedo.data()),
                                  "blood", 1.05f, 0.12f, true, 7, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f,
                                  0.f);
    CHECK(mgr.setTextures(blood, gfx->newTexture(128, 128, bloodBake.value().normal.data()),
                          gfx->newTexture(128, 128, bloodBake.value().params.data())));
    CHECK(mgr.setStrength(blood, 1.f, 1.f, 0.f, 0.f));
    // Dirt: gray-brown speckle, rough + slight normal wobble.
    const int dirt = mgr.project(0.f, 0.01f, 0.1f, 0.f, 1.f, 0.f,
                                 gfx->newTexture(128, 128, dirtBake.value().albedo.data()), "dirt",
                                 1.3f, 0.12f, true, 23, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f);
    CHECK(mgr.setTextures(dirt, gfx->newTexture(128, 128, dirtBake.value().normal.data()),
                          gfx->newTexture(128, 128, dirtBake.value().params.data())));
    CHECK(mgr.setStrength(dirt, 1.f, 1.f, 0.f, 0.f));
    CHECK(mgr.setProjection(dirt, "spherical", 4.f) == DecalProjectionStatus::Applied);
    // Dent: concave normal map + crater albedo (real indentation shading).
    const int dent = mgr.project(0.85f, 0.01f, 0.15f, 0.f, 1.f, 0.f,
                                 gfx->newTexture(128, 128, damageBake.value().albedo.data()),
                                 "dent", 1.05f, 0.12f, false, 0, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f,
                                 0.f);
    CHECK(mgr.setTextures(dent, gfx->newTexture(128, 128, damageBake.value().normal.data()),
                          gfx->newTexture(128, 128, damageBake.value().params.data())));
    CHECK(mgr.setStrength(dent, 1.f, 1.f, 0.f, 0.f));
    CHECK(mgr.setParallax(dent, 0.055f, 8.f, 24.f) == DecalParallaxStatus::Applied);

    gfx->setScreenReadbackEnabled(true);
    for (int i = 0; i < 3; ++i) {
        RenderSystem3D::render(*gfx);
        RenderSystem::render(*gfx);
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) break;
        }
    }
    eve::image::ImageData *snap = gfx->newImageData();
    REQUIRE(snap != nullptr);
    const std::string outDir = std::string(EVENGINE_TEST_BINARY_DIR) + "/out/decal";
    std::error_code ec;
    std::filesystem::create_directories(outDir, ec);
    REQUIRE(saveImagePng(*snap, outDir + "/decal_gallery.png"));
    std::printf("decal gallery saved: %s/decal_gallery.png\n", outDir.c_str());
    delete snap;

    DecalManager::inst().clearAll();
    win->close();
}
