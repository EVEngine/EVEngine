#include "PathBesideSource.h"
#include "asset/AssetCooker.h"
#include "asset/CanonicalMesh.h"
#include "asset/graphics/EvpackGraphicsLoader.h"
#include "asset/graphics/EvpackStaticPrefab.h"
#include "asset/import/StructuredModelImporter.h"
#include "asset/scene/EvpackSceneTemplateLoader.h"
#include "graphics/IImageResourceFactory.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <algorithm>
#include <bit>
#include <filesystem>

using namespace eve;
using namespace eve::asset_import;
namespace {
ImportPackageIdentity package() {
    return {*PersistentId::parse("018f6f22-2490-7ad2-bf58-4f1dbca31040"),
            "structured-model",
            "1.0.0",
            {{"provider", "local"}, {"license", Value::Object{{"redistribution", "project-only"}}}}};
}
LDrawImportRequest ldraw(std::string text) {
    return {package(), "model.ldr", {{"model.ldr", {text.begin(), text.end()}}}};
}
asset::CanonicalMeshData mesh(const PreparedAssetImport& prepared, std::string_view part) {
    const auto ref  = prepared.manifest.entrypoints.at("mesh:" + std::string(part));
    const auto path = "assets/" + ref.id().format() + "/mesh.bin";
    const auto entry =
        std::find_if(prepared.entries.begin(), prepared.entries.end(), [&](const auto& e) { return e.path == path; });
    REQUIRE(entry != prepared.entries.end());
    auto decoded = asset::decodeCanonicalMesh(entry->bytes);
    REQUIRE(decoded.ok());
    return std::move(decoded).takeValue();
}
ScadImportRequest stl() {
    ScadImportRequest request{
        package(), "triangle.scad", {'c', 'u', 'b', 'e', '(', '1', ')', ';'}, std::vector<std::uint8_t>(84, 0)};
    request.binaryStl[80] = 1;
    auto put              = [&](float value) {
        auto bits = std::bit_cast<std::uint32_t>(value);
        for (unsigned i = 0; i < 4; ++i) request.binaryStl.push_back(std::uint8_t(bits >> (i * 8)));
    };
    for (float value : {0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1000.f, 0.f, 0.f, 0.f, 1000.f, 0.f}) put(value);
    request.binaryStl.push_back(0);
    request.binaryStl.push_back(0);
    return request;
}

void verifyComposition(const PreparedAssetImport& prepared, std::size_t expectedParts) {
    auto eva = asset::buildEvaArchive(prepared.manifest, prepared.entries);
    REQUIRE(eva.ok());
    auto archive = asset::parseEvaArchive(eva.value());
    REQUIRE(archive.ok());
    auto profile = asset::assetCookProfileForTarget("windows-x86_64-vulkan");
    REQUIRE(profile.ok());
    auto cooked = asset::cookEvaToEvpack(archive.value(), profile.value());
    REQUIRE(cooked.ok());
    auto pack = asset::parseEvpack(cooked.value().bytes);
    REQUIRE(pack.ok());
    asset::EvpackResourceReader     reader(std::make_shared<const asset::Evpack>(std::move(pack).takeValue()));
    const asset::EvpackCapabilities caps{"windows", "x86_64", "vulkan", {"rgba8"}, {"spirv-1.6"}, {"high"}, {}};
    auto scene = asset_scene::EvpackSceneTemplateLoader(reader).load(prepared.manifest.entrypoints.at("default"), caps);
    REQUIRE(scene.ok());
    REQUIRE_EQ(scene.value().renderers.size(), expectedParts);
    class Meshes final : public graphics::IMeshResourceFactory {
    public:
        int                     token = 0, uploads = 0, releases = 0;
        Result<graphics::Mesh*> uploadMesh(const float*, const float*, const float*, int vertices, const std::uint32_t*,
                                           int indices) override {
            REQUIRE(vertices > 0);
            REQUIRE(indices > 0);
            ++uploads;
            return Result<graphics::Mesh*>::success(reinterpret_cast<graphics::Mesh*>(&token));
        }
        Result<void> releaseMesh(graphics::Mesh*) override {
            ++releases;
            return Result<void>::success();
        }
    } meshes;
    class Images final : public graphics::IImageResourceFactory {
    public:
        Result<graphics::Texture*> uploadRgba8(std::uint32_t, std::uint32_t, const std::uint8_t*, bool) override {
            return Result<graphics::Texture*>::failure(Diagnostic::error(DiagnosticCode::Failed, "unexpected texture"));
        }
        Result<void> releaseImage(graphics::Texture*) override { return Result<void>::success(); }
    } images;
    {
        auto prefab = asset_graphics::EvpackStaticPrefab::load(reader, meshes, images,
                                                               prepared.manifest.entrypoints.at("default"), caps);
        REQUIRE(prefab.ok());
        REQUIRE_EQ(prefab.value()->drawCount(), expectedParts);
        REQUIRE_EQ(std::size_t(meshes.uploads), expectedParts);
    }
    REQUIRE_EQ(meshes.releases, meshes.uploads);
}
}  // namespace

TEST_CASE("asset.import.structuredModelsLDrawMpdReferenceColourAndComposition") {
    auto request = ldraw(
        "0 FILE model.ldr\n1 4 20 -40 0 1 0 0 0 1 0 0 0 1 part.dat\n"
        "1 1 -20 0 0 1 0 0 0 1 0 0 0 1 part.dat\n0 FILE part.dat\n"
        "0 BFC CERTIFY CCW\n4 16 0 0 0 20 0 0 20 20 0 0 20 0\n0 NOFILE\n");
    auto prepared = prepareLDrawImport(request);
    REQUIRE(prepared.ok());
    auto red = mesh(prepared.value(), "colour-4");
    REQUIRE_EQ(red.indices.size(), std::size_t(6));
    REQUIRE(std::abs(red.positions[0] - .008f) < 1e-6f);
    REQUIRE(std::abs(red.positions[1] - .016f) < 1e-6f);
    verifyComposition(prepared.value(), 2);
    auto again = prepareLDrawImport(request);
    REQUIRE(again.ok());
    auto firstArchive  = asset::buildEvaArchive(prepared.value().manifest, prepared.value().entries);
    auto secondArchive = asset::buildEvaArchive(again.value().manifest, again.value().entries);
    REQUIRE(firstArchive.ok());
    REQUIRE(secondArchive.ok());
    REQUIRE(firstArchive.value() == secondArchive.value());
}
TEST_CASE("asset.import.structuredModelsLDrawLibraryCaseAndDirectColour") {
    auto              request         = ldraw("1 16 0 0 0 1 0 0 0 1 0 0 0 1 S\\PART.DAT\n");
    const std::string triangle        = "3 0x2FF0000 0 0 0 20 0 0 0 20 0\n";
    request.files["parts/s/part.dat"] = {triangle.begin(), triangle.end()};
    auto prepared                     = prepareLDrawImport(request);
    REQUIRE(prepared.ok());
    verifyComposition(prepared.value(), 1);
    auto              externalMpd     = ldraw("1 4 0 0 0 1 0 0 0 1 0 0 0 1 assembly.mpd\n");
    const std::string assembly        = "0 FILE root.ldr\n3 16 0 0 0 20 0 0 0 20 0\n0 NOFILE\n";
    externalMpd.files["assembly.mpd"] = {assembly.begin(), assembly.end()};
    auto nested                       = prepareLDrawImport(externalMpd);
    REQUIRE(nested.ok());
    verifyComposition(nested.value(), 1);
    auto sameName = prepareLDrawImport(ldraw("0 Preamble\n0 FILE model.ldr\n3 4 0 0 0 20 0 0 0 20 0\n"));
    REQUIRE(sameName.ok());
}
TEST_CASE("asset.import.structuredModelsLDrawBfcMirrorAndEdges") {
    auto request = ldraw(
        "0 BFC INVERTNEXT\n1 4 0 0 0 -1 0 0 0 1 0 0 0 1 part.dat\n"
        "2 24 0 0 0 20 0 0\n5 24 0 0 0 20 0 0 0 20 0 20 20 0\n");
    const std::string triangle = "0 BFC CERTIFY CCW\n3 16 0 0 0 20 0 0 0 20 0\n";
    request.files["part.dat"]  = {triangle.begin(), triangle.end()};
    auto prepared              = prepareLDrawImport(request);
    REQUIRE(prepared.ok());
    auto decoded = mesh(prepared.value(), "colour-4");
    REQUIRE(decoded.normals[2] < -.99f);
    REQUIRE(std::any_of(prepared.value().findings.begin(), prepared.value().findings.end(),
                        [](const auto& f) { return f.feature == "LDraw.edgeLines"; }));
}
TEST_CASE("asset.import.structuredModelsLDrawConfigColour") {
    auto              request     = ldraw("3 100 0 0 0 20 0 0 0 20 0\n");
    const std::string config      = "0 !COLOUR Custom CODE 100 VALUE #00FF00 EDGE #000000 ALPHA 128\n";
    request.files["LDConfig.ldr"] = {config.begin(), config.end()};
    auto prepared                 = prepareLDrawImport(request);
    REQUIRE(prepared.ok());
    verifyComposition(prepared.value(), 1);
}
TEST_CASE("asset.import.structuredModelsLDrawColourScopeDoesNotLeak") {
    auto request = ldraw(
        "0 !COLOUR Local CODE 100 VALUE #FF0000 EDGE #000000\n"
        "1 100 0 0 0 1 0 0 0 1 0 0 0 1 part.dat\n"
        "3 100 0 0 0 20 0 0 0 20 0\n");
    const std::string part =
        "3 100 0 0 0 20 0 0 0 20 0\n"
        "0 !COLOUR Local CODE 100 VALUE #0000FF EDGE #000000\n"
        "3 100 0 0 0 20 0 0 0 20 0\n"
        "3 16 0 0 0 20 0 0 0 20 0\n";
    request.files["part.dat"]      = {part.begin(), part.end()};
    const std::string unrelated    = "0 !COLOUR Local CODE 100 VALUE #00FF00 EDGE #000000\n";
    request.files["unrelated.dat"] = {unrelated.begin(), unrelated.end()};
    // A complete official library also contains an alternative palette; it is not the selected default.
    request.files["LDCfgalt.ldr"] = {unrelated.begin(), unrelated.end()};
    auto prepared                 = prepareLDrawImport(request);
    REQUIRE(prepared.ok());
    verifyComposition(prepared.value(), 2);
    request.files["model.ldr"] = {'3', ' ', '1', '0', '0', ' ', '0', ' ', '0', ' ', '0', ' ', '2',
                                  '0', ' ', '0', ' ', '0', ' ', '0', ' ', '2', '0', ' ', '0', '\n'};
    auto undefined             = prepareLDrawImport(request);
    REQUIRE(!undefined.ok());
}
TEST_CASE("asset.import.structuredModelsLDrawMissingCycleAndInvalidGeometry") {
    for (const auto* source : {"1 16 0 0 0 1 0 0 0 1 0 0 0 1 missing.dat\n", "1 16 0 0 0 1 0 0 0 1 0 0 0 1 model.ldr\n",
                               "3 4 0 0 0 1 0 0\n", "3 4 0 0 0 0 0 0 0 0 0\n", "3 99999 0 0 0 20 0 0 0 20 0\n",
                               "0 !TEXMAP START PLANAR\n", "0 BFC INVERTNEXT\n", "6 4 0 0 0\n"}) {
        auto prepared = prepareLDrawImport(ldraw(source));
        REQUIRE(!prepared.ok());
    }
}
TEST_CASE("asset.import.structuredModelsLDrawBudgetsAndAmbiguousPaths") {
    auto request                               = ldraw("3 4 0 0 0 20 0 0 0 20 0\n");
    request.limits.maximumVerticesPerPrimitive = 2;
    auto vertices                              = prepareLDrawImport(request);
    REQUIRE(!vertices.ok());
    request.limits.maximumVerticesPerPrimitive = 100;
    request.limits.maximumDecodedBytes         = 50;
    auto bytes                                 = prepareLDrawImport(request);
    REQUIRE(!bytes.ok());
    request.limits.maximumDecodedBytes = 10000;
    request.files["MODEL.LDR"]         = request.files.at("model.ldr");
    auto duplicate                     = prepareLDrawImport(request);
    REQUIRE(!duplicate.ok());
    request.files.erase("MODEL.LDR");
    request.files["../escape.dat"] = {};
    auto escape                    = prepareLDrawImport(request);
    REQUIRE(!escape.ok());
}
TEST_CASE("asset.import.structuredModelsScadUnitsNormalsAndComposition") {
    auto prepared = prepareScadImport(stl());
    REQUIRE(prepared.ok());
    auto decoded = mesh(prepared.value(), "default");
    REQUIRE_EQ(decoded.positions[3], 1.f);
    REQUIRE_EQ(decoded.positions[8], -1.f);
    REQUIRE_EQ(decoded.normals[1], 1.f);
    verifyComposition(prepared.value(), 1);
}
TEST_CASE("asset.import.structuredModelsScadMalformedAndBudgets") {
    auto request = stl();
    request.binaryStl.pop_back();
    auto truncated = prepareScadImport(request);
    REQUIRE(!truncated.ok());
    request                  = stl();
    request.binaryStl.back() = 1;
    auto extended            = prepareScadImport(request);
    REQUIRE(!extended.ok());
    request                                    = stl();
    request.limits.maximumVerticesPerPrimitive = 2;
    auto vertices                              = prepareScadImport(request);
    REQUIRE(!vertices.ok());
    request               = stl();
    request.binaryStl[98] = 0x80;
    request.binaryStl[99] = 0x7f;
    auto infinity         = prepareScadImport(request);
    REQUIRE(!infinity.ok());
}
TEST_CASE("asset.import.structuredModelsScadCompilerAbsent") {
    ScadFileImportRequest request{
        package(), (std::filesystem::path(eve_test_path::sourceBesideTestDir(__FILE__)).parent_path() / "fixtures/structured_models/cup.scad").string(),
        "eve-definitely-missing-openscad-executable"};
    auto prepared = prepareScadFileImport(request);
    REQUIRE(!prepared.ok());
    REQUIRE(prepared.error()->message().find("OpenSCAD") != std::string::npos);
    request.timeoutSeconds = 0;
    auto timeout           = prepareScadFileImport(request);
    REQUIRE(!timeout.ok());
}

#if defined(EVE_SCAD_TEST_COMPILER)
TEST_CASE("asset.import.structuredModelsScadCompilerTimeout") {
    ScadFileImportRequest request{
        package(), (std::filesystem::path(eve_test_path::sourceBesideTestDir(__FILE__)).parent_path() / "fixtures/structured_models/cup.scad").string(),
        EVE_SCAD_TEST_COMPILER, 1};
    auto prepared = prepareScadFileImport(request);
    REQUIRE(!prepared.ok());
    REQUIRE(prepared.error()->message().find("timed out") != std::string::npos);
}
#endif
