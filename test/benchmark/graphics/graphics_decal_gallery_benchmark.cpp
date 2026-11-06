// eve-test-policy:
// {"schema":"evengine.test-policy","version":1,"role":"tests","default":"render-window","module":"decal","cases":{"platform-file":["decal.renderImportedPresetContactSheet"]}}
#include <SDL2/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include "Fixtures.h"
#include "PrivateTestScope.h"
#include "RenderImageAudit.h"
#include "decal/Decal.h"
#include "decal/DecalManager.h"
#include "decal/ProceduralDecal.h"
#include "graphics/Graphics.h"
#include "graphics/Mesh.h"
#include "graphics/RenderControl.h"
#include "graphics/RenderSystem.h"
#include "graphics/RenderSystem3D.h"
#include "graphics/Texture.h"
#include "image/ImageData.h"
#include "window/Window.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
namespace test_decal_gallery_benchmark {
using namespace eve::decal;
using namespace eve::graphics;
namespace {
[[maybe_unused]] eve::graphics::Texture *makeSolidTex(eve::graphics::Graphics *gfx, uint8_t r, uint8_t g, uint8_t b) {
    const int            size = 16;
    std::vector<uint8_t> px(size_t(size) * size_t(size) * 4u);
    for (size_t i = 0; i < px.size(); i += 4) {
        px[i]     = r;
        px[i + 1] = g;
        px[i + 2] = b;
        px[i + 3] = 255;
    }
    return gfx->newTexture(size, size, px.data());
}

[[maybe_unused]] eve::graphics::Mesh *makePlane(eve::graphics::Graphics *gfx, float size) {
    const float                 h   = size * 0.5f;
    const std::vector<float>    pos = {-h, -h, 0.f, h, -h, 0.f, h, h, 0.f, -h, h, 0.f};
    const std::vector<float>    nrm = {0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f};
    const std::vector<float>    uv  = {0.f, 0.f, 1.f, 0.f, 1.f, 1.f, 0.f, 1.f};
    const std::vector<uint32_t> idx = {0, 1, 2, 0, 2, 3};
    return gfx->newMeshFromArrays(pos.data(), nrm.data(), uv.data(), 4, idx.data(), 6);
}

[[maybe_unused]] eve::graphics::Texture *makeChecker(eve::graphics::Graphics *gfx, int size, int cells) {
    std::vector<uint8_t> px(size_t(size) * size_t(size) * 4u);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const bool    dark = ((x * cells / size) + (y * cells / size)) % 2 == 0;
            const uint8_t v    = dark ? 150 : 205;
            size_t        i    = (size_t(y) * size_t(size) + size_t(x)) * 4u;
            px[i]              = v;
            px[i + 1]          = v;
            px[i + 2]          = v;
            px[i + 3]          = 255;
        }
    }
    return gfx->newTexture(size, size, px.data());
}
}  // namespace
TEST_CASE("decal.renderImportedPresetContactSheet") {
    const auto directory = std::filesystem::path(EVENGINE_SOURCE_DIR) / "test" / "fixtures" / "procedural_decal";
    std::vector<std::filesystem::path> presets;
    for (const auto &entry : std::filesystem::directory_iterator(directory)) {
        if (entry.path().extension() == ".sbsprs") presets.push_back(entry.path());
    }
    std::sort(presets.begin(), presets.end());
    REQUIRE_EQ(presets.size(), 40u);

    constexpr int             tile        = 128;
    constexpr int             gap         = 4;
    constexpr int             columns     = 5;
    constexpr int             rows        = 8;
    constexpr int             sheetWidth  = columns * tile + (columns + 1) * gap;
    constexpr int             sheetHeight = rows * tile + (rows + 1) * gap;
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

    const auto      output = std::filesystem::path(EVENGINE_TEST_BINARY_DIR) / "out" / "decal" / "imported_presets";
    std::error_code ec;
    std::filesystem::create_directories(output, ec);
    REQUIRE(!ec);
    std::ofstream manifest(output / "contact_sheet_order.txt", std::ios::binary);
    REQUIRE(manifest.good());

    for (std::size_t presetIndex = 0; presetIndex < presets.size(); ++presetIndex) {
        std::ifstream stream(presets[presetIndex], std::ios::binary);
        REQUIRE(stream.good());
        const std::string xml((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
        auto              recipe = importProceduralDecalSbsprs(xml);
        REQUIRE(recipe.ok());
        recipe.value().width  = tile;
        recipe.value().height = tile;
        auto bake             = bakeProceduralDecal(recipe.value());
        REQUIRE(bake.ok());

        eve::image::ImageData image(tile, tile, "RGBA8", bake.value().albedo.data(), false);
        REQUIRE(saveImagePng(image, (output / (presets[presetIndex].stem().string() + ".png")).string()));
        manifest << presetIndex + 1 << "\t" << presets[presetIndex].stem().string() << "\n";

        const int originX = gap + static_cast<int>(presetIndex % columns) * (tile + gap);
        const int originY = gap + static_cast<int>(presetIndex / columns) * (tile + gap);
        for (int y = 0; y < tile; ++y) {
            for (int x = 0; x < tile; ++x) {
                const auto  source = static_cast<std::size_t>((y * tile + x) * 4);
                const auto  target = static_cast<std::size_t>(((originY + y) * sheetWidth + originX + x) * 4);
                const float alpha  = float(bake.value().albedo[source + 3]) / 255.f;
                for (int channel = 0; channel < 3; ++channel) {
                    sheet[target + channel] =
                        static_cast<std::uint8_t>(float(bake.value().albedo[source + channel]) * alpha +
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
    eve::window::Window     *win = nullptr;
    eve::graphics::Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 1280, 800);

    auto *cam         = Camera3D::createCamera();
    cam->data()->eyeZ = 6.0f;
    cam->setAmbient(0.075f, 0.08f, 0.095f);
    RenderSystem3D::setDirectionalLight(-0.65f, 0.8f, 0.9f, 1.f, 0.92f, 0.78f);

    auto *background                      = Renderable3D::create();
    background->meshRenderer()->mesh      = makePlane(gfx, 12.f);
    background->meshRenderer()->texture   = makeSolidTex(gfx, 28, 32, 42);
    background->meshRenderer()->roughness = 0.9f;
    background->transform()->z            = -0.65f;

    static bool sMaterialSphereDrawer = false;
    if (!sMaterialSphereDrawer) {
        sMaterialSphereDrawer = true;
        RenderSystem3D::addDecalExtraDrawer(
            [](eve::graphics::Graphics &g, const Camera3D::Data &camData, const glm::mat4 &viewProj, float aspect) {
                DecalManager::inst().drawAll(g, camData.eyeX, camData.eyeY, camData.eyeZ, viewProj, aspect);
            });
    }
    gfx->getRenderControl()->enable("decal");
    gfx->getRenderControl()->compile();
    gfx->setScreenReadbackEnabled(true);

    const auto directory = std::filesystem::path(EVENGINE_SOURCE_DIR) / "test" / "fixtures" / "procedural_decal";
    std::vector<std::filesystem::path> presets;
    for (const auto &entry : std::filesystem::directory_iterator(directory)) {
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
        auto              recipe = importProceduralDecalSbsprs(xml);
        REQUIRE(recipe.ok());
        recipe.value().width  = 128;
        recipe.value().height = 128;
        auto bake             = bakeProceduralDecal(recipe.value());
        REQUIRE(bake.ok());

        const int   column                = static_cast<int>(presetIndex % columns);
        const int   row                   = static_cast<int>(presetIndex / columns);
        const float x                     = (float(column) - 3.5f) * 0.9f;
        const float y                     = (2.f - float(row)) * 0.9f;
        auto       *entity                = Renderable3D::create();
        entity->meshRenderer()->mesh      = sphere;
        entity->meshRenderer()->texture   = makeSolidTex(gfx, 185, 185, 190);
        entity->meshRenderer()->roughness = 0.48f;
        entity->setPosition(x, y, 0.f);
        entity->setScale(0.36f, 0.36f, 0.36f);

        const int decal = DecalManager::inst().project(
            x, y, 0.18f, 0.f, 0.f, 1.f, gfx->newTexture(128, 128, bake.value().albedo.data()), "material-sphere", 0.78f,
            0.72f, false, static_cast<int>(presetIndex), 0.f, 0.f, 0.f, 1.f, 1.f, 1.f, 1.f);
        REQUIRE(DecalManager::inst().setTextures(decal, gfx->newTexture(128, 128, bake.value().normal.data()),
                                                 gfx->newTexture(128, 128, bake.value().params.data())));
        REQUIRE(DecalManager::inst().setProjection(decal, "spherical", 4.f) == DecalProjectionStatus::Applied);
        REQUIRE(DecalManager::inst().setParallax(decal, 0.025f, 8.f, 24.f) == DecalParallaxStatus::Applied);
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
    const auto      output = std::filesystem::path(EVENGINE_TEST_BINARY_DIR) / "out" / "decal" / "imported_presets";
    std::error_code ec;
    std::filesystem::create_directories(output, ec);
    REQUIRE(!ec);
    REQUIRE(saveImagePng(*snapshot, (output / "lit_material_spheres.png").string()));
    delete snapshot;

    DecalManager::inst().clearAll();
    win->close();
}

TEST_CASE("decal.renderGalleryPng") {
    eve::window::Window     *win = nullptr;
    eve::graphics::Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 480, 360);

    auto *mesh = makePlane(gfx, 3.f);
    REQUIRE(mesh != nullptr);
    auto *cam                    = Camera3D::createCamera();
    cam->data()->eyeY            = 1.9f;
    cam->data()->eyeZ            = 2.35f;
    auto *ent                    = Renderable3D::create();
    ent->meshRenderer()->mesh    = mesh;
    ent->transform()->pitch      = -3.14159265f * 0.5f;  // lay the plane flat (normal +Y)
    ent->meshRenderer()->texture = makeChecker(gfx, 64, 8);
    RenderSystem3D::setDirectionalLight(0.4f, 1.f, 0.3f, 1.f, 1.f, 1.f);

    static bool sGalleryDrawer = false;
    if (!sGalleryDrawer) {
        sGalleryDrawer = true;
        RenderSystem3D::addDecalExtraDrawer(
            [](eve::graphics::Graphics &g, const Camera3D::Data &camData, const glm::mat4 &viewProj, float aspect) {
                DecalManager::inst().drawAll(g, camData.eyeX, camData.eyeY, camData.eyeZ, viewProj, aspect);
            });
    }
    gfx->getRenderControl()->enable("decal");
    gfx->getRenderControl()->compile();

    DecalManager::inst().clearAll();
    auto &mgr          = DecalManager::inst();
    auto  bloodRecipe  = proceduralDecalPreset("blood-wet", 7u);
    auto  dirtRecipe   = proceduralDecalPreset("dirt", 23u);
    auto  damageRecipe = proceduralDecalPreset("damage", 37u);
    REQUIRE(bloodRecipe.ok());
    REQUIRE(dirtRecipe.ok());
    REQUIRE(damageRecipe.ok());
    for (auto *recipe : {&bloodRecipe.value(), &dirtRecipe.value(), &damageRecipe.value()}) {
        recipe->width  = 128;
        recipe->height = 128;
    }
    auto bloodBake  = bakeProceduralDecal(bloodRecipe.value());
    auto dirtBake   = bakeProceduralDecal(dirtRecipe.value());
    auto damageBake = bakeProceduralDecal(damageRecipe.value());
    REQUIRE(bloodBake.ok());
    REQUIRE(dirtBake.ok());
    REQUIRE(damageBake.ok());
    // Blood: dark red splat, wet gloss (roughness down via params R).
    const int blood =
        mgr.project(-0.85f, 0.01f, 0.15f, 0.f, 1.f, 0.f, gfx->newTexture(128, 128, bloodBake.value().albedo.data()),
                    "blood", 1.05f, 0.12f, true, 7, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f);
    CHECK(mgr.setTextures(blood, gfx->newTexture(128, 128, bloodBake.value().normal.data()),
                          gfx->newTexture(128, 128, bloodBake.value().params.data())));
    CHECK(mgr.setStrength(blood, 1.f, 1.f, 0.f, 0.f));
    // Dirt: gray-brown speckle, rough + slight normal wobble.
    const int dirt =
        mgr.project(0.f, 0.01f, 0.1f, 0.f, 1.f, 0.f, gfx->newTexture(128, 128, dirtBake.value().albedo.data()), "dirt",
                    1.3f, 0.12f, true, 23, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f);
    CHECK(mgr.setTextures(dirt, gfx->newTexture(128, 128, dirtBake.value().normal.data()),
                          gfx->newTexture(128, 128, dirtBake.value().params.data())));
    CHECK(mgr.setStrength(dirt, 1.f, 1.f, 0.f, 0.f));
    CHECK(mgr.setProjection(dirt, "spherical", 4.f) == DecalProjectionStatus::Applied);
    // Dent: concave normal map + crater albedo (real indentation shading).
    const int dent =
        mgr.project(0.85f, 0.01f, 0.15f, 0.f, 1.f, 0.f, gfx->newTexture(128, 128, damageBake.value().albedo.data()),
                    "dent", 1.05f, 0.12f, false, 0, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f);
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
    std::error_code   ec;
    std::filesystem::create_directories(outDir, ec);
    REQUIRE(saveImagePng(*snap, outDir + "/decal_gallery.png"));
    std::printf("decal gallery saved: %s/decal_gallery.png\n", outDir.c_str());
    delete snap;

    DecalManager::inst().clearAll();
    win->close();
}
}  // namespace test_decal_gallery_benchmark
