#include "PathBesideSource.h"
#include "housegen/HouseComponentLibrary.h"
#include "housegen/HouseGenerator.h"
#include "housegen/HouseLayout.h"

#include "data/ByteData.h"
#include "filesystem/FileData.h"
#include "graphics/AmbientOcclusion.h"
#include "graphics/AntiAliasing.h"
#include "graphics/Canvas.h"
#include "graphics/DrawItem2D.h"
#include "graphics/Font.h"
#include "graphics/GBuffer.h"
#include "graphics/GlobalIllumination.h"
#include "graphics/Graphics.h"
#include "graphics/Grass.h"
#include "graphics/Light.h"
#include "graphics/Material.h"
#include "graphics/Mesh.h"
#include "graphics/Outline.h"
#include "graphics/Quad.h"
#include "graphics/RenderControl.h"
#include "graphics/RenderSystem.h"
#include "graphics/RenderSystem3D.h"
#include "graphics/ScreenSpaceReflection.h"
#include "graphics/Shader.h"
#include "graphics/Texture.h"
#include "graphics/Volumetric.h"
#include "graphics/Water.h"
#include "graphics/Waterfall.h"
#include "image/Image.h"
#include "image/ImageData.h"
#include "medialoader/model/ModelLoader.h"
#include "model3d/Model3D.h"
#include "window/Window.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <SDL2/SDL.h>
#include <assimp/scene.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>
// Color lives in eve::graphics (see graphics/Canvas.h); keep the unqualified form.
using eve::graphics::Color;

using namespace eve::graphics;
using namespace eve::housegen;

namespace {

int envInt(const char *name, int fallback) {
    const char *value = std::getenv(name);
    if (!value || !*value) return fallback;
    try {
        return std::stoi(value);
    } catch (...) {
        return fallback;
    }
}

std::string envString(const char *name, const char *fallback) {
    const char *value = std::getenv(name);
    return value && *value ? value : fallback;
}

struct Palette {
    float wallR, wallG, wallB;
    float trimR, trimG, trimB;
    float roofR, roofG, roofB;
    float glassR, glassG, glassB;
};

Palette gPalette{0.76f, 0.58f, 0.38f, 0.82f, 0.64f, 0.42f,
                 0.42f, 0.16f, 0.13f, 0.18f, 0.55f, 0.72f};

void selectPalette(uint32_t seed) {
    switch (seed % 3u) {
    case 0: gPalette = {0.78f, 0.80f, 0.76f, 0.25f, 0.28f, 0.30f,
                        0.16f, 0.20f, 0.25f, 0.22f, 0.48f, 0.64f}; break;
    case 1: gPalette = {0.76f, 0.58f, 0.38f, 0.82f, 0.64f, 0.42f,
                        0.42f, 0.16f, 0.13f, 0.18f, 0.55f, 0.72f}; break;
    default: gPalette = {0.64f, 0.70f, 0.78f, 0.92f, 0.90f, 0.82f,
                         0.20f, 0.28f, 0.36f, 0.16f, 0.52f, 0.70f}; break;
    }
}

// A neutral unit cube is enough for this preview: HouseLayout supplies all placement and the
// renderer supplies material tint, lighting and perspective. Real GLB components use the same
// transforms through HouseLayout::instantiate().
constexpr char kCubeObj[] =
    "v -0.5 -0.5  0.5\nv  0.5 -0.5  0.5\nv  0.5  0.5  0.5\nv -0.5  0.5  0.5\n"
    "v  0.5 -0.5 -0.5\nv -0.5 -0.5 -0.5\nv -0.5  0.5 -0.5\nv  0.5  0.5 -0.5\n"
    "vt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\n"
    "vn 0 0 1\nvn 0 0 -1\nvn 1 0 0\nvn -1 0 0\nvn 0 1 0\nvn 0 -1 0\n"
    "f 1/1/1 2/2/1 3/3/1\nf 1/1/1 3/3/1 4/4/1\n"
    "f 5/1/2 6/2/2 7/3/2\nf 5/1/2 7/3/2 8/4/2\n"
    "f 2/1/3 5/2/3 8/3/3\nf 2/1/3 8/3/3 3/4/3\n"
    "f 6/1/4 1/2/4 4/3/4\nf 6/1/4 4/3/4 7/4/4\n"
    "f 4/1/5 3/2/5 8/3/5\nf 4/1/5 8/3/5 7/4/5\n"
    "f 6/1/6 5/2/6 2/3/6\nf 6/1/6 2/3/6 1/4/6\n";

// Unit triangular prism whose base starts at y=0. Scaling it produces a continuous gable end;
// unlike stacked cubes it cannot poke through the sloped roof or leave stair-step notches.
constexpr char kGableObj[] =
    "v -0.5 0.0  0.5\nv 0.5 0.0  0.5\nv 0.0 1.0  0.5\n"
    "v -0.5 0.0 -0.5\nv 0.5 0.0 -0.5\nv 0.0 1.0 -0.5\n"
    "f 1 2 3\nf 6 5 4\nf 1 4 5\nf 1 5 2\n"
    "f 2 5 6\nf 2 6 3\nf 3 6 4\nf 3 4 1\n";

Mesh *previewCube(Graphics *gfx) {
    medialoader::ModelLoader loader;
    auto scene = loader.loadFromMemory(kCubeObj, sizeof(kCubeObj) - 1, ".obj");
    REQUIRE(!scene.empty());
    REQUIRE(scene->mNumMeshes > 0);
    return gfx->newMeshFromAssimp(*scene->mMeshes[0]);
}

Mesh *previewGable(Graphics *gfx) {
    medialoader::ModelLoader loader;
    auto scene = loader.loadFromMemory(kGableObj, sizeof(kGableObj) - 1, ".obj");
    REQUIRE(!scene.empty());
    REQUIRE(scene->mNumMeshes > 0);
    return gfx->newMeshFromAssimp(*scene->mMeshes[0]);
}

Renderable3D *box(Mesh *mesh, float x, float y, float z, float sx, float sy, float sz,
                  float r, float g, float b, float roll = 0.f) {
    auto *entity = Renderable3D::create();
    entity->setMesh(mesh);
    entity->setPosition(x, y, z);
    entity->setScale(sx, sy, sz);
    entity->setRotation(0.f, 0.f, roll);
    entity->setTint(r, g, b, 1.f);
    entity->setRoughness(0.72f);
    return entity;
}

Texture *gBrickColor = nullptr;
Texture *gBrickNormal = nullptr;
Texture *gBrickHeight = nullptr;

Renderable3D *wallSurfaceBox(Mesh *mesh, float x, float y, float z, float sx, float sy,
                             float sz) {
    auto *entity = box(mesh, x, y, z, sx, sy, sz, 1.f, 1.f, 1.f);
    if (gBrickColor) entity->setTexture(gBrickColor);
    if (gBrickNormal) entity->setNormalTexture(gBrickNormal);
    if (gBrickHeight) {
        entity->setHeightTexture(gBrickHeight);
        entity->setParallax(0.018f, 8.f, 20.f);
    }
    entity->setMetallic(0.f);
    entity->setRoughness(0.86f);
    entity->setTexCellBomb(4.f, 0.05f, 0.f);
    return entity;
}

void addWallModule(Mesh *mesh, const HouseInstance &instance, float floorHeight) {
    const float outX = instance.rotationDeg == 90 ? 1.f : instance.rotationDeg == 270 ? -1.f : 0.f;
    const float outZ = instance.rotationDeg == 180 ? 1.f : instance.rotationDeg == 0 ? -1.f : 0.f;
    const float x = float(instance.x) + outX * 0.50f;
    const float y0 = float(instance.z) * floorHeight;
    const float z = float(instance.y) + outZ * 0.50f;
    const bool alongX = instance.rotationDeg == 0 || instance.rotationDeg == 180;
    const float sx = alongX ? 1.02f : 0.12f;
    const float sz = alongX ? 0.12f : 1.02f;

    if (instance.componentId == "wall.window") {
        wallSurfaceBox(mesh, x, y0 + 0.40f, z, sx, 0.80f, sz);
        wallSurfaceBox(mesh, x, y0 + 2.08f, z, sx, 0.64f, sz);
        const float glassX = x + outX * 0.015f, glassZ = z + outZ * 0.015f;
        box(mesh, glassX, y0 + 1.28f, glassZ, alongX ? 0.72f : 0.07f, 0.92f,
            alongX ? 0.07f : 0.72f,
            gPalette.glassR, gPalette.glassG, gPalette.glassB);
        for (float side : {-0.40f, 0.40f})
            box(mesh, glassX + (alongX ? side : 0.f), y0 + 1.28f,
                glassZ + (alongX ? 0.f : side), alongX ? 0.07f : 0.08f, 1.08f,
                alongX ? 0.08f : 0.07f, gPalette.trimR, gPalette.trimG, gPalette.trimB);
        for (float side : {0.76f, 1.80f})
            box(mesh, glassX, y0 + side, glassZ, alongX ? 0.86f : 0.08f, 0.07f,
                alongX ? 0.08f : 0.86f, gPalette.trimR, gPalette.trimG, gPalette.trimB);
        return;
    }
    wallSurfaceBox(mesh, x, y0 + floorHeight * 0.5f, z, sx, floorHeight, sz);
}

void addDoorModule(Mesh *mesh, const HouseInstance &instance, float floorHeight) {
    float x = float(instance.x);
    const float y0 = float(instance.z) * floorHeight;
    float z = float(instance.y);
    const bool alongX = instance.rotationDeg == 0 || instance.rotationDeg == 180;
    const float outX = instance.rotationDeg == 90 ? 1.f : instance.rotationDeg == 270 ? -1.f : 0.f;
    const float outZ = instance.rotationDeg == 180 ? 1.f : instance.rotationDeg == 0 ? -1.f : 0.f;
    x += outX * 0.50f; z += outZ * 0.50f;
    const float leafX = alongX ? 0.58f : 0.12f, leafZ = alongX ? 0.12f : 0.58f;
    box(mesh, x, y0 + 0.92f, z, leafX, 1.84f, leafZ, 0.16f, 0.24f, 0.20f);
    for (float side : {-0.39f, 0.39f})
        box(mesh, x + (alongX ? side : 0.f), y0 + floorHeight * 0.5f,
            z + (alongX ? 0.f : side),
            alongX ? 0.15f : 0.16f, floorHeight, alongX ? 0.16f : 0.15f,
            gPalette.trimR, gPalette.trimG, gPalette.trimB);
    box(mesh, x, y0 + 2.25f, z, alongX ? 0.64f : 0.16f, 0.30f, alongX ? 0.16f : 0.64f,
        gPalette.trimR, gPalette.trimG, gPalette.trimB);
    box(mesh, x + (alongX ? 0.20f : outX * 0.075f), y0 + 0.95f,
        z + (alongX ? outZ * 0.075f : 0.20f), 0.055f, 0.055f, 0.055f,
        0.92f, 0.72f, 0.20f);

    // A small porch makes entrance-side variation readable from a distance.
    const float porchX = x + outX * 0.55f, porchZ = z + outZ * 0.55f;
    box(mesh, porchX, y0 + 0.04f, porchZ, alongX ? 1.55f : 0.80f, 0.10f,
        alongX ? 0.80f : 1.55f, 0.38f, 0.28f, 0.19f);
    box(mesh, porchX, y0 + 2.48f, porchZ, alongX ? 1.75f : 0.92f, 0.10f,
        alongX ? 0.92f : 1.75f, gPalette.roofR, gPalette.roofG, gPalette.roofB);
    for (float side : {-0.67f, 0.67f})
        box(mesh, porchX + (alongX ? side : 0.f), y0 + 1.24f,
            porchZ + (alongX ? 0.f : side), 0.07f, 2.42f, 0.07f,
            gPalette.trimR, gPalette.trimG, gPalette.trimB);
}

void saveFrame(Graphics *gfx, const std::string &name) {
    eve::image::Image::create();
    auto *frame = gfx->newImageData();
    REQUIRE(frame != nullptr);
    auto *png = frame->encode(medialoader::FormatHandler::ENCODED_PNG, name.c_str(), false);
    REQUIRE(png != nullptr);
    const std::filesystem::path outDir =
        std::filesystem::path(EVENGINE_TEST_BINARY_DIR) / "out";
    std::filesystem::create_directories(outDir);
    const auto outPath = outDir / name;
    std::ofstream out(outPath, std::ios::binary);
    REQUIRE(out.good());
    out.write(static_cast<const char *>(png->getData()),
              static_cast<std::streamsize>(png->getSize()));
    REQUIRE(out.good());
    std::printf("housegen render saved: %s\n", outPath.string().c_str());
    delete png;
    delete frame;
}

Texture *loadPreviewTexture(Graphics *gfx, const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    REQUIRE(input.good());
    const std::streamsize size = input.tellg();
    REQUIRE(size > 0);
    input.seekg(0, std::ios::beg);
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    REQUIRE(input.read(reinterpret_cast<char *>(bytes.data()), size).good());
    eve::data::ByteData source(bytes.data(), bytes.size());
    std::unique_ptr<eve::image::ImageData> decoded(
        eve::image::Image::create()->newImageData(&source));
    TextureCreateInfo info = TextureCreateInfo::withMipmaps(true);
    info.sampler.repeatU = true;
    info.sampler.repeatV = true;
    return gfx->newTexture(decoded.get(), info);
}

}  // namespace

TEST_CASE("housegen.renderPreview") {
    constexpr char kit[] = R"({"components":[
      {"id":"foundation","model":"foundation.glb","category":"foundation"},
      {"id":"floor","model":"floor.glb","category":"floor"},
      {"id":"wall.solid","model":"wall.glb","category":"wall","weight":2},
      {"id":"wall.window","model":"window.glb","category":"wall","weight":5,"tags":["window"]},
      {"id":"door","model":"door.glb","category":"door"},
      {"id":"roof","model":"roof.glb","category":"roof"}
    ]})";

    HouseComponentLibrary library;
    REQUIRE(library.loadFromJson(kit));
    HouseRequest request;
    request.seed = static_cast<unsigned>(envInt("EVHOUSE_PREVIEW_SEED", 20260815));
    request.width = envInt("EVHOUSE_PREVIEW_WIDTH", 7);
    request.depth = envInt("EVHOUSE_PREVIEW_DEPTH", 6);
    request.floors = envInt("EVHOUSE_PREVIEW_FLOORS", 2);
    request.footprint = envString("EVHOUSE_PREVIEW_FOOTPRINT", "auto");
    request.roof = envString("EVHOUSE_PREVIEW_ROOF", "auto");
    request.entrance = envString("EVHOUSE_PREVIEW_ENTRANCE", "auto");
    request.floorHeight = 2.4f;
    HouseLayout layout;
    HouseGenerator generator(&library);
    REQUIRE(generator.generate(request, layout));

    auto *window = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(window != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings settings;
    settings.width = 960;
    settings.height = 640;
    settings.centered = true;
    REQUIRE(window->setWindowSettings(settings));

    Mesh *cube = previewCube(gfx);
    Mesh *gable = previewGable(gfx);
    REQUIRE(cube != nullptr);
    REQUIRE(gable != nullptr);
    const std::filesystem::path brickDir =
        eve_test_path::pathBesideTestDir(__FILE__, "assets/housegen/materials/ambientcg-bricks001");
    gBrickColor = loadPreviewTexture(gfx, brickDir / "bricks001-color.jpg");
    gBrickNormal = loadPreviewTexture(gfx, brickDir / "bricks001-normal-gl.jpg");
    gBrickHeight = loadPreviewTexture(gfx, brickDir / "bricks001-height.jpg");
    REQUIRE(gBrickColor != nullptr);
    REQUIRE(gBrickNormal != nullptr);
    REQUIRE(gBrickHeight != nullptr);
    selectPalette(request.seed);
    gfx->setScreenReadbackEnabled(true);
    gfx->setBackgroundColor(Color(0.58f, 0.75f, 0.90f, 1.f));

    // Lawn, foundation/floors, then facade modules selected by the generated layout.
    const float plotCenterX = float(request.width - 1) * 0.5f;
    const float plotCenterZ = float(request.depth - 1) * 0.5f;
    box(cube, plotCenterX, -0.30f, plotCenterZ, float(request.width + 4), 0.25f,
        float(request.depth + 4), 0.20f, 0.42f, 0.18f);
    for (const auto &instance : layout.instances) {
        const auto *component = library.find(instance.componentId);
        REQUIRE(component != nullptr);
        if (component->category == "foundation") {
            box(cube, float(instance.x), -0.08f, float(instance.y), 0.96f, 0.16f, 0.96f,
                0.35f, 0.34f, 0.33f);
        } else if (component->category == "floor") {
            box(cube, float(instance.x), float(instance.z) * request.floorHeight - 0.03f,
                float(instance.y), 0.96f, 0.06f, 0.96f, 0.48f, 0.33f, 0.22f);
        } else if (component->category == "wall") {
            addWallModule(cube, instance, request.floorHeight);
        } else if (component->category == "door") {
            addDoorModule(cube, instance, request.floorHeight);
        }
    }

    // One coherent gable roof replaces the per-cell debug roof blocks while still deriving its
    // footprint and elevation from the generated request/layout.
    int roofLevel = 0;
    for (const auto &instance : layout.instances) {
        const auto *component = library.find(instance.componentId);
        if (component && component->category == "roof")
            roofLevel = std::max(roofLevel, instance.z);
    }
    int roofMinX = request.width, roofMaxX = 0, roofMinZ = request.depth, roofMaxZ = 0;
    for (const auto &instance : layout.instances) {
        const auto *component = library.find(instance.componentId);
        if (component && component->category == "roof" && instance.z == roofLevel) {
            roofMinX = std::min(roofMinX, instance.x);
            roofMaxX = std::max(roofMaxX, instance.x);
            roofMinZ = std::min(roofMinZ, instance.y);
            roofMaxZ = std::max(roofMaxZ, instance.y);
        }
    }
    const float roofWidth = float(roofMaxX - roofMinX + 1);
    const float roofDepth = float(roofMaxZ - roofMinZ + 1);
    const float roofCenterX = float(roofMinX + roofMaxX) * 0.5f;
    const float roofCenterZ = float(roofMinZ + roofMaxZ) * 0.5f;
    const float eaveY = roofLevel * request.floorHeight;
    std::unordered_set<int> topRoofCells;
    for (const auto &instance : layout.instances) {
        const auto *component = library.find(instance.componentId);
        if (component && component->category == "roof" && instance.z == roofLevel)
            topRoofCells.insert(instance.y * request.width + instance.x);
    }
    auto hasTopRoof = [&](int x, int z) {
        return x >= 0 && z >= 0 && x < request.width && z < request.depth &&
               topRoofCells.contains(z * request.width + x);
    };
    // Roof instances below the main roof are mandatory covers for cells exposed by a setback.
    for (const auto &instance : layout.instances) {
        const auto *component = library.find(instance.componentId);
        if (!component || component->category != "roof" || instance.z == roofLevel) continue;
        box(cube, float(instance.x), instance.z * request.floorHeight, float(instance.y),
            1.02f, 0.14f, 1.02f, gPalette.roofR, gPalette.roofG, gPalette.roofB);
    }
    if (layout.roofStyle == "gable" && layout.footprintStyle == "rectangle") {
        const float halfRun = roofWidth * 0.5f + 0.30f;
        const float rise = roofWidth * 0.22f;
        const float angle = std::atan2(rise, halfRun);
        const float panelLength = std::hypot(halfRun, rise);
        const float panelOffset = halfRun * 0.5f;
        const float roofY = eaveY + rise * 0.5f;
        box(cube, roofCenterX - panelOffset, roofY, roofCenterZ, panelLength, 0.18f,
            roofDepth + 0.65f, gPalette.roofR, gPalette.roofG, gPalette.roofB, angle);
        box(cube, roofCenterX + panelOffset, roofY, roofCenterZ, panelLength, 0.18f,
            roofDepth + 0.65f, gPalette.roofR, gPalette.roofG, gPalette.roofB, -angle);
        const float wallRise = roofWidth * 0.5f * std::tan(angle) - 0.05f;
        for (float z : {float(roofMinZ) - 0.50f, float(roofMaxZ) + 0.50f})
            box(gable, roofCenterX, eaveY - 0.02f, z, roofWidth, wallRise, 0.12f,
                gPalette.wallR, gPalette.wallG, gPalette.wallB);
    } else {
        // Tile-wise flat/shed roofs preserve L/T-shaped silhouettes instead of filling their
        // missing quadrants with a rectangular cap.
        const float shedAngle = layout.roofStyle == "shed" ? -0.16f : 0.f;
        const float roofY = eaveY + (layout.roofStyle == "shed" ? roofWidth * 0.08f : 0.f);
        for (const auto &instance : layout.instances) {
            const auto *component = library.find(instance.componentId);
            if (!component || component->category != "roof" || instance.z != roofLevel) continue;
            const float slopeY = layout.roofStyle == "shed"
                ? (roofCenterX - float(instance.x)) * 0.16f : 0.f;
            box(cube, float(instance.x), roofY + slopeY, float(instance.y),
                1.08f, layout.roofStyle == "flat" ? 0.20f : 0.14f, 1.08f,
                gPalette.roofR, gPalette.roofG, gPalette.roofB, shedAngle);
        }
        // Close every actual roof boundary. Flat roofs receive a complete parapet; shed roofs
        // receive stepped wall infill up to the sloped plane.
        const int dx[] = {0, 1, 0, -1}, dz[] = {-1, 0, 1, 0};
        for (int z = roofMinZ; z <= roofMaxZ; ++z) for (int x = roofMinX; x <= roofMaxX; ++x) {
            if (!hasTopRoof(x, z)) continue;
            const float tileY = roofY + (layout.roofStyle == "shed" ? (roofCenterX - float(x)) * 0.16f : 0.f);
            for (int side = 0; side < 4; ++side) {
                if (hasTopRoof(x + dx[side], z + dz[side])) continue;
                const bool alongX = side == 0 || side == 2;
                const float px = float(x) + float(dx[side]) * 0.50f;
                const float pz = float(z) + float(dz[side]) * 0.50f;
                if (layout.roofStyle == "flat") {
                    box(cube, px, eaveY + 0.20f, pz, alongX ? 1.02f : 0.10f, 0.34f,
                        alongX ? 0.10f : 1.02f, gPalette.trimR, gPalette.trimG, gPalette.trimB);
                } else {
                    const float fillHeight = std::max(0.02f, tileY - eaveY - 0.14f);
                    wallSurfaceBox(cube, px, eaveY + fillHeight * 0.5f - 0.07f, pz,
                                   alongX ? 1.02f : 0.11f, fillHeight,
                                   alongX ? 0.11f : 1.02f);
                    box(cube, px, tileY - 0.10f, pz, alongX ? 1.06f : 0.13f, 0.18f,
                        alongX ? 0.13f : 1.06f, gPalette.roofR, gPalette.roofG, gPalette.roofB);
                }
            }
        }
    }

    // Some seeds receive a front balcony; it is an optional decorative grammar production.
    if (request.floors >= 2 && request.seed % 2u == 0u) {
        const float balconyZ = -0.88f;
        box(cube, plotCenterX, request.floorHeight + 0.14f, balconyZ, 2.8f, 0.14f, 0.74f,
            0.38f, 0.28f, 0.19f);
        box(cube, plotCenterX, request.floorHeight + 0.72f, balconyZ - 0.30f, 2.8f, 0.08f, 0.08f,
            gPalette.trimR, gPalette.trimG, gPalette.trimB);
        for (float side : {-1.30f, 0.f, 1.30f})
            box(cube, plotCenterX + side, request.floorHeight + 0.44f, balconyZ - 0.30f,
                0.07f, 0.60f, 0.07f, gPalette.trimR, gPalette.trimG, gPalette.trimB);
    }

    auto *camera = Camera3D::createCamera();
    const float plotSpan = float(std::max(request.width, request.depth));
    camera->setTarget(plotCenterX, request.floors * request.floorHeight * 0.52f, plotCenterZ);
    float eyeX = plotCenterX + plotSpan * 1.10f;
    float eyeZ = plotCenterZ - plotSpan * 1.55f;
    if (layout.entranceSide == "east") {
        eyeX = plotCenterX + plotSpan * 1.55f; eyeZ = plotCenterZ - plotSpan * 1.10f;
    } else if (layout.entranceSide == "south") {
        eyeX = plotCenterX - plotSpan * 1.10f; eyeZ = plotCenterZ + plotSpan * 1.55f;
    } else if (layout.entranceSide == "west") {
        eyeX = plotCenterX - plotSpan * 1.55f; eyeZ = plotCenterZ - plotSpan * 1.10f;
    }
    camera->setEye(eyeX, request.floors * request.floorHeight + plotSpan * 0.48f, eyeZ);
    camera->setAmbient(0.34f, 0.34f, 0.38f);
    camera->data()->farZ = 80.f;
    auto *sun = Light3D::createLight("dir");
    sun->setDirection(-0.7f, -1.0f, -0.45f);
    sun->setColor(1.f, 0.94f, 0.84f, 1.55f);
    sun->setCastShadow(true);
    sun->setShadowStrength(0.82f);

    // RenderSystem closes the 3D pass and presents. The tiny off-screen-ish HUD also exercises
    // the normal mixed 3D/2D game frame path.
    auto *hud = Renderable2D::create();
    hud->transform()->x = 0.f;
    hud->transform()->y = 0.f;
    hud->sprite()->width = 1.f;
    hud->sprite()->height = 1.f;
    hud->sprite()->a = 0.f;
    for (int frame = 0; frame < 4; ++frame) {
        RenderSystem3D::render(*gfx);
        RenderSystem::render(*gfx);
        SDL_Event event;
        while (SDL_PollEvent(&event)) {}
    }

    saveFrame(gfx, envString("EVHOUSE_PREVIEW_NAME", "housegen_seed_20260815.png"));
    window->close();
}

TEST_CASE("housegen.materialPreview") {
    const std::filesystem::path assetDir =
        eve_test_path::pathBesideTestDir(__FILE__, "assets/housegen/kenney-modular-buildings");
    HouseComponentLibrary library;
    std::string error;
    REQUIRE(library.loadFromFile((assetDir / "components.json").string(), &error));
    REQUIRE_EQ(library.count(), 4);
    for (const auto &id : library.ids())
        REQUIRE(std::filesystem::is_regular_file(library.find(id)->modelPath));

    auto *window = eve::window::Window::create();
    auto *gfx = Graphics::create();
    auto *models = eve::model3d::Model3D::create();
    REQUIRE(window != nullptr);
    REQUIRE(gfx != nullptr);
    REQUIRE(models != nullptr);
    eve::window::WindowSettings settings;
    settings.width = 960;
    settings.height = 640;
    settings.centered = true;
    REQUIRE(window->setWindowSettings(settings));

    gfx->setScreenReadbackEnabled(true);
    gfx->setBackgroundColor(Color(0.56f, 0.72f, 0.86f, 1.f));
    Mesh *cube = previewCube(gfx);
    REQUIRE(cube != nullptr);
    box(cube, 2.1f, -0.12f, 0.f, 7.2f, 0.18f, 3.2f, 0.22f, 0.38f, 0.18f);

    HouseLayout layout;
    layout.moduleSize = 1.4f;
    layout.floorHeight = 2.f;
    layout.instances = {
        {"kenney.wall.block", 0, 0, 0, 0},
        {"kenney.wall.window", 1, 0, 0, 0},
        {"kenney.door", 2, 0, 0, 0},
        {"kenney.roof.gable", 3, 0, 0, 0},
    };
    const auto entities = layout.instantiate(gfx, models, library, &error);
    if (!error.empty()) std::printf("housegen material instantiate error: %s\n", error.c_str());
    REQUIRE(error.empty());
    REQUIRE(entities.size() >= layout.instances.size());
    REQUIRE(entities.front()->meshRenderer()->texture != nullptr);
    REQUIRE(entities.front()->meshRenderer()->normalTexture != nullptr);
    REQUIRE(entities.front()->meshRenderer()->heightTexture != nullptr);
    REQUIRE(entities.front()->meshRenderer()->parallaxScale > 0.f);

    auto *camera = Camera3D::createCamera();
    camera->setTarget(2.1f, 0.55f, 0.f);
    camera->setEye(4.9f, 2.8f, 5.0f);
    camera->setAmbient(0.30f, 0.30f, 0.34f);
    camera->data()->farZ = 60.f;
    auto *sun = Light3D::createLight("dir");
    sun->setDirection(-0.7f, -1.f, -0.5f);
    sun->setColor(1.f, 0.94f, 0.84f, 1.6f);
    sun->setCastShadow(true);
    sun->setShadowStrength(0.78f);
    auto *hud = Renderable2D::create();
    hud->sprite()->a = 0.f;
    for (int frame = 0; frame < 4; ++frame) {
        RenderSystem3D::render(*gfx);
        RenderSystem::render(*gfx);
        SDL_Event event;
        while (SDL_PollEvent(&event)) {}
    }
    saveFrame(gfx, envString("EVHOUSE_MATERIAL_PREVIEW_NAME", "housegen_materials.png"));
    window->close();
}
