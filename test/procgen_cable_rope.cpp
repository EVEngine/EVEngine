// Cable / chain / hemp-rope mesh + texture recipe coverage (process-isolated).

#include "procgen/MeshBuild.h"
#include "procgen/Params.h"
#include "procgen/algorithms/CableChainRope.h"
#include "procgen/algorithms/MarchingCubes.h"
#include "procgen/texture/CableTextures.h"
#include "procgen/texture/PbrMaterial.h"
#include "procgen/texture/TextureRecipe.h"

#include "image/ImageData.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <string>

using namespace eve::procgen;

namespace {

bool meshIndicesInRange(const MeshBuild& m) {
    const int vc = m.getVertexCount();
    for (int i = 0; i < m.getIndexCount(); ++i) {
        if (int(m.getIndex(i)) >= vc) return false;
    }
    return true;
}

bool meshPositionsFinite(const MeshBuild& m) {
    for (int i = 0; i < m.getVertexCount(); ++i) {
        if (!std::isfinite(m.getPositionX(i)) || !std::isfinite(m.getPositionY(i)) ||
            !std::isfinite(m.getPositionZ(i)))
            return false;
    }
    return true;
}

bool meshNormalsFiniteUnit(const MeshBuild& m, float tol = 0.2f) {
    for (int i = 0; i < m.getVertexCount(); ++i) {
        const float nx = m.getNormalX(i), ny = m.getNormalY(i), nz = m.getNormalZ(i);
        if (!std::isfinite(nx) || !std::isfinite(ny) || !std::isfinite(nz)) return false;
        const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
        if (std::fabs(len - 1.f) > tol) return false;
    }
    return true;
}

}  // namespace

TEST_CASE("procgen.cableRope.mesh.recipesRegistered") {
    MeshRecipeRegistry::instance().registerBuiltins();
    for (const char* id : {"mesh.cable", "mesh.chain", "mesh.rope"}) {
        REQUIRE(MeshRecipeRegistry::instance().has(id));
        const RecipeDescriptor* schema = MeshRecipeRegistry::instance().descriptor(id);
        REQUIRE(schema != nullptr);
        CHECK_EQ(schema->id, id);
        CHECK(schema->find("segments") != nullptr);
        CHECK(schema->find("radius") != nullptr);
        CHECK(schema->find("thickness") != nullptr);
    }
}

TEST_CASE("procgen.cableRope.mesh.buildsAllKinds") {
    MeshRecipeRegistry::instance().registerBuiltins();
    for (const char* id : {"mesh.cable", "mesh.chain", "mesh.rope"}) {
        Params p;
        p.setSeed(11);
        p.setInt("segments", 3);
        p.setFloat("segLength", 1.f);
        MeshBuild   m;
        std::string err;
        REQUIRE(MeshRecipeRegistry::instance().generate(id, p, m, err));
        CHECK_GT(m.getVertexCount(), 0);
        CHECK_GT(m.getIndexCount(), 0);
        CHECK_EQ(m.getIndexCount() % 3, 0);
        CHECK(meshIndicesInRange(m));
        CHECK(meshPositionsFinite(m));
        CHECK(meshNormalsFiniteUnit(m));
        CHECK_EQ(m.getMeta("algorithm", ""), std::string(id));
        CHECK_EQ(m.getMeta("segments", ""), std::string("3"));
    }
}

TEST_CASE("procgen.cableRope.mesh.reproducibleAndScalable") {
    MeshRecipeRegistry::instance().registerBuiltins();
    Params p;
    p.setSeed(5);
    p.setInt("segments", 4);
    p.setFloat("segLength", 0.8f);
    p.setInt("strands", 6);
    p.setInt("twists", 1);

    MeshBuild a, b, scaled;
    std::string err;
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cable", p, a, err));
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cable", p, b, err));
    CHECK(a.positions() == b.positions());
    CHECK(a.indices() == b.indices());

    Params p2 = p;
    p2.setFloat("scale", 2.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cable", p2, scaled, err));
    CHECK_EQ(a.getVertexCount(), scaled.getVertexCount());
    float minAx = 1e30f, maxAx = -1e30f, minSx = 1e30f, maxSx = -1e30f;
    for (int i = 0; i < a.getVertexCount(); ++i) {
        minAx = std::min(minAx, a.getPositionX(i));
        maxAx = std::max(maxAx, a.getPositionX(i));
        minSx = std::min(minSx, scaled.getPositionX(i));
        maxSx = std::max(maxSx, scaled.getPositionX(i));
    }
    CHECK(std::fabs((maxAx - minAx) * 2.f - (maxSx - minSx)) < 1e-3f);
}

TEST_CASE("procgen.cableRope.mesh.segmentsScaleVertexCount") {
    MeshRecipeRegistry::instance().registerBuiltins();
    Params lo;
    lo.setInt("segments", 2);
    lo.setFloat("segLength", 1.f);
    Params hi = lo;
    hi.setInt("segments", 6);
    MeshBuild a, b;
    std::string err;
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.rope", lo, a, err));
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.rope", hi, b, err));
    CHECK_EQ(3 * a.getVertexCount(), b.getVertexCount());
}

TEST_CASE("procgen.cableRope.mesh.directApi") {
    Params p;
    p.setInt("segments", 2);
    p.setFloat("segLength", 1.f);
    MeshBuild   m;
    std::string err;
    REQUIRE(generateCableChainRope("mesh.chain", p, m, err));
    CHECK_GT(m.getVertexCount(), 0);
    CHECK(!generateCableChainRope("mesh.missing", p, m, err));
}

TEST_CASE("procgen.cableRope.textures.reproducible") {
    TextureRecipeRegistry::instance().registerBuiltins();
    for (const char* id : {"tex.cable.steel", "tex.chain.iron", "tex.rope.hemp"}) {
        REQUIRE(TextureRecipeRegistry::instance().has(id));
        Params p;
        p.setSeed(42);
        p.setSize(48, 48);
        p.setInt("seamless", 1);
        std::string err;
        auto        a = TextureRecipeRegistry::instance().generate(id, p, err);
        auto        b = TextureRecipeRegistry::instance().generate(id, p, err);
        REQUIRE(static_cast<bool>(a));
        REQUIRE(static_cast<bool>(b));
        CHECK_EQ(a->getWidth(), 48);
        CHECK_EQ(a->getHeight(), 48);
        CHECK_EQ(a->getFormat(), std::string("RGBA8"));
        CHECK(std::memcmp(a->getData(), b->getData(), a->getSize()) == 0);
    }
}

TEST_CASE("procgen.cableRope.textures.paramsChangeOutput") {
    TextureRecipeRegistry::instance().registerBuiltins();
    Params a;
    a.setSeed(9);
    a.setSize(32, 32);
    a.setInt("strands", 3);
    Params b = a;
    b.setInt("strands", 8);
    std::string err;
    auto        imgA = TextureRecipeRegistry::instance().generate("tex.cable.steel", a, err);
    auto        imgB = TextureRecipeRegistry::instance().generate("tex.cable.steel", b, err);
    REQUIRE(static_cast<bool>(imgA));
    REQUIRE(static_cast<bool>(imgB));
    CHECK(std::memcmp(imgA->getData(), imgB->getData(), imgA->getSize()) != 0);
}

TEST_CASE("procgen.cableRope.textures.resultApiAndDefaults") {
    Params p;
    p.setSeed(1);
    p.setSize(40, 40);
    auto steel = generateSteelCableTexture(p);
    REQUIRE(steel.ok());
    REQUIRE(static_cast<bool>(steel.value()));
    CHECK_EQ(steel.value()->getWidth(), 40);

    auto iron = generateIronChainTexture(p);
    REQUIRE(iron.ok());
    auto hemp = generateHempRopeTexture(p);
    REQUIRE(hemp.ok());

    TextureRecipeRegistry::instance().registerBuiltins();
    Params filled;
    filled.setSize(16, 16);
    REQUIRE(TextureRecipeRegistry::instance().applyDefaults("tex.cable.steel", filled));
    CHECK_EQ(filled.getInt("strands", 0), 6);
    REQUIRE(TextureRecipeRegistry::instance().applyDefaults("tex.rope.hemp", filled));
    CHECK_EQ(filled.getInt("strands", 0), 3);
}

TEST_CASE("procgen.cableRope.pbr.fullMapSet") {
    PbrRecipeRegistry::instance().registerPbrBuiltins();
    for (const char* id : {"pbr.cable.steel", "pbr.chain.iron", "pbr.rope.hemp"}) {
        REQUIRE(PbrRecipeRegistry::instance().has(id));
        Params p;
        p.setSeed(17);
        p.setSize(32, 32);
        p.setInt("seamless", 1);
        std::string err;
        auto        set = PbrRecipeRegistry::instance().generate(id, p, err);
        REQUIRE(static_cast<bool>(set));
        REQUIRE(set->albedo != nullptr);
        REQUIRE(set->normal != nullptr);
        REQUIRE(set->roughness != nullptr);
        REQUIRE(set->metallic != nullptr);
        REQUIRE(set->height != nullptr);
        REQUIRE(set->ao != nullptr);
        CHECK_EQ(set->albedo->getWidth(), 32);
        CHECK_EQ(set->normal->getWidth(), 32);
    }

    Params steel;
    steel.setSeed(3);
    steel.setSize(24, 24);
    auto pbr = generateSteelCablePbr(steel);
    REQUIRE(pbr.ok());
    REQUIRE(static_cast<bool>(pbr.value()));
    CHECK(pbr.value()->albedo != nullptr);

    auto ironPbr = generateIronChainPbr(steel);
    REQUIRE(ironPbr.ok());
    auto hempPbr = generateHempRopePbr(steel);
    REQUIRE(hempPbr.ok());
}
