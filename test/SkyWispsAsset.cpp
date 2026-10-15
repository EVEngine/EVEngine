#include "graphics/sky/SkyWispsAsset.h"
#include <algorithm>
#include <fstream>
#include <optional>
#include "Fixtures.h"
#include "common/Value.h"
#include "filesystem/Filesystem.h"
#include "graphics/ShaderResources.h"
#include "graphics/sky/SkyWispsLayer.h"
#include "zeroerr/unittest.h"

TEST_CASE("graphics.sky generated assets own data and use deterministic independent seeds") {
    using namespace eve::graphics;
    auto first = SkyWispsAsset::generate(2026), again = SkyWispsAsset::generate(2026),
         other = SkyWispsAsset::generate(17);
    REQUIRE(first.ok());
    REQUIRE(again.ok());
    REQUIRE(other.ok());
    const auto& a = first.value().layer();
    const auto& b = again.value().layer();
    REQUIRE(a.daylight.validate().ok());
    REQUIRE(a.daylightEnabled);
    REQUIRE(a.opticalCycleEnabled);
    REQUIRE(a.daylight.moonDiskColor[3] == 1);
    REQUIRE(std::equal(a.corners.begin(), a.corners.end(), b.corners.begin(), b.corners.end()));
    const auto equal = [](const auto* x, const auto* y) {
        return std::equal(x->bytes.begin(), x->bytes.end(), y->bytes.begin(), y->bytes.end());
    };
    REQUIRE(equal(a.densityTexture, b.densityTexture));
    REQUIRE(equal(a.starsTexture, b.starsTexture));
    REQUIRE(equal(a.moonColorTexture, b.moonColorTexture));
    REQUIRE(!equal(a.densityTexture, other.value().layer().densityTexture));
    REQUIRE(!equal(a.starsTexture, other.value().layer().starsTexture));
    REQUIRE(!equal(a.moonColorTexture, other.value().layer().moonColorTexture));
    REQUIRE(a.starsTexture->mipLevels == 11);
    REQUIRE(a.moonColorTexture->mipLevels == 9);
    REQUIRE(!a.moonColorTexture->sampler.repeatU);
    auto bad             = a.daylight;
    bad.moonDiskShape[0] = 0;
    REQUIRE(!bad.validate().ok());
    bad                     = a.daylight;
    bad.moonDiskLighting[2] = 30;
    REQUIRE(!bad.validate().ok());
    bad                 = a.daylight;
    bad.moonDiskGlow[1] = 0;
    REQUIRE(!bad.validate().ok());
}

TEST_CASE("graphics.sky external packs validate all versions without private assets") {
    using namespace eve::graphics;
    using eve::Value;
    TempDir temporary;
    auto*   fs = eve::filesystem::Filesystem::create();
    REQUIRE(fs != nullptr);
    REQUIRE(fs->mountRealDirectory(temporary.str(), "sky-fixture", false));
    const auto write = [&](const std::string& path, const Value& value) {
        auto encoded = value.toJson();
        REQUIRE(encoded.ok());
        std::ofstream(temporary.path() / path) << encoded.value();
    };
    const auto parse = [&](const char* text) {
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
    const auto& defaults = generated.value().layer().daylight;
    const auto  vec      = [](const auto& values) {
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
            for (const auto& k : defaults.scatteringCurve) keys.pushBack(vec(k));
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
