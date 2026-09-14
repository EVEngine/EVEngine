#include <algorithm>
#include <limits>
#include "asset/AssetCooker.h"
#include "asset/SpriteAnimation.h"
#include "asset/import/UnityImporter.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
using namespace eve;
using namespace eve::asset;
using namespace eve::asset_import;
namespace {
UnityProjectImportRequest spriteRequest() {
    UnityProjectImportRequest r;
    r.package               = {*PersistentId::parse("018f6f22-2490-7ad2-bf58-4f1dbca31040"),
                               "sprite-test",
                               "1.0.0",
                               {{"provider", "local"}, {"license", Value::Object{{"redistribution", "project-only"}}}}};
    auto put                = [&](const char* path, const std::string& s) { r.files[path] = {s.begin(), s.end()}; };
    r.files["Assets/s.png"] = {137, 80,  78, 71,  13, 10, 26,  10, 0,   0,   0,   13,  73,  72,  68,  82, 0, 0,
                               0,   1,   0,  0,   0,  1,  8,   6,  0,   0,   0,   31,  21,  196, 137, 0,  0, 0,
                               13,  73,  68, 65,  84, 8,  215, 99, 248, 207, 192, 240, 31,  0,   5,   0,  1, 255,
                               114, 156, 82, 103, 0,  0,  0,   0,  73,  69,  78,  68,  174, 66,  96,  130};
    std::string meta =
        "guid: aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\nTextureImporter:\n  spriteMode: 2\n  spritePixelsToUnits: 100\n    "
        "filterMode: 0\n  spriteSheet:\n    sprites:";
    for (const auto id : {"-7", "-8"})
        meta += "\n    - serializedVersion: 2\n      name: frame" + std::string(id) +
                "\n      rect:\n        x: 0\n        y: 0\n        width: 1\n        height: 1\n      alignment: 0\n  "
                "    pivot: {x: 0, y: 0}\n      border: {x: 0, y: 0, z: 0, w: 0}\n      internalID: " +
                id + "\n";
    put("Assets/s.png.meta", meta);
    put("Assets/a.anim.meta", "guid: bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\nNativeFormatImporter:\n");
    put("Assets/a.anim", R"(--- !u!74 &7400000
AnimationClip:
  m_RotationCurves: []
  m_CompressedRotationCurves: []
  m_EulerCurves: []
  m_PositionCurves: []
  m_ScaleCurves: []
  m_FloatCurves: []
  m_PPtrCurves:
  - curve:
    - time: 0
      value: {fileID: -7, guid: aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa, type: 3}
    - time: 0.5
      value: {fileID: -8, guid: aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa, type: 3}
    attribute: m_Sprite
    path:
    classID: 212
    script: {fileID: 0}
  m_SampleRate: 2
  m_AnimationClipSettings:
    m_StartTime: 0
    m_StopTime: 1
    m_LoopTime: 1
  m_Events: []
)");
    return r;
}
}  // namespace
TEST_CASE("asset.import.unitySpriteRoundTripsSignedKeysPivotAndLoopBoundary") {
    auto request  = spriteRequest();
    auto prepared = prepareUnityProjectImport(request);
    REQUIRE(prepared.ok());
    REQUIRE_EQ(prepared.value().manifest.assets.size(), std::size_t(2));
    auto encoded = buildEvaArchive(prepared.value().manifest, prepared.value().entries);
    REQUIRE(encoded.ok());
    auto archive = parseEvaArchive(encoded.value());
    REQUIRE(archive.ok());
    auto profile = assetCookProfileForTarget("windows-x86_64-vulkan");
    REQUIRE(profile.ok());
    auto cooked = cookEvaToEvpack(archive.value(), profile.value());
    REQUIRE(cooked.ok());
    auto pack = parseEvpack(cooked.value().bytes);
    REQUIRE(pack.ok());
    EvpackResourceReader reader(std::make_shared<const Evpack>(std::move(pack).takeValue()));
    EvpackCapabilities   caps{"windows", "x86_64", "vulkan", {"rgba8"}, {"spirv-1.6"}, {"high"}, {}};
    auto clip = SpriteAnimationClip::load(reader, prepared.value().manifest.entrypoints.at("Assets/a.anim"), caps);
    REQUIRE(clip.ok());
    REQUIRE_EQ(clip.value().frameCount(), std::size_t(2));
    auto first = clip.value().sample(0);
    REQUIRE(first.ok());
    REQUIRE_EQ(first.value().pivot[0], .5);
    REQUIRE_EQ(first.value().pivot[1], .5);
    auto second = clip.value().sample(.5);
    REQUIRE(second.ok());
    REQUIRE_EQ(second.value().name, std::string("frame-8"));
    auto loop = clip.value().sample(1);
    REQUIRE(loop.ok());
    REQUIRE_EQ(loop.value().name, first.value().name);
    auto invalid = clip.value().sample(std::numeric_limits<double>::quiet_NaN());
    REQUIRE(!invalid.ok());
    auto negative = clip.value().sample(-1);
    REQUIRE(!negative.ok());
}
TEST_CASE("asset.import.unitySpriteRejectsMissingFileIdAndOutOfBoundsRect") {
    auto        request = spriteRequest();
    auto&       b       = request.files.at("Assets/a.anim");
    std::string s(b.begin(), b.end());
    s.replace(s.find("fileID: -8"), 10, "fileID: -9");
    b.assign(s.begin(), s.end());
    auto missing = prepareUnityProjectImport(request);
    REQUIRE(!missing.ok());
    request = spriteRequest();
    auto& m = request.files.at("Assets/s.png.meta");
    s.assign(m.begin(), m.end());
    s.replace(s.find("width: 1"), 8, "width: 2");
    m.assign(s.begin(), s.end());
    auto bounds = prepareUnityProjectImport(request);
    REQUIRE(!bounds.ok());
}

TEST_CASE("asset.import.unitySpriteSchemaRejectsVersionAndDuplicateTimeAndHoldsNonLoopEnd") {
    auto prepared = prepareUnityProjectImport(spriteRequest());
    REQUIRE(prepared.ok());
    const auto& assets = prepared.value().manifest.assets;
    auto        asset =
        std::find_if(assets.begin(), assets.end(), [](const auto& a) { return a.type == "eve.sprite-animation"; });
    REQUIRE(asset != assets.end());
    const auto& entries = prepared.value().entries;
    auto        entry =
        std::find_if(entries.begin(), entries.end(), [&](const auto& e) { return e.path == asset->definition; });
    REQUIRE(entry != entries.end());
    auto value = Value::fromJson(std::string(entry->bytes.begin(), entry->bytes.end()));
    REQUIRE(value.ok());
    auto& object          = *value.value().getIf<Value::Object>();
    object["loop"]        = false;
    object["futureField"] = "retained";
    auto clip             = SpriteAnimationClip::decode(value.value());
    REQUIRE(clip.ok());
    auto last = clip.value().sample(100);
    REQUIRE(last.ok());
    REQUIRE_EQ(last.value().name, std::string("frame-8"));
    object["schemaVersion"] = 2;
    auto version            = SpriteAnimationClip::decode(value.value());
    REQUIRE(!version.ok());
    object["schemaVersion"]                     = 1;
    auto& frames                                = *object.at("frames").getIf<Value::Array>();
    (*frames[1].getIf<Value::Object>())["time"] = 0;
    auto duplicate                              = SpriteAnimationClip::decode(value.value());
    REQUIRE(!duplicate.ok());
}
