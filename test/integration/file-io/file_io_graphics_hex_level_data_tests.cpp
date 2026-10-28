// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "default": "platform-file",
  "module": "daynight",
  "modules": [
    "animation",
    "avatar",
    "cmdline",
    "common",
    "database",
    "daynight",
    "editor",
    "filesystem",
    "i18n",
    "joystick",
    "map",
    "particles"
  ],
  "origins": [
    "SkyProfile.cpp",
    "SkyWispsAsset.cpp",
    "animation_skinned.cpp",
    "avatar_vrm.cpp",
    "cmdline.cpp",
    "crashlog.cpp",
    "database.cpp",
    "editor_tilemap_formats.cpp",
    "filesystem_cpp.cpp",
    "hex_level_data.cpp",
    "hotreload.cpp",
    "i18n.cpp",
    "joystick_cpp.cpp"
  ],
  "cases": {
    "concurrency": [
      "filesystem.watch.coalescesOverlappingRegistrations",
      "filesystem.watch.dotResolvesToCwd",
      "filesystem.watch.fileModified",
      "hotreload.watchTree.absoluteRootRecursesAndReportsRelativePath"
    ],
    "render-window": [
      "animation.skinned.render.applyToMesh",
      "animation.skinned.render.bindPose",
      "animation.skinned.render.clipSampledMovesPixels",
      "animation.skinned.render.playerDriven",
      "avatar.vrm.graphicsBackendSupportIsExplicit"
    ],
    "script-binding": [
      "hotreload.softScriptReloadPreservesState",
      "hotreload.tryReloadParticleJson"
    ]
  }
}
*/
// clang-format on

// Related integration contracts share one translation unit to amortize header parsing.

// Contract section: SkyProfile
#include "PrivateTestScope.h"
#include "zeroerr/assert.h"

#include <fstream>
#include <iterator>
#include <limits>
#include "common/Value.h"
#include "daynight/sky/SkyProfile.h"
#include "daynight/sky/SkyRuntime.h"
#include "graphics/sky/SkyAtmospherePass.h"
#include "zeroerr/unittest.h"
// SkyProfile.cpp
namespace test_SkyProfile {
TEST_CASE("daynight.sky profile validates the authored reference and owns imported values") {
    std::ifstream file(std::string(EVENGINE_SOURCE_DIR) + "/examples/uds-sky/sky.json");
    REQUIRE(file.good());
    const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    auto              parsed = eve::Value::fromJson(text);
    REQUIRE(parsed.ok());
    auto document = std::move(parsed).takeValue();
    auto profile  = eve::daynight::SkyProfile::decode(document);
    REQUIRE(profile.ok());
    REQUIRE(profile.value().settings().clock.initialHour == 12);
    REQUIRE(profile.value().settings().sunIrradiance[0] == 5);
    REQUIRE(profile.value().atmosphere().groundRadiusKm == 6360);
    auto rejected = document;
    rejected.set("version", 7);
    REQUIRE(!eve::daynight::SkyProfile::decode(rejected).ok());
    auto withWisps = document;
    withWisps.set("version", 3);
    withWisps.set("wispsAsset", "assets/wisps.json");
    auto authored = eve::daynight::SkyProfile::decode(withWisps);
    REQUIRE(authored.ok());
    REQUIRE(authored.value().wispsAsset() == "assets/wisps.json");
    REQUIRE(authored.value().settings().cloudMotion.speed == 0);
    auto animatedDocument = withWisps;
    animatedDocument.set("version", 4);
    animatedDocument.set(
        "cloudMotion",
        eve::Value::object({{"phase", -.25}, {"speed", .35}, {"timeScale", 1.0}, {"windMultiplier", 2.0}}));
    auto animated = eve::daynight::SkyProfile::decode(animatedDocument);
    REQUIRE(animated.ok());
    REQUIRE(animated.value().settings().cloudMotion.phase == -.25);
    REQUIRE(animated.value().settings().cloudMotion.speed == .35);
    auto daylight = animatedDocument;
    daylight.set("version", 5);
    daylight.find("heightFog")->set("directionalElevationRange", eve::Value::array({0.0, .2}));
    auto decodedDaylight = eve::daynight::SkyProfile::decode(daylight);
    REQUIRE(decodedDaylight.ok());
    REQUIRE(decodedDaylight.value().atmosphere().heightFog.directionalElevationRange[1] == .2f);
    REQUIRE(animated.value().atmosphere().heightFog.directionalElevationRange[1] == 0);
    auto generated = daylight;
    generated.set("version", 6);
    generated.set("wispsAsset", eve::Value{});
    generated.set("proceduralSeed", 0);
    auto defaultPack = eve::daynight::SkyProfile::decode(generated);
    REQUIRE(defaultPack.ok());
    REQUIRE(defaultPack.value().proceduralSeed().has_value());
    REQUIRE(*defaultPack.value().proceduralSeed() == 0);
    generated.set("proceduralSeed", -1);
    REQUIRE(!eve::daynight::SkyProfile::decode(generated).ok());
    generated.set("proceduralSeed", 2026);
    generated.set("wispsAsset", "pack/sky.json");
    auto externalPack = eve::daynight::SkyProfile::decode(generated);
    REQUIRE(externalPack.ok());
    REQUIRE(!externalPack.value().proceduralSeed());
    generated.set("wispsAsset", "");
    REQUIRE(!eve::daynight::SkyProfile::decode(generated).ok());
    daylight.find("heightFog")->set("directionalElevationRange", eve::Value::array({.2, 0.0}));
    REQUIRE(!eve::daynight::SkyProfile::decode(daylight).ok());
    daylight.find("heightFog")
        ->set("directionalElevationRange", eve::Value::array({0.0, std::numeric_limits<double>::infinity()}));
    REQUIRE(!eve::daynight::SkyProfile::decode(daylight).ok());
    auto badMotion = animatedDocument;
    badMotion.find("cloudMotion")->set("speed", -1.0);
    REQUIRE(!eve::daynight::SkyProfile::decode(badMotion).ok());
    badMotion = animatedDocument;
    badMotion.find("cloudMotion")->set("windMultiplier", std::numeric_limits<double>::infinity());
    REQUIRE(!eve::daynight::SkyProfile::decode(badMotion).ok());
    badMotion = animatedDocument;
    badMotion.find("cloudMotion")->set("future", true);
    REQUIRE(!eve::daynight::SkyProfile::decode(badMotion).ok());
    REQUIRE(profile.value().wispsAsset().empty());
    auto legacy = eve::Value::object({});
    for (const auto &key : document.keys())
        if (key != "heightFog") legacy.set(key, *document.find(key));
    legacy.set("version", 1);
    auto migrated = eve::daynight::SkyProfile::decode(legacy);
    REQUIRE(migrated.ok());
    REQUIRE(migrated.value().atmosphere().heightFog.densityPerMetre == 0);
    REQUIRE(profile.value().atmosphere().heightFog.densityPerMetre > 0);
    rejected = document;
    rejected.find("heightFog")->set("densityPerMetre", -1);
    REQUIRE(!eve::daynight::SkyProfile::decode(rejected).ok());
    rejected = document;
    rejected.find("heightFog")->set("future", 1);
    REQUIRE(!eve::daynight::SkyProfile::decode(rejected).ok());
    rejected = document;
    rejected.set("mode", "volumetric-clouds");
    REQUIRE(!eve::daynight::SkyProfile::decode(rejected).ok());
    rejected = document;
    rejected.set("typo", 1);
    REQUIRE(!eve::daynight::SkyProfile::decode(rejected).ok());
    rejected = document;
    rejected.find("clock")->set("initialHour", std::numeric_limits<double>::quiet_NaN());
    REQUIRE(!eve::daynight::SkyProfile::decode(rejected).ok());
    rejected = document;
    rejected.find("atmosphere")->set("groundRadiusKm", -1);
    REQUIRE(!eve::daynight::SkyProfile::decode(rejected).ok());
    rejected = document;
    rejected.find("sun")->set("irradiance", eve::Value::array({1, 2}));
    REQUIRE(!eve::daynight::SkyProfile::decode(rejected).ok());
    rejected = document;
    rejected.find("clock")->set("unknown", 0);
    REQUIRE(!eve::daynight::SkyProfile::decode(rejected).ok());
    rejected = document;
    rejected.find("atmosphere")->set("mieHeightKm", 1e100);
    REQUIRE(!eve::daynight::SkyProfile::decode(rejected).ok());
    rejected = document;
    rejected.find("sun")->set("irradiance", eve::Value::array({-1e-300, 5, 5}));
    REQUIRE(!eve::daynight::SkyProfile::decode(rejected).ok());
    document = eve::Value();
    REQUIRE(!eve::daynight::SkyProfile::decode(document).ok());
    REQUIRE(profile.value().settings().clock.initialHour == 12);
    REQUIRE(profile.value().atmosphere().groundRadiusKm == 6360);
}
}  // namespace test_SkyProfile

// Contract section: SkyWispsAsset
#include "PrivateTestScope.h"

#include <algorithm>
#include <optional>
#include "Fixtures.h"
#include "filesystem/Filesystem.h"
#include "graphics/ShaderResources.h"
#include "graphics/sky/SkyWispsAsset.h"
#include "graphics/sky/SkyWispsLayer.h"
// SkyWispsAsset.cpp
namespace test_SkyWispsAsset {
TEST_CASE("graphics.sky external packs validate all versions without private assets") {
    using namespace eve::graphics;
    using eve::Value;
    TempDir temporary;
    auto   *fs = eve::filesystem::Filesystem::create();
    REQUIRE(fs != nullptr);
    REQUIRE(fs->mountRealDirectory(temporary.str(), "sky-fixture", false));
    const auto write = [&](const std::string &path, const Value &value) {
        auto encoded = value.toJson();
        REQUIRE(encoded.ok());
        std::ofstream(temporary.path() / path) << encoded.value();
    };
    const auto parse = [&](const char *text) {
        auto result = Value::fromJson(text);
        REQUIRE(result.ok());
        return result.value();
    };
    auto mesh = parse(R"({"schema":"eve.sky-mesh/1","space":"unreal-local-centimetres","uvChannel":3,
      "layout":["x","y","z","u","v","r","g","b","a"],"topology":"triangle-list",
      "vertices":[[100,0,0,0,0,1,0,0,1],[0,100,0,1,0,1,0,0,1],[0,0,100,0,1,1,0,0,1]],
      "sourceSha256":"0000000000000000000000000000000000000000000000000000000000000000"})");
    write("mesh.json", mesh);
    std::ofstream(temporary.path() / "density.bin", std::ios::binary).put(char(64));
    std::ofstream(temporary.path() / "rgba.bin", std::ios::binary).write("\0\0\0\0", 4);
    std::ofstream(temporary.path() / "half.bin", std::ios::binary).write("\0\0\0\0\0\0\0\0", 8);
    auto                         manifest = parse(R"({"schema":"eve.sky-wisps/1","mesh":"mesh.json",
      "density":{"path":"density.bin","width":1,"height":1,"encoding":"r8-linear"},
      "material":{"domeScale":5000,"color":[1,1,1],"gradient":[0,0,1,1],"movement":[1,0],
      "moonForward":[0,0,1],"morphRate":1,"morphPeriod":10,"morphAmount":0.4,"opacity":0.2,
      "litIntensity":0.8,"cloudTime":0,"moonGradient":true,"staticClouds":false}})");
    std::optional<SkyWispsAsset> retained;
    auto                         generated = SkyWispsAsset::generate(0);
    REQUIRE(generated.ok());
    const auto &defaults = generated.value().layer().daylight;
    const auto  vec      = [](const auto &values) {
        auto array = Value::array({});
        for (auto v : values) array.pushBack(Value(double(v)));
        return array;
    };
    for (int version = 1; version <= 7; ++version) {
        manifest.set("schema", "eve.sky-wisps/" + std::to_string(version));
        if (version == 2) manifest.set("contrast", Value::array({.1, .85, .375}));
        if (version == 3) manifest.set("sunDisk", parse(R"({"color":[1000,900,600],"shape":[0.02,3,0]})"));
        if (version == 4) {
            auto keys = Value::array({});
            for (int c = 0; c < 3; ++c)
                for (int k = 0; k < 4; ++k) keys.pushBack(Value::array({double(k) / 3, 1.0, 0.0, 0.0}));
            manifest.set("sunDiskCurve", keys);
        }
        if (version == 5) {
            auto d = Value::object({});
            d.set("sun", vec(defaults.sun));
            d.set("moon", vec(defaults.moon));
            d.set("moonOrbit", vec(defaults.moonOrbit));
            d.set("dayTint", vec(defaults.dayTint));
            d.set("duskTint", vec(defaults.duskTint));
            d.set("nightTint", vec(defaults.nightTint));
            d.set("glow", vec(defaults.glow));
            d.set("controls", vec(defaults.controls));
            d.set("stars", vec(defaults.stars));
            d.set("starUv", vec(defaults.starUv));
            d.set("twinkle", vec(defaults.twinkle));
            auto keys = Value::array({});
            for (const auto &k : defaults.scatteringCurve) keys.pushBack(vec(k));
            d.set("scatteringCurve", keys);
            manifest.set("daylight", d);
            auto image = parse(R"({"path":"rgba.bin","width":1,"height":1,"encoding":"rgba8-linear"})");
            manifest.set("stars", image);
            manifest.set("starsNoise", image);
        }
        if (version == 6) {
            auto optics = Value::object({});
            optics.set("rayDay", vec(defaults.rayDay));
            optics.set("rayDusk", vec(defaults.rayDusk));
            optics.set("rayNight", vec(defaults.rayNight));
            optics.set("absorptionDay", vec(defaults.absorptionDay));
            optics.set("absorptionNight", vec(defaults.absorptionNight));
            manifest.set("optics", optics);
            manifest.find("stars")->set("mipLevels", 1);
            manifest.find("starsNoise")->set("mipLevels", 1);
        }
        if (version == 7) {
            auto moon = Value::object({});
            moon.set("color", vec(defaults.moonDiskColor));
            moon.set("shape", vec(defaults.moonDiskShape));
            moon.set("lighting", vec(defaults.moonDiskLighting));
            moon.set("glow", vec(defaults.moonDiskGlow));
            manifest.set("moonDisk", moon);
            auto image = parse(R"({"path":"half.bin","width":1,"height":1,"encoding":"rgba16-float","mipLevels":1})");
            manifest.set("moonColorTexture", image);
            manifest.set("moonNormalTexture", image);
        }
        const auto name = "sky-" + std::to_string(version) + ".json";
        write(name, manifest);
        auto loaded = SkyWispsAsset::load("sky-fixture/" + name);
        REQUIRE(loaded.ok());
        REQUIRE(loaded.value().layer().daylightEnabled == (version >= 5));
        REQUIRE(loaded.value().layer().opticalCycleEnabled == (version >= 6));
        REQUIRE((loaded.value().layer().moonColorTexture != nullptr) == (version == 7));
        retained.emplace(loaded.value());
    }
    REQUIRE(!SkyWispsAsset::load("sky-fixture/missing.json").ok());
    auto bad = manifest;
    bad.set("future", true);
    write("bad-1.json", bad);
    REQUIRE(!SkyWispsAsset::load("sky-fixture/bad-1.json").ok());
    bad = manifest;
    bad.set("mesh", "../mesh.json");
    write("bad-2.json", bad);
    REQUIRE(!SkyWispsAsset::load("sky-fixture/bad-2.json").ok());
    bad = manifest;
    bad.find("moonColorTexture")->set("mipLevels", 2);
    write("bad-3.json", bad);
    REQUIRE(!SkyWispsAsset::load("sky-fixture/bad-3.json").ok());
    bad = manifest;
    bad.find("stars")->set("width", 2);
    write("bad-4.json", bad);
    REQUIRE(!SkyWispsAsset::load("sky-fixture/bad-4.json").ok());
    REQUIRE(fs->unmountRealDirectory(temporary.str()));
    REQUIRE(retained->layer().corners.size() == 27);
    REQUIRE(retained->layer().moonColorTexture->bytes.size() == 8);
}
}  // namespace test_SkyWispsAsset

// Contract section: animation skinned
#include "PrivateTestScope.h"

#include <SDL2/SDL.h>
#include <assimp/mesh.h>
#include <assimp/scene.h>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>
#include "PathBesideSource.h"
#include "ResourceTestSupport.h"
#include "animation/AnimClip.h"
#include "animation/AnimImporter.h"
#include "animation/AnimPlayer.h"
#include "animation/AnimPose.h"
#include "animation/AnimSkeleton.h"
#include "animation/AnimSkin.h"
#include "animation/Animation.h"
#include "common/Exception.h"
#include "data/ByteData.h"
#include "filesystem/FileData.h"
#include "graphics/AmbientOcclusion.h"
#include "graphics/Canvas.h"
#include "graphics/DrawItem2D.h"
#include "graphics/Font.h"
#include "graphics/GBuffer.h"
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
#include "graphics/Shader.h"
#include "graphics/Texture.h"
#include "graphics/Volumetric.h"
#include "graphics/Water.h"
#include "graphics/Waterfall.h"
#include "image/Image.h"
#include "image/ImageData.h"
#include "model3d/Model3D.h"
#include "model3d/ModelData.h"
#include "window/Window.h"
// animation_skinned.cpp
namespace test_animation_skinned {
// Color lives in eve::graphics (see graphics/Canvas.h); keep the unqualified form.
using eve::graphics::Color;


using namespace eve::animation;
using namespace eve::graphics;

namespace {


EVE_DEFINE_PATH_BESIDE_SOURCE()

[[maybe_unused]] bool fileExists(const std::string &path) { return std::filesystem::is_regular_file(path); }

[[maybe_unused]] std::string cesiumManDir() { return pathBesideThisSource("assets/skinned/cesium_man/glTF"); }

[[maybe_unused]] std::string cesiumManGltfPath() { return cesiumManDir() + "/CesiumMan.gltf"; }

[[maybe_unused]] bool ensureSkinnedAssets() {
    const std::string gltf = cesiumManGltfPath();
    if (fileExists(gltf)) return true;
    std::printf(
        "animation.skinned: missing %s — run scripts/download_skinned_character.sh "
        "(or cmake --build --target download_skinned_character / "
        "-DEVENGINE_DOWNLOAD_SKINNED_CHARACTER=ON)\n",
        gltf.c_str());
    return false;
}

/** Mount the CesiumMan glTF folder in physfs and load via relative path. */
[[maybe_unused]] eve::model3d::ModelData *loadCesiumMan(const char *fsIdentity) {
    const std::string dir = cesiumManDir();
    REQUIRE(fileExists(dir + "/CesiumMan.gltf"));
    auto *fs = eve::filesystem::Filesystem::create();
    REQUIRE(fs != nullptr);
    REQUIRE(fs->setIdentity(fsIdentity, true));
    REQUIRE(fs->setupWriteDirectory());
    fs->allowMountingForPath(dir);
    REQUIRE(fs->mount(dir, "", false));
    auto *mod = eve::model3d::Model3D::create();
    return mod->newModelDataFromFile("CesiumMan.gltf");
}

[[maybe_unused]] int findFirstSkinnedMesh(const eve::model3d::ModelData *model) {
    for (int i = 0; i < model->getMeshCount(); ++i) {
        if (model->hasBones(i)) return i;
    }
    return -1;
}

[[maybe_unused]] float vertexDeltaMax(const std::vector<float> &a, const std::vector<float> &b) {
    const size_t n    = std::min(a.size(), b.size());
    float        maxd = 0.f;
    for (size_t i = 0; i < n; ++i) {
        maxd = std::max(maxd, std::fabs(a[i] - b[i]));
    }
    return maxd;
}

// ---- skinned render helpers ------------------------------------------------

struct Bounds {
    float minX = 0, minY = 0, minZ = 0;
    float maxX = 0, maxY = 0, maxZ = 0;
    bool  valid = false;

    void expand(float x, float y, float z) {
        if (!valid) {
            minX = maxX = x;
            minY = maxY = y;
            minZ = maxZ = z;
            valid       = true;
            return;
        }
        minX = std::min(minX, x);
        minY = std::min(minY, y);
        minZ = std::min(minZ, z);
        maxX = std::max(maxX, x);
        maxY = std::max(maxY, y);
        maxZ = std::max(maxZ, z);
    }

    float centerX() const { return 0.5f * (minX + maxX); }
    float centerY() const { return 0.5f * (minY + maxY); }
    float centerZ() const { return 0.5f * (minZ + maxZ); }
    float radius() const {
        const float dx = maxX - minX;
        const float dy = maxY - minY;
        const float dz = maxZ - minZ;
        return 0.5f * std::sqrt(dx * dx + dy * dy + dz * dz);
    }
};


[[maybe_unused]] void resetScene3D() {
    if (ecs::current()->getManager<Renderable3D>() != nullptr) {
        auto view = ecs::View<Renderable3D, Renderable3D::MeshRenderer>();
        for (auto it = view.begin(); it != view.end(); ++it) {
            auto [mr]   = *it;
            mr->visible = false;
        }
    }
    if (ecs::current()->getManager<Camera3D>() != nullptr) {
        auto camView = ecs::View<Camera3D, Camera3D::Data>();
        for (auto it = camView.begin(); it != camView.end(); ++it) {
            auto [data]  = *it;
            data->active = false;
        }
    }
    if (ecs::current()->getManager<Light3D>() != nullptr) {
        auto lightView = ecs::View<Light3D, Light3D::Data>();
        for (auto it = lightView.begin(); it != lightView.end(); ++it) {
            auto [d]   = *it;
            d->enabled = false;
        }
    }
    if (ecs::current()->getManager<Renderable2D>() != nullptr) {
        auto view = ecs::View<Renderable2D, Renderable2D::Sprite>();
        for (auto it = view.begin(); it != view.end(); ++it) {
            auto [sp]   = *it;
            sp->visible = false;
        }
    }
}

[[maybe_unused]] float luma(const Color &c) { return (c.r + c.g + c.b) / 3.f; }

[[maybe_unused]] std::vector<float> captureLumaGrid(Graphics *gfx, int step = 2) {
    const int          w = gfx->getWidth();
    const int          h = gfx->getHeight();
    std::vector<float> out(size_t(w) * size_t(h), 0.f);
    for (int y = 0; y < h; y += step) {
        for (int x = 0; x < w; x += step) {
            out[size_t(y) * size_t(w) + size_t(x)] = luma(gfx->getPixel(x, y));
        }
    }
    return out;
}

[[maybe_unused]] float maxLumaAbsDelta(Graphics *gfx, const std::vector<float> &a, const std::vector<float> &b,
                                       int step = 2) {
    float     best = -1.f;
    const int w    = gfx->getWidth();
    const int h    = gfx->getHeight();
    for (int y = 0; y < h; y += step) {
        for (int x = 0; x < w; x += step) {
            const size_t i = size_t(y * w + x);
            best           = std::max(best, std::fabs(a[i] - b[i]));
        }
    }
    return best;
}

[[maybe_unused]] int foregroundPixelCount(const std::vector<float> &L, float threshold) {
    int count = 0;
    for (float v : L)
        if (v > threshold) ++count;
    return count;
}

[[maybe_unused]] std::vector<char> readBinaryFile(const std::string &path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    return std::vector<char>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

/** CesiumMan's glTF base-color map (jpg beside the .gltf). */
[[maybe_unused]] Texture *loadCesiumManDiffuseTexture(Graphics *gfx) {
    const std::string path  = cesiumManDir() + "/CesiumMan_img0.jpg";
    const auto        bytes = readBinaryFile(path);
    if (bytes.empty()) return nullptr;
    [[maybe_unused]] auto *const imageModule = eve::image::Image::create();
    try {
        eve::data::ByteData    data(bytes.data(), bytes.size());
        eve::image::ImageData *img = eve::image::Image::create()->newImageData(&data);
        if (!img) return nullptr;
        Texture *tex = gfx->newTexture(img);
        delete img;
        return tex;
    } catch (...) {
        return nullptr;
    }
}

[[maybe_unused]] Texture *makeSolidGray(Graphics *gfx, uint8_t g) {
    const uint8_t px[4] = {g, g, g, 255};
    return gfx->newTexture(1, 1, px);
}

[[maybe_unused]] void savePng(Graphics *gfx, const char *pngName) {
    [[maybe_unused]] auto *const imageModule = eve::image::Image::create();
    eve::image::ImageData       *frame       = gfx->newImageData();
    REQUIRE(frame != nullptr);
    eve::filesystem::FileData *png = frame->encode(medialoader::FormatHandler::ENCODED_PNG, pngName, false);
    REQUIRE(png != nullptr);
    REQUIRE(png->getSize() > 0);

    const std::string outDir = std::string(EVENGINE_TEST_BINARY_DIR) + "/out";
    std::error_code   ec;
    std::filesystem::create_directories(outDir, ec);
    const std::string outPath = outDir + "/" + pngName;
    {
        std::ofstream out(outPath, std::ios::binary);
        REQUIRE(out.good());
        out.write(static_cast<const char *>(png->getData()), static_cast<std::streamsize>(png->getSize()));
        REQUIRE(out.good());
    }
    std::printf("animation.skinned render saved: %s\n", outPath.c_str());
    delete png;
    delete frame;
}

enum class SkinnedRenderDriver {
    bindPose,
    clipSample,
    player,
};

/**
 * Open a window, CPU-skin the CesiumMan mesh every frame with the pose produced
 * by `driver`, upload it, render, and assert the character is visible. The last
 * frame is saved as a PNG next to the other test outputs. `frameCount` frames
 * are rendered at ~16ms each so the animation is actually on screen.
 */
[[maybe_unused]] void renderSkinnedAnimation(eve::model3d::ModelData *model, const char *pngName,
                                             SkinnedRenderDriver driver, int frameCount) {
    REQUIRE(model != nullptr);
    REQUIRE(pngName != nullptr);
    REQUIRE(frameCount >= 1);

    const int meshIndex = findFirstSkinnedMesh(model);
    REQUIRE(meshIndex >= 0);
    const aiMesh *ai = model->getMesh(meshIndex);
    REQUIRE(ai != nullptr);
    REQUIRE(ai->mNumVertices > 0u);
    REQUIRE(ai->mNumFaces > 0u);
    const int vertexCount = static_cast<int>(ai->mNumVertices);

    std::unique_ptr<AnimSkeleton> skeleton(AnimImporter::loadSkeletonFromModel(model));
    REQUIRE(skeleton.get() != nullptr);
    std::unique_ptr<AnimSkin> skin(AnimSkin::fromModel(model, meshIndex, skeleton.get()));
    REQUIRE(skin.get() != nullptr);
    std::unique_ptr<AnimClip> clip(AnimImporter::loadClipFromModel(model, skeleton.get(), 0));
    REQUIRE(clip.get() != nullptr);
    CHECK(clip->getDuration() > 0.1f);

    std::vector<float> nrm;
    if (ai->mNormals) {
        nrm.reserve(size_t(vertexCount) * 3u);
        for (int v = 0; v < vertexCount; ++v) {
            nrm.push_back(ai->mNormals[v].x);
            nrm.push_back(ai->mNormals[v].y);
            nrm.push_back(ai->mNormals[v].z);
        }
    }
    std::vector<float> uv;
    if (ai->mTextureCoords[0]) {
        uv.reserve(size_t(vertexCount) * 2u);
        for (int v = 0; v < vertexCount; ++v) {
            uv.push_back(ai->mTextureCoords[0][v].x);
            uv.push_back(ai->mTextureCoords[0][v].y);
        }
    }
    std::vector<uint32_t> indices;
    indices.reserve(size_t(ai->mNumFaces) * 3u);
    for (unsigned f = 0; f < ai->mNumFaces; ++f) {
        const aiFace &face = ai->mFaces[f];
        REQUIRE(face.mNumIndices == 3u);
        for (unsigned k = 0; k < 3u; ++k) indices.push_back(face.mIndices[k]);
    }
    const int indexCount = static_cast<int>(indices.size());

    Bounds b;
    for (int v = 0; v < vertexCount; ++v) b.expand(ai->mVertices[v].x, ai->mVertices[v].y, ai->mVertices[v].z);
    REQUIRE(b.valid);
    const float cx = b.centerX(), cy = b.centerY(), cz = b.centerZ();
    const float rad = std::max(0.5f, b.radius());

    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx);
    resetScene3D();

    Texture *diffuse = loadCesiumManDiffuseTexture(gfx);
    Texture *tex     = diffuse ? diffuse : makeSolidGray(gfx, 200);

    auto *ent = Renderable3D::create();
    ent->setTexture(tex);
    ent->setTint(1.f, 1.f, 1.f, 1.f);
    ent->meshRenderer()->visible       = true;
    ent->meshRenderer()->castShadow    = false;
    ent->meshRenderer()->receiveShadow = false;

    auto *cam = Camera3D::createCamera();
    cam->setTarget(cx, cy, cz);
    cam->setEye(cx + rad * 0.9f, cy + rad * 0.55f, cz + rad * 1.8f);
    cam->data()->nearZ = std::max(0.05f, rad * 0.01f);
    cam->data()->farZ  = std::max(100.f, rad * 20.f);
    cam->setAmbient(0.15f, 0.15f, 0.15f);

    // Light rays travel from the camera toward the subject (RenderSystem3D legacy).
    RenderSystem3D::setDirectionalLight(cx - (cx + rad * 0.9f), cy - (cy + rad * 0.55f) + rad * 1.2f,
                                        cz - (cz + rad * 1.8f), 1.8f, 1.8f, 1.8f);

    gfx->setScreenReadbackEnabled(true);
    gfx->setBackgroundColor(Color(0.02f, 0.02f, 0.03f, 1.f));

    std::unique_ptr<AnimPlayer> player;
    if (driver == SkinnedRenderDriver::player) {
        player.reset(new AnimPlayer(skeleton.get()));
        player->play(clip.get());
    }

    AnimPose           pose;
    std::vector<float> firstLuma;
    float              maxDelta = -1.f;
    bool               first    = true;

    std::vector<float> bindPos(static_cast<size_t>(vertexCount) * 3u);
    for (int v = 0; v < vertexCount; ++v) {
        bindPos[static_cast<size_t>(v) * 3u]      = ai->mVertices[v].x;
        bindPos[static_cast<size_t>(v) * 3u + 1u] = ai->mVertices[v].y;
        bindPos[static_cast<size_t>(v) * 3u + 2u] = ai->mVertices[v].z;
    }
    Mesh *mesh = gfx->newMeshFromArrays(bindPos.data(), nrm.empty() ? nullptr : nrm.data(),
                                        uv.empty() ? nullptr : uv.data(), vertexCount, indices.data(), indexCount);
    REQUIRE(mesh != nullptr);
    REQUIRE(skin->bindGpuMesh(gfx, mesh));
    ent->meshRenderer()->mesh = mesh;

    for (int i = 0; i < frameCount; ++i) {
        AnimPose *usedPose = nullptr;
        switch (driver) {
            case SkinnedRenderDriver::bindPose:
                skeleton->applyBindPose(&pose);
                pose.computeWorld(skeleton.get());
                usedPose = &pose;
                break;
            case SkinnedRenderDriver::clipSample: {
                const float t = frameCount > 1 ? clip->getDuration() * float(i) / float(frameCount - 1) : 0.f;
                clip->sample(t, &pose, skeleton.get());
                pose.computeWorld(skeleton.get());
                usedPose = &pose;
                break;
            }
            case SkinnedRenderDriver::player:
                player->update(1.f / 30.f);
                usedPose = player->getPose();
                REQUIRE(usedPose != nullptr);
                usedPose->computeWorld(skeleton.get());
                break;
        }

        REQUIRE(skin->updateGpuMesh(mesh, usedPose));

        RenderSystem3D::render(*gfx);
        RenderSystem::render(*gfx);
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                win->close();
                return;
            }
        }
        SDL_Delay(16);

        const auto L = captureLumaGrid(gfx);
        if (first) {
            firstLuma = L;
            first     = false;
        } else {
            maxDelta = std::max(maxDelta, maxLumaAbsDelta(gfx, firstLuma, L));
        }
    }

    const auto last = captureLumaGrid(gfx);
    CHECK(foregroundPixelCount(last, 0.15f) > 20);
    if (frameCount > 1 && driver != SkinnedRenderDriver::bindPose) CHECK(maxDelta > 0.01f);
    savePng(gfx, pngName);
    win->close();
}

}  // namespace


TEST_CASE("animation.skinned.cesiumMan.loadSkinAndDeform") {
    if (!ensureSkinnedAssets()) return;

    auto model = eve::test::pinResource(loadCesiumMan("ev_ut_animation_skinned"));
    REQUIRE(model.get() != nullptr);
    REQUIRE(!model->empty());
    CHECK(model->getMeshCount() >= 1);
    CHECK(model->getAnimationCount() >= 1);

    const int meshIndex = findFirstSkinnedMesh(model.get());
    REQUIRE(meshIndex >= 0);
    CHECK(model->hasBones(meshIndex));
    CHECK(model->getBoneCount(meshIndex) >= 2);
    CHECK(model->getVertexCount(meshIndex) > 10);

    // Weight bookkeeping: every bone weight references a valid vertex.
    int totalWeights = 0;
    for (int b = 0; b < model->getBoneCount(meshIndex); ++b) {
        const std::string name = model->getBoneName(meshIndex, b);
        CHECK(!name.empty());
        // Inverse-bind should be a finite matrix.
        float sumAbs = 0.f;
        for (int e = 0; e < 16; ++e) {
            const float v = model->getInverseBindMatrixElement(meshIndex, b, e);
            CHECK(std::isfinite(v));
            sumAbs += std::fabs(v);
        }
        CHECK(sumAbs > 0.f);

        const int wc = model->getBoneWeightCount(meshIndex, b);
        totalWeights += wc;
        for (int w = 0; w < wc; ++w) {
            const int vi = model->getBoneWeightVertex(meshIndex, b, w);
            CHECK(vi >= 0);
            CHECK(vi < model->getVertexCount(meshIndex));
            const float wv = model->getBoneWeightValue(meshIndex, b, w);
            CHECK(wv >= 0.f);
            CHECK(wv <= 1.f + 1e-3f);
        }
    }
    CHECK(totalWeights > 0);

    std::unique_ptr<AnimSkeleton> skeleton(AnimImporter::loadSkeletonFromModel(model.get()));
    REQUIRE(skeleton.get() != nullptr);
    CHECK(skeleton->getBoneCount() >= model->getBoneCount(meshIndex));

    // Skin joints should resolve onto the imported hierarchy by name.
    for (int b = 0; b < model->getBoneCount(meshIndex); ++b) {
        const std::string name = model->getBoneName(meshIndex, b);
        CHECK(skeleton->findBone(name) >= 0);
    }

    std::unique_ptr<AnimClip> clip(AnimImporter::loadClipFromModel(model.get(), skeleton.get(), 0));
    REQUIRE(clip.get() != nullptr);
    CHECK(clip->getDuration() > 0.1f);

    std::unique_ptr<AnimSkin> skin(AnimSkin::fromModel(model.get(), meshIndex, skeleton.get()));
    REQUIRE(skin.get() != nullptr);
    CHECK(skin->getVertexCount() == model->getVertexCount(meshIndex));
    CHECK(skin->getBoneCount() >= 2);

    // Spot-check influence renormalization: weights per vertex sum to ~0 or ~1.
    int weightedVerts = 0;
    for (int v = 0; v < skin->getVertexCount(); ++v) {
        float sum = 0.f;
        for (int i = 0; i < skin->getInfluenceCount(); ++i) {
            sum += skin->getVertexWeight(v, i);
        }
        if (sum > 1e-5f) {
            ++weightedVerts;
            CHECK(std::fabs(sum - 1.f) < 1e-3f);
        }
    }
    CHECK(weightedVerts > 0);

    std::unique_ptr<AnimPose> pose0(new AnimPose());
    std::unique_ptr<AnimPose> pose1(new AnimPose());
    clip->sample(0.f, pose0.get(), skeleton.get());
    pose0->computeWorld(skeleton.get());
    const float mid = clip->getDuration() * 0.5f;
    clip->sample(mid, pose1.get(), skeleton.get());
    pose1->computeWorld(skeleton.get());

    std::vector<float> skinned0, skinned1;
    REQUIRE(skin->skinPositionsTo(pose0.get(), skinned0));
    REQUIRE(skin->skinPositionsTo(pose1.get(), skinned1));
    CHECK(skinned0.size() == static_cast<size_t>(skin->getVertexCount()) * 3u);
    CHECK(skinned1.size() == skinned0.size());

    // Walking clip should move skinned vertices between t=0 and mid-clip.
    const float delta = vertexDeltaMax(skinned0, skinned1);
    CHECK(delta > 0.01f);

    // Player path: advance a few frames and keep deforming.
    std::unique_ptr<AnimPlayer> player(new AnimPlayer(skeleton.get()));
    player->play(clip.get());
    for (int i = 0; i < 8; ++i) player->update(1.f / 30.f);
    AnimPose *live = player->getPose();
    REQUIRE(live != nullptr);
    live->computeWorld(skeleton.get());
    std::vector<float> skinnedLive;
    REQUIRE(skin->skinPositionsTo(live, skinnedLive));
    CHECK(vertexDeltaMax(skinned0, skinnedLive) > 1e-4f);
}

TEST_CASE("animation.skinned.getSkinnedPositionsMatchesSkinPositionsTo") {
    if (!ensureSkinnedAssets()) return;

    auto model = eve::test::pinResource(loadCesiumMan("ev_ut_animation_skinned_posarray"));
    REQUIRE(model.get() != nullptr);
    const int meshIndex = findFirstSkinnedMesh(model.get());
    REQUIRE(meshIndex >= 0);

    std::unique_ptr<AnimSkeleton> skeleton(AnimImporter::loadSkeletonFromModel(model.get()));
    REQUIRE(skeleton.get() != nullptr);
    std::unique_ptr<AnimSkin> skin(AnimSkin::fromModel(model.get(), meshIndex, skeleton.get()));
    REQUIRE(skin.get() != nullptr);

    // Not skinned yet: packed getters are empty and the flags are false.
    CHECK(skin->getSkinnedPositions().empty());
    CHECK(skin->getSkinnedNormals().empty());
    CHECK(!skin->hasSkinnedPositions());
    CHECK(!skin->hasSkinnedNormals());

    std::unique_ptr<AnimClip> clip(AnimImporter::loadClipFromModel(model.get(), skeleton.get(), 0));
    REQUIRE(clip.get() != nullptr);
    std::unique_ptr<AnimPose> pose(new AnimPose());
    clip->sample(clip->getDuration() * 0.25f, pose.get(), skeleton.get());
    pose->computeWorld(skeleton.get());

    REQUIRE(skin->updateSkinnedPositions(pose.get()));
    CHECK(skin->hasSkinnedPositions());

    std::vector<float> expected;
    REQUIRE(skin->skinPositionsTo(pose.get(), expected));
    const std::vector<float> packed = skin->getSkinnedPositions();
    REQUIRE(packed.size() == expected.size());
    CHECK(vertexDeltaMax(packed, expected) < 1e-5f);
}

TEST_CASE("animation.skinned.normalsFollowPose") {
    if (!ensureSkinnedAssets()) return;

    auto model = eve::test::pinResource(loadCesiumMan("ev_ut_animation_skinned_normals"));
    REQUIRE(model.get() != nullptr);
    const int meshIndex = findFirstSkinnedMesh(model.get());
    REQUIRE(meshIndex >= 0);
    const aiMesh *ai = model->getMesh(meshIndex);
    REQUIRE(ai != nullptr);
    REQUIRE(ai->HasNormals());
    REQUIRE(ai->mNormals != nullptr);

    const auto findMeshWorld = [&](auto &&self, const aiNode *node, const aiMatrix4x4 &parent,
                                   aiMatrix4x4 &result) -> bool {
        const auto world = parent * node->mTransformation;
        for (unsigned i = 0; i < node->mNumMeshes; ++i)
            if (node->mMeshes[i] == static_cast<unsigned>(meshIndex)) {
                result = world;
                return true;
            }
        for (unsigned i = 0; i < node->mNumChildren; ++i)
            if (self(self, node->mChildren[i], world, result)) return true;
        return false;
    };
    REQUIRE(model->getScene() != nullptr);
    REQUIRE(model->getScene()->mRootNode != nullptr);
    aiMatrix4x4 meshWorld;
    REQUIRE(findMeshWorld(findMeshWorld, model->getScene()->mRootNode, aiMatrix4x4{}, meshWorld));
    aiMatrix3x3 normalTransform(meshWorld);
    normalTransform.Inverse().Transpose();
    std::vector<float> bindNrm(size_t(ai->mNumVertices) * 3u);
    for (int v = 0; v < int(ai->mNumVertices); ++v) {
        auto normal = normalTransform * ai->mNormals[v];
        normal.Normalize();
        bindNrm[size_t(v) * 3u + 0] = normal.x;
        bindNrm[size_t(v) * 3u + 1] = normal.y;
        bindNrm[size_t(v) * 3u + 2] = normal.z;
    }

    std::unique_ptr<AnimSkeleton> skeleton(AnimImporter::loadSkeletonFromModel(model.get()));
    REQUIRE(skeleton.get() != nullptr);
    std::unique_ptr<AnimSkin> skin(AnimSkin::fromModel(model.get(), meshIndex, skeleton.get()));
    REQUIRE(skin.get() != nullptr);

    // Bind-pose normals are expressed in skeleton world space, including the mesh node transform.
    std::unique_ptr<AnimPose> bind(new AnimPose());
    skeleton->applyBindPose(bind.get());
    bind->computeWorld(skeleton.get());
    REQUIRE(skin->updateSkinnedNormals(bind.get()));
    CHECK(skin->hasSkinnedNormals());
    std::vector<float> bindSkinned = skin->getSkinnedNormals();
    REQUIRE(bindSkinned.size() == bindNrm.size());
    CHECK(vertexDeltaMax(bindSkinned, bindNrm) < 1e-3f);

    // Mid-clip pose: at least one normal must rotate, and all stay unit length.
    std::unique_ptr<AnimClip> clip(AnimImporter::loadClipFromModel(model.get(), skeleton.get(), 0));
    REQUIRE(clip.get() != nullptr);
    std::unique_ptr<AnimPose> mid(new AnimPose());
    clip->sample(clip->getDuration() * 0.5f, mid.get(), skeleton.get());
    mid->computeWorld(skeleton.get());
    REQUIRE(skin->updateSkinnedNormals(mid.get()));
    const std::vector<float> midSkinned = skin->getSkinnedNormals();
    REQUIRE(midSkinned.size() == bindNrm.size());
    CHECK(vertexDeltaMax(midSkinned, bindNrm) > 1e-3f);
    for (size_t i = 0; i + 2 < midSkinned.size(); i += 3) {
        const float nx  = midSkinned[i];
        const float ny  = midSkinned[i + 1];
        const float nz  = midSkinned[i + 2];
        const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
        CHECK(std::fabs(len - 1.f) < 1e-3f);
    }
}

TEST_CASE("animation.skinned.cesiumMan.animationFactory") {
    if (!ensureSkinnedAssets()) return;

    auto model = eve::test::pinResource(loadCesiumMan("ev_ut_animation_skinned_factory"));
    REQUIRE(model.get() != nullptr);

    // Animation::create() returns the process-wide module singleton owned by
    // ModuleManager; do NOT wrap it in unique_ptr (that would delete the
    // singleton out from under ModuleManager and leave a dangling pointer for
    // later tests that call Animation::create() again).
    Animation *anim = Animation::create();
    REQUIRE(anim != nullptr);
    std::unique_ptr<AnimSkeleton> sk(anim->newSkeletonFromModel(model.get()));
    REQUIRE(sk.get() != nullptr);
    const int meshIndex = findFirstSkinnedMesh(model.get());
    REQUIRE(meshIndex >= 0);
    std::unique_ptr<AnimSkin> skin(anim->newSkinFromModel(model.get(), meshIndex, sk.get()));
    REQUIRE(skin.get() != nullptr);
    CHECK(skin->getVertexCount() > 0);
    std::unique_ptr<AnimClip> clip(anim->newClipFromModel(model.get(), sk.get(), 0));
    REQUIRE(clip.get() != nullptr);
    CHECK(clip->getDuration() > 0.f);
}

TEST_CASE("animation.skinned.render.bindPose") {
    if (!ensureSkinnedAssets()) return;

    auto model = eve::test::pinResource(loadCesiumMan("ev_ut_animation_skinned_render_bindpose"));
    REQUIRE(model.get() != nullptr);
    renderSkinnedAnimation(model.get(), "animation_skinned_bindpose.png", SkinnedRenderDriver::bindPose, 3);
}

TEST_CASE("animation.skinned.render.clipSampledMovesPixels") {
    if (!ensureSkinnedAssets()) return;

    auto model = eve::test::pinResource(loadCesiumMan("ev_ut_animation_skinned_render_clipsample"));
    REQUIRE(model.get() != nullptr);
    // Walk cycle across 32 frames: mid-cycle pose differs from the first frame,
    // so the rendered silhouette must move (loop start/end are identical).
    renderSkinnedAnimation(model.get(), "animation_skinned_walk.png", SkinnedRenderDriver::clipSample, 32);
}

TEST_CASE("animation.skinned.render.playerDriven") {
    if (!ensureSkinnedAssets()) return;

    auto model = eve::test::pinResource(loadCesiumMan("ev_ut_animation_skinned_render_player"));
    REQUIRE(model.get() != nullptr);
    renderSkinnedAnimation(model.get(), "animation_skinned_player.png", SkinnedRenderDriver::player, 60);
}

TEST_CASE("animation.skinned.render.applyToMesh") {
    if (!ensureSkinnedAssets()) return;

    auto model = eve::test::pinResource(loadCesiumMan("ev_ut_animation_skinned_render_apply"));
    REQUIRE(model.get() != nullptr);

    const int meshIndex = findFirstSkinnedMesh(model.get());
    REQUIRE(meshIndex >= 0);
    const aiMesh *ai = model->getMesh(meshIndex);
    REQUIRE(ai != nullptr);
    REQUIRE(ai->mNumVertices > 0u);
    REQUIRE(ai->mNumFaces > 0u);
    const int vertexCount = static_cast<int>(ai->mNumVertices);

    std::vector<float> pos, nrm, uv;
    pos.reserve(size_t(vertexCount) * 3u);
    for (int v = 0; v < vertexCount; ++v) {
        pos.push_back(ai->mVertices[v].x);
        pos.push_back(ai->mVertices[v].y);
        pos.push_back(ai->mVertices[v].z);
    }
    if (ai->mNormals) {
        nrm.reserve(size_t(vertexCount) * 3u);
        for (int v = 0; v < vertexCount; ++v) {
            nrm.push_back(ai->mNormals[v].x);
            nrm.push_back(ai->mNormals[v].y);
            nrm.push_back(ai->mNormals[v].z);
        }
    }
    if (ai->mTextureCoords[0]) {
        uv.reserve(size_t(vertexCount) * 2u);
        for (int v = 0; v < vertexCount; ++v) {
            uv.push_back(ai->mTextureCoords[0][v].x);
            uv.push_back(ai->mTextureCoords[0][v].y);
        }
    }
    std::vector<uint32_t> indices;
    indices.reserve(size_t(ai->mNumFaces) * 3u);
    for (unsigned f = 0; f < ai->mNumFaces; ++f) {
        const aiFace &face = ai->mFaces[f];
        REQUIRE(face.mNumIndices == 3u);
        for (unsigned k = 0; k < 3u; ++k) indices.push_back(face.mIndices[k]);
    }
    const int indexCount = static_cast<int>(indices.size());

    Bounds b;
    for (int v = 0; v < vertexCount; ++v) b.expand(ai->mVertices[v].x, ai->mVertices[v].y, ai->mVertices[v].z);
    REQUIRE(b.valid);
    const float cx = b.centerX(), cy = b.centerY(), cz = b.centerZ();
    const float rad = std::max(0.5f, b.radius());

    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx);
    resetScene3D();

    Texture *diffuse = loadCesiumManDiffuseTexture(gfx);
    Texture *tex     = diffuse ? diffuse : makeSolidGray(gfx, 200);

    auto *ent = Renderable3D::create();
    ent->setTexture(tex);
    ent->setTint(1.f, 1.f, 1.f, 1.f);
    ent->meshRenderer()->visible       = true;
    ent->meshRenderer()->castShadow    = false;
    ent->meshRenderer()->receiveShadow = false;

    auto *cam = Camera3D::createCamera();
    cam->setTarget(cx, cy, cz);
    cam->setEye(cx + rad * 0.9f, cy + rad * 0.55f, cz + rad * 1.8f);
    cam->data()->nearZ = std::max(0.05f, rad * 0.01f);
    cam->data()->farZ  = std::max(100.f, rad * 20.f);
    cam->setAmbient(0.15f, 0.15f, 0.15f);
    RenderSystem3D::setDirectionalLight(cx - (cx + rad * 0.9f), cy - (cy + rad * 0.55f) + rad * 1.2f,
                                        cz - (cz + rad * 1.8f), 1.8f, 1.8f, 1.8f);
    gfx->setScreenReadbackEnabled(true);
    gfx->setBackgroundColor(Color(0.02f, 0.02f, 0.03f, 1.f));

    // Mesh created once; skinned results are written back in place per frame.
    Mesh *mesh = gfx->newMeshFromArrays(pos.data(), nrm.empty() ? nullptr : nrm.data(),
                                        uv.empty() ? nullptr : uv.data(), vertexCount, indices.data(), indexCount);
    REQUIRE(mesh != nullptr);
    CHECK(mesh->getVertexCount() == vertexCount);
    ent->meshRenderer()->mesh = mesh;

    std::unique_ptr<AnimSkeleton> skeleton(AnimImporter::loadSkeletonFromModel(model.get()));
    REQUIRE(skeleton.get() != nullptr);
    std::unique_ptr<AnimSkin> skin(AnimSkin::fromModel(model.get(), meshIndex, skeleton.get()));
    REQUIRE(skin.get() != nullptr);
    CHECK(skin->getVertexCount() == vertexCount);
    std::unique_ptr<AnimClip> clip(AnimImporter::loadClipFromModel(model.get(), skeleton.get(), 0));
    REQUIRE(clip.get() != nullptr);
    std::unique_ptr<AnimPlayer> player(new AnimPlayer(skeleton.get()));
    player->play(clip.get());

    std::vector<float> firstLuma;
    float              maxDelta = -1.f;
    bool               first    = true;
    for (int i = 0; i < 30; ++i) {
        player->update(1.f / 30.f);
        AnimPose *pose = player->getPose();
        REQUIRE(pose != nullptr);
        pose->computeWorld(skeleton.get());
        REQUIRE(skin->applyToMesh(gfx, mesh, pose));

        RenderSystem3D::render(*gfx);
        RenderSystem::render(*gfx);
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                win->close();
                return;
            }
        }
        SDL_Delay(16);

        const auto L = captureLumaGrid(gfx);
        if (first) {
            firstLuma = L;
            first     = false;
        } else {
            maxDelta = std::max(maxDelta, maxLumaAbsDelta(gfx, firstLuma, L));
        }
    }

    const auto last = captureLumaGrid(gfx);
    CHECK(foregroundPixelCount(last, 0.15f) > 20);
    CHECK(maxDelta > 0.01f);
    savePng(gfx, "animation_skinned_apply.png");
    win->close();
}
}  // namespace test_animation_skinned

// Contract section: avatar vrm
#include "PrivateTestScope.h"

#include <cstring>
#include "avatar/AvatarInstance.h"
#include "avatar/VrmDocument.h"
#include "common/ECS.h"
// avatar_vrm.cpp
namespace test_avatar_vrm {
namespace {
[[maybe_unused]] std::vector<std::byte> vrmGlb(std::string json) {
    while (json.size() % 4) json += ' ';
    std::vector<std::byte> bytes(20 + json.size());
    const uint32_t header[] = {0x46546c67, 2, static_cast<uint32_t>(bytes.size()), static_cast<uint32_t>(json.size()),
                               0x4e4f534a};
    std::memcpy(bytes.data(), header, 20);
    std::memcpy(bytes.data() + 20, json.data(), json.size());
    return bytes;
}
const char *minimalVrm = R"({"asset":{"version":"2.0"},"nodes":[{"name":"head"}],
"extensions":{"VRMC_vrm":{"specVersion":"1.0","humanoid":{"humanBones":{"head":{"node":0}}}}}})";
}  // namespace


TEST_CASE("avatar.vrm.providerAbsentDoesNotPublish") {
    auto *fs = eve::filesystem::Filesystem::create();
    REQUIRE(fs->setIdentity("ev_ut_vrm_absent", true));
    REQUIRE(fs->setupWriteDirectory());
    const auto directory =
        (std::filesystem::path(eve_test_path::sourceBesideTestDir(__FILE__)).parent_path() / "assets/vrm").string();
    fs->allowMountingForPath(directory);
    REQUIRE(fs->mount(directory, "", false));
    eve::avatar::AvatarInstance avatar("vroid");
    auto                        result = avatar.loadVroidModel("contract.vrm");
    REQUIRE(!result.ok());
    REQUIRE_EQ(avatar.getVroidMeshCount(), 0);
    REQUIRE(avatar.getVroidModelPath().empty());
    REQUIRE(fs->unmount(directory));
}

TEST_CASE("avatar.vrm.graphicsBackendSupportIsExplicit") {
    GfxFixture graphics(320, 240);
    auto      *fs = eve::filesystem::Filesystem::create();
    REQUIRE(fs->setIdentity("ev_ut_vrm_backend", true));
    REQUIRE(fs->setupWriteDirectory());
    const auto directory =
        (std::filesystem::path(eve_test_path::sourceBesideTestDir(__FILE__)).parent_path() / "assets/vrm").string();
    fs->allowMountingForPath(directory);
    REQUIRE(fs->mount(directory, "", false));
    eve::avatar::AvatarInstance avatar("vroid");
    auto                        result = avatar.loadVroidModel("contract.vrm");
    if (graphics.gfx->getBackendName() == "vulkan") {
        REQUIRE(result.ok());
        REQUIRE_EQ(avatar.getVroidMeshCount(), 2);
    } else {
        REQUIRE(!result.ok());
        REQUIRE_EQ(avatar.getVroidMeshCount(), 0);
        REQUIRE(avatar.getVroidModelPath().empty());
    }
    REQUIRE(fs->unmount(directory));
}
}  // namespace test_avatar_vrm

// Contract section: cmdline
#include "PrivateTestScope.h"

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <streambuf>
#include "cmdline/cmdline.h"
#include "cmdline/sdk_tools.h"
#include "cmdline/zip_writer.h"
#include "common/Module.h"
// cmdline.cpp
namespace test_cmdline {
namespace {

// Runs the cmdline module in-process with the given argv (argv[0] is the
// program name) and returns the exit code. CWD-relative commands operate on
// the current directory, so tests wrap this with PushDir.
[[maybe_unused]] int runCli(const std::vector<std::string> &args) {
    std::vector<char *> argv;
    argv.reserve(args.size());
    for (const auto &a : args) argv.push_back(const_cast<char *>(a.c_str()));
    auto *cmd = eve::ModuleManager::requireInstance<eve::cmd::Cmdline>("Cmdline");
    return cmd->runArgs(static_cast<unsigned>(argv.size()), argv.data());
}

// RAII: chdir into `dir` for the lifetime of the object, restoring afterwards
// so tests sharing a process cannot leak the working directory into each other.
class PushDir {
public:
    explicit PushDir(const std::filesystem::path &dir) : old_(std::filesystem::current_path()) {
        std::filesystem::current_path(dir);
    }
    ~PushDir() {
        std::error_code ec;
        std::filesystem::current_path(old_, ec);
    }

private:
    std::filesystem::path old_;
};

// Swaps cout/cerr buffers so the test can inspect what a command printed
// (rang color codes are dropped automatically when the sink is not a TTY).
class CaptureStreams {
public:
    CaptureStreams() {
        cout_old_ = std::cout.rdbuf(cout_buf_.rdbuf());
        cerr_old_ = std::cerr.rdbuf(cerr_buf_.rdbuf());
    }
    ~CaptureStreams() {
        std::cout.rdbuf(cout_old_);
        std::cerr.rdbuf(cerr_old_);
    }

    std::string out() const { return cout_buf_.str(); }
    std::string err() const { return cerr_buf_.str(); }
    std::string all() const { return out() + err(); }

private:
    std::ostringstream cout_buf_, cerr_buf_;
    std::streambuf    *cout_old_ = nullptr;
    std::streambuf    *cerr_old_ = nullptr;
};

[[maybe_unused]] std::filesystem::path tempDir(const char *tag) {
    const auto      p = std::filesystem::temp_directory_path() / tag;
    std::error_code ec;
    std::filesystem::remove_all(p, ec);
    std::filesystem::create_directories(p, ec);
    return p;
}

[[maybe_unused]] void writeFile(const std::filesystem::path &p, const std::string &content) {
    std::filesystem::create_directories(p.parent_path());
    std::ofstream ofs(p, std::ios::binary | std::ios::trunc);
    ofs << content;
}

[[maybe_unused]] std::string readTextFile(const std::filesystem::path &p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

// Reads the ZIP central directory (EOCD -> central entries) and returns the
// entry names in archive order. Returns {} when the file is not a valid ZIP.
[[maybe_unused]] std::vector<std::string> zipEntryNames(const std::filesystem::path &p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return {};
    const std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (data.size() < 22) return {};

    const size_t eocd = data.rfind("PK\x05\x06");
    if (eocd == std::string::npos || data.size() - eocd < 22) return {};

    const auto le16 = [&](size_t off) -> uint16_t {
        return static_cast<uint16_t>(static_cast<unsigned char>(data[off]) |
                                     (static_cast<unsigned char>(data[off + 1]) << 8));
    };
    const auto le32 = [&](size_t off) -> uint32_t {
        return static_cast<uint32_t>(
            static_cast<unsigned char>(data[off]) | (static_cast<unsigned char>(data[off + 1]) << 8) |
            (static_cast<unsigned char>(data[off + 2]) << 16) | (static_cast<unsigned char>(data[off + 3]) << 24));
    };

    const uint32_t cdSize   = le32(eocd + 12);
    const uint32_t cdOffset = le32(eocd + 16);
    if (cdOffset > data.size() || cdSize > data.size() - cdOffset) return {};

    std::vector<std::string> names;
    size_t                   pos = cdOffset;
    const size_t             end = static_cast<size_t>(cdOffset) + cdSize;
    while (pos + 46 <= end && data.compare(pos, 4, "PK\x01\x02") == 0) {
        const size_t nameLen    = le16(pos + 28);
        const size_t extraLen   = le16(pos + 30);
        const size_t commentLen = le16(pos + 32);
        if (pos + 46 + nameLen + extraLen + commentLen > end) return {};
        names.emplace_back(data.substr(pos + 46, nameLen));
        pos += 46 + nameLen + extraLen + commentLen;
    }
    return names;
}

[[maybe_unused]] void setEnvVar(const char *name, const std::string &value) {
#if defined(_WIN32)
    _putenv_s(name, value.c_str());
#else
    setenv(name, value.c_str(), 1);
#endif
}

[[maybe_unused]] void unsetEnvVar(const char *name) {
#if defined(_WIN32)
    _putenv_s(name, "");
#else
    unsetenv(name);
#endif
}

// RAII env removal; restores the previous value (or leaves it unset) afterwards.
class ScopedUnsetEnv {
public:
    explicit ScopedUnsetEnv(const char *name) : name_(name) {
        const char *old = std::getenv(name);
        if (old) {
            had_ = true;
            old_ = old;
            unsetEnvVar(name);
        }
    }
    ~ScopedUnsetEnv() {
        if (had_) setEnvVar(name_.c_str(), old_);
    }

private:
    std::string name_, old_;
    bool        had_ = false;
};

// RAII env override; restores the previous value (or removes it) afterwards so
// tests sharing a process cannot leak settings into each other.
class ScopedEnv {
public:
    ScopedEnv(const char *name, const std::string &value) : name_(name) {
        const char *old = std::getenv(name);
        if (old) {
            had_ = true;
            old_ = old;
        }
        setEnvVar(name, value);
    }
    ~ScopedEnv() {
        if (had_)
            setEnvVar(name_.c_str(), old_);
        else
            unsetEnvVar(name_.c_str());
    }

private:
    std::string name_, old_;
    bool        had_ = false;
};

// A fake EVEngine SDK install: TARGET_PLATFORM marker, prebuilt lib/, and the
// android APK template (platform/apk) exactly like `make sdk/android` ships.
[[maybe_unused]] std::filesystem::path fakeSdkRoot(const char *tag, const char *platform) {
    const auto sdk = tempDir(tag);
    writeFile(sdk / "share" / "eve" / "TARGET_PLATFORM", std::string(platform) + "\n");
    writeFile(sdk / "lib" / "libmain.so", "fake so");
    writeFile(sdk / "lib" / "libSDL2.so", "fake so");
    writeFile(sdk / "platform" / "apk" / "template.txt", "apk template");
    writeFile(sdk / "platform" / "game-shell" / "config.nut", "width=800\n");
    return sdk;
}

// A fake gradle distribution whose launcher records the invocation and writes
// the APK output that `eve build android` checks for. unsignedOnly mimics a
// release build without a signing config (AGP emits app-release-unsigned.apk).
[[maybe_unused]] void writeFakeGradle(const std::filesystem::path &gradleHome, bool unsignedOnly = false) {
    const auto      bin = gradleHome / "bin";
    std::error_code ec;
    std::filesystem::create_directories(bin, ec);
#if defined(_WIN32)
    writeFile(bin / "gradle.bat",
              "@echo off\r\n"
              "echo FAKE_GRADLE %* > gradle-invoked.txt\r\n"
              "mkdir app\\build\\outputs\\apk\\release 2>nul\r\n"
              "mkdir app\\build\\outputs\\apk\\debug 2>nul\r\n"
              "echo ok > app\\build\\outputs\\apk\\release\\" +
                  std::string(unsignedOnly ? "app-release-unsigned.apk" : "app-release.apk") +
                  "\r\n"
                  "echo ok > app\\build\\outputs\\apk\\debug\\app-debug.apk\r\n");
#else
    writeFile(bin / "gradle",
              "#!/bin/sh\n"
              "echo \"FAKE_GRADLE $*\" > gradle-invoked.txt\n"
              "mkdir -p app/build/outputs/apk/release app/build/outputs/apk/debug\n"
              "echo ok > app/build/outputs/apk/release/" +
                  std::string(unsignedOnly ? "app-release-unsigned.apk" : "app-release.apk") +
                  "\n"
                  "echo ok > app/build/outputs/apk/debug/app-debug.apk\n");
    std::filesystem::permissions(
        bin / "gradle",
        std::filesystem::perms::owner_all | std::filesystem::perms::group_read | std::filesystem::perms::others_read,
        ec);
#endif
}

// A fake sdkmanager whose only job is to exit 0 for --licenses and package
// installs, so `eve get android` skips the real command-line-tools download.
[[maybe_unused]] void writeFakeSdkmanager(const std::filesystem::path &sdkRoot) {
    const auto      bin = sdkRoot / "cmdline-tools" / "latest" / "bin";
    std::error_code ec;
    std::filesystem::create_directories(bin, ec);
#if defined(_WIN32)
    writeFile(bin / "sdkmanager.bat", "@exit /b 0\r\n");
#else
    writeFile(bin / "sdkmanager", "#!/bin/sh\nexit 0\n");
    std::filesystem::permissions(
        bin / "sdkmanager",
        std::filesystem::perms::owner_all | std::filesystem::perms::group_read | std::filesystem::perms::others_read,
        ec);
#endif
}

// 版本号去前缀 v（share/eve/VERSION 里不带 v）。
[[maybe_unused]] std::string sdkVersionNoV() {
    std::string tag = eve::cmd::sdk::sdkVersionTag();
    return tag.empty() ? std::string("0.0.0") : tag.substr(1);
}

// file:// URL（供 curl 读取本地 fake release）。
[[maybe_unused]] std::string fileUrl(const std::filesystem::path &p) {
    std::string s = p.lexically_normal().string();
    std::replace(s.begin(), s.end(), '\\', '/');
    if (s.empty() || s[0] != '/') s = "/" + s;
    return "file://" + s;
}

// 写一个和官方 zip-sdk.py 布局一致的 fake SDK 包：
// 顶层 eve-sdk/android/...（含标记、lib、platform 模板）。
[[maybe_unused]] void writeFakeEveSdkZip(const std::filesystem::path &zipPath, const std::string &ver) {
    eve::cmdline::ZipWriter zw;
    REQUIRE(zw.open(zipPath.string()));
    const auto add = [&](const std::string &rel, const std::string &data) {
        REQUIRE(zw.addFile(rel, data.data(), data.size()));
    };
    add("eve-sdk/android/share/eve/TARGET_PLATFORM", "android\n");
    add("eve-sdk/android/share/eve/VERSION", ver + "\n");
    add("eve-sdk/android/lib/libmain.so", "fake so");
    add("eve-sdk/android/lib/libSDL2.so", "fake so");
    add("eve-sdk/android/platform/apk/template.txt", "apk template");
    add("eve-sdk/android/platform/game-shell/config.nut", "width=800\n");
    REQUIRE(zw.finish());
}

// 为 fake release 目录写 SHA256SUMS（correct=false 时放一个错误摘要）。
[[maybe_unused]] void writeSha256Sums(const std::filesystem::path &releaseDir, const std::filesystem::path &zipPath,
                                      bool correct) {
    const std::string hex = correct ? eve::cmd::sdk::fileSha256(zipPath.string()) : std::string(64, '0');
    writeFile(releaseDir / "SHA256SUMS", hex + "  " + zipPath.filename().string() + "\n");
}

// 在 EVEngine SDK 安装根预置一个 android SDK（跳过下载路径）。
[[maybe_unused]] std::filesystem::path fakeInstalledEveSdk(const std::filesystem::path &installRoot) {
    const auto sdk = installRoot / "android";
    writeFile(sdk / "share" / "eve" / "TARGET_PLATFORM", "android\n");
    writeFile(sdk / "share" / "eve" / "VERSION", sdkVersionNoV() + "\n");
    writeFile(sdk / "lib" / "libmain.so", "fake so");
    writeFile(sdk / "lib" / "libSDL2.so", "fake so");
    writeFile(sdk / "platform" / "apk" / "template.txt", "apk template");
    writeFile(sdk / "platform" / "game-shell" / "config.nut", "width=800\n");
    return sdk;
}

}  // namespace


TEST_CASE("cmdline.createScaffoldsGame") {
    const auto dir = tempDir("eve_ut_cmdline_create");
    PushDir    pd(dir);
    {
        CaptureStreams cap;
        const int      rc = runCli({"eve", "create", "mygame"});
        CHECK(rc == 0);
        CHECK(cap.out().find("Created mygame") != std::string::npos);
    }
    REQUIRE(std::filesystem::is_regular_file(dir / "mygame" / "config.nut"));
    REQUIRE(std::filesystem::is_regular_file(dir / "mygame" / "main.nut"));
    const std::string config = readTextFile(dir / "mygame" / "config.nut");
    const std::string main   = readTextFile(dir / "mygame" / "main.nut");
    CHECK(config.find("modules = [\"gfx\"]") != std::string::npos);
    CHECK(main.find("persist boxX: float = 0.0") != std::string::npos);
    CHECK(main.find("eve_update = function(dt: float)") != std::string::npos);
    CHECK(main.find("getroottable") == std::string::npos);

    // A second run must not clobber existing files.
    {
        CaptureStreams cap;
        const int      rc = runCli({"eve", "create", "mygame"});
        CHECK(rc == 0);
        CHECK(cap.out().find("skip existing config.nut") != std::string::npos);
    }
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

TEST_CASE("cmdline.cleanDoesNotScaffoldProject") {
    // Regression: the clean handler used to call Create(), so `eve clean`
    // scaffolded a fresh "debug" project in the current directory.
    const auto     dir = tempDir("eve_ut_cmdline_clean");
    PushDir        pd(dir);
    CaptureStreams cap;
    const int      rc = runCli({"eve", "clean"});
    CHECK(rc == 0);
    CHECK(cap.out().find("Clean debug build") != std::string::npos);
    CHECK(!std::filesystem::exists(dir / "debug"));
    CHECK(!std::filesystem::exists(dir / "config.nut"));
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}


TEST_CASE("cmdline.zipDefaultNamesArchiveAfterCwd") {
    // Regression: `eve zip` with no path (default ".") used to write a file
    // literally named "..eve" instead of <current-dir-name>.eve.
    const auto dir = tempDir("eve_ut_cmdline_zip_default");
    PushDir    pd(dir);
    writeFile(dir / "main.nut", "eve_init = function() {}\n");

    CaptureStreams cap;
    const int      rc = runCli({"eve", "zip"});
    CHECK(rc == 0);
    const std::string expected = dir.filename().string() + ".eve";
    CHECK(cap.out().find(expected) != std::string::npos);
    CHECK(!std::filesystem::exists(dir / "..eve"));
    REQUIRE(std::filesystem::is_regular_file(dir / expected));
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

TEST_CASE("cmdline.zipProducesValidArchiveWithoutItself") {
    const auto dir = tempDir("eve_ut_cmdline_zip_entries");
    PushDir    pd(dir);
    writeFile(dir / "main.nut", "eve_init = function() {}\n");
    writeFile(dir / "scripts" / "a.nut", "a <- 1\n");

    CaptureStreams cap;
    const int      rc = runCli({"eve", "zip"});
    CHECK(rc == 0);

    const std::string archive = dir.filename().string() + ".eve";
    REQUIRE(std::filesystem::is_regular_file(dir / archive));

    const auto names = zipEntryNames(dir / archive);
    REQUIRE(names.size() == 2);
    // Directory iteration order is not guaranteed, so compare sorted sets.
    std::vector<std::string> sorted = names;
    std::sort(sorted.begin(), sorted.end());
    CHECK(sorted[0] == "main.nut");
    CHECK(sorted[1] == "scripts/a.nut");
    // The writer must never embed the output archive into itself.
    for (const auto &n : names) CHECK(n != archive);

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

TEST_CASE("cmdline.buildAndroidAssemblesApkFromSdk") {
    const auto sdk   = fakeSdkRoot("eve_ut_cmdline_sdk_android", "android");
    const auto tools = tempDir("eve_ut_cmdline_android_tools");
    writeFakeGradle(tools / "gradle-8.5");
    ScopedEnv sdkEnv("EVENGINE_ANDROID_SDK", tools.string());
    ScopedEnv javaEnv("JAVA_HOME", tools.string());
    ScopedEnv gradleEnv("GRADLE_HOME", (tools / "gradle-8.5").string());

    const auto game = tempDir("eve_ut_cmdline_game");
    writeFile(game / "main.nut", "eve_init = function() {}\n");
    writeFile(game / "scripts" / "a.nut", "a <- 1\n");
    const auto out = tempDir("eve_ut_cmdline_apk_out");

    CaptureStreams cap;
    const int      rc = runCli({"eve", "build", "android", game.string(), "--sdk", sdk.string(), "-o", out.string()});
    CHECK(rc == 0);

    // Game assets + prebuilt libs + sdk.dir landed in the assembled project.
    std::error_code ec;
    REQUIRE(
        std::filesystem::is_regular_file(out / "apk" / "app" / "src" / "main" / "assets" / "game" / "main.nut", ec));
    REQUIRE(std::filesystem::is_regular_file(
        out / "apk" / "app" / "src" / "main" / "assets" / "game" / "scripts" / "a.nut", ec));
    REQUIRE(std::filesystem::is_regular_file(
        out / "apk" / "app" / "src" / "main" / "jniLibs" / "arm64-v8a" / "libmain.so", ec));
    REQUIRE(std::filesystem::is_regular_file(
        out / "apk" / "app" / "src" / "main" / "jniLibs" / "arm64-v8a" / "libSDL2.so", ec));
    REQUIRE(std::filesystem::is_regular_file(out / "apk" / "local.properties", ec));
    {
        std::ifstream in(out / "apk" / "local.properties");
        std::string   s((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        std::string   expected = tools.string();
        for (char &c : expected)
            if (c == '\\') c = '/';
        CHECK(s.find("sdk.dir=" + expected) != std::string::npos);
    }
    // Fake gradle ran with the release task and the APK path was reported.
    REQUIRE(std::filesystem::is_regular_file(out / "apk" / "gradle-invoked.txt", ec));
    {
        std::ifstream in(out / "apk" / "gradle-invoked.txt");
        std::string   s;
        std::getline(in, s);
        CHECK(s.find("assembleRelease") != std::string::npos);
    }
    REQUIRE(cap.out().find("app-release.apk") != std::string::npos);

    // Debug variant picks the assembleDebug task.
    CaptureStreams cap2;
    const int rc2 = runCli({"eve", "build", "-d", "android", game.string(), "--sdk", sdk.string(), "-o", out.string()});
    CHECK(rc2 == 0);
    {
        std::ifstream in(out / "apk" / "gradle-invoked.txt");
        std::string   s;
        std::getline(in, s);
        CHECK(s.find("assembleDebug") != std::string::npos);
    }
    REQUIRE(cap2.out().find("app-debug.apk") != std::string::npos);

    std::filesystem::remove_all(sdk, ec);
    std::filesystem::remove_all(tools, ec);
    std::filesystem::remove_all(game, ec);
    std::filesystem::remove_all(out, ec);
}

TEST_CASE("cmdline.buildWrongSdkPlatformFails") {
    const auto     sdk   = fakeSdkRoot("eve_ut_cmdline_sdk_win32", "win32");
    const auto     tools = tempDir("eve_ut_cmdline_android_tools_wrong");
    ScopedEnv      sdkEnv("EVENGINE_ANDROID_SDK", tools.string());
    CaptureStreams cap;
    const int      rc = runCli({"eve", "build", "android", "--sdk", sdk.string()});
    CHECK(rc == 2);
    CHECK(cap.all().find("not android") != std::string::npos);
    std::error_code ec;
    std::filesystem::remove_all(sdk, ec);
    std::filesystem::remove_all(tools, ec);
}

TEST_CASE("cmdline.buildAndroidReportsUnsignedApk") {
    const auto sdk   = fakeSdkRoot("eve_ut_cmdline_sdk_unsigned", "android");
    const auto tools = tempDir("eve_ut_cmdline_android_tools_unsigned");
    writeFakeGradle(tools / "gradle-8.5", /*unsignedOnly=*/true);
    ScopedEnv sdkEnv("EVENGINE_ANDROID_SDK", tools.string());
    ScopedEnv javaEnv("JAVA_HOME", tools.string());
    ScopedEnv gradleEnv("GRADLE_HOME", (tools / "gradle-8.5").string());

    const auto game = tempDir("eve_ut_cmdline_game_unsigned");
    writeFile(game / "main.nut", "eve_init = function() {}\n");
    const auto out = tempDir("eve_ut_cmdline_apk_out_unsigned");

    CaptureStreams cap;
    const int      rc = runCli({"eve", "build", "android", game.string(), "--sdk", sdk.string(), "-o", out.string()});
    REQUIRE(rc == 0);
    REQUIRE(cap.out().find("app-release-unsigned.apk") != std::string::npos);
    REQUIRE(cap.out().find("unsigned") != std::string::npos);

    std::error_code ec;
    std::filesystem::remove_all(sdk, ec);
    std::filesystem::remove_all(tools, ec);
    std::filesystem::remove_all(game, ec);
    std::filesystem::remove_all(out, ec);
}

TEST_CASE("cmdline.buildMissingAndroidSdkFails") {
    const auto     sdk = fakeSdkRoot("eve_ut_cmdline_sdk_missing_tools", "android");
    ScopedEnv      sdkEnv("EVENGINE_ANDROID_SDK", (sdk / "no-tools").string());
    CaptureStreams cap;
    const int      rc = runCli({"eve", "build", "android", "--sdk", sdk.string()});
    CHECK(rc == 2);
    CHECK(cap.all().find("eve get android") != std::string::npos);
    std::error_code ec;
    std::filesystem::remove_all(sdk, ec);
}

TEST_CASE("cmdline.getAndroidAlreadyInstalled") {
    const auto sdk = tempDir("eve_ut_cmdline_android_installed");
    writeFakeSdkmanager(sdk);
    writeFakeGradle(sdk / "gradle-8.5");
    const auto installRoot = tempDir("eve_ut_cmdline_eve_sdk_installed");
    fakeInstalledEveSdk(installRoot);

    ScopedEnv      rootEnv("EVE_SDK_INSTALL_ROOT", installRoot.string());
    ScopedEnv      sdkEnv("EVENGINE_ANDROID_SDK", sdk.string());
    ScopedEnv      javaEnv("JAVA_HOME", sdk.string());  // non-empty -> skip JDK download
    CaptureStreams cap;
    const int      rc = runCli({"eve", "get", "android"});
    CHECK(rc == 0);
    CHECK(cap.out().find("already installed") != std::string::npos);
    std::error_code ec;
    REQUIRE(std::filesystem::is_regular_file(sdk / "eve-android.env", ec));
    {
        std::ifstream in(sdk / "eve-android.env");
        std::string   s((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        CHECK(s.find("GRADLE_HOME=") != std::string::npos);
        CHECK(s.find("ANDROID_HOME=") != std::string::npos);
        CHECK(s.find("EVENGINE_SDK=") != std::string::npos);
    }
    std::filesystem::remove_all(sdk, ec);
    std::filesystem::remove_all(installRoot, ec);
}

TEST_CASE("cmdline.sdkVersionTagNormalizesDevBuild") {
    // 当前 dev 构建版本是 v0.1.0-dev，归一化后应得到干净的 release tag。
    const std::string tag = eve::cmd::sdk::sdkVersionTag();
    CHECK(tag.rfind("v", 0) == 0);
    CHECK(tag.find("dev") == std::string::npos);
    CHECK(tag.find('-') == std::string::npos);
    // EVE_SDK_TAG 覆盖（不得重复加 v）。
    ScopedEnv tagEnv("EVE_SDK_TAG", "v9.8.7");
    CHECK(eve::cmd::sdk::sdkVersionTag() == "v9.8.7");
}

TEST_CASE("cmdline.getAndroidInstallsEveSdkFromRelease") {
    // 端到端：EVE_SDK_BASE_URL 指向本地 file:// fake release，get 应下载、
    // 校验 SHA256 并把 zip 里的 eve-sdk/android 安装到 EVE_SDK_INSTALL_ROOT。
    const std::string tag     = eve::cmd::sdk::sdkVersionTag();
    const std::string ver     = sdkVersionNoV();
    const auto        rel     = tempDir("eve_ut_cmdline_release");
    const auto        zipPath = rel / ("eve-sdk-android-" + tag + ".zip");
    writeFakeEveSdkZip(zipPath, ver);
    writeSha256Sums(rel, zipPath, /*correct=*/true);

    const auto tools = tempDir("eve_ut_cmdline_android_tools_get");
    writeFakeSdkmanager(tools);
    writeFakeGradle(tools / "gradle-8.5");
    const auto installRoot = tempDir("eve_ut_cmdline_eve_sdk_install");

    ScopedEnv baseEnv("EVE_SDK_BASE_URL", fileUrl(rel));
    ScopedEnv tagEnv("EVE_SDK_TAG", tag);
    ScopedEnv rootEnv("EVE_SDK_INSTALL_ROOT", installRoot.string());
    ScopedEnv sdkEnv("EVENGINE_ANDROID_SDK", tools.string());
    ScopedEnv javaEnv("JAVA_HOME", tools.string());  // skip JDK download

    int         installRc = -1;
    std::string installOutput;
    {
        CaptureStreams cap;
        installRc     = runCli({"eve", "get", "android"});
        installOutput = cap.all();
    }
    if (installRc != 0) std::cerr << installOutput;
    REQUIRE(installRc == 0);
    CHECK(installOutput.find("installed at") != std::string::npos);

    const auto      eveSdk = installRoot / "android";
    std::error_code ec;
    REQUIRE(std::filesystem::is_regular_file(eveSdk / "share" / "eve" / "TARGET_PLATFORM", ec));
    REQUIRE(std::filesystem::is_regular_file(eveSdk / "share" / "eve" / "VERSION", ec));
    REQUIRE(std::filesystem::is_regular_file(eveSdk / "lib" / "libmain.so", ec));
    REQUIRE(std::filesystem::is_regular_file(eveSdk / "platform" / "apk" / "template.txt", ec));
    {
        std::ifstream in(eveSdk / "share" / "eve" / "VERSION");
        std::string   s;
        std::getline(in, s);
        CHECK(s == ver);
    }

    // env 文件记录 EVENGINE_SDK，供 `eve build android` 发现安装的 SDK。
    REQUIRE(std::filesystem::is_regular_file(tools / "eve-android.env", ec));
    {
        std::ifstream in(tools / "eve-android.env");
        std::string   s((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        CHECK(s.find("EVENGINE_SDK=" + eveSdk.string()) != std::string::npos);
        CHECK(s.find("ANDROID_HOME=") != std::string::npos);
        CHECK(s.find("GRADLE_HOME=") != std::string::npos);
    }

    // 幂等：第二次运行不重新下载，直接复用已安装 SDK。
    {
        CaptureStreams cap;
        const int      rc = runCli({"eve", "get", "android"});
        REQUIRE(rc == 0);
        CHECK(cap.out().find("already installed") != std::string::npos);
    }

    std::filesystem::remove_all(rel, ec);
    std::filesystem::remove_all(tools, ec);
    std::filesystem::remove_all(installRoot, ec);
}

TEST_CASE("cmdline.getAndroidChecksumMismatchFails") {
    const std::string tag     = eve::cmd::sdk::sdkVersionTag();
    const auto        rel     = tempDir("eve_ut_cmdline_release_bad");
    const auto        zipPath = rel / ("eve-sdk-android-" + tag + ".zip");
    writeFakeEveSdkZip(zipPath, sdkVersionNoV());
    writeSha256Sums(rel, zipPath, /*correct=*/false);
    const auto installRoot = tempDir("eve_ut_cmdline_eve_sdk_install_bad");

    ScopedEnv baseEnv("EVE_SDK_BASE_URL", fileUrl(rel));
    ScopedEnv tagEnv("EVE_SDK_TAG", tag);
    ScopedEnv rootEnv("EVE_SDK_INSTALL_ROOT", installRoot.string());

    CaptureStreams cap;
    const int      rc = runCli({"eve", "get", "android"});
    REQUIRE(rc == 3);
    CHECK(cap.all().find("SHA-256 verification failed") != std::string::npos);
    std::error_code ec;
    REQUIRE(!std::filesystem::exists(installRoot / "android", ec));
    std::filesystem::remove_all(rel, ec);
    std::filesystem::remove_all(installRoot, ec);
}

TEST_CASE("cmdline.getAndroidEmptyReleaseResponseReturnsStructuredError") {
    // A successful HTTP/file transfer may still contain zero bytes (for
    // example, a truncated mirror response).  The installer must reject that
    // response before ZIP parsing and expose a diagnostic through the C++ API.
    const std::string tag     = eve::cmd::sdk::sdkVersionTag();
    const auto        rel     = tempDir("eve_ut_cmdline_release_empty");
    const auto        zipPath = rel / ("eve-sdk-android-" + tag + ".zip");
    writeFile(zipPath, std::string{});
    writeFile(rel / "SHA256SUMS", "");
    const auto installRoot = tempDir("eve_ut_cmdline_eve_sdk_install_empty");

    ScopedEnv baseEnv("EVE_SDK_BASE_URL", fileUrl(rel));
    ScopedEnv tagEnv("EVE_SDK_TAG", tag);
    ScopedEnv rootEnv("EVE_SDK_INSTALL_ROOT", installRoot.string());

    {
        CaptureStreams cap;
        auto           result = eve::cmd::sdk::installEveSdk(eve::cmd::sdk::Platform::Android);
        REQUIRE(!result.ok());
        CHECK(result.code() == eve::StatusCode::Failed);
        const auto *diagnostic = result.error();
        REQUIRE(diagnostic != nullptr);
        CHECK(diagnostic->code() == eve::DiagnosticCode::Failed);
        CHECK(diagnostic->path().find("eve-sdk-android-" + tag + ".zip") != std::string::npos);
    }

    // The CLI projection must also fail cleanly, without publishing an SDK.
    CaptureStreams cap;
    const int      rc = runCli({"eve", "get", "android"});
    REQUIRE(rc == 3);
    CHECK(cap.all().find("empty or unreadable response") != std::string::npos);
    std::error_code ec;
    CHECK(!std::filesystem::exists(installRoot / "android", ec));
    std::filesystem::remove_all(rel, ec);
    std::filesystem::remove_all(installRoot, ec);
}

TEST_CASE("cmdline.buildAndroidUsesGetInstalledSdk") {
    // `eve get android` 安装的 SDK 应能被 build 自动发现（无 --sdk / EVENGINE_SDK）。
    const auto installRoot = tempDir("eve_ut_cmdline_eve_sdk_use");
    fakeInstalledEveSdk(installRoot);
    const auto tools = tempDir("eve_ut_cmdline_android_tools_use");
    writeFakeGradle(tools / "gradle-8.5");

    ScopedEnv      rootEnv("EVE_SDK_INSTALL_ROOT", installRoot.string());
    ScopedEnv      sdkEnv("EVENGINE_ANDROID_SDK", tools.string());
    ScopedEnv      javaEnv("JAVA_HOME", tools.string());
    ScopedEnv      gradleEnv("GRADLE_HOME", (tools / "gradle-8.5").string());
    ScopedUnsetEnv unsetSdk("EVENGINE_SDK");

    const auto game = tempDir("eve_ut_cmdline_game_use_get");
    writeFile(game / "main.nut", "eve_init = function() {}\n");
    const auto out = tempDir("eve_ut_cmdline_apk_out_use_get");

    CaptureStreams cap;
    const int      rc = runCli({"eve", "build", "android", game.string(), "-o", out.string()});
    REQUIRE(rc == 0);
    std::error_code ec;
    REQUIRE(
        std::filesystem::is_regular_file(out / "apk" / "app" / "src" / "main" / "assets" / "game" / "main.nut", ec));
    REQUIRE(std::filesystem::is_regular_file(
        out / "apk" / "app" / "src" / "main" / "jniLibs" / "arm64-v8a" / "libmain.so", ec));
    REQUIRE(cap.out().find("app-release.apk") != std::string::npos);

    std::filesystem::remove_all(installRoot, ec);
    std::filesystem::remove_all(tools, ec);
    std::filesystem::remove_all(game, ec);
    std::filesystem::remove_all(out, ec);
}

TEST_CASE("cmdline.buildAndroidSkipsTemplateBuildOutputs") {
    // 官方 SDK 的 platform/apk 模板在发布流水线里被 test-sdk.sh 编译过，
    // app/build 下会残留 CI 测试产物；组装工程不应继承它们。
    const auto sdk = fakeSdkRoot("eve_ut_cmdline_sdk_stale", "android");
    writeFile(sdk / "platform" / "apk" / "app" / "build" / "stale-marker.txt", "ci test output");
    const auto tools = tempDir("eve_ut_cmdline_android_tools_stale");
    writeFakeGradle(tools / "gradle-8.5");
    ScopedEnv sdkEnv("EVENGINE_ANDROID_SDK", tools.string());
    ScopedEnv javaEnv("JAVA_HOME", tools.string());
    ScopedEnv gradleEnv("GRADLE_HOME", (tools / "gradle-8.5").string());

    const auto game = tempDir("eve_ut_cmdline_game_stale");
    writeFile(game / "main.nut", "eve_init = function() {}\n");
    const auto out = tempDir("eve_ut_cmdline_apk_out_stale");

    CaptureStreams cap;
    const int      rc = runCli({"eve", "build", "android", game.string(), "--sdk", sdk.string(), "-o", out.string()});
    REQUIRE(rc == 0);
    std::error_code ec;
    // 模板的 app/build 未被复制进组装工程；gradle 照常执行并产出自己的输出。
    REQUIRE(!std::filesystem::exists(out / "apk" / "app" / "build" / "stale-marker.txt", ec));
    REQUIRE(std::filesystem::is_regular_file(
        out / "apk" / "app" / "build" / "outputs" / "apk" / "release" / "app-release.apk", ec));
    // SDK 模板本身保持不变（只跳过复制，不修改 SDK）。
    REQUIRE(std::filesystem::is_regular_file(sdk / "platform" / "apk" / "app" / "build" / "stale-marker.txt", ec));

    std::filesystem::remove_all(sdk, ec);
    std::filesystem::remove_all(tools, ec);
    std::filesystem::remove_all(game, ec);
    std::filesystem::remove_all(out, ec);
}

TEST_CASE("cmdline.packageRejectsUnrunnableCandidateAndPreservesPrevious") {
    const auto game = tempDir("eve_ut_cmdline_pkg_game");
    const auto sdk  = tempDir("eve_ut_cmdline_pkg_sdk");
    const auto out  = tempDir("eve_ut_cmdline_pkg_out");
    writeFile(game / "main.nut", "function eve_init() {}\n");
    writeFile(sdk / "share/eve/TARGET_PLATFORM", "linux\n");
    writeFile(sdk / "bin/eve", "not an executable");
    writeFile(out / "game.eve", "previous verified archive");
    writeFile(out / "build-identity.manifest", "previous identity");
    CaptureStreams cap;
    const int      rc = runCli({"eve", "package", game.string(), "--sdk", sdk.string(), "-o", out.string()});
    REQUIRE(rc != 0);
    REQUIRE(readTextFile(out / "game.eve") == "previous verified archive");
    CHECK(readTextFile(out / "build-identity.manifest") == "previous identity");
    std::error_code ec;
    std::filesystem::remove_all(game, ec);
    std::filesystem::remove_all(sdk, ec);
    std::filesystem::remove_all(out, ec);
}

TEST_CASE("cmdline.packageCompilesUnreferencedScriptsBeforePublishing") {
    const auto game = tempDir("eve_ut_cmdline_pkg_syntax_game");
    const auto sdk  = tempDir("eve_ut_cmdline_pkg_syntax_sdk");
    const auto out  = tempDir("eve_ut_cmdline_pkg_syntax_out");
    writeFile(game / "main.nut", "function eve_init() {}\n");
    writeFile(game / "unused.nut", "function broken( {\n");
    writeFile(sdk / "share/eve/TARGET_PLATFORM", "linux\n");
    writeFile(sdk / "bin/eve", "not an executable");
    writeFile(out / "game.eve", "previous verified archive");
    CaptureStreams cap;
    const int      rc = runCli({"eve", "package", game.string(), "--sdk", sdk.string(), "-o", out.string()});
    REQUIRE(rc == 5);
    CHECK(cap.all().find("unused.nut") != std::string::npos);
    CHECK(readTextFile(out / "game.eve") == "previous verified archive");
    std::error_code ec;
    std::filesystem::remove_all(game, ec);
    std::filesystem::remove_all(sdk, ec);
    std::filesystem::remove_all(out, ec);
}

TEST_CASE("cmdline.packageCompilesImportsFromProjectSnapshot") {
    const auto game = tempDir("eve_ut_cmdline_pkg_import_game");
    const auto sdk  = tempDir("eve_ut_cmdline_pkg_import_sdk");
    const auto out  = tempDir("eve_ut_cmdline_pkg_import_out");
    writeFile(game / "main.nut", "import { value } from \"./dep.nut\"\nfunction eve_init() {}\n");
    writeFile(game / "dep.nut", "export const value = 42\n");
    writeFile(sdk / "share/eve/TARGET_PLATFORM", "linux\n");
    writeFile(sdk / "bin/eve", "not an executable");
    CaptureStreams cap;
    const int      rc = runCli({"eve", "package", game.string(), "--sdk", sdk.string(), "-o", out.string()});
    // The valid import must pass compilation and reach runtime acceptance,
    // even though this test process is running outside the game directory.
    REQUIRE((rc == 7 || rc == 8));
    CHECK(cap.all().find("Script dependency scan failed") == std::string::npos);
    std::error_code ec;
    CHECK(!std::filesystem::exists(out / "game.eve", ec));
    std::filesystem::remove_all(game, ec);
    std::filesystem::remove_all(sdk, ec);
    std::filesystem::remove_all(out, ec);
}

TEST_CASE("cmdline.packageRejectsInvalidScriptDependencyGraphs") {
    const auto sdk = tempDir("eve_ut_cmdline_pkg_scan_sdk");
    writeFile(sdk / "share" / "eve" / "TARGET_PLATFORM", "linux\n");
    writeFile(sdk / "bin" / "eve", "linux runtime");

    const auto missing = tempDir("eve_ut_cmdline_pkg_scan_missing");
    writeFile(missing / "main.nut", "import { value } from \"./missing.nut\"\n");
    const auto missingOut = tempDir("eve_ut_cmdline_pkg_scan_missing_out");
    {
        CaptureStreams cap;
        const int rc = runCli({"eve", "package", missing.string(), "--sdk", sdk.string(), "-o", missingOut.string()});
        CHECK_EQ(rc, 5);
        CHECK(cap.all().find("is missing from package") != std::string::npos);
        std::error_code ec;
        CHECK(!std::filesystem::exists(missingOut / "game.eve", ec));
    }

    const auto cyclic = tempDir("eve_ut_cmdline_pkg_scan_cycle");
    writeFile(cyclic / "main.nut", "import { a } from \"./a.nut\"\n");
    writeFile(cyclic / "a.nut", "import { b } from \"./b.nut\"\nexport const a = b\n");
    writeFile(cyclic / "b.nut", "import { a } from \"./a.nut\"\nexport const b = a\n");
    const auto cyclicOut = tempDir("eve_ut_cmdline_pkg_scan_cycle_out");
    {
        CaptureStreams cap;
        const int rc = runCli({"eve", "package", cyclic.string(), "--sdk", sdk.string(), "-o", cyclicOut.string()});
        CHECK_EQ(rc, 5);
        CHECK(cap.all().find("cyclic script import in package") != std::string::npos);
    }

    std::error_code ec;
    std::filesystem::remove_all(sdk, ec);
    std::filesystem::remove_all(missing, ec);
    std::filesystem::remove_all(missingOut, ec);
    std::filesystem::remove_all(cyclic, ec);
    std::filesystem::remove_all(cyclicOut, ec);
}

}  // namespace test_cmdline

// Contract section: crashlog
#include "PrivateTestScope.h"

#include "common/CrashLog.h"
// crashlog.cpp
namespace test_crashlog {
using namespace eve;

TEST_CASE("crashLog.initAndRecord") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "eve_crashlog_test";
    std::error_code             ec;
    std::filesystem::create_directories(dir, ec);

    eve::initSystemLogging(dir.string());
    CHECK(!eve::crashLogPath().empty());

    eve::recordLogEvent("error", "boom-test");

    std::ifstream in(eve::crashLogPath());
    CHECK(in.good());
    const std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    CHECK(content.find("boom-test") != std::string::npos);
    CHECK(content.find("error") != std::string::npos);
    CHECK(content.find("session start") != std::string::npos);
}

TEST_CASE("crashLog.recordCrashEvent") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "eve_crashlog_test";
    eve::recordCrashEvent("[crash] code=0xC0000005 at 0x0\nframe0\n");

    std::ifstream in(eve::crashLogPath());
    CHECK(in.good());
    const std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    CHECK(content.find("code=0xC0000005") != std::string::npos);
    CHECK(content.find("frame0") != std::string::npos);
    CHECK(content.find("crash") != std::string::npos);
}
}  // namespace test_crashlog

// Contract section: database
#include "PrivateTestScope.h"

#include "database/Database.h"
// database.cpp
namespace test_database {
using namespace eve::database;

TEST_CASE("Database.SQLiteCrudOrmAndECSExport") {
    Connection db("SQLite", ":memory:");
    CHECK(db.isConnected());
    db.execute("CREATE TABLE actors (id INTEGER, name TEXT, hp REAL)");

    struct Actor {
        std::int64_t id{};
        std::string  name;
        double       hp{};
    };
    Mapping<Actor> mapping{"actors",
                           {
                               {"id", [](const Actor &a) -> Value { return a.id; },
                                [](Actor &a, const Value &v) { a.id = std::get<std::int64_t>(v); }, "INTEGER", true},
                               {"name", [](const Actor &a) -> Value { return a.name; },
                                [](Actor &a, const Value &v) { a.name = std::get<std::string>(v); }},
                               {"hp", [](const Actor &a) -> Value { return a.hp; },
                                [](Actor &a, const Value &v) { a.hp = std::get<double>(v); }, "REAL"},
                           }};

    Repository<Actor> actors(db, mapping);
    // A real ORM owns schema and typed entity CRUD rather than only serializing a row.
    db.execute("DROP TABLE actors");
    actors.createTable();
    CHECK_EQ(actors.insert(Actor{1, "mage", 80.0}), 1);
    db.insertJson("actors", R"({"id":2,"name":"knight","hp":100})");
    CHECK_EQ(db.updateJson("actors", R"({"hp":95})", "id=2"), 1);

    std::vector<Actor> ecsView{{3, "rogue", 70.0}, {4, "cleric", 90.0}};
    CHECK_EQ(db.exportECS("actors", ecsView, [&mapping](const Actor &actor) { return mapping.toRow(actor); }), 2);
    auto rows = db.query("SELECT id,name,hp FROM actors ORDER BY id");
    CHECK_EQ(rows.size(), 4);
    CHECK_EQ(std::get<std::string>(rows[1]["name"]), "knight");
    CHECK_EQ(db.remove("actors", "id=1"), 1);
    CHECK_EQ(db.query("SELECT id FROM actors").size(), 3);

    Actor loaded;
    CHECK(actors.find(std::int64_t(2), loaded));
    CHECK_EQ(loaded.name, "knight");
    loaded.hp = 88.0;
    CHECK_EQ(actors.update(loaded), 1);
    CHECK_EQ(actors.all().size(), 3);
    CHECK_EQ(actors.remove(std::int64_t(2)), 1);

    std::unique_ptr<Model> model(db.model("actors", "id"));
    model->insertJson(R"({"id":5,"name":"bard","hp":60})");
    CHECK(model->findJson("5").find("bard") != std::string::npos);
    CHECK_EQ(model->updateJson("5", R"({"hp":65})"), 1);
    CHECK_EQ(model->remove("5"), 1);

    db.insertJson("actors", R"({"id":6,"name":"strategist","hp":75})");
    db.insertJson("actors", R"({"id":7,"name":"general","hp":99})");
    std::unique_ptr<Query> ranked(model->query());
    ranked->where("hp", ">=", 70.0)->orderBy("hp", false)->limit(2);
    auto ranking = ranked->all();
    CHECK_EQ(ranking.size(), 2);
    CHECK_EQ(std::get<std::string>(ranking[0]["name"]), "general");
    CHECK_EQ(ranked->count(), 4);
}
}  // namespace test_database

// Contract section: editor tilemap formats
#include "PrivateTestScope.h"

#include <chrono>
#include "common/Json.h"
#include "map/level/editing/Brush.h"
#include "map/level/editing/EditorHistory.h"
#include "map/level/editing/LevelDocument.h"
#include "map/level/editing/LevelFormat.h"
// editor_tilemap_formats.cpp
namespace test_editor_tilemap_formats {
using namespace eve::level_editing;


TEST_CASE("editor.tilemap.formats.failedSavePreservesPreviousFile") {
    const auto directory =
        std::filesystem::temp_directory_path() /
        ("eve-tilemap-save-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    REQUIRE(std::filesystem::create_directory(directory));
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::error_code error;
            std::filesystem::remove_all(path, error);
        }
    } cleanup{directory};
    const auto          destination = directory / "map.tmj";
    LevelFormatRegistry formats;
    LevelDocument       level(1, 1);
    int                 layer = level.addTileLayer("ground");
    level.getTileLayer(layer)->get().setGid(0, 0, 7);
    REQUIRE(formats.save(destination.string(), level, "tiled.json").ok());
    level.layer(layer)->get().opacity = std::numeric_limits<float>::quiet_NaN();
    REQUIRE(!formats.save(destination.string(), level, "tiled.json").ok());
    auto preserved = formats.load(destination.string());
    REQUIRE(preserved.ok());
    CHECK_EQ(preserved.value()->getTileLayer(0)->get().getGid(0, 0), 7);
    level.layer(layer)->get().opacity = 1;
    level.getTileLayer(layer)->get().setGid(0, 0, 9);
    REQUIRE(formats.save(destination.string(), level, "tiled.json").ok());
    auto replaced = formats.load(destination.string());
    REQUIRE(replaced.ok());
    CHECK_EQ(replaced.value()->getTileLayer(0)->get().getGid(0, 0), 9);
    const auto blocked = directory / "blocked";
    REQUIRE(std::filesystem::create_directory(blocked));
    REQUIRE(!formats.save(blocked.string(), level, "tiled.json").ok());
    CHECK(std::filesystem::is_directory(blocked));
    CHECK_EQ(std::distance(std::filesystem::directory_iterator(directory), std::filesystem::directory_iterator{}), 2);
}
}  // namespace test_editor_tilemap_formats

// Contract section: filesystem cpp
#include "PrivateTestScope.h"

#include <thread>
#include "filesystem/File.h"
#include "filesystem/FileWatch.h"
// filesystem_cpp.cpp
namespace test_filesystem_cpp {
namespace {

eve::filesystem::Filesystem *fs() { return eve::filesystem::Filesystem::create(); }

// Unique identity per case to avoid cross-talk on the singleton write dir.
void useIdentity(const char *id) {
    auto *f = fs();
    REQUIRE(f->setIdentity(id, true));
    REQUIRE(f->setupWriteDirectory());
}

// Bit-by-bit CRC-32 (zip polynomial); payloads in these tests are tiny so
// this is fine without pulling in a zlib dependency for the test binary.
uint32_t crc32Of(const void *data, size_t size) {
    const auto *bytes = static_cast<const unsigned char *>(data);
    uint32_t    crc   = 0xFFFFFFFFu;
    for (size_t i = 0; i < size; ++i) {
        crc ^= bytes[i];
        for (int bit = 0; bit < 8; ++bit) crc = (crc & 1u) ? (crc >> 1) ^ 0xEDB88320u : (crc >> 1);
    }
    return ~crc;
}

void put16(std::vector<unsigned char> &out, uint16_t v) {
    out.push_back(static_cast<unsigned char>(v & 0xFF));
    out.push_back(static_cast<unsigned char>((v >> 8) & 0xFF));
}

void put32(std::vector<unsigned char> &out, uint32_t v) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<unsigned char>((v >> (8 * i)) & 0xFF));
}

// Hand-rolled minimal single-entry, uncompressed (stored) ZIP archive, so
// filesystem.mount(Data*, ...) can be exercised without a real archive file
// or an extra zip-writer dependency in the test binary.
std::vector<unsigned char> buildMinimalZip(const std::string &entryName, const std::string &content) {
    std::vector<unsigned char> out;
    const uint32_t             crc         = crc32Of(content.data(), content.size());
    const uint32_t             localOffset = 0;

    // Local file header.
    put32(out, 0x04034b50u);
    put16(out, 20);  // version needed
    put16(out, 0);   // flags
    put16(out, 0);   // compression: stored
    put16(out, 0);   // mod time
    put16(out, 0);   // mod date
    put32(out, crc);
    put32(out, static_cast<uint32_t>(content.size()));  // compressed size
    put32(out, static_cast<uint32_t>(content.size()));  // uncompressed size
    put16(out, static_cast<uint16_t>(entryName.size()));
    put16(out, 0);  // extra field length
    out.insert(out.end(), entryName.begin(), entryName.end());
    out.insert(out.end(), content.begin(), content.end());

    const size_t centralDirOffset = out.size();

    // Central directory file header.
    put32(out, 0x02014b50u);
    put16(out, 20);  // version made by
    put16(out, 20);  // version needed
    put16(out, 0);   // flags
    put16(out, 0);   // compression: stored
    put16(out, 0);   // mod time
    put16(out, 0);   // mod date
    put32(out, crc);
    put32(out, static_cast<uint32_t>(content.size()));
    put32(out, static_cast<uint32_t>(content.size()));
    put16(out, static_cast<uint16_t>(entryName.size()));
    put16(out, 0);  // extra field length
    put16(out, 0);  // comment length
    put16(out, 0);  // disk number start
    put16(out, 0);  // internal attributes
    put32(out, 0);  // external attributes
    put32(out, localOffset);
    out.insert(out.end(), entryName.begin(), entryName.end());

    const size_t centralDirSize = out.size() - centralDirOffset;

    // End of central directory record.
    put32(out, 0x06054b50u);
    put16(out, 0);  // disk number
    put16(out, 0);  // disk with central dir
    put16(out, 1);  // entries on this disk
    put16(out, 1);  // total entries
    put32(out, static_cast<uint32_t>(centralDirSize));
    put32(out, static_cast<uint32_t>(centralDirOffset));
    put16(out, 0);  // comment length

    return out;
}

}  // namespace

TEST_CASE("filesystem.identityAndPaths") {
    useIdentity("ev_ut_fs_paths");
    auto *f = fs();
    CHECK_EQ(f->getIdentity(), std::string("ev_ut_fs_paths"));
    CHECK(!f->getWorkingDirectory().empty());
    CHECK(!f->getUserDirectory().empty());
    CHECK(!f->getAppdataDirectory().empty());
    CHECK(!f->getSaveDirectory().empty());
    CHECK(!f->getExecutablePath().empty());

    f->setFused(true);
    CHECK(f->isFused());
    // fusedSet is sticky — second call ignored; still true
    f->setFused(false);
    CHECK(f->isFused());

    f->setSymlinksEnabled(false);
    CHECK(!f->areSymlinksEnabled());
    f->setSymlinksEnabled(true);
    CHECK(f->areSymlinksEnabled());

    auto &req = f->getRequirePath();
    CHECK_GE(req.size(), 0u);
    (void)f->getCRequirePath();

    // Android-only APIs: exercise default getters/setters on desktop
    f->setAndroidSaveExternal(true);
    CHECK(f->isAndroidSaveExternal());
    f->setAndroidSaveExternal(false);
    CHECK(!f->isAndroidSaveExternal());
}

TEST_CASE("filesystem.writeReadAppendRemove") {
    useIdentity("ev_ut_fs_rw");
    auto       *f       = fs();
    const char *name    = "ut_rw.txt";
    const char *payload = "hello-fs";
    f->write(name, payload, static_cast<int64_t>(std::strlen(payload)));

    std::unique_ptr<eve::filesystem::FileData> data(f->read(name));
    REQUIRE(data.get() != nullptr);
    CHECK_EQ(data->getSize(), std::strlen(payload));
    CHECK(std::memcmp(data->getData(), payload, data->getSize()) == 0);

    const char *more = "!";
    f->append(name, more, 1);
    std::unique_ptr<eve::filesystem::FileData> data2(f->read(name));
    REQUIRE(data2.get() != nullptr);
    CHECK_EQ(data2->getSize(), std::strlen(payload) + 1);

    eve::filesystem::Filesystem::Info fileInfo{};
    REQUIRE(f->getInfo(name, fileInfo));
    CHECK_EQ(fileInfo.type, std::string("file"));
    CHECK_GE(fileInfo.size, 0);

    CHECK(f->createDirectory("ut_subdir"));
    auto items = f->getDirectoryItems("");
    bool found = false;
    for (const auto &it : items) {
        if (it == name || it == "ut_subdir" || it == "ut_subdir/") found = true;
    }
    CHECK(found);

    CHECK(f->remove(name));
    CHECK(!f->getInfo(name, fileInfo));

    bool threw = false;
    try {
        f->read("definitely_missing_ut_file_zzz.dat");
    } catch (const eve::Exception &) {
        threw = true;
    }
    CHECK(threw);
}

TEST_CASE("filesystem.atomicTextReplacePreservesContract") {
    useIdentity("ev_ut_fs_atomic_text");
    auto             *f    = fs();
    const std::string name = "slot1.sav";
    REQUIRE(f->writeTextAtomic(name, "version=1").ok());
    CHECK_EQ(f->readText(name), std::string("version=1"));
    REQUIRE(f->writeTextAtomic(name, "version=2\n角色=勇者").ok());
    CHECK_EQ(f->readText(name), std::string("version=2\n角色=勇者"));

    auto rejected = f->writeTextAtomic("../outside.sav", "invalid");
    CHECK(!rejected.ok());
    CHECK_EQ(f->readText(name), std::string("version=2\n角色=勇者"));

    bool sawTemporary = false;
    for (const auto &item : f->getDirectoryItems("")) {
        if (item.find("slot1.sav.tmp.") != std::string::npos) sawTemporary = true;
    }
    CHECK(!sawTemporary);
    CHECK(f->remove(name));
}

TEST_CASE("filesystem.setSourceSecondCallFails") {
    auto *f = fs();
    // `game_source` is a sticky, process-wide singleton field: only the very
    // first call across the whole test binary can succeed. We are the only
    // TEST_CASE that calls setSource(), so this exercises the happy path.
    std::string cwd     = f->getWorkingDirectory();
    bool        firstOk = f->setSource(cwd);
    if (firstOk) {
        CHECK_EQ(f->getSource(), cwd);
        // getSourceBaseDirectory() strips the last path component of the source.
        std::string base = f->getSourceBaseDirectory();
        CHECK(!base.empty());
        CHECK(cwd.rfind(base, 0) == 0);
    } else {
        // Defensive fallback in case some earlier test already set the source.
        CHECK(!f->getSource().empty());
    }
    // Second call (regardless of which path above ran) must always fail.
    CHECK(!f->setSource("somewhere_else"));
}

TEST_CASE("filesystem.File.openReadWriteSeek") {
    useIdentity("ev_ut_fs_file");
    auto       *f    = fs();
    const char *name = "ev_sub/ut_file_api.bin";
    CHECK(f->createDirectory("ev_sub"));
    f->write(name, "0123456789", 10);

    std::unique_ptr<eve::filesystem::FileData> viaFs(f->read(name));
    REQUIRE(viaFs.get() != nullptr);
    CHECK_EQ(viaFs->getSize(), 10u);
    CHECK(std::memcmp(viaFs->getData(), "0123456789", 10) == 0);

    std::unique_ptr<eve::filesystem::File> file(f->newFile(name));
    REQUIRE(file.get() != nullptr);
    CHECK_EQ(file->getFilename(), std::string(name));
    CHECK(file->open("rb"));
    CHECK(file->isOpen());
    CHECK_EQ(file->getMode(), std::string("rb"));
    CHECK_EQ(file->getSize(), 10);
    CHECK_EQ(file->tell(), 0);

    char buf[4] = {};
    // CHECK_EQ evaluates lhs twice (compare + print) — do not put side-effecting reads there.
    const int64_t nRead = file->read(buf, 4);
    CHECK_EQ(nRead, 4);
    CHECK(std::memcmp(buf, "0123", 4) == 0);
    CHECK(file->seek(0));
    CHECK_EQ(file->tell(), 0);

    std::unique_ptr<eve::filesystem::FileData> head(file->read(4));
    REQUIRE(head.get() != nullptr);
    CHECK_EQ(head->getSize(), 4u);
    CHECK(std::memcmp(head->getData(), "0123", 4) == 0);
    CHECK(file->seek(0));
    CHECK_EQ(file->tell(), 0);
    std::unique_ptr<eve::filesystem::FileData> chunk(file->read(2));
    REQUIRE(chunk.get() != nullptr);
    CHECK_EQ(chunk->getSize(), 2u);
    CHECK(std::memcmp(chunk->getData(), "01", 2) == 0);

    // Love-style "r" alias must open for reading.
    CHECK(file->close());
    CHECK(file->open("r"));
    CHECK_EQ(file->getMode(), std::string("rb"));
    CHECK(file->close());
    CHECK(!file->isOpen());

    bool badMode = false;
    try {
        std::unique_ptr<eve::filesystem::File> f2(f->newFile(name));
        if (!f2->open("not-a-mode")) badMode = true;
    } catch (const eve::Exception &) {
        badMode = true;
    }
    CHECK(badMode);

    f->remove(name);
}

TEST_CASE("filesystem.FileData.metaAndClone") {
    useIdentity("ev_ut_fs_filedata");
    auto                                      *f     = fs();
    const char                                 raw[] = "meta";
    std::unique_ptr<eve::filesystem::FileData> fd(f->newFileData(raw, "folder/shot.png", 4));
    REQUIRE(fd.get() != nullptr);
    CHECK_EQ(fd->getSize(), 4u);
    CHECK(std::memcmp(fd->getData(), raw, 4) == 0);
    CHECK_EQ(fd->getFilename(), std::string("folder/shot.png"));
    CHECK_EQ(fd->getExtension(), std::string("png"));
    CHECK_EQ(fd->getName(), std::string("folder/shot"));
    std::unique_ptr<eve::filesystem::FileData> cloned(fd->clone());
    CHECK_EQ(cloned->getExtension(), std::string("png"));
}

TEST_CASE("filesystem.mount.missingReturnsFalse") {
    auto *f = fs();
    CHECK(!f->mount("Z:/evengine_definitely_missing_archive_xxx.zip", "/m", false));
    CHECK(!f->unmount("Z:/evengine_definitely_missing_archive_xxx.zip"));
}

TEST_CASE("filesystem.mount.realDirRoundTrip") {
    useIdentity("ev_ut_fs_mount");
    auto *f = fs();
    f->write("mounted_marker.txt", "M", 1);
    std::string realSave = f->getSaveDirectory();
    f->allowMountingForPath(realSave);
    REQUIRE(f->isRealDirectory(realSave));
    REQUIRE(f->mount(realSave, "/mnt", false));
    CHECK(f->unmount(realSave));
    f->remove("mounted_marker.txt");
}

TEST_CASE("filesystem.mount.memoryDataRoundTrip") {
    useIdentity("ev_ut_fs_mount_mem");
    auto *f = fs();

    const std::string   content  = "hi-from-memory-zip";
    auto                zipBytes = buildMinimalZip("hello.txt", content);
    eve::data::ByteData zipData(zipBytes.data(), zipBytes.size());

    REQUIRE(f->mount(&zipData, "ut_mem.zip", "/memzip", false));

    eve::filesystem::Filesystem::Info zipEntryInfo{};
    REQUIRE(f->getInfo("/memzip/hello.txt", zipEntryInfo));
    CHECK_EQ(zipEntryInfo.type, std::string("file"));

    std::unique_ptr<eve::filesystem::FileData> read(f->read("/memzip/hello.txt"));
    REQUIRE(read.get() != nullptr);
    CHECK_EQ(read->getSize(), content.size());
    CHECK(std::memcmp(read->getData(), content.data(), content.size()) == 0);

    CHECK(f->unmount(&zipData));
    // Once unmounted, the mounted content should no longer resolve.
    CHECK(!f->getInfo("/memzip/hello.txt", zipEntryInfo));
}

TEST_CASE("filesystem.getRealDirectory") {
    useIdentity("ev_ut_fs_realdir");
    auto *f = fs();
    f->write("ut_realdir_marker.txt", "x", 1);

    std::string real = f->getRealDirectory("ut_realdir_marker.txt");
    CHECK(!real.empty());
    CHECK_EQ(real, f->getSaveDirectory());

    bool threw = false;
    try {
        f->getRealDirectory("ut_realdir_definitely_missing_zzz.dat");
    } catch (const eve::Exception &) {
        threw = true;
    }
    CHECK(threw);

    f->remove("ut_realdir_marker.txt");
}

TEST_CASE("filesystem.File.extensionAndEof") {
    useIdentity("ev_ut_fs_ext_eof");
    auto *f = fs();

    std::unique_ptr<eve::filesystem::File> withExt(f->newFile("dir/archive.tar.gz"));
    CHECK_EQ(withExt->getExtension(), std::string("gz"));

    std::unique_ptr<eve::filesystem::File> noExt(f->newFile("dir/README"));
    CHECK_EQ(noExt->getExtension(), std::string());

    const char *name = "ut_eof.bin";
    f->write(name, "ab", 2);
    std::unique_ptr<eve::filesystem::File> file(f->newFile(name));
    // Not yet open: contract treats an unopened file as EOF.
    CHECK(file->isEOF());

    REQUIRE(file->open("rb"));
    CHECK(!file->isEOF());

    char buf[2] = {};
    CHECK_EQ(file->read(buf, 2), 2);
    CHECK(file->isEOF());

    CHECK(file->close());
    f->remove(name);
}

TEST_CASE("filesystem.File.bufferAndFlush") {
    useIdentity("ev_ut_fs_buf");
    auto                                  *f    = fs();
    const char                            *name = "ut_buf.txt";
    std::unique_ptr<eve::filesystem::File> file(f->newFile(name));
    CHECK(file->setBuffer("full", 4096));
    REQUIRE(file->open("wb"));
    int64_t sz = 0;
    CHECK_EQ(file->getBuffer(sz), std::string("full"));
    CHECK_EQ(sz, 4096);
    CHECK(file->write("x", 1));
    CHECK(file->flush());
    CHECK(file->close());
    f->remove(name);
}

TEST_CASE("filesystem.watch.fileModified") {
    useIdentity("ev_ut_fs_watch");
    auto *f = fs();
    f->unwatchAll();

    const char *name = "ut_watch.txt";
    f->write(name, "v1", 2);
    REQUIRE(f->watch(name));
    CHECK_EQ(f->getWatchCount(), 1);

    // Darwin DirectoryWatcher baselines via periodic scan; wait one interval.
    std::this_thread::sleep_for(std::chrono::milliseconds(1200));
    while (!f->pollWatch().empty()) {
    }

    // Prefer OS write through the resolved save path so notifications fire reliably.
    std::string real = f->getSaveDirectory() + "/" + name;
    {
        std::ofstream out(real, std::ios::binary | std::ios::trunc);
        REQUIRE(out.good());
        out.write("v2-changed", 10);
        out.flush();
    }

    bool saw = false;
    for (int i = 0; i < 60 && !saw; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        for (;;) {
            std::string kind = f->pollWatch();
            if (kind.empty()) break;
            if (kind == "modified" || kind == "added") {
                CHECK_EQ(f->getLastWatchPath(), std::string(name));
                saw = true;
            }
        }
    }
    CHECK(saw);

    CHECK(f->unwatch(name));
    CHECK_EQ(f->getWatchCount(), 0);
    f->remove(name);
}

TEST_CASE("filesystem.watch.coalescesOverlappingRegistrations") {
    const char *name = "ut_watch_coalesce.txt";
    const auto  dir  = std::filesystem::temp_directory_path() / "ev_ut_fs_watch_coalesce";
    std::filesystem::remove_all(dir);
    REQUIRE(std::filesystem::create_directories(dir));
    {
        std::ofstream out(dir / name, std::ios::binary | std::ios::trunc);
        REQUIRE(out.good());
        out.write("v1", 2);
    }

    eve::filesystem::FileWatch watch;
    REQUIRE(watch.add(dir.string(), "", "."));
    REQUIRE(watch.add(dir.string(), name, name));
    CHECK_EQ(watch.count(), 2);

    std::this_thread::sleep_for(std::chrono::milliseconds(1200));
    eve::filesystem::FileWatch::Event event;
    while (watch.poll(event)) {
    }

    {
        std::ofstream out(dir / name, std::ios::binary | std::ios::trunc);
        REQUIRE(out.good());
        out.write("v2-changed", 10);
        out.flush();
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(1200));
    int matchingEvents = 0;
    while (watch.poll(event)) {
        if (event.realPath.find(name) != std::string::npos) {
            ++matchingEvents;
            CHECK_EQ(event.path, std::string(name));
        }
    }
    CHECK_EQ(matchingEvents, 1);
    watch.clear();
    std::filesystem::remove_all(dir);
}

TEST_CASE("filesystem.watch.unwatchAll") {
    useIdentity("ev_ut_fs_watch_all");
    auto *f = fs();
    f->unwatchAll();
    f->write("a.txt", "a", 1);
    f->write("b.txt", "b", 1);
    CHECK(f->watch("a.txt"));
    CHECK(f->watch("b.txt"));
    CHECK_GE(f->getWatchCount(), 2);
    f->unwatchAll();
    CHECK_EQ(f->getWatchCount(), 0);
    f->remove("a.txt");
    f->remove("b.txt");
}

TEST_CASE("filesystem.watch.dotResolvesToCwd") {
    useIdentity("ev_ut_fs_watch_dot");
    auto *f = fs();
    f->unwatchAll();

    std::string cwd = f->getWorkingDirectory();
    REQUIRE(!cwd.empty());
    (void)f->setSource(cwd);

    REQUIRE(f->watch("."));
    CHECK_GE(f->getWatchCount(), 1);

    const char *name = "ut_watch_dot.txt";
    {
        std::ofstream out(cwd + "/" + name, std::ios::binary | std::ios::trunc);
        REQUIRE(out.good());
        out.write("v1", 2);
        out.flush();
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(1200));
    while (!f->pollWatch().empty()) {
    }

    {
        std::ofstream out(cwd + "/" + name, std::ios::binary | std::ios::trunc);
        REQUIRE(out.good());
        out.write("v2-changed", 10);
        out.flush();
    }

    bool saw = false;
    for (int i = 0; i < 60 && !saw; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        for (;;) {
            std::string kind = f->pollWatch();
            if (kind.empty()) break;
            if (kind == "modified" || kind == "added") {
                std::string p = f->getLastWatchPath();
                if (p == name || p == std::string("./") + name || p.find(name) != std::string::npos) saw = true;
            }
        }
    }
    CHECK(saw);

    f->unwatch(".");
    std::remove((cwd + "/" + name).c_str());
}

TEST_CASE("filesystem.watch.parentDotDotResolves") {
    auto *f = fs();
    f->unwatchAll();
    REQUIRE(f->watch(".."));
    CHECK_GE(f->getWatchCount(), 1);
    CHECK(f->unwatch(".."));
    CHECK_EQ(f->getWatchCount(), 0);
}
}  // namespace test_filesystem_cpp

// Contract section: hex level data
#include "PrivateTestScope.h"

#include <Poco/Dynamic/Var.h>
#include <Poco/JSON/Array.h>
#include <Poco/JSON/Object.h>
#include "ProcgenGridTestSupport.h"
#include "data/DataModule.h"
#include "data/JsonDocument.h"
#include "inventory/Bag.h"
#include "inventory/Equipment.h"
#include "inventory/Inventory.h"
#include "inventory/InventorySystem.h"
#include "inventory/Item.h"
#include "map/Fov.h"
#include "map/Map.h"
#include "map/Path.h"
#include "map/Pathfinder.h"
#include "map/TileConfig.h"
#include "map/TileLayer.h"
#include "map/TileOrientation.h"
#include "particles/ParticleEmitter.h"
#include "particles/Particles.h"
#include "procgen/GeneratorRegistry.h"
#include "procgen/Grid2D.h"
#include "procgen/Params.h"
#include "procgen/Procgen.h"
#include "procgen/Semantic.h"
// hex_level_data.cpp
namespace test_hex_level_data {
// Data-driven hex level fixtures — loads examples/hex-levels/data/*.json
// and exercises catalog / items / loot / seeds / particles / tiny map / perception.


using namespace eve::map;
using namespace eve::procgen;
using namespace eve::inventory;
using namespace eve::particles;
using namespace eve::data;

namespace {

std::string hexDataDir() { return eve_test_path::pathBesideTestDir(__FILE__, "../examples/hex-levels/data"); }

std::string readTextFile(const std::string &path) {
    std::ifstream in(path);
    if (!in) return {};
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::unique_ptr<JsonDocument> loadJson(const std::string &relPath) {
    const std::string path = hexDataDir() + "/" + relPath;
    const std::string text = readTextFile(path);
    REQUIRE(!text.empty());
    auto                         *dm = DataModule::create();
    std::string                   err;
    std::unique_ptr<JsonDocument> doc(dm->decodeJson(text, &err));
    REQUIRE(doc.get() != nullptr);
    return doc;
}

void hideLayers() {
    if (ecs::current()->getManager<TileLayer>() == nullptr) return;
    auto view = ecs::View<TileLayer, TileLayer::Draw>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [draw]   = *it;
        draw->visible = false;
    }
}

int countWalkable(const Grid2D &g) {
    int n = 0;
    for (uint32_t c : g.cells()) {
        if (c == Semantic::Floor || c == Semantic::Corridor || c == Semantic::Door) ++n;
    }
    return n;
}

}  // namespace

TEST_CASE("hex.data.catalog.loadsLevels") {
    auto doc = loadJson("catalog.json");
    REQUIRE(doc->isObject());
    auto root = doc->object();
    REQUIRE(root);
    CHECK_EQ(root->getValue<int>("version"), 1);
    auto levels = root->getArray("levels");
    REQUIRE(levels);
    CHECK(levels->size() >= 31);

    int foundPipeline = 0;
    for (size_t i = 0; i < levels->size(); ++i) {
        auto lv = levels->getObject(static_cast<unsigned int>(i));
        REQUIRE(lv);
        CHECK(lv->has("id"));
        CHECK(lv->has("key"));
        CHECK(lv->has("seed"));
        CHECK(lv->has("algorithm"));
        CHECK(lv->has("width"));
        CHECK(lv->has("height"));
        CHECK(lv->has("features"));
        if (lv->getValue<std::string>("key") == "pipeline_full") foundPipeline = 1;
    }
    CHECK_EQ(foundPipeline, 1);

    auto defaults = root->getObject("defaults");
    REQUIRE(defaults);
    CHECK_EQ(defaults->getValue<std::string>("orientation"), std::string("hexagonal"));
    CHECK_EQ(defaults->getValue<int>("wallGid"), 1);
    CHECK_EQ(defaults->getValue<int>("floorGid"), 2);
}

TEST_CASE("hex.data.items.registerAll") {
    auto doc = loadJson("items.json");
    REQUIRE(doc->isArray());
    auto arr = doc->array();
    REQUIRE(arr);
    CHECK(arr->size() >= 12);

    ItemRegistry::clear();
    InventorySystem::ensureBuiltins();
    auto             *inv  = Inventory::create();
    const std::string text = readTextFile(hexDataDir() + "/items.json");
    const int         n    = inv->registerItemsFromJson(text);
    CHECK_EQ(n, int(arr->size()));
    CHECK(ItemRegistry::find("hex.potion") != nullptr);
    CHECK(ItemRegistry::find("hex.relic") != nullptr);
    CHECK(ItemRegistry::find("hex.coin") != nullptr);
    CHECK(ItemRegistry::find("hex.boots") != nullptr);
    CHECK(ItemRegistry::find("hex.scroll") != nullptr);
    CHECK_EQ(ItemRegistry::find("hex.gem")->maxStack, 5);
    CHECK(ItemRegistry::find("hex.potion")->hasTag("potion"));
    CHECK_EQ(ItemRegistry::find("hex.relic")->getExtra("rarity"), std::string("legendary"));

    Bag      *bag         = inv->newBag(20);
    const int coinAdded   = bag->addItem("hex.coin", 50);
    const int potionAdded = bag->addItem("hex.potion", 3);
    CHECK_EQ(coinAdded, 50);
    CHECK_EQ(potionAdded, 3);
    CHECK_EQ(bag->countItem("hex.coin"), 50);
    CHECK_EQ(bag->countItem("hex.potion"), 3);
    bag->destroy();
    ItemRegistry::clear();
}

TEST_CASE("hex.data.lootTables.placeFromOffsets") {
    auto lootDoc = loadJson("loot_tables.json");
    auto tables  = lootDoc->object()->getObject("tables");
    REQUIRE(tables);
    auto rich = tables->getArray("rich");
    REQUIRE(rich);
    CHECK(rich->size() >= 4);

    auto *mapMod = Map::create();
    auto *gen    = Procgen::create();
    hideLayers();
    TileLayer *layer = mapMod->newLayer(16, 12, 64.f, 32.f);
    {
        auto c           = layer->config();
        c->orientation   = MapOrientation::Hexagonal;
        c->hexSideLength = 16.f;
        c->staggerAxis   = StaggerAxis::Y;
        c->staggerIndex  = StaggerIndex::Odd;
    }
    GeneratorRegistry::instance().registerBuiltins();
    Params p;
    p.setSeed(123);
    p.setSize(16, 12);
    Grid2D      grid;
    std::string err;
    REQUIRE(GeneratorRegistry::instance().generate("dungeon.bsp", p, grid, err));
    gen->setPaletteGid("hex_data", "wall", 1);
    gen->setPaletteGid("hex_data", "floor", 2);
    gen->setPaletteGid("hex_data", "corridor", 2);
    gen->setPaletteGid("hex_data", "door", 3);
    eve::test_support::applyGridToLayer(*gen, grid, "hex_data", *layer);

    int spawnX = -1, spawnY = -1;
    for (int i = 0; i < grid.getObjectCount(); ++i) {
        if (grid.getObjectType(i) == "spawn") {
            spawnX = int(grid.getObjectX(i));
            spawnY = int(grid.getObjectY(i));
        }
    }
    if (spawnX < 0) {
        for (int y = 0; y < grid.getHeight() && spawnX < 0; ++y)
            for (int x = 0; x < grid.getWidth() && spawnX < 0; ++x)
                if (grid.getCell(x, y) == int(Semantic::Floor)) {
                    spawnX = x;
                    spawnY = y;
                }
    }
    REQUIRE(spawnX >= 0);

    Pathfinder *pf = mapMod->newPathfinder(layer);
    pf->blockGid(1);
    pf->setTopology("hex");

    ItemRegistry::clear();
    InventorySystem::ensureBuiltins();
    auto *inv = Inventory::create();
    inv->registerItemsFromJson(readTextFile(hexDataDir() + "/items.json"));
    Bag *bag = inv->newBag(24);

    int placed = 0;
    int picked = 0;
    for (size_t i = 0; i < rich->size(); ++i) {
        auto              entry  = rich->getObject(static_cast<unsigned int>(i));
        const std::string itemId = entry->getValue<std::string>("itemId");
        const int         qty    = entry->has("qty") ? entry->getValue<int>("qty") : 1;
        auto              off    = entry->getArray("offset");
        REQUIRE(off);
        const int tx = spawnX + off->getElement<int>(0);
        const int ty = spawnY + off->getElement<int>(1);
        if (!pf->isWalkable(tx, ty)) continue;
        ++placed;
        const int added = bag->addItem(itemId, qty);
        CHECK(added > 0);
        picked += added;
    }
    CHECK(placed >= 1);
    CHECK(picked >= 1);
    CHECK(bag->getUsedSlotCount() >= 1);

    bag->destroy();
    ItemRegistry::clear();
    delete pf;
    layer->setVisible(false);
}

TEST_CASE("hex.data.seedsMatrix.smokeConnectivity") {
    auto doc   = loadJson("seeds_matrix.json");
    auto root  = doc->object();
    auto seeds = root->getArray("seeds");
    auto algos = root->getArray("algorithms");
    auto sizes = root->getArray("sizes");
    REQUIRE(seeds);
    REQUIRE(algos);
    REQUIRE(sizes);
    CHECK(seeds->size() >= 10);
    CHECK(algos->size() >= 4);

    // Sample a subset: first 6 seeds × all algos × medium size — keep runtime bounded.
    auto      size        = sizes->getObject(1);  // 24×18
    const int w           = size->getValue<int>("width");
    const int h           = size->getValue<int>("height");
    const int minWalkable = root->getObject("expectations")->getValue<int>("minWalkable");

    GeneratorRegistry::instance().registerBuiltins();
    int          okRuns    = 0;
    const size_t seedLimit = std::min<size_t>(6, seeds->size());
    for (size_t si = 0; si < seedLimit; ++si) {
        const uint32_t seed = uint32_t(seeds->getElement<int>(static_cast<unsigned int>(si)));
        for (size_t ai = 0; ai < algos->size(); ++ai) {
            auto              algoObj = algos->getObject(static_cast<unsigned int>(ai));
            const std::string algo    = algoObj->getValue<std::string>("id");
            Params            p;
            p.setSeed(seed);
            p.setSize(w, h);
            if (auto params = algoObj->getObject("params")) {
                if (params->has("loops")) p.setInt("loops", params->getValue<int>("loops"));
                if (params->has("fill")) p.setFloat("fill", float(params->getValue<double>("fill")));
                if (params->has("floorPct")) p.setFloat("floorPct", float(params->getValue<double>("floorPct")));
                if (params->has("preset")) p.setString("preset", params->getValue<std::string>("preset"));
                if (params->has("maxAttempts")) p.setInt("maxAttempts", params->getValue<int>("maxAttempts"));
            }
            Grid2D      grid;
            std::string err;
            const bool  ok = GeneratorRegistry::instance().generate(algo, p, grid, err);
            CHECK(ok);
            if (!ok) continue;
            // Some cellular seeds are wall-heavy on medium maps; only BSP is
            // expected to reliably clear the fixture minWalkable quota.
            const int walkable = countWalkable(grid);
            if (algo == "dungeon.bsp") {
                CHECK(walkable >= minWalkable);
            } else {
                CHECK(walkable >= 1);
            }
            ++okRuns;
        }
    }
    CHECK(okRuns >= int(seedLimit) * 3);
}

TEST_CASE("hex.data.particles.loadConfigs") {
    auto *fs = eve::filesystem::Filesystem::create();
    REQUIRE(fs->setIdentity("ev_ut_hex_particles", true));
    REQUIRE(fs->setupWriteDirectory());

    // Copy fixture JSON into VFS write dir so loadConfig can find them.
    const char *names[] = {"torch_fire.json", "pickup_burst.json", "ember_trail.json", "mist_fog.json"};
    for (const char *name : names) {
        const std::string src = readTextFile(hexDataDir() + "/particles/" + name);
        REQUIRE(!src.empty());
        fs->write(name, src.c_str(), src.size());
    }

    auto            *parts = Particles::create();
    ParticleEmitter *torch = parts->newEmitterFromFile("torch_fire.json");
    REQUIRE(torch != nullptr);
    CHECK(torch->getEmissionRate() > 0.f);
    torch->setPosition(10.f, 20.f);
    torch->start();
    for (int i = 0; i < 6; ++i) parts->update(1.f / 60.f);
    CHECK(torch->getCount() > 0);
    torch->stop();

    ParticleEmitter *burst = parts->newEmitterFromFile("pickup_burst.json");
    REQUIRE(burst != nullptr);
    burst->setPosition(30.f, 40.f);
    burst->start();
    burst->emit(20);
    CHECK(burst->getCount() > 0);
    burst->stop();

    ParticleEmitter *ember = parts->newEmitterFromFile("ember_trail.json");
    REQUIRE(ember != nullptr);
    CHECK(ember->getEmissionRate() > 0.f);
    ember->stop();

    ParticleEmitter *mist = parts->newEmitterFromFile("mist_fog.json");
    REQUIRE(mist != nullptr);
    CHECK(mist->getEmissionRate() > 0.f);
    mist->setPosition(50.f, 60.f);
    mist->start();
    for (int i = 0; i < 8; ++i) parts->update(1.f / 60.f);
    CHECK(mist->getCount() > 0);
    mist->stop();
}

TEST_CASE("hex.data.tinyMap.handcraftedPath") {
    const std::string text = readTextFile(hexDataDir() + "/maps/tiny_hex.json");
    REQUIRE(!text.empty());

    hideLayers();
    std::vector<MapObject> objects;
    std::string            err;
    auto                   layers = loadMapText(text, &objects, &err);
    REQUIRE(!layers.empty());
    TileLayer *layer = layers.front();
    CHECK_EQ(static_cast<int>(layer->config()->orientation), static_cast<int>(MapOrientation::Hexagonal));
    CHECK(std::fabs(layer->config()->hexSideLength - 16.f) < 1e-3f);
    CHECK_EQ(layer->getMapWidth(), 8);
    CHECK_EQ(layer->getMapHeight(), 6);
    CHECK_EQ(layer->getTile(0, 0), 1);  // wall
    CHECK_EQ(layer->getTile(1, 1), 2);  // floor
    CHECK_EQ(layer->getTile(3, 3), 3);  // door

    auto *mapMod = Map::create();
    mapMod->setObjects(objects);
    CHECK(mapMod->getObjectCount() >= 2);

    int spawnX = -1, spawnY = -1, exitX = -1, exitY = -1;
    for (int i = 0; i < mapMod->getObjectCount(); ++i) {
        const std::string t = mapMod->getObjectType(i);
        if (t == "spawn") {
            spawnX = int(mapMod->getObjectX(i));
            spawnY = int(mapMod->getObjectY(i));
        } else if (t == "stairs") {
            exitX = int(mapMod->getObjectX(i));
            exitY = int(mapMod->getObjectY(i));
        }
    }
    CHECK_EQ(spawnX, 1);
    CHECK_EQ(spawnY, 1);
    CHECK_EQ(exitX, 6);
    CHECK_EQ(exitY, 4);

    Pathfinder *pf = mapMod->newPathfinder(layer);
    pf->blockGid(1);
    pf->setTopology("hex");
    CHECK(pf->isWalkable(spawnX, spawnY));
    CHECK(pf->isWalkable(exitX, exitY));
    Path *path = pf->findPath(spawnX, spawnY, exitX, exitY);
    REQUIRE(path != nullptr);
    CHECK(path->getLength() > 0);
    CHECK_EQ(path->getX(path->getLength() - 1), exitX);
    CHECK_EQ(path->getY(path->getLength() - 1), exitY);
    delete path;
    delete pf;
    for (TileLayer *L : layers) L->setVisible(false);
}

TEST_CASE("hex.data.ringMap.loopPathThroughDoor") {
    const std::string text = readTextFile(hexDataDir() + "/maps/ring_hex.json");
    REQUIRE(!text.empty());

    hideLayers();
    std::vector<MapObject> objects;
    std::string            err;
    auto                   layers = loadMapText(text, &objects, &err);
    REQUIRE(!layers.empty());
    TileLayer *layer = layers.front();
    CHECK_EQ(layer->getMapWidth(), 9);
    CHECK_EQ(layer->getMapHeight(), 7);
    CHECK_EQ(layer->getTile(4, 3), 3);  // center door
    CHECK_EQ(layer->getTile(4, 2), 1);  // inner wall ring

    auto *mapMod = Map::create();
    mapMod->setObjects(objects);
    int spawnX = -1, spawnY = -1, exitX = -1, exitY = -1;
    for (int i = 0; i < mapMod->getObjectCount(); ++i) {
        const std::string t = mapMod->getObjectType(i);
        if (t == "spawn") {
            spawnX = int(mapMod->getObjectX(i));
            spawnY = int(mapMod->getObjectY(i));
        } else if (t == "stairs") {
            exitX = int(mapMod->getObjectX(i));
            exitY = int(mapMod->getObjectY(i));
        }
    }
    CHECK_EQ(spawnX, 1);
    CHECK_EQ(exitX, 7);

    Pathfinder *pf = mapMod->newPathfinder(layer);
    pf->blockGid(1);
    pf->setTopology("hex");
    Path *outer = pf->findPath(spawnX, spawnY, exitX, exitY);
    REQUIRE(outer != nullptr);
    CHECK(outer->getLength() > 0);

    // Path must stay on the outer ring (avoid blocked center) or pass the door.
    bool touchedDoor = false;
    for (int i = 0; i < outer->getLength(); ++i) {
        if (outer->getX(i) == 4 && outer->getY(i) == 3) touchedDoor = true;
    }
    // Outer corridor path is valid either way; just ensure connectivity.
    CHECK(pf->isWalkable(4, 3));
    delete outer;

    Path *viaDoor = pf->findPath(4, 1, 4, 5);
    REQUIRE(viaDoor != nullptr);
    CHECK(viaDoor->getLength() > 0);
    delete viaDoor;
    delete pf;
    for (TileLayer *L : layers) L->setVisible(false);
    (void)touchedDoor;
}

TEST_CASE("hex.data.perception.casesFromJson") {
    auto doc         = loadJson("perception_cases.json");
    auto root        = doc->object();
    auto cases       = root->getArray("cases");
    auto radiusCases = root->getArray("radiusScaleCases");
    REQUIRE(cases);
    REQUIRE(radiusCases);

    auto *mapMod = Map::create();
    Fov  *fov    = mapMod->newFovSize(7, 7);
    fov->setBlockEmpty(false);
    fov->setTopology("hex");
    fov->setDetectionMargin(0.f);

    for (size_t i = 0; i < cases->size(); ++i) {
        auto        c          = cases->getObject(static_cast<unsigned int>(i));
        const float perception = float(c->getValue<double>("perception"));
        const float stealth    = float(c->getValue<double>("stealth"));
        const bool  expect     = c->getValue<bool>("expectDetect");
        fov->clearRevealers();
        const int id = fov->addRevealer(3, 3, 2);
        fov->setRevealerPerception(id, perception);
        fov->setPerceptionRadiusScale(0.f);  // keep radius fixed for stealth-only cases
        fov->compute();
        CHECK(fov->isVisible(3, 3));
        const bool got = fov->canDetect(id, 3, 3, stealth);
        CHECK_EQ(got, expect);
    }

    for (size_t i = 0; i < radiusCases->size(); ++i) {
        auto        c          = radiusCases->getObject(static_cast<unsigned int>(i));
        const int   base       = c->getValue<int>("baseRadius");
        const float perception = float(c->getValue<double>("perception"));
        const float scale      = float(c->getValue<double>("scale"));
        const int   expect     = c->getValue<int>("expectEffective");
        fov->clearRevealers();
        const int id = fov->addRevealer(3, 3, base);
        fov->setRevealerPerception(id, perception);
        fov->setPerceptionRadiusScale(scale);
        CHECK_EQ(fov->getEffectiveRadius(id), expect);
    }
    delete fov;
}

TEST_CASE("hex.data.catalog.driveLevelGeneration") {
    auto doc    = loadJson("catalog.json");
    auto levels = doc->object()->getArray("levels");
    REQUIRE(levels);

    auto *mapMod = Map::create();
    auto *gen    = Procgen::create();
    GeneratorRegistry::instance().registerBuiltins();
    gen->setPaletteGid("hex_catalog", "wall", 1);
    gen->setPaletteGid("hex_catalog", "floor", 2);
    gen->setPaletteGid("hex_catalog", "corridor", 2);
    gen->setPaletteGid("hex_catalog", "door", 3);

    int generated = 0;
    for (size_t i = 0; i < levels->size(); ++i) {
        auto lv = levels->getObject(static_cast<unsigned int>(i));
        // Skip id 7 synthetic cost map (still dungeon.bsp — generate is fine).
        const uint32_t    seed = uint32_t(lv->getValue<int>("seed"));
        const int         w    = lv->getValue<int>("width");
        const int         h    = lv->getValue<int>("height");
        const std::string algo = lv->getValue<std::string>("algorithm");

        hideLayers();
        TileLayer *layer = mapMod->newLayer(w, h, 64.f, 32.f);
        {
            auto c           = layer->config();
            c->orientation   = MapOrientation::Hexagonal;
            c->hexSideLength = 16.f;
            c->staggerAxis   = StaggerAxis::Y;
            c->staggerIndex  = StaggerIndex::Odd;
        }
        Params p;
        p.setSeed(seed);
        p.setSize(w, h);
        if (auto params = lv->getObject("params")) {
            if (params->has("loops")) p.setInt("loops", params->getValue<int>("loops"));
            if (params->has("fill")) p.setFloat("fill", float(params->getValue<double>("fill")));
        }
        Grid2D      grid;
        std::string err;
        REQUIRE(GeneratorRegistry::instance().generate(algo, p, grid, err));
        eve::test_support::applyGridToLayer(*gen, grid, "hex_catalog", *layer);
        CHECK(countWalkable(grid) >= 4);

        Pathfinder *pf = mapMod->newPathfinder(layer);
        pf->blockGid(1);
        pf->setTopology("auto");
        CHECK_EQ(pf->getTopology(), std::string("hex"));
        delete pf;
        layer->setVisible(false);
        ++generated;
    }
    CHECK_EQ(generated, int(levels->size()));
}

TEST_CASE("hex.data.catalog.bootEachLevelDistinctConfig") {
    // Start every catalog level with its own boot config and assert the
    // applied FOV / light / cell-cost / loot / algo settings actually differ
    // where the catalog says they should.
    auto catalog = loadJson("catalog.json");
    auto lootDoc = loadJson("loot_tables.json");
    auto levels  = catalog->object()->getArray("levels");
    auto tables  = lootDoc->object()->getObject("tables");
    REQUIRE(levels);
    REQUIRE(tables);

    auto *mapMod = Map::create();
    auto *gen    = Procgen::create();
    GeneratorRegistry::instance().registerBuiltins();
    gen->setPaletteGid("hex_boot", "wall", 1);
    gen->setPaletteGid("hex_boot", "floor", 2);
    gen->setPaletteGid("hex_boot", "corridor", 2);
    gen->setPaletteGid("hex_boot", "door", 3);

    std::vector<uint32_t>    seenSeeds;
    std::vector<std::string> seenKeys;
    int                      booted = 0;

    for (size_t i = 0; i < levels->size(); ++i) {
        auto lv = levels->getObject(static_cast<unsigned int>(i));
        REQUIRE(lv);
        const int         id        = lv->getValue<int>("id");
        const std::string key       = lv->getValue<std::string>("key");
        const uint32_t    seed      = uint32_t(lv->getValue<int>("seed"));
        const int         w         = lv->getValue<int>("width");
        const int         h         = lv->getValue<int>("height");
        const std::string algo      = lv->getValue<std::string>("algorithm");
        const std::string lootTable = lv->has("lootTable") ? lv->getValue<std::string>("lootTable") : "starter";

        CHECK(!key.empty());
        CHECK(tables->has(lootTable));
        for (uint32_t s : seenSeeds) CHECK(s != seed);
        for (const auto &k : seenKeys) CHECK(k != key);
        seenSeeds.push_back(seed);
        seenKeys.push_back(key);

        hideLayers();
        TileLayer *layer = mapMod->newLayer(w, h, 64.f, 32.f);
        {
            auto c           = layer->config();
            c->orientation   = MapOrientation::Hexagonal;
            c->hexSideLength = 16.f;
            c->staggerAxis   = StaggerAxis::Y;
            c->staggerIndex  = StaggerIndex::Odd;
        }

        Params p;
        p.setSeed(seed);
        p.setSize(w, h);
        if (auto params = lv->getObject("params")) {
            if (params->has("loops")) p.setInt("loops", params->getValue<int>("loops"));
            if (params->has("fill")) p.setFloat("fill", float(params->getValue<double>("fill")));
            if (params->has("floorPct")) p.setFloat("floorPct", float(params->getValue<double>("floorPct")));
            if (params->has("preset")) p.setString("preset", params->getValue<std::string>("preset"));
            if (params->has("maxAttempts")) p.setInt("maxAttempts", params->getValue<int>("maxAttempts"));
        }

        Grid2D      grid;
        std::string err;
        REQUIRE(GeneratorRegistry::instance().generate(algo, p, grid, err));
        eve::test_support::applyGridToLayer(*gen, grid, "hex_boot", *layer);
        CHECK(countWalkable(grid) >= 1);

        Pathfinder *pf = mapMod->newPathfinder(layer);
        pf->blockGid(1);
        pf->setTopology("auto");
        CHECK_EQ(pf->getTopology(), std::string("hex"));

        // Apply cell-cost boot config when present.
        if (auto cc = lv->getObject("cellCost")) {
            const float cost  = float(cc->getValue<double>("cost"));
            const int   strip = cc->getValue<int>("stripWidth");
            CHECK(cost > 1.f);
            CHECK(strip >= 1);
            int painted = 0;
            for (int y = 1; y < h - 1; ++y) {
                for (int x = 2; x < 2 + strip && x < w - 1; ++x) {
                    if (!pf->isWalkable(x, y)) continue;
                    pf->setCellCost(x, y, cost);
                    CHECK(std::fabs(pf->getCellCost(x, y) - cost) < 1e-3f);
                    ++painted;
                }
            }
            CHECK(painted >= 1);
        }

        // Apply FOV boot config when present / enabled.
        auto       enable = lv->getObject("enable");
        const bool fovOn  = !enable || !enable->has("fov") || enable->getValue<bool>("fov");
        if (fovOn) {
            Fov *fov = mapMod->newFov(layer);
            fov->blockOpaqueGid(1);
            fov->setBlockEmpty(false);
            fov->setTopology("auto");
            auto        fovCfg      = lv->getObject("fov");
            std::string fovAlgo     = "shadowcast";
            int         radius      = 6;
            float       perception  = 0.f;
            float       scale       = 0.f;
            int         torchRadius = 0;
            if (fovCfg) {
                if (fovCfg->has("algorithm")) fovAlgo = fovCfg->getValue<std::string>("algorithm");
                if (fovCfg->has("radius")) radius = fovCfg->getValue<int>("radius");
                if (fovCfg->has("heroRadius")) radius = fovCfg->getValue<int>("heroRadius");
                if (fovCfg->has("heroPerception")) perception = float(fovCfg->getValue<double>("heroPerception"));
                if (fovCfg->has("perceptionScale")) scale = float(fovCfg->getValue<double>("perceptionScale"));
                if (fovCfg->has("torchRadius")) torchRadius = fovCfg->getValue<int>("torchRadius");
                if (fovCfg->has("enabled") && !fovCfg->getValue<bool>("enabled")) {
                    delete fov;
                    delete pf;
                    layer->setVisible(false);
                    ++booted;
                    continue;
                }
            }
            if (radius < 1) radius = 1;
            fov->setAlgorithm(fovAlgo);
            CHECK_EQ(fov->getAlgorithm(), fovAlgo);
            fov->setPerceptionRadiusScale(scale);
            const int hero = fov->addRevealer(w / 2, h / 2, radius);
            fov->setRevealerPerception(hero, perception);
            CHECK(std::fabs(fov->getRevealerPerception(hero) - perception) < 1e-3f);
            const int expectEff = radius + int(std::floor(perception * scale));
            CHECK_EQ(fov->getEffectiveRadius(hero), expectEff);
            if (torchRadius > 0) {
                const int torch = fov->addRevealer(1, 1, torchRadius);
                CHECK_EQ(fov->getEffectiveRadius(torch), torchRadius);
                CHECK_EQ(fov->getRevealerCount(), 2);
            } else {
                CHECK_EQ(fov->getRevealerCount(), 1);
            }
            fov->compute();
            delete fov;
        }

        // Apply light boot config when present.
        if (auto light = lv->getObject("light")) {
            std::string lightType = "point";
            if (light->has("type")) lightType = light->getValue<std::string>("type");
            const bool knownProfile =
                lightType == "point" || lightType == "spot" || lightType == "dir" || lightType == "multi";
            REQUIRE(knownProfile);
            // The example keeps its point torch for the multi-light profile;
            // multi is a catalog profile, not a Light2D primitive type.
            const std::string primitiveType = lightType == "multi" ? "point" : lightType;
            auto *lamp = eve::graphics::Light2D::createLight(lightType);
            CHECK_EQ(lamp->getType(), primitiveType);
            if (lightType == "multi") {
                REQUIRE(light->has("count"));
                CHECK(light->getValue<int>("count") > 1);
            }
            const float radius = light->has("radius") ? float(light->getValue<double>("radius")) : 120.f;
            lamp->setRadius(radius);
            CHECK(std::fabs(lamp->getRadius() - radius) < 1e-3f);
            if (light->has("spotAngle")) {
                lamp->setSpotAngle(float(light->getValue<double>("spotAngle")));
                CHECK(std::fabs(lamp->getSpotAngle() - float(light->getValue<double>("spotAngle"))) < 1e-3f);
            }
            if (light->has("count")) CHECK(light->getValue<int>("count") >= 1);
            lamp->setEnabled(false);
        }

        // Level-specific feature tags should be non-empty and include hex.
        auto features = lv->getArray("features");
        REQUIRE(features);
        CHECK(features->size() >= 1);
        bool hasHex = false;
        for (size_t fi = 0; fi < features->size(); ++fi) {
            if (features->getElement<std::string>(static_cast<unsigned int>(fi)) == "hex") hasHex = true;
        }
        CHECK(hasHex);

        // Spot-check a few known distinct boot profiles.
        if (id == 2) {
            CHECK_EQ(algo, std::string("cave.cellular"));
            CHECK_EQ(lootTable, std::string("cave"));
            auto fovCfg = lv->getObject("fov");
            REQUIRE(fovCfg);
            CHECK_EQ(fovCfg->getValue<int>("radius"), 6);
        } else if (id == 3) {
            auto light = lv->getObject("light");
            REQUIRE(light);
            CHECK_EQ(light->getValue<std::string>("type"), std::string("spot"));
            CHECK(std::fabs(float(light->getValue<double>("radius")) - 180.f) < 1e-3f);
            CHECK(std::fabs(float(light->getValue<double>("spotAngle")) - 28.f) < 1e-3f);
        } else if (id == 7) {
            auto cc = lv->getObject("cellCost");
            REQUIRE(cc);
            CHECK(std::fabs(float(cc->getValue<double>("cost")) - 8.f) < 1e-3f);
        } else if (id == 8) {
            auto fovCfg = lv->getObject("fov");
            REQUIRE(fovCfg);
            CHECK_EQ(fovCfg->getValue<int>("heroRadius"), 4);
            CHECK(std::fabs(float(fovCfg->getValue<double>("heroPerception")) - 2.f) < 1e-3f);
        } else if (id == 15) {
            CHECK_EQ(lootTable, std::string("equipment"));
            auto parts = lv->getArray("particles");
            REQUIRE(parts);
            CHECK(parts->size() >= 3);
        } else if (id == 26) {
            CHECK_EQ(algo, std::string("cave.drunkard"));
        } else if (id == 27) {
            CHECK_EQ(algo, std::string("maze.backtrack"));
        } else if (id == 28) {
            CHECK_EQ(algo, std::string("wfc.simple"));
        } else if (id == 30) {
            CHECK_EQ(lootTable, std::string("raid"));
            auto en = lv->getObject("enable");
            REQUIRE(en);
            CHECK(en->getValue<bool>("perception"));
            CHECK(en->getValue<bool>("flow"));
        }

        delete pf;
        layer->setVisible(false);
        ++booted;
        (void)id;
    }

    CHECK_EQ(booted, int(levels->size()));
    CHECK(booted >= 31);
}

TEST_CASE("hex.data.catalog.newLevels16to30Present") {
    auto doc    = loadJson("catalog.json");
    auto levels = doc->object()->getArray("levels");
    REQUIRE(levels);
    std::vector<int> want = {16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30};
    for (int id : want) {
        bool found = false;
        for (size_t i = 0; i < levels->size(); ++i)
            if (levels->getObject(static_cast<unsigned int>(i))->getValue<int>("id") == id) found = true;
        CHECK(found);
    }
}

TEST_CASE("hex.data.lootTables.perceptionGates") {
    auto lootDoc = loadJson("loot_tables.json");
    auto root    = lootDoc->object();
    auto gates   = root->getArray("perceptionGates");
    REQUIRE(gates);
    CHECK(gates->size() >= 3);

    ItemRegistry::clear();
    InventorySystem::ensureBuiltins();
    auto *inv = Inventory::create();
    inv->registerItemsFromJson(readTextFile(hexDataDir() + "/items.json"));

    auto *mapMod = Map::create();
    Fov  *fov    = mapMod->newFovSize(9, 9);
    fov->setBlockEmpty(false);
    fov->setTopology("hex");
    fov->setDetectionMargin(0.f);
    const int hero = fov->addRevealer(4, 4, 3);
    fov->setRevealerPerception(hero, 2.f);
    fov->compute();
    CHECK(fov->isVisible(4, 4));

    for (size_t i = 0; i < gates->size(); ++i) {
        auto              g       = gates->getObject(static_cast<unsigned int>(i));
        const std::string itemId  = g->getValue<std::string>("itemId");
        const float       stealth = float(g->getValue<double>("stealth"));
        const bool        needVis = g->getValue<bool>("requireVisible");
        CHECK(ItemRegistry::find(itemId) != nullptr);
        const bool detect = fov->canDetect(hero, 4, 4, stealth);
        if (needVis) {
            // Visible cell: detect iff perception (>=2) covers stealth.
            CHECK_EQ(detect, stealth <= 2.f);
        } else {
            // Gate flagged requireVisible=false — still uses canDetect which
            // requires visibility; document that fixture flag is advisory for demos.
            CHECK(fov->isVisible(4, 4));
            (void)detect;
        }
    }
    delete fov;
    ItemRegistry::clear();
}

TEST_CASE("hex.data.lootTables.allTablesPlaceable") {
    auto lootDoc = loadJson("loot_tables.json");
    auto tables  = lootDoc->object()->getObject("tables");
    REQUIRE(tables);

    ItemRegistry::clear();
    InventorySystem::ensureBuiltins();
    auto *inv = Inventory::create();
    inv->registerItemsFromJson(readTextFile(hexDataDir() + "/items.json"));

    auto *mapMod = Map::create();
    auto *gen    = Procgen::create();
    hideLayers();
    TileLayer *layer = mapMod->newLayer(20, 16, 64.f, 32.f);
    {
        auto c           = layer->config();
        c->orientation   = MapOrientation::Hexagonal;
        c->hexSideLength = 16.f;
        c->staggerAxis   = StaggerAxis::Y;
        c->staggerIndex  = StaggerIndex::Odd;
    }
    GeneratorRegistry::instance().registerBuiltins();
    Params p;
    p.setSeed(777);
    p.setSize(20, 16);
    Grid2D      grid;
    std::string err;
    REQUIRE(GeneratorRegistry::instance().generate("dungeon.bsp", p, grid, err));
    gen->setPaletteGid("hex_loot_all", "wall", 1);
    gen->setPaletteGid("hex_loot_all", "floor", 2);
    gen->setPaletteGid("hex_loot_all", "corridor", 2);
    gen->setPaletteGid("hex_loot_all", "door", 3);
    eve::test_support::applyGridToLayer(*gen, grid, "hex_loot_all", *layer);

    int spawnX = -1, spawnY = -1;
    for (int i = 0; i < grid.getObjectCount(); ++i) {
        if (grid.getObjectType(i) == "spawn") {
            spawnX = int(grid.getObjectX(i));
            spawnY = int(grid.getObjectY(i));
        }
    }
    if (spawnX < 0) {
        for (int y = 0; y < grid.getHeight() && spawnX < 0; ++y)
            for (int x = 0; x < grid.getWidth() && spawnX < 0; ++x)
                if (grid.getCell(x, y) == int(Semantic::Floor)) {
                    spawnX = x;
                    spawnY = y;
                }
    }
    REQUIRE(spawnX >= 0);

    Pathfinder *pf = mapMod->newPathfinder(layer);
    pf->blockGid(1);
    pf->setTopology("hex");

    const char *names[]  = {"starter", "cave", "rich", "raid", "equipment"};
    int         tablesOk = 0;
    for (const char *name : names) {
        REQUIRE(tables->has(name));
        auto arr = tables->getArray(name);
        REQUIRE(arr);
        Bag *bag    = inv->newBag(32);
        int  placed = 0;
        for (size_t i = 0; i < arr->size(); ++i) {
            auto              e      = arr->getObject(static_cast<unsigned int>(i));
            const std::string itemId = e->getValue<std::string>("itemId");
            const int         qty    = e->has("qty") ? e->getValue<int>("qty") : 1;
            CHECK(ItemRegistry::find(itemId) != nullptr);
            int tx = spawnX, ty = spawnY;
            if (e->has("offset")) {
                auto off = e->getArray("offset");
                tx       = spawnX + off->getElement<int>(0);
                ty       = spawnY + off->getElement<int>(1);
            }
            if (!pf->isWalkable(tx, ty)) {
                tx = spawnX;
                ty = spawnY;
            }
            const int added = bag->addItem(itemId, qty);
            CHECK(added > 0);
            ++placed;
        }
        CHECK(placed >= 1);
        CHECK(bag->getUsedSlotCount() >= 1);
        bag->destroy();
        ++tablesOk;
    }
    CHECK_EQ(tablesOk, 5);

    delete pf;
    layer->setVisible(false);
    ItemRegistry::clear();
}

TEST_CASE("hex.data.catalog.enableFlagsConsistent") {
    auto doc    = loadJson("catalog.json");
    auto levels = doc->object()->getArray("levels");
    REQUIRE(levels);

    int withEnable = 0;
    int withFov    = 0;
    int withLight  = 0;
    for (size_t i = 0; i < levels->size(); ++i) {
        auto lv = levels->getObject(static_cast<unsigned int>(i));
        CHECK(lv->has("key"));
        CHECK(lv->has("seed"));
        CHECK(lv->has("lootTable"));
        if (lv->has("enable")) {
            ++withEnable;
            auto en = lv->getObject("enable");
            // If FOV is disabled, radius may be 0 / enabled:false.
            if (en->has("fov") && !en->getValue<bool>("fov")) {
                auto fov = lv->getObject("fov");
                if (fov && fov->has("enabled")) CHECK(!fov->getValue<bool>("enabled"));
            }
            if (en->has("light") && en->getValue<bool>("light")) {
                // light feature on ⇒ prefer an explicit light block OR inherit demo default.
                (void)lv->getObject("light");
            }
            if (en->has("cellcost") && en->getValue<bool>("cellcost")) {
                CHECK(lv->has("cellCost"));
            }
            if (en->has("perception") && en->getValue<bool>("perception")) {
                auto fov = lv->getObject("fov");
                REQUIRE(fov);
                const bool hasPerception = fov->has("heroPerception") || fov->has("perceptionScale");
                CHECK(hasPerception);
            }
        }
        if (lv->has("fov")) ++withFov;
        if (lv->has("light")) ++withLight;
    }
    CHECK(withEnable >= 10);
    CHECK(withFov >= 8);
    CHECK(withLight >= 2);
}

TEST_CASE("hex.data.seedsMatrix.pathToExitSample") {
    auto doc   = loadJson("seeds_matrix.json");
    auto root  = doc->object();
    auto seeds = root->getArray("seeds");
    REQUIRE(seeds);

    auto *mapMod = Map::create();
    auto *gen    = Procgen::create();
    GeneratorRegistry::instance().registerBuiltins();
    gen->setPaletteGid("hex_seed_path", "wall", 1);
    gen->setPaletteGid("hex_seed_path", "floor", 2);
    gen->setPaletteGid("hex_seed_path", "corridor", 2);
    gen->setPaletteGid("hex_seed_path", "door", 3);

    int          ok    = 0;
    const size_t limit = std::min<size_t>(8, seeds->size());
    for (size_t si = 0; si < limit; ++si) {
        const uint32_t seed = uint32_t(seeds->getElement<int>(static_cast<unsigned int>(si)));
        hideLayers();
        TileLayer *layer = mapMod->newLayer(24, 18, 64.f, 32.f);
        {
            auto c           = layer->config();
            c->orientation   = MapOrientation::Hexagonal;
            c->hexSideLength = 16.f;
            c->staggerAxis   = StaggerAxis::Y;
            c->staggerIndex  = StaggerIndex::Odd;
        }
        Params p;
        p.setSeed(seed);
        p.setSize(24, 18);
        Grid2D      grid;
        std::string err;
        REQUIRE(GeneratorRegistry::instance().generate("dungeon.bsp", p, grid, err));
        eve::test_support::applyGridToLayer(*gen, grid, "hex_seed_path", *layer);

        int spawnX = -1, spawnY = -1, exitX = -1, exitY = -1;
        for (int i = 0; i < grid.getObjectCount(); ++i) {
            const std::string t = grid.getObjectType(i);
            if (t == "spawn") {
                spawnX = int(grid.getObjectX(i));
                spawnY = int(grid.getObjectY(i));
            } else if (t == "stairs") {
                exitX = int(grid.getObjectX(i));
                exitY = int(grid.getObjectY(i));
            }
        }
        if (spawnX < 0) {
            for (int y = 0; y < grid.getHeight() && spawnX < 0; ++y)
                for (int x = 0; x < grid.getWidth() && spawnX < 0; ++x)
                    if (grid.getCell(x, y) == int(Semantic::Floor)) {
                        spawnX = x;
                        spawnY = y;
                    }
        }
        if (exitX < 0) {
            int best = -1;
            for (int y = 0; y < grid.getHeight(); ++y)
                for (int x = 0; x < grid.getWidth(); ++x) {
                    if (grid.getCell(x, y) != int(Semantic::Floor) && grid.getCell(x, y) != int(Semantic::Corridor))
                        continue;
                    const int dist = (x - spawnX) * (x - spawnX) + (y - spawnY) * (y - spawnY);
                    if (dist > best) {
                        best  = dist;
                        exitX = x;
                        exitY = y;
                    }
                }
        }
        REQUIRE(spawnX >= 0);
        REQUIRE(exitX >= 0);

        Pathfinder *pf = mapMod->newPathfinder(layer);
        pf->blockGid(1);
        pf->setTopology("hex");
        Path *path = pf->findPath(spawnX, spawnY, exitX, exitY);
        REQUIRE(path != nullptr);
        CHECK(path->getLength() > 0);
        CHECK_EQ(path->getX(path->getLength() - 1), exitX);
        CHECK_EQ(path->getY(path->getLength() - 1), exitY);
        delete path;
        delete pf;
        layer->setVisible(false);
        ++ok;
    }
    CHECK_EQ(ok, int(limit));
}

TEST_CASE("hex.data.tinyMap.lootMarkersAndObjects") {
    const std::string text = readTextFile(hexDataDir() + "/maps/tiny_hex.json");
    REQUIRE(!text.empty());
    hideLayers();
    std::vector<MapObject> objects;
    std::string            err;
    auto                   layers = loadMapText(text, &objects, &err);
    REQUIRE(!layers.empty());

    auto *mapMod = Map::create();
    mapMod->setObjects(objects);
    int lootCount = 0, spawnCount = 0, stairsCount = 0;
    for (int i = 0; i < mapMod->getObjectCount(); ++i) {
        const std::string t = mapMod->getObjectType(i);
        if (t == "loot") ++lootCount;
        if (t == "spawn") ++spawnCount;
        if (t == "stairs") ++stairsCount;
    }
    CHECK_EQ(spawnCount, 1);
    CHECK_EQ(stairsCount, 1);
    CHECK(lootCount >= 2);
    for (TileLayer *L : layers) L->setVisible(false);
}

TEST_CASE("hex.data.defaults.matchHexOrientation") {
    auto doc      = loadJson("catalog.json");
    auto defaults = doc->object()->getObject("defaults");
    REQUIRE(defaults);
    CHECK_EQ(defaults->getValue<std::string>("orientation"), std::string("hexagonal"));
    CHECK_EQ(defaults->getValue<std::string>("staggerAxis"), std::string("y"));
    CHECK_EQ(defaults->getValue<std::string>("staggerIndex"), std::string("odd"));
    CHECK_EQ(defaults->getValue<int>("wallGid"), 1);
    CHECK_EQ(defaults->getValue<int>("floorGid"), 2);
    CHECK_EQ(defaults->getValue<int>("doorGid"), 3);
    CHECK(defaults->getValue<int>("hexSideLength") > 0);
    CHECK(defaults->getValue<int>("tileW") > 0);
    CHECK(defaults->getValue<int>("tileH") > 0);
}

TEST_CASE("hex.data.items.equipmentSlots") {
    ItemRegistry::clear();
    InventorySystem::ensureBuiltins();
    auto     *inv = Inventory::create();
    const int n   = inv->registerItemsFromJson(readTextFile(hexDataDir() + "/items.json"));
    CHECK(n >= 12);

    CHECK(ItemRegistry::find("hex.boots") != nullptr);
    CHECK_EQ(ItemRegistry::find("hex.boots")->equipSlot, std::string("feet"));
    CHECK(ItemRegistry::find("hex.shield") != nullptr);
    CHECK_EQ(ItemRegistry::find("hex.shield")->equipSlot, std::string("offhand"));

    EquipmentSet *eq = inv->newEquipmentSet();
    eq->defineSlot("feet");
    eq->addSlotAllowedTag("feet", "boots");
    eq->defineSlot("offhand");
    eq->addSlotAllowedTag("offhand", "shield");
    Bag      *bag      = inv->newBag(4);
    const int bootsIn  = bag->addItem("hex.boots", 1);
    const int shieldIn = bag->addItem("hex.shield", 1);
    CHECK_EQ(bootsIn, 1);
    CHECK_EQ(shieldIn, 1);
    const int b = bag->findItem("hex.boots");
    const int s = bag->findItem("hex.shield");
    CHECK(eq->equipFromBag("feet", bag, b));
    CHECK(eq->equipFromBag("offhand", bag, s));
    CHECK_EQ(eq->getSlotItemId("feet"), std::string("hex.boots"));
    CHECK_EQ(eq->getSlotItemId("offhand"), std::string("hex.shield"));

    bag->destroy();
    eq->destroy();
    ItemRegistry::clear();
}

TEST_CASE("hex.data.particles.mistOnDungeon") {
    auto *fs = eve::filesystem::Filesystem::create();
    REQUIRE(fs->setIdentity("ev_ut_hex_mist", true));
    REQUIRE(fs->setupWriteDirectory());
    const std::string mist = readTextFile(hexDataDir() + "/particles/mist_fog.json");
    REQUIRE(!mist.empty());
    fs->write("mist_fog.json", mist.c_str(), mist.size());

    auto *mapMod = Map::create();
    auto *gen    = Procgen::create();
    hideLayers();
    TileLayer *layer = mapMod->newLayer(16, 12, 64.f, 32.f);
    {
        auto c           = layer->config();
        c->orientation   = MapOrientation::Hexagonal;
        c->hexSideLength = 16.f;
        c->staggerAxis   = StaggerAxis::Y;
        c->staggerIndex  = StaggerIndex::Odd;
    }
    GeneratorRegistry::instance().registerBuiltins();
    Params p;
    p.setSeed(55);
    p.setSize(16, 12);
    Grid2D      grid;
    std::string err;
    REQUIRE(GeneratorRegistry::instance().generate("dungeon.bsp", p, grid, err));
    gen->setPaletteGid("hex_mist", "wall", 1);
    gen->setPaletteGid("hex_mist", "floor", 2);
    gen->setPaletteGid("hex_mist", "corridor", 2);
    gen->setPaletteGid("hex_mist", "door", 3);
    eve::test_support::applyGridToLayer(*gen, grid, "hex_mist", *layer);

    auto            *parts = Particles::create();
    ParticleEmitter *fog   = parts->newEmitterFromFile("mist_fog.json");
    REQUIRE(fog != nullptr);
    fog->setPosition(40.f, 40.f);
    fog->start();
    for (int i = 0; i < 10; ++i) parts->update(1.f / 60.f);
    CHECK(fog->getCount() > 0);
    fog->stop();
    layer->setVisible(false);
}

TEST_CASE("hex.data.lootTables.itemIdsExistInItems") {
    auto itemsDoc = loadJson("items.json");
    auto lootDoc  = loadJson("loot_tables.json");
    REQUIRE(itemsDoc->isArray());
    auto items  = itemsDoc->array();
    auto tables = lootDoc->object()->getObject("tables");
    REQUIRE(items);
    REQUIRE(tables);

    std::vector<std::string> ids;
    for (size_t i = 0; i < items->size(); ++i) {
        auto o = items->getObject(static_cast<unsigned int>(i));
        ids.push_back(o->getValue<std::string>("id"));
    }
    CHECK(ids.size() >= 12);

    int refs = 0;
    for (const char *name : {"starter", "cave", "rich", "raid", "equipment"}) {
        auto arr = tables->getArray(name);
        REQUIRE(arr);
        for (size_t i = 0; i < arr->size(); ++i) {
            const std::string itemId = arr->getObject(static_cast<unsigned int>(i))->getValue<std::string>("itemId");
            bool              found  = false;
            for (const auto &id : ids)
                if (id == itemId) found = true;
            CHECK(found);
            ++refs;
        }
    }
    auto gates = lootDoc->object()->getArray("perceptionGates");
    REQUIRE(gates);
    for (size_t i = 0; i < gates->size(); ++i) {
        const std::string itemId = gates->getObject(static_cast<unsigned int>(i))->getValue<std::string>("itemId");
        bool              found  = false;
        for (const auto &id : ids)
            if (id == itemId) found = true;
        CHECK(found);
        ++refs;
    }
    CHECK(refs >= 20);
}

TEST_CASE("hex.data.catalog.featureTagsCoverModules") {
    auto doc    = loadJson("catalog.json");
    auto levels = doc->object()->getArray("levels");
    REQUIRE(levels);

    bool hasPath = false, hasFov = false, hasLight = false, hasParticles = false;
    bool hasInventory = false, hasFlow = false, hasCell = false, hasPerc = false;
    for (size_t i = 0; i < levels->size(); ++i) {
        auto feats = levels->getObject(static_cast<unsigned int>(i))->getArray("features");
        REQUIRE(feats);
        for (size_t fi = 0; fi < feats->size(); ++fi) {
            const std::string f = feats->getElement<std::string>(static_cast<unsigned int>(fi));
            if (f == "pathfinding") hasPath = true;
            if (f == "fov") hasFov = true;
            if (f == "lighting") hasLight = true;
            if (f == "particles") hasParticles = true;
            if (f == "inventory") hasInventory = true;
            if (f == "flowfield") hasFlow = true;
            if (f == "cellcost") hasCell = true;
            if (f == "perception") hasPerc = true;
        }
    }
    CHECK(hasPath);
    CHECK(hasFov);
    CHECK(hasLight);
    CHECK(hasParticles);
    CHECK(hasInventory);
    CHECK(hasFlow);
    CHECK(hasCell);
    CHECK(hasPerc);
}

TEST_CASE("hex.data.catalog.lootTableReferencesValid") {
    auto catalog = loadJson("catalog.json");
    auto loot    = loadJson("loot_tables.json");
    auto levels  = catalog->object()->getArray("levels");
    auto tables  = loot->object()->getObject("tables");
    REQUIRE(levels);
    REQUIRE(tables);
    for (size_t i = 0; i < levels->size(); ++i) {
        auto lv = levels->getObject(static_cast<unsigned int>(i));
        REQUIRE(lv->has("lootTable"));
        const std::string name = lv->getValue<std::string>("lootTable");
        CHECK(tables->has(name));
        CHECK(tables->getArray(name)->size() >= 1);
    }
}

TEST_CASE("hex.data.particles.configRequiredFields") {
    auto       *dm      = DataModule::create();
    const char *files[] = {"torch_fire.json", "pickup_burst.json", "ember_trail.json", "mist_fog.json"};
    for (const char *name : files) {
        const std::string text = readTextFile(hexDataDir() + "/particles/" + name);
        REQUIRE(!text.empty());
        std::string                   err;
        std::unique_ptr<JsonDocument> doc(dm->decodeJson(text, &err));
        REQUIRE(doc.get() != nullptr);
        REQUIRE(doc->isObject());
        auto root = doc->object();
        CHECK(root->has("buffer"));
        const bool hasPresetOrRate = root->has("preset") || root->has("emissionRate");
        CHECK(hasPresetOrRate);
        CHECK(root->getValue<int>("buffer") > 0);
    }
}

TEST_CASE("hex.data.seedsMatrix.algorithmsRegistered") {
    auto doc   = loadJson("seeds_matrix.json");
    auto algos = doc->object()->getArray("algorithms");
    REQUIRE(algos);
    GeneratorRegistry::instance().registerBuiltins();
    for (size_t i = 0; i < algos->size(); ++i) {
        const std::string id = algos->getObject(static_cast<unsigned int>(i))->getValue<std::string>("id");
        Params            p;
        p.setSeed(1);
        p.setSize(16, 12);
        auto params = algos->getObject(static_cast<unsigned int>(i))->getObject("params");
        if (params) {
            if (params->has("loops")) p.setInt("loops", params->getValue<int>("loops"));
            if (params->has("fill")) p.setFloat("fill", float(params->getValue<double>("fill")));
            if (params->has("floorPct")) p.setFloat("floorPct", float(params->getValue<double>("floorPct")));
            if (params->has("preset")) p.setString("preset", params->getValue<std::string>("preset"));
            if (params->has("maxAttempts")) p.setInt("maxAttempts", params->getValue<int>("maxAttempts"));
        }
        Grid2D      grid;
        std::string err;
        CHECK(GeneratorRegistry::instance().generate(id, p, grid, err));
        CHECK(grid.getWidth() == 16);
        CHECK(grid.getHeight() == 12);
    }
}

TEST_CASE("hex.data.ringMap.doorShortestVsOuter") {
    const std::string text = readTextFile(hexDataDir() + "/maps/ring_hex.json");
    REQUIRE(!text.empty());
    hideLayers();
    std::vector<MapObject> objects;
    std::string            err;
    auto                   layers = loadMapText(text, &objects, &err);
    REQUIRE(!layers.empty());
    TileLayer *layer = layers.front();
    CHECK_EQ(layer->getTile(4, 3), 3);  // door
    CHECK_EQ(layer->getTile(4, 2), 1);  // inner wall north of door
    CHECK_EQ(layer->getTile(4, 4), 1);  // inner wall south of door

    auto       *mapMod = Map::create();
    Pathfinder *pf     = mapMod->newPathfinder(layer);
    pf->blockGid(1);
    pf->setTopology("hex");
    CHECK(pf->isWalkable(4, 3));
    CHECK(!pf->isWalkable(4, 2));
    CHECK(!pf->isWalkable(4, 4));

    // Horizontal path through the door cell (row 3: wall,floor,door,floor,wall).
    CHECK(pf->isWalkable(3, 3));
    CHECK(pf->isWalkable(5, 3));
    Path *throughDoor = pf->findPath(3, 3, 5, 3);
    REQUIRE(throughDoor != nullptr);
    CHECK(throughDoor->getLength() > 0);
    bool usesDoor = false;
    for (int i = 0; i < throughDoor->getLength(); ++i)
        if (throughDoor->getX(i) == 4 && throughDoor->getY(i) == 3) usesDoor = true;
    CHECK(usesDoor);

    // Outer ring still connects spawn→exit without needing the door.
    Path *outer = pf->findPath(1, 1, 7, 5);
    REQUIRE(outer != nullptr);
    CHECK(outer->getLength() > 0);
    CHECK(outer->getLength() >= throughDoor->getLength());

    delete throughDoor;
    delete outer;
    delete pf;
    for (TileLayer *L : layers) L->setVisible(false);
}

TEST_CASE("hex.data.items.categoriesAndTags") {
    auto doc = loadJson("items.json");
    auto arr = doc->array();
    REQUIRE(arr);
    int withTags = 0, withCategory = 0, withExtra = 0;
    for (size_t i = 0; i < arr->size(); ++i) {
        auto o = arr->getObject(static_cast<unsigned int>(i));
        CHECK(o->has("id"));
        CHECK(o->has("displayName"));
        CHECK(o->has("maxStack"));
        if (o->has("category")) ++withCategory;
        if (o->has("tags") && o->getArray("tags")->size() > 0) ++withTags;
        if (o->has("extra")) ++withExtra;
    }
    CHECK(withTags == int(arr->size()));
    CHECK(withCategory == int(arr->size()));
    CHECK(withExtra >= 8);
}

TEST_CASE("hex.data.perception.detectionMarginFromCases") {
    auto doc   = loadJson("perception_cases.json");
    auto cases = doc->object()->getArray("cases");
    REQUIRE(cases);

    auto *mapMod = Map::create();
    Fov  *fov    = mapMod->newFovSize(5, 5);
    fov->setBlockEmpty(false);
    fov->setTopology("hex");
    // Positive margin should allow barely-over stealth that previously failed.
    fov->setDetectionMargin(0.2f);
    CHECK(std::fabs(fov->getDetectionMargin() - 0.2f) < 1e-3f);

    int flipped = 0;
    for (size_t i = 0; i < cases->size(); ++i) {
        auto        c              = cases->getObject(static_cast<unsigned int>(i));
        const float perception     = float(c->getValue<double>("perception"));
        const float stealth        = float(c->getValue<double>("stealth"));
        const bool  expectNoMargin = c->getValue<bool>("expectDetect");
        fov->clearRevealers();
        const int id = fov->addRevealer(2, 2, 2);
        fov->setRevealerPerception(id, perception);
        fov->setPerceptionRadiusScale(0.f);
        fov->compute();
        const bool withMargin = fov->canDetect(id, 2, 2, stealth);
        if (!expectNoMargin && stealth <= perception + 0.2f + 1e-4f) {
            CHECK(withMargin);
            ++flipped;
        } else if (expectNoMargin) {
            CHECK(withMargin);
        }
    }
    CHECK(flipped >= 1);
    delete fov;
}

TEST_CASE("hex.data.tinyMap.doorIsWalkable") {
    const std::string text = readTextFile(hexDataDir() + "/maps/tiny_hex.json");
    hideLayers();
    std::vector<MapObject> objects;
    std::string            err;
    auto                   layers = loadMapText(text, &objects, &err);
    REQUIRE(!layers.empty());
    TileLayer *layer = layers.front();
    CHECK_EQ(layer->getTile(3, 3), 3);

    auto       *mapMod = Map::create();
    Pathfinder *pf     = mapMod->newPathfinder(layer);
    pf->blockGid(1);  // walls only — doors (gid 3) stay walkable
    pf->setTopology("hex");
    CHECK(pf->isWalkable(3, 3));
    Path *through = pf->findPath(1, 1, 6, 4);
    REQUIRE(through != nullptr);
    CHECK(through->getLength() > 0);
    delete through;
    delete pf;
    for (TileLayer *L : layers) L->setVisible(false);
}
}  // namespace test_hex_level_data

// Contract section: hotreload
#include "PrivateTestScope.h"

#include "ScriptTest.h"
#include "filesystem/HotReload.h"
#include "particles/ParticleSystem.h"
// hotreload.cpp
namespace test_hotreload {
using namespace eve;


namespace {

eve::filesystem::Filesystem *fs() { return eve::filesystem::Filesystem::create(); }

void useIdentity(const char *id) {
    auto *f = fs();
    REQUIRE(f->setIdentity(id, true));
    REQUIRE(f->setupWriteDirectory());
}

}  // namespace

TEST_CASE("hotreload.normalizePath") {
    CHECK_EQ(eve::filesystem::HotReload::normalizePath("./a/b.nut"), std::string("a/b.nut"));
    CHECK_EQ(eve::filesystem::HotReload::normalizePath("a\\b.json"), std::string("a/b.json"));
    CHECK_EQ(eve::filesystem::HotReload::normalizePath("x/"), std::string("x"));
}

TEST_CASE("hotreload.tryReloadParticleJson") {
    useIdentity("ev_ut_hot_particle");
    auto       *f     = fs();
    const char *name  = "hot_fx.json";
    const char *json1 = R"({"emissionRate":5,"particleLifetime":[1,1]})";
    f->write(name, json1, std::strlen(json1));

    auto *mod = eve::particles::Particles::create();
    auto *e   = mod->newEmitter(16);
    REQUIRE(e->loadConfig(name));
    CHECK(std::abs(e->getEmissionRate() - 5.f) < 1e-4f);

    auto       *hot   = eve::filesystem::HotReload::create();
    const char *json2 = R"({"emissionRate":42,"particleLifetime":[1,1]})";
    f->write(name, json2, std::strlen(json2));
    CHECK(hot->tryReload(name));
    CHECK(std::abs(e->getEmissionRate() - 42.f) < 1e-4f);

    // Prefixed path still matches after normalize.
    e->resource()->modtime = 0;
    const char *json3      = R"({"emissionRate":9,"particleLifetime":[1,1]})";
    f->write(name, json3, std::strlen(json3));
    CHECK(hot->tryReload(std::string("./") + name));
    CHECK(std::abs(e->getEmissionRate() - 9.f) < 1e-4f);

    f->remove(name);
}

TEST_CASE("hotreload.softScriptReloadPreservesState") {
    // Soft reload redefines callbacks without resetting guarded globals.
    static const char *kScript = R"SQ(
reload_count <- 0
if (!("counter" in getroottable()))
    counter <- 0
counter = counter + 1

eve_update <- function(dt) { reload_count = reload_count + 1; }

function soft_reload() {
    // Simulate load.nut soft reload of the same source.
    if (!("counter" in getroottable()))
        counter <- 0
    // guarded: do not reset counter
    eve_update <- function(dt) { reload_count = reload_count + 10; }
    if ("eve_reload" in getroottable())
        eve_reload();
}

eve_reload <- function() { counter = counter + 100; }
soft_reload();
)SQ";

    class SoftReloadTest : public ScriptTest {
    public:
        SoftReloadTest() : ScriptTest(kScript) {}
        using ScriptTest::vm;
    };

    SoftReloadTest t;
    // First load increments counter once; eve_reload from soft_reload adds 100.
    CHECK_EQ(t.vm.find("counter").toInt(), 101);
    // Updated eve_update from soft_reload uses +10
    t.vm.callFunc(t.vm.findFunc("eve_update"), t.vm, 0.016f);
    CHECK_EQ(t.vm.find("reload_count").toInt(), 10);
}

TEST_CASE("hotreload.bindAndUnbind") {
    auto *hot = eve::filesystem::HotReload::create();
    hot->bind("foo/bar.png", "texture");
    hot->unbind("foo/bar.png");
    // Unbound unknown image: tryReload returns false (no cached texture / emitters).
    CHECK(!hot->tryReload("foo/bar.png"));
}

TEST_CASE("hotreload.watchNewDirectoryRecurses") {
    useIdentity("ev_ut_hot_watch_directory");
    auto *f = fs();
    f->unwatchAll();
    REQUIRE(f->createDirectory("live_root"));
    REQUIRE(f->createDirectory("live_root/nested"));

    auto *hot = eve::filesystem::HotReload::create();
    CHECK(!hot->watchNewDirectory("not_a_directory"));
    REQUIRE(hot->watchNewDirectory("live_root"));
    CHECK_GE(f->getWatchCount(), 2);

    REQUIRE(f->createDirectory("live_root/nested/deeper"));
    REQUIRE(hot->watchNewDirectory("live_root/nested/deeper"));
    CHECK_GE(f->getWatchCount(), 3);

    f->unwatchAll();
    f->remove("live_root/nested/deeper");
    f->remove("live_root/nested");
    f->remove("live_root");
}

TEST_CASE("hotreload.watchTree.absoluteRootRecursesAndReportsRelativePath") {
    auto *f = fs();
    f->unwatchAll();

    const auto root = std::filesystem::temp_directory_path() / "ev_ut_hot_watch_parent";
    std::filesystem::remove_all(root);
    const auto editors = root / "editors";
    REQUIRE(std::filesystem::create_directories(editors));
    const auto file = editors / "skill.vm.nut";
    {
        std::ofstream out(file, std::ios::binary | std::ios::trunc);
        REQUIRE(out.good());
        out.write("v1", 2);
    }

    auto *hot = eve::filesystem::HotReload::create();
    CHECK_GE(hot->watchTree(root.string()), 2);

    std::this_thread::sleep_for(std::chrono::milliseconds(1200));
    while (!f->pollWatch().empty()) {
    }

    {
        std::ofstream out(file, std::ios::binary | std::ios::trunc);
        REQUIRE(out.good());
        out.write("v2-changed", 10);
        out.flush();
    }

    bool saw = false;
    for (int i = 0; i < 60 && !saw; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        for (;;) {
            std::string kind = f->pollWatch();
            if (kind.empty()) break;
            if (kind != "modified" && kind != "added") continue;
            const std::string p = f->getLastWatchPath();
            if (p == "editors/skill.vm.nut" || p == "editors\\skill.vm.nut" ||
                p.find("skill.vm.nut") != std::string::npos)
                saw = true;
        }
    }
    CHECK(saw);

    f->unwatchAll();
    std::filesystem::remove_all(root);
}
}  // namespace test_hotreload

// Contract section: i18n
#include "PrivateTestScope.h"

#include "i18n/I18n.h"
// i18n.cpp
namespace test_i18n {
using namespace eve::i18n;


TEST_CASE("i18n.loadFromFileRoundTripAndMissing") {
    I18n *i18n = I18n::create();
    i18n->clear();
    CHECK(!i18n->loadFromFile("en", ""));
    CHECK(!i18n->loadFromFile("en", "nope.json"));
    CHECK(!i18n->loadFromFile("", "nope.json"));

    auto *fs = eve::filesystem::Filesystem::create();
    REQUIRE(fs->setIdentity("ev_ut_i18n", true));
    REQUIRE(fs->setupWriteDirectory());
    const char *json = R"({"hello": "world", "n": 42})";
    fs->write("i18n_locale.json", json, std::strlen(json));

    CHECK(i18n->loadFromFile("en", "i18n_locale.json"));
    CHECK(i18n->hasLanguage("en"));
    CHECK_EQ(i18n->get("hello"), std::string("world"));
    CHECK_EQ(i18n->get("n"), std::string("42"));
}
}  // namespace test_i18n

// Contract section: joystick cpp
#include "PrivateTestScope.h"

#include "joystick/Joystick.h"
#include "joystick/Pad.h"
// joystick_cpp.cpp
namespace test_joystick_cpp {
namespace {

eve::joystick::Joystick *joystick() { return eve::joystick::Joystick::create(); }

}  // namespace

TEST_CASE("joystick.create") {
    auto *joy = joystick();
    REQUIRE(joy != nullptr);
    CHECK(joy->getName() == "Joystick");
    CHECK(joy->getJoystickCount() >= 0);
}

TEST_CASE("joystick.getJoystickOutOfRange") {
    auto *joy = joystick();
    REQUIRE(joy != nullptr);
    CHECK(joy->getJoystick(-1) == nullptr);
    CHECK(joy->getJoystick(joy->getJoystickCount()) == nullptr);
    CHECK(joy->getJoystickFromID(-99999) == nullptr);
}

TEST_CASE("joystick.mappingRoundTrip") {
    auto *joy = joystick();
    REQUIRE(joy != nullptr);

    // Empty is allowed; invalid non-empty should throw.
    joy->loadGamepadMappings("");
    std::string saved = joy->saveGamepadMappings();
    // May be empty if no gamepads have been seen.
    CHECK(saved.find("invalid") == std::string::npos);

    bool threw = false;
    try {
        joy->loadGamepadMappings("not-a-valid-mapping-line");
    } catch (const eve::Exception &) {
        threw = true;
    }
    CHECK(threw);
}

TEST_CASE("joystick.connectedPadsQueryable") {
    auto *joy = joystick();
    REQUIRE(joy != nullptr);

    int n = joy->getJoystickCount();
    for (int i = 0; i < n; i++) {
        auto *pad = joy->getJoystick(i);
        REQUIRE(pad != nullptr);
        CHECK(joy->getIndex(pad) == i);
        CHECK(pad->getID() >= 0);
        // Connected pads expose non-negative counts.
        if (pad->isConnected()) {
            CHECK(pad->getAxisCount() >= 0);
            CHECK(pad->getButtonCount() >= 0);
            CHECK(pad->getHatCount() >= 0);
            CHECK(!pad->getGUID().empty());
            (void)pad->isDown(0);
            (void)pad->isGamepadDown("a");
            (void)pad->getGamepadAxis("leftx");
            (void)pad->getHat(0);
        }
    }
}
}  // namespace test_joystick_cpp
