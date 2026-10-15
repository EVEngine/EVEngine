#include "zeroerr/unittest.h"

#include "image/ImageData.h"
#include "procgen/Params.h"
#include "procgen/algorithms/MarchingCubes.h"
#include "procgen/algorithms/PrototypeKit.h"
#include "procgen/texture/PrototypeTextures.h"
#include "procgen/texture/TextureRecipe.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>

using namespace eve::procgen;

namespace {

uint64_t imageChecksum(const eve::image::ImageData& image) {
    const auto* bytes = static_cast<const uint8_t*>(image.getData());
    uint64_t    hash  = 1469598103934665603ull;
    for (size_t i = 0; i < image.getSize(); ++i) {
        hash ^= bytes[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

bool windingNeverOpposesNormals(const MeshBuild& mesh) {
    for (int triangle = 0; triangle + 2 < mesh.getIndexCount(); triangle += 3) {
        const int i0 = mesh.getIndex(triangle);
        const int i1 = mesh.getIndex(triangle + 1);
        const int i2 = mesh.getIndex(triangle + 2);
        const float ax = mesh.getPositionX(i1) - mesh.getPositionX(i0);
        const float ay = mesh.getPositionY(i1) - mesh.getPositionY(i0);
        const float az = mesh.getPositionZ(i1) - mesh.getPositionZ(i0);
        const float bx = mesh.getPositionX(i2) - mesh.getPositionX(i0);
        const float by = mesh.getPositionY(i2) - mesh.getPositionY(i0);
        const float bz = mesh.getPositionZ(i2) - mesh.getPositionZ(i0);
        const float cx = ay * bz - az * by;
        const float cy = az * bx - ax * bz;
        const float cz = ax * by - ay * bx;
        const float nx = mesh.getNormalX(i0) + mesh.getNormalX(i1) + mesh.getNormalX(i2);
        const float ny = mesh.getNormalY(i0) + mesh.getNormalY(i1) + mesh.getNormalY(i2);
        const float nz = mesh.getNormalZ(i0) + mesh.getNormalZ(i1) + mesh.getNormalZ(i2);
        if (cx * nx + cy * ny + cz * nz < -1.0e-6f) return false;
    }
    return true;
}

bool windingFacesAwayFromPoint(const MeshBuild& mesh, float px, float py, float pz) {
    for (int triangle = 0; triangle + 2 < mesh.getIndexCount(); triangle += 3) {
        const int i0 = mesh.getIndex(triangle);
        const int i1 = mesh.getIndex(triangle + 1);
        const int i2 = mesh.getIndex(triangle + 2);
        const float ax = mesh.getPositionX(i1) - mesh.getPositionX(i0);
        const float ay = mesh.getPositionY(i1) - mesh.getPositionY(i0);
        const float az = mesh.getPositionZ(i1) - mesh.getPositionZ(i0);
        const float bx = mesh.getPositionX(i2) - mesh.getPositionX(i0);
        const float by = mesh.getPositionY(i2) - mesh.getPositionY(i0);
        const float bz = mesh.getPositionZ(i2) - mesh.getPositionZ(i0);
        const float nx = ay * bz - az * by;
        const float ny = az * bx - ax * bz;
        const float nz = ax * by - ay * bx;
        const float cx =
            (mesh.getPositionX(i0) + mesh.getPositionX(i1) + mesh.getPositionX(i2)) / 3.0f;
        const float cy =
            (mesh.getPositionY(i0) + mesh.getPositionY(i1) + mesh.getPositionY(i2)) / 3.0f;
        const float cz =
            (mesh.getPositionZ(i0) + mesh.getPositionZ(i1) + mesh.getPositionZ(i2)) / 3.0f;
        if (nx * (cx - px) + ny * (cy - py) + nz * (cz - pz) < -1.0e-6f) return false;
    }
    return true;
}

}  // namespace

TEST_CASE("procgen.prototype.mesh.catalogueBuilds75GroundedPieces") {
    const auto pieces = prototypePieceDescriptors();
    REQUIRE_EQ(pieces.size(), size_t(75));
    Params params;
    for (const auto& piece : pieces) {
        auto result = generatePrototypePiece(piece.id, params);
        REQUIRE(result.ok());
        MeshBuild mesh = std::move(result).takeValue();
        REQUIRE(!mesh.empty());
        REQUIRE(mesh.getVertexCount() > 0);
        REQUIRE(mesh.getIndexCount() > 0);
        float minimumY = mesh.getPositionY(0);
        for (int vertex = 1; vertex < mesh.getVertexCount(); ++vertex)
            minimumY = std::min(minimumY, mesh.getPositionY(vertex));
        CHECK(minimumY >= -1.0e-5f);
        for (int index = 0; index < mesh.getIndexCount(); ++index) {
            CHECK(mesh.getIndex(index) >= 0);
            CHECK(mesh.getIndex(index) < mesh.getVertexCount());
        }
        CHECK(windingNeverOpposesNormals(mesh));
        CHECK_EQ(mesh.getMeta("source", ""), std::string("procedural"));
    }
}

TEST_CASE("procgen.prototype.mesh.parametersChangeTopologyAndDimensions") {
    Params coarse;
    coarse.setInt("steps", 3);
    coarse.setFloat("width", 2.0f);
    Params detailed;
    detailed.setInt("steps", 9);
    detailed.setFloat("width", 4.0f);
    auto aResult = generatePrototypePiece("stairs", coarse);
    auto bResult = generatePrototypePiece("stairs", detailed);
    REQUIRE(aResult.ok());
    REQUIRE(bResult.ok());
    MeshBuild a = std::move(aResult).takeValue();
    MeshBuild b = std::move(bResult).takeValue();
    CHECK(b.getVertexCount() > a.getVertexCount());
    float width = 0.0f;
    for (int vertex = 0; vertex < b.getVertexCount(); ++vertex)
        width = std::max(width, std::abs(b.getPositionX(vertex)) * 2.0f);
    CHECK(width > 3.9f);
}

TEST_CASE("procgen.prototype.mesh.rampsHaveOutwardFaces") {
    Params params;
    for (const char* id : {"ramp", "ramp1"}) {
        auto result = generatePrototypePiece(id, params);
        REQUIRE(result.ok());
        MeshBuild mesh = std::move(result).takeValue();
        float minX = mesh.getPositionX(0), maxX = minX;
        float minY = mesh.getPositionY(0), maxY = minY;
        float minZ = mesh.getPositionZ(0), maxZ = minZ;
        for (int vertex = 1; vertex < mesh.getVertexCount(); ++vertex) {
            minX = std::min(minX, mesh.getPositionX(vertex));
            maxX = std::max(maxX, mesh.getPositionX(vertex));
            minY = std::min(minY, mesh.getPositionY(vertex));
            maxY = std::max(maxY, mesh.getPositionY(vertex));
            minZ = std::min(minZ, mesh.getPositionZ(vertex));
            maxZ = std::max(maxZ, mesh.getPositionZ(vertex));
        }
        const float interiorX = (minX + maxX) * 0.5f;
        const float interiorY = minY + (maxY - minY) / 3.0f;
        const float interiorZ = minZ + (maxZ - minZ) * 2.0f / 3.0f;
        CHECK(windingFacesAwayFromPoint(mesh, interiorX, interiorY, interiorZ));
    }
}

TEST_CASE("procgen.prototype.mesh.uvScaleChangesDensityWithoutChangingGeometry") {
    Params base;
    base.setFloat("uvScale", 0.5f);
    Params dense;
    dense.setFloat("uvScale", 2.0f);
    auto baseResult  = generatePrototypePiece("wall", base);
    auto denseResult = generatePrototypePiece("wall", dense);
    REQUIRE(baseResult.ok());
    REQUIRE(denseResult.ok());
    MeshBuild a = std::move(baseResult).takeValue();
    MeshBuild b = std::move(denseResult).takeValue();
    REQUIRE_EQ(a.positions().size(), b.positions().size());
    REQUIRE_EQ(a.uvs().size(), b.uvs().size());
    CHECK(std::equal(a.positions().begin(), a.positions().end(), b.positions().begin()));
    bool changed = false;
    for (size_t i = 0; i < a.uvs().size(); ++i) {
        if (std::abs(a.uvs()[i] - b.uvs()[i]) > 1.0e-5f) changed = true;
    }
    CHECK(changed);
}

TEST_CASE("procgen.prototype.mesh.registryExposesEveryPiece") {
    auto& registry = MeshRecipeRegistry::instance();
    registry.registerBuiltins();
    for (const auto& piece : prototypePieceDescriptors()) {
        const std::string recipe = "prototype." + std::string(piece.id);
        CHECK(registry.has(recipe));
        const RecipeDescriptor* schema = registry.descriptor(recipe);
        REQUIRE(schema != nullptr);
        CHECK(schema->find("width") != nullptr);
        CHECK(schema->find("detail") != nullptr);
        CHECK(schema->find("uvScale") != nullptr);
    }
}

TEST_CASE("procgen.prototype.mesh.unknownIdIsStructuredFailure") {
    Params params;
    auto   result = generatePrototypePiece("not-a-piece", params);
    CHECK(!result.ok());
    CHECK_EQ(result.status().code(), eve::StatusCode::NotFound);
}

TEST_CASE("procgen.prototype.texture.catalogueBuilds13PatternsIn6Palettes") {
    constexpr std::array<const char*, 6> palettes{{"dark", "light", "purple", "orange", "green", "red"}};
    const auto                           patterns = prototypeTextureDescriptors();
    REQUIRE_EQ(patterns.size(), size_t(13));
    for (const auto& pattern : patterns) {
        uint64_t previous = 0;
        for (const char* palette : palettes) {
            Params params;
            params.setSize(96, 80);
            params.setString("palette", palette);
            params.setInt("cellSize", 16);
            auto result = generatePrototypeTexture(pattern.id, params);
            REQUIRE(result.ok());
            auto image = std::move(result).takeValue();
            REQUIRE(static_cast<bool>(image));
            CHECK_EQ(image->getWidth(), 96);
            CHECK_EQ(image->getHeight(), 80);
            CHECK_EQ(image->getFormat(), std::string("RGBA8"));
            const uint64_t checksum = imageChecksum(*image);
            if (previous != 0) CHECK(checksum != previous);
            previous = checksum;
        }
    }
}

TEST_CASE("procgen.prototype.texture.isDeterministicAndParameterized") {
    Params base;
    base.setSize(128, 128);
    base.setString("palette", "custom");
    base.setInt("backgroundR", 12);
    base.setInt("backgroundG", 34);
    base.setInt("backgroundB", 56);
    base.setInt("cellSize", 16);
    auto firstResult  = generatePrototypeTexture("diagonal-grid", base);
    auto secondResult = generatePrototypeTexture("diagonal-grid", base);
    REQUIRE(firstResult.ok());
    REQUIRE(secondResult.ok());
    auto first  = std::move(firstResult).takeValue();
    auto second = std::move(secondResult).takeValue();
    CHECK_EQ(imageChecksum(*first), imageChecksum(*second));

    base.setInt("cellSize", 32);
    auto changedResult = generatePrototypeTexture("diagonal-grid", base);
    REQUIRE(changedResult.ok());
    auto changed = std::move(changedResult).takeValue();
    CHECK(imageChecksum(*first) != imageChecksum(*changed));
}

TEST_CASE("procgen.prototype.texture.rejectsInvalidParameters") {
    Params invalidPalette;
    invalidPalette.setSize(64, 64);
    invalidPalette.setString("palette", "blue");
    auto paletteResult = generatePrototypeTexture("fine-grid", invalidPalette);
    CHECK(!paletteResult.ok());
    REQUIRE(paletteResult.error() != nullptr);
    CHECK_EQ(paletteResult.error()->code(), eve::DiagnosticCode::InvalidArgument);

    Params invalidOpacity;
    invalidOpacity.setSize(64, 64);
    invalidOpacity.setFloat("minorAlpha", std::numeric_limits<float>::quiet_NaN());
    auto opacityResult = generatePrototypeTexture("fine-grid", invalidOpacity);
    CHECK(!opacityResult.ok());
    REQUIRE(opacityResult.error() != nullptr);
    CHECK_EQ(opacityResult.error()->code(), eve::DiagnosticCode::InvalidArgument);
}

TEST_CASE("procgen.prototype.texture.registryExposesEveryPattern") {
    auto& registry = TextureRecipeRegistry::instance();
    registry.registerBuiltins();
    for (const auto& pattern : prototypeTextureDescriptors()) {
        const std::string recipe = "tex.prototype." + std::string(pattern.id);
        CHECK(registry.has(recipe));
        const RecipeDescriptor* schema = registry.descriptor(recipe);
        REQUIRE(schema != nullptr);
        CHECK(schema->find("palette") != nullptr);
        CHECK(schema->find("cellSize") != nullptr);
    }
}
