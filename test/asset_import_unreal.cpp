#include "asset/AssetCooker.h"
#include "asset/EvpackResourceReader.h"
#include "asset_procgen/EvpackInstanceSetLoader.h"
#include "asset_procgen/EvpackPointGraphLoader.h"
#include "asset_procgen/EvpackTerrainMaterialLoader.h"
#include "asset_import/UnrealImporter.h"
#include "procgen/PointGraph.h"
#include "procgen/PointSet.h"
#include "procgen/SpatialData.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve;
using namespace eve::asset;
using namespace eve::asset_import;

namespace {

ImportPackageIdentity unrealIdentity() {
    return {*PersistentId::parse("018f6f22-2490-7ad2-bf58-4f1dbca31040"), "fab.m4-terrain", "1.0.0",
            {{"provider", Value("fab")}, {"productId", Value("m4-representative")},
             {"license", Value(Value::Object{{"redistribution", Value("project-only")},
                                              {"ueOnly", Value(false)}})}}};
}

std::vector<std::uint8_t> text(std::string_view value) { return {value.begin(), value.end()}; }

AssetCookProfile target(std::string os, std::string arch, std::string graphics,
                        std::string texture, std::string shader) {
    return {{std::move(os), std::move(arch), std::move(graphics), {std::move(texture)},
             std::move(shader), "high", {}}, CookPublication::LocalInspection, 16};
}

}  // namespace

TEST_CASE("asset.import.unrealM4LandscapeTraversesWindowsAndWebRuntimePacks") {
    constexpr std::string_view descriptor = R"json({
      "schema":"eve.unreal-landscape-import","schemaVersion":1,
      "landscape":{"width":3,"height":2,"heightmap":"Content/M4/Landscape.r16",
                   "scaleCm":[100,200,100]},
      "layers":[{"name":"Grass","diffuse":"Content/M4/T_Grass_D.png",
                 "normal":"Content/M4/T_Grass_N.png","weight":"Content/M4/Grass.weight",
                 "tileSizeCm":800}],
      "grass":[{"id":"grass-main","prototype":"/Game/M4/SM_Grass",
                "layer":"Grass","densityPerSquareMeter":2.5,"seed":42}],
      "instances":[{"prototype":"/Game/M4/SM_Rock","positionCm":[100,200,300],
                    "rotationQuat":[0,0,0,1],"scale":[1,2,3]}],
      "unsupported":["Blueprint","RVT","VHFM","Nanite-private-payload"]
    })json";
    // 3x2 UE Landscape samples: 32768, 32896, 32640, 32768, 33024, 32512.
    const std::vector<std::uint8_t> heights = {0x00,0x80, 0x80,0x80, 0x80,0x7f,
                                                0x00,0x80, 0x00,0x81, 0x00,0x7f};
    UnrealProjectImportRequest request;
    request.package = unrealIdentity();
    request.descriptorPath = "Config/M4.evimport.json";
    request.files = {
        {request.descriptorPath, text(descriptor)},
        {"Content/M4/Landscape.r16", heights},
        {"Content/M4/T_Grass_D.png", {1,2,3}},
        {"Content/M4/T_Grass_N.png", {4,5,6}},
        {"Content/M4/Grass.weight", {7,8,9}}
    };
    auto prepared = prepareUnrealM4Import(request);
    REQUIRE(prepared.ok());
    CHECK_EQ(prepared.value().manifest.assets.size(), std::size_t(4));
    CHECK_EQ(prepared.value().sourceMappings.size(), prepared.value().manifest.assets.size());
    CHECK(std::any_of(prepared.value().entries.begin(), prepared.value().entries.end(),
                      [](const auto& entry) { return entry.path == "reports/import.json"; }));
    CHECK(std::any_of(prepared.value().manifest.assets.begin(), prepared.value().manifest.assets.end(),
                      [](const auto& asset) { return asset.type == "eve.pcg-graph"; }));
    CHECK(std::any_of(prepared.value().findings.begin(), prepared.value().findings.end(), [](const auto& finding) {
        return finding.feature == "RVT" && finding.disposition == ImportDisposition::Unsupported;
    }));
    auto evaBytes = buildEvaArchive(prepared.value().manifest, prepared.value().entries);
    REQUIRE(evaBytes.ok());
    auto eva = parseEvaArchive(evaBytes.value());
    REQUIRE(eva.ok());

    auto windows = cookEvaToEvpack(eva.value(), target("windows", "x86_64", "vulkan", "bc", "spirv-1.6"));
    auto web = cookEvaToEvpack(eva.value(), target("web", "wasm32", "webgpu", "rgba8", "wgsl-1"));
    REQUIRE(windows.ok());
    REQUIRE(web.ok());
    CHECK(windows.value().buildId != web.value().buildId);
    auto windowsPack = parseEvpack(windows.value().bytes);
    auto webPack = parseEvpack(web.value().bytes);
    REQUIRE(windowsPack.ok());
    REQUIRE(webPack.ok());
    CHECK_EQ(windowsPack.value().packageId(), webPack.value().packageId());
    CHECK_EQ(windowsPack.value().chunks().size(), webPack.value().chunks().size());
    auto windowsAdmitted = std::make_shared<const Evpack>(std::move(windowsPack).takeValue());
    auto webAdmitted = std::make_shared<const Evpack>(std::move(webPack).takeValue());
    EvpackResourceReader windowsReader(windowsAdmitted);
    EvpackResourceReader webReader(webAdmitted);
    for (const auto& sourceAsset : prepared.value().manifest.assets) {
        const std::string expected = sourceAsset.type + "/" +
                                     std::to_string(sourceAsset.schemaVersion.value());
        auto windowsAsset = windowsReader.read(
            sourceAsset.asset, expected,
            {"windows", "x86_64", "vulkan", {"bc"}, {"spirv-1.6"}, {"high"}, {}},
            16 * 1024 * 1024);
        auto webAsset = webReader.read(
            sourceAsset.asset, expected,
            {"web", "wasm32", "webgpu", {"rgba8"}, {"wgsl-1"}, {"high"}, {}},
            16 * 1024 * 1024);
        REQUIRE(windowsAsset.ok());
        REQUIRE(webAsset.ok());
        CHECK_EQ(windowsAsset.value().chunks.front().bytes,
                 webAsset.value().chunks.front().bytes);
    }

    const auto pcgAsset = std::find_if(prepared.value().manifest.assets.begin(),
                                       prepared.value().manifest.assets.end(),
                                       [](const auto& asset) { return asset.type == "eve.pcg-graph"; });
    REQUIRE(pcgAsset != prepared.value().manifest.assets.end());
    eve::asset_procgen::EvpackPointGraphLoader loader(windowsReader);
    auto terrain = eve::procgen::SpatialData::box(0.f, 0.f, 0.f, 2.f, 0.f, 1.f);
    auto loaded = loader.load(
        pcgAsset->asset,
        {"windows", "x86_64", "vulkan", {"bc"}, {"spirv-1.6"}, {"high"}, {}},
        terrain);
    REQUIRE(loaded.ok());
    std::unique_ptr<eve::procgen::PointSet> first(
        loaded.value().graph->execute(loaded.value().outputNode));
    REQUIRE(bool(first));
    REQUIRE(first->getCount() > 0);
    CHECK_EQ(first->getStringAttribute(0, "prototype", ""), std::string("/Game/M4/SM_Grass"));
    CHECK_EQ(first->getStringAttribute(0, "layer", ""), std::string("Grass"));
    std::unique_ptr<eve::procgen::PointSet> second(
        loaded.value().graph->execute(loaded.value().outputNode));
    REQUIRE(bool(second));
    CHECK_EQ(first->getCount(), second->getCount());
    CHECK_EQ(first->getPointSeed(0), second->getPointSeed(0));

    const auto instancesAsset = std::find_if(
        prepared.value().manifest.assets.begin(), prepared.value().manifest.assets.end(),
        [](const auto& asset) { return asset.type == "eve.instance-set"; });
    REQUIRE(instancesAsset != prepared.value().manifest.assets.end());
    eve::asset_procgen::EvpackInstanceSetLoader instanceLoader(windowsReader);
    auto loadedInstances = instanceLoader.load(
        instancesAsset->asset,
        {"windows", "x86_64", "vulkan", {"bc"}, {"spirv-1.6"}, {"high"}, {}});
    REQUIRE(loadedInstances.ok());
    REQUIRE_EQ(loadedInstances.value().instances.size(), std::size_t(1));
    const auto& rock = loadedInstances.value().instances.front();
    CHECK_EQ(rock.prototype, std::string("/Game/M4/SM_Rock"));
    CHECK(std::abs(rock.position[0] - 2.f) < 0.0001f);
    CHECK(std::abs(rock.position[1] - 3.f) < 0.0001f);
    CHECK(std::abs(rock.position[2] + 1.f) < 0.0001f);
    CHECK(std::abs(rock.scale[0] - 2.f) < 0.0001f);
    CHECK(std::abs(rock.scale[1] - 3.f) < 0.0001f);
    CHECK(std::abs(rock.scale[2] - 1.f) < 0.0001f);

    const auto materialAsset = std::find_if(
        prepared.value().manifest.assets.begin(), prepared.value().manifest.assets.end(),
        [](const auto& asset) { return asset.type == "eve.terrain-material"; });
    REQUIRE(materialAsset != prepared.value().manifest.assets.end());
    eve::asset_procgen::EvpackTerrainMaterialLoader materialLoader(windowsReader);
    auto loadedMaterial = materialLoader.load(
        materialAsset->asset,
        {"windows", "x86_64", "vulkan", {"bc"}, {"spirv-1.6"}, {"high"}, {}});
    REQUIRE(loadedMaterial.ok());
    REQUIRE_EQ(loadedMaterial.value().layers.size(), std::size_t(1));
    const auto& grassLayer = loadedMaterial.value().layers.front();
    CHECK_EQ(grassLayer.name, std::string("Grass"));
    CHECK_EQ(grassLayer.normalConvention, std::string("directx"));
    CHECK(std::abs(grassLayer.tileSizeMeters - 8.f) < 0.0001f);
}

TEST_CASE("asset.import.unrealM4RejectsTraversalAndWrongR16Length") {
    UnrealProjectImportRequest traversal;
    traversal.package = unrealIdentity();
    traversal.descriptorPath = "../M4.json";
    auto unsafe = prepareUnrealM4Import(traversal);
    REQUIRE(!unsafe.ok());
    CHECK_EQ(unsafe.error()->code(), DiagnosticCode::InvalidArgument);

    const std::string descriptor = R"json({"schema":"eve.unreal-landscape-import","schemaVersion":1,
      "landscape":{"width":2,"height":2,"heightmap":"Content/L.r16","scaleCm":[100,100,100]}})json";
    UnrealProjectImportRequest truncated;
    truncated.package = unrealIdentity();
    truncated.descriptorPath = "Config/M4.json";
    truncated.files = {{truncated.descriptorPath, text(descriptor)}, {"Content/L.r16", {0,0}}};
    auto rejected = prepareUnrealM4Import(truncated);
    REQUIRE(!rejected.ok());
    CHECK_EQ(rejected.error()->code(), DiagnosticCode::ParseError);
}
