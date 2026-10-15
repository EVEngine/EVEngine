#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#include "CaveFixtures.h"
#include "image/ImageData.h"
#include "procgen/AttributeTable.h"
#include "procgen/Biome.h"
#include "procgen/BuildLayerStack.h"
#include "procgen/MeshBuild.h"
#include "procgen/Params.h"
#include "procgen/PointSet.h"
#include "procgen/Semantic.h"
#include "procgen/algorithms/BushMesh.h"
#include "procgen/algorithms/CableChainRope.h"
#include "procgen/algorithms/MarchingCubes.h"
#include "procgen/texture/CableTextures.h"
#include "procgen/texture/PbrMaterial.h"
#include "procgen/texture/TextureRecipe.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

// procgen_attribute_table.cpp
using namespace eve;
using namespace eve::procgen;

TEST_CASE("procgen.attributeTable.storesSparseTypedColumns") {
    AttributeTable table;
    const size_t   firstRow  = table.appendRow();
    const size_t   secondRow = table.appendRow();
    CHECK_EQ(firstRow, size_t(0));
    CHECK_EQ(secondRow, size_t(1));

    auto height = table.setFloat(1, "height", 12.5f);
    REQUIRE(height.ok());
    auto biome = table.setString(0, "biome", "forest");
    REQUIRE(biome.ok());

    CHECK_EQ(table.rowCount(), size_t(2));
    CHECK_EQ(table.columnCount(), size_t(2));
    CHECK_EQ(table.columnName(0), "height");
    CHECK_EQ(table.columnName(1), "biome");
    CHECK(!table.getFloat(0, "height").has_value());
    REQUIRE(table.getFloat(1, "height").has_value());
    CHECK_EQ(*table.getFloat(1, "height"), 12.5f);
    REQUIRE(table.getString(0, "biome").has_value());
    CHECK_EQ(*table.getString(0, "biome"), "forest");
}

TEST_CASE("procgen.attributeTable.rejectsTypeConflictWithoutMutation") {
    AttributeTable table;
    const size_t   row = table.appendRow();
    CHECK_EQ(row, size_t(0));
    auto initial = table.setFloat(0, "weight", 0.75f);
    REQUIRE(initial.ok());

    auto conflict = table.setInt(0, "weight", 4);
    REQUIRE(!conflict.ok());
    REQUIRE(conflict.error() != nullptr);
    CHECK_EQ(conflict.error()->code(), DiagnosticCode::TypeMismatch);
    REQUIRE(table.typeOf("weight").has_value());
    CHECK_EQ(int(*table.typeOf("weight")), int(ProcgenAttributeType::Float));
    REQUIRE(table.getFloat(0, "weight").has_value());
    CHECK_EQ(*table.getFloat(0, "weight"), 0.75f);
}

TEST_CASE("procgen.attributeTable.appendsRowsTransactionally") {
    AttributeTable source;
    source.resize(2);
    auto sourceValue = source.setInt(1, "layer", 7);
    REQUIRE(sourceValue.ok());

    AttributeTable target;
    const size_t   targetRow = target.appendRow();
    CHECK_EQ(targetRow, size_t(0));
    auto targetValue = target.setString(0, "name", "root");
    REQUIRE(targetValue.ok());
    auto copied = target.appendRowFrom(source, 1);
    REQUIRE(copied.ok());
    CHECK_EQ(copied.value(), size_t(1));
    CHECK_EQ(target.rowCount(), size_t(2));
    REQUIRE(target.getString(0, "name").has_value());
    CHECK_EQ(*target.getString(0, "name"), "root");
    REQUIRE(target.getInt(1, "layer").has_value());
    CHECK_EQ(*target.getInt(1, "layer"), std::int64_t(7));

    AttributeTable incompatible;
    const size_t   incompatibleRow = incompatible.appendRow();
    CHECK_EQ(incompatibleRow, size_t(0));
    auto incompatibleValue = incompatible.setFloat(0, "layer", 1.f);
    REQUIRE(incompatibleValue.ok());
    const size_t rowsBefore = target.rowCount();
    auto         rejected   = target.appendRowFrom(incompatible, 0);
    REQUIRE(!rejected.ok());
    CHECK_EQ(rejected.error()->code(), DiagnosticCode::TypeMismatch);
    CHECK_EQ(target.rowCount(), rowsBefore);
    REQUIRE(target.typeOf("layer").has_value());
    CHECK_EQ(int(*target.typeOf("layer")), int(ProcgenAttributeType::Int));
}

TEST_CASE("procgen.attributeTable.supportsSelfCopy") {
    AttributeTable table;
    const size_t   row = table.appendRow();
    CHECK_EQ(row, size_t(0));
    auto value = table.setVector(0, "normal", {1.f, 2.f, 3.f});
    REQUIRE(value.ok());

    auto copied = table.appendRowFrom(table, 0);
    REQUIRE(copied.ok());
    REQUIRE(table.getVector(copied.value(), "normal").has_value());
    const auto normal = *table.getVector(copied.value(), "normal");
    CHECK_EQ(normal.x, 1.f);
    CHECK_EQ(normal.y, 2.f);
    CHECK_EQ(normal.z, 3.f);
}

TEST_CASE("procgen.pointSet.preservesColumnarAttributesAcrossOperations") {
    PointSet  points;
    const int first  = points.add(0.f, 1.f, 0.f);
    const int second = points.add(2.f, 3.f, 0.f);
    REQUIRE(points.trySetFloatAttribute(first, "weight", 0.25f).ok());
    REQUIRE(points.trySetFloatAttribute(second, "weight", 0.75f).ok());
    REQUIRE(points.trySetStringAttribute(second, "biome", "forest").ok());

    const PointSet filtered = filterPointHeight(points, 2.f, 4.f);
    CHECK_EQ(filtered.getCount(), 1);
    CHECK_EQ(filtered.attributes().rowCount(), size_t(1));
    CHECK_EQ(filtered.getFloatAttribute(0, "weight", -1.f), 0.75f);
    CHECK_EQ(filtered.getStringAttribute(0, "biome", ""), std::string("forest"));

    const PointSet transformed = transformPointSet3D(filtered, 1.f, 2.f, 3.f, 10.f, 20.f, 30.f, 2.f, 2.f, 2.f);
    CHECK_EQ(transformed.attributes().rowCount(), size_t(1));
    CHECK_EQ(transformed.getFloatAttribute(0, "weight", -1.f), 0.75f);
    CHECK_EQ(transformed.getStringAttribute(0, "biome", ""), std::string("forest"));
}

TEST_CASE("procgen.pointSet.rejectsConflictingAttributeSchema") {
    PointSet  points;
    const int row = points.add(0.f, 0.f, 0.f);
    REQUIRE(points.trySetFloatAttribute(row, "weight", 0.5f).ok());

    const auto conflict = points.trySetIntAttribute(row, "weight", 2);
    REQUIRE(!conflict.ok());
    REQUIRE(conflict.error() != nullptr);
    CHECK_EQ(conflict.error()->code(), DiagnosticCode::TypeMismatch);
    CHECK_EQ(points.getAttributeType(row, "weight"), std::string("float"));
    CHECK_EQ(points.getFloatAttribute(row, "weight", -1.f), 0.5f);
    CHECK_EQ(points.attributes().rowCount(), size_t(points.getCount()));
}

TEST_CASE("procgen.attributeTable.renamesCopiesAndRemovesColumns") {
    AttributeTable table;
    CHECK_EQ(table.appendRow(), size_t(0));
    CHECK_EQ(table.appendRow(), size_t(1));
    REQUIRE(table.setFloat(0, "height", 1.5f).ok());
    REQUIRE(table.setFloat(1, "height", 2.5f).ok());
    REQUIRE(table.setString(0, "biome", "grass").ok());

    REQUIRE(table.copyColumn("height", "altitude").ok());
    REQUIRE(table.getFloat(1, "altitude").has_value());
    CHECK_EQ(*table.getFloat(1, "altitude"), 2.5f);

    REQUIRE(table.renameColumn("biome", "cover").ok());
    CHECK(!table.typeOf("biome").has_value());
    REQUIRE(table.getString(0, "cover").has_value());
    CHECK_EQ(*table.getString(0, "cover"), "grass");

    REQUIRE(table.removeColumn("altitude").ok());
    CHECK(!table.typeOf("altitude").has_value());
    CHECK(table.typeOf("height").has_value());

    CHECK(!table.renameColumn("missing", "x").ok());
    CHECK(!table.removeColumn("missing").ok());
    CHECK(!table.copyColumn("height", "$Density").ok());
}

// procgen_biome.cpp
using namespace eve::procgen;

TEST_CASE("procgen.biome.appliesPriorityExclusionAndAssetMetadata") {
    BiomeRules  rules;
    SpatialData domain  = SpatialData::box(0.f, 0.f, 0.f, 4.f, 0.f, 4.f);
    SpatialData forest  = domain;
    SpatialData town    = SpatialData::sphere(2.f, 0.f, 2.f, 1.1f);
    SpatialData noSpawn = SpatialData::sphere(0.f, 0.f, 0.f, 0.5f);
    CHECK(rules.addLayer("forest", &forest, 1, 1.f));
    CHECK(rules.addLayer("town", &town, 10, 1.f));
    CHECK(!rules.addLayer("forest", &forest, 2, 1.f));
    CHECK(rules.addAsset("forest", "oak", 3.f, 0.8f, 1.2f, true));
    CHECK(rules.addAsset("forest", "pine", 1.f, 1.f, 1.f, true));
    CHECK(rules.addAsset("town", "lamp", 1.f, 1.f, 1.f, false));
    CHECK(rules.addExclusion(&noSpawn));

    PointSet* first  = rules.generate(&domain, 2.f, 42, 0.f);
    PointSet* second = rules.generate(&domain, 2.f, 42, 0.f);
    REQUIRE(bool(first));
    REQUIRE(bool(second));
    CHECK_EQ(first->getCount(), 8);
    CHECK_EQ(second->getCount(), first->getCount());
    int townCount = 0;
    for (int i = 0; i < first->getCount(); ++i) {
        CHECK_EQ(first->getStringAttribute(i, "asset", ""), second->getStringAttribute(i, "asset", ""));
        CHECK_EQ(first->getYaw(i), second->getYaw(i));
        CHECK_EQ(first->getScaleX(i), second->getScaleX(i));
        if (first->getStringAttribute(i, "biome", "") == "town") {
            ++townCount;
            CHECK_EQ(first->getStringAttribute(i, "asset", ""), std::string("lamp"));
            CHECK_EQ(first->getYaw(i), 0.f);
        }
    }
    CHECK_EQ(townCount, 1);
    CHECK(rules.debugReport().find("output=8") != std::string::npos);

    delete second;
    delete first;
}

TEST_CASE("procgen.biome.validatesRulesAndEmptyDensity") {
    BiomeRules  rules;
    SpatialData domain = SpatialData::box(0.f, 0.f, 0.f, 2.f, 0.f, 2.f);
    CHECK(!rules.generate(&domain, 1.f, 1, 0.f));
    CHECK(rules.getError().find("no biome layers") != std::string::npos);
    CHECK(rules.addLayer("empty", &domain, 0, 0.f));
    CHECK(!rules.addAsset("missing", "asset", 1.f, 1.f, 1.f, true));
    CHECK(rules.addAsset("empty", "asset", 1.f, 1.f, 1.f, true));
    PointSet* result = rules.generate(&domain, 1.f, 1, 0.f);
    REQUIRE(bool(result));
    CHECK_EQ(result->getCount(), 0);
    delete result;
}

// procgen_build_layer_stack.cpp
using namespace eve::procgen;

TEST_CASE("procgen.buildLayers.executesOrderedTileAndObjectDefinitions") {
    Grid2D grid;
    grid.resize(2, 1);
    grid.setCell(0, 0, int(Semantic::Floor));
    grid.setCell(1, 0, int(Semantic::Floor));

    PointSet points;
    points.add(2.f, 0.f, 3.f);
    ObjectBuildLayer objects;
    REQUIRE(objects.addAsset("props/crate", 1.f).ok());
    objects.setSeed(15);

    BuildLayerStack stack;
    REQUIRE(stack.addTileLayer("floor", true, 2.f, 0.5f, "floor").ok());
    REQUIRE(stack.addObjectLayer("props", true, objects).ok());
    REQUIRE(stack.addTileLayer("disabled", false, 1.f, 1.f, "disabled").ok());
    CHECK_EQ(stack.getLayerCount(), 3);
    CHECK_EQ(stack.getLayerId(1), "props");
    CHECK_EQ(stack.getLayerType(0), "tiles");
    CHECK(!stack.isLayerEnabled(2));

    auto result = stack.execute(grid, points);
    REQUIRE(result.ok());
    CHECK_EQ(result.value().getCount(), 2);
    CHECK_EQ(result.value().getId(0), "floor");
    CHECK_EQ(result.value().getType(0), "mesh");
    CHECK_EQ(result.value().getType(1), "points");
    auto mesh = result.value().getMesh(0);
    REQUIRE(mesh.ok());
    CHECK_EQ(mesh.value().getIndexCount(), 48);
    auto built = result.value().getPoints(1);
    REQUIRE(built.ok());
    CHECK_EQ(built.value().getStringAttribute(0, "asset", ""), "props/crate");
    CHECK(!result.value().getPoints(0).ok());
    CHECK(!result.value().getMesh(1).ok());
}

TEST_CASE("procgen.buildLayers.roundTripsAndRejectsDamageAtomically") {
    ObjectBuildLayer objects;
    REQUIRE(objects.addAsset("props/a", 2.f).ok());
    REQUIRE(objects.addAsset("props/b", 1.f).ok());
    REQUIRE(objects.setPositionRadius(0.4f).ok());
    REQUIRE(objects.addChild("props/child", 2, 1.f, 0.5f, 1.5f, -20.f, 20.f).ok());
    const auto       objectDefinition = objects.serializeDefinition();
    ObjectBuildLayer restoredObject;
    REQUIRE(restoredObject.deserializeDefinition(objectDefinition).ok());
    CHECK_EQ(restoredObject.serializeDefinition(), objectDefinition);
    CHECK(!restoredObject.deserializeDefinition("EVPCG_OBJECT_LAYER 1\nUNKNOWN 1\nEND\n").ok());
    CHECK_EQ(restoredObject.serializeDefinition(), objectDefinition);

    BuildLayerStack stack;
    REQUIRE(stack.addObjectLayer("objects", true, objects).ok());
    REQUIRE(stack.addTileLayer("tiles", false, 1.25f, 0.3f, "terrain main").ok());
    const auto      definition = stack.serializeDefinition();
    BuildLayerStack restored;
    REQUIRE(restored.deserializeDefinition(definition).ok());
    CHECK_EQ(restored.serializeDefinition(), definition);
    CHECK(!restored.deserializeDefinition("EVPCG_BUILD_LAYERS 1\nOBJECT \"bad\" 1 xyz\nEND\n").ok());
    CHECK_EQ(restored.serializeDefinition(), definition);
    CHECK(!restored.addTileLayer("objects", true, 1.f, 0.f, "duplicate").ok());
    CHECK(!restored.setEnabled("missing", true).ok());
}

// procgen_bush_winding.cpp
using eve::procgen::MeshBuild;
using eve::procgen::Params;

namespace {

struct Vec3 {
    float x;
    float y;
    float z;
};

Vec3 position(const MeshBuild& mesh, int index) {
    return {mesh.getPositionX(index), mesh.getPositionY(index), mesh.getPositionZ(index)};
}

Vec3 normal(const MeshBuild& mesh, int index) {
    return {mesh.getNormalX(index), mesh.getNormalY(index), mesh.getNormalZ(index)};
}

Vec3 subtract(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }

Vec3 cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }

float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

}  // namespace

TEST_CASE("procgen.mesh.bush.outwardFoliageWinding") {
    Params params;
    params.setSeed(20260815u);
    params.setString("style", "mound");
    params.setString("leafMode", "blobs");
    params.setInt("blobs", 3);
    params.setInt("rings", 5);
    params.setInt("radialSegments", 12);
    params.setInt("twigs", 0);

    MeshBuild   mesh;
    std::string error;
    REQUIRE(eve::procgen::generateBushMesh(params, mesh, error));

    int checkedTriangles = 0;
    for (int i = 0; i < mesh.getIndexCount(); i += 3) {
        const int  ia = mesh.getIndex(i);
        const int  ib = mesh.getIndex(i + 1);
        const int  ic = mesh.getIndex(i + 2);
        const Vec3 face =
            cross(subtract(position(mesh, ib), position(mesh, ia)), subtract(position(mesh, ic), position(mesh, ia)));
        if (dot(face, face) < 1e-12f) continue;
        const Vec3 averagedNormal{
            normal(mesh, ia).x + normal(mesh, ib).x + normal(mesh, ic).x,
            normal(mesh, ia).y + normal(mesh, ib).y + normal(mesh, ic).y,
            normal(mesh, ia).z + normal(mesh, ib).z + normal(mesh, ic).z,
        };
        CHECK(dot(face, averagedNormal) > 0.f);
        ++checkedTriangles;
    }
    CHECK(checkedTriangles > 0);
}

// procgen_cable_rope.cpp
// Cable / chain / hemp-rope mesh + texture recipe coverage (process-isolated).


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
        if (!std::isfinite(m.getPositionX(i)) || !std::isfinite(m.getPositionY(i)) || !std::isfinite(m.getPositionZ(i)))
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

    MeshBuild   a, b, scaled;
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
    MeshBuild   a, b;
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

// procgen_cave.cpp
TEST_CASE("procgen.mesh.cave.correlatedReactivePatchesAreCoherentSeededAndSurfaceBound") {
    constexpr int                  size = 21;
    std::vector<float>             first(size_t(size * size * size));
    std::vector<float>             rates(first.size(), 1.f);
    std::vector<CaveHydrologyVec3> flow(first.size(), {1.f, 0.f, 0.f});
    for (int z = 0; z < size; ++z) {
        for (int y = 0; y < size; ++y) {
            for (int x = 0; x < size; ++x) {
                const float px                                = float(x) / float(size - 1) * 2.f - 1.f;
                first[size_t(x + y * size + z * size * size)] = px;
            }
        }
    }
    std::vector<float> repeated  = first;
    std::vector<float> otherSeed = first;
    const auto result = evolveCaveSurfaceByCorrelatedReactivity(first, rates, flow, size, size, size, 1.f, 20260830u);
    const auto repeatedResult =
        evolveCaveSurfaceByCorrelatedReactivity(repeated, rates, flow, size, size, size, 1.f, 20260830u);
    const auto otherResult =
        evolveCaveSurfaceByCorrelatedReactivity(otherSeed, rates, flow, size, size, size, 1.f, 20260831u);
    CHECK(first == repeated);
    CHECK(first != otherSeed);
    CHECK_EQ(result.totalRetreat, repeatedResult.totalRetreat);
    CHECK(result.affectedVoxels > 0);
    CHECK(otherResult.affectedVoxels > 0);
    CHECK(result.minimumPatchRate < 1.f);
    CHECK(result.maximumPatchRate > 1.f);
    CHECK(result.meanNeighborCoherence > 0.8f);
    CHECK(result.meanFlowCoherence > result.meanTransverseCoherence);
    CHECK(result.channelAnisotropy > 1.5f);
    CHECK_EQ(first.front(), -1.f);
    CHECK_EQ(first.back(), 1.f);
}

TEST_CASE("procgen.mesh.cave.reactivePatchinessIsObservableAndDefaultCompatible") {
    MeshRecipeRegistry::instance().registerBuiltins();
    Params params;
    params.setSeed(20260830);
    params.setString("style", "mixed");
    params.setInt("resolution", 24);
    params.setInt("chambers", 5);
    params.setInt("branches", 3);
    params.setInt("dripstones", 0);
    params.setInt("flowstones", 0);
    params.setInt("curtains", 0);
    MeshBuild   omitted, disabled, patched, repeated;
    std::string error;
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, omitted, error));
    params.setFloat("reactivePatchiness", 0.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, disabled, error));
    REQUIRE(omitted.positions() == disabled.positions());
    REQUIRE(omitted.indices() == disabled.indices());

    params.setFloat("reactivePatchiness", 1.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, patched, error));
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, repeated, error));
    REQUIRE(disabled.positions() != patched.positions());
    REQUIRE(patched.positions() == repeated.positions());
    REQUIRE(patched.indices() == repeated.indices());
    REQUIRE_EQ(patched.getMeta("reactivePatchModel", ""), std::string("flow-aligned-correlated-psd-v2"));
    REQUIRE(std::stoi(patched.getMeta("reactivePatchAffectedVoxels", "0")) > 0);
    REQUIRE(std::stof(patched.getMeta("maximumReactivePatchRate", "0")) > 1.f);
    REQUIRE(std::stof(patched.getMeta("minimumReactivePatchRate", "1")) < 1.f);
    REQUIRE(std::stof(patched.getMeta("reactivePatchNeighborCoherence", "0")) > 0.8f);
    REQUIRE(std::stof(patched.getMeta("reactivePatchFlowCoherence", "0")) >
            std::stof(patched.getMeta("reactivePatchTransverseCoherence", "1")));
    REQUIRE(std::stof(patched.getMeta("reactivePatchChannelAnisotropy", "0")) > 1.f);
    params.setFloat("reactivePatchiness", 1.1f);
    REQUIRE(!MeshRecipeRegistry::instance().generate("mesh.cave", params, repeated, error));
}

TEST_CASE("procgen.mesh.cave.surfaceReactivityDistinguishesEdgesFromPlanesRotationInvariantly") {
    constexpr int      size = 15;
    std::vector<float> plane(size_t(size * size * size));
    std::vector<float> corner(size_t(size * size * size));
    std::vector<float> rotatedCorner(size_t(size * size * size));
    std::vector<float> rates(plane.size(), 1.f);
    for (int z = 0; z < size; ++z) {
        for (int y = 0; y < size; ++y) {
            for (int x = 0; x < size; ++x) {
                const float  px      = float(x) / float(size - 1) * 2.f - 1.f;
                const float  py      = float(y) / float(size - 1) * 2.f - 1.f;
                const float  pz      = float(z) / float(size - 1) * 2.f - 1.f;
                const size_t index   = size_t(x + y * size + z * size * size);
                plane[index]         = px;
                corner[index]        = std::max(px, py);
                rotatedCorner[index] = std::max((px + py) * 0.70710678f, pz);
            }
        }
    }
    std::vector<float> repeated       = corner;
    const auto         planeResult    = evolveCaveSurfaceByReactivity(plane, rates, size, size, size, 1.f);
    const auto         cornerResult   = evolveCaveSurfaceByReactivity(corner, rates, size, size, size, 1.f);
    const auto         repeatedResult = evolveCaveSurfaceByReactivity(repeated, rates, size, size, size, 1.f);
    const auto         rotatedResult  = evolveCaveSurfaceByReactivity(rotatedCorner, rates, size, size, size, 1.f);
    CHECK_EQ(planeResult.affectedVoxels, 0);
    CHECK(cornerResult.affectedVoxels > 0);
    CHECK(rotatedResult.affectedVoxels > 0);
    CHECK(cornerResult.maximumNormalDispersion > 0.2f);
    CHECK(rotatedResult.maximumNormalDispersion > 0.2f);
    CHECK(corner == repeated);
    CHECK_EQ(cornerResult.affectedVoxels, repeatedResult.affectedVoxels);
    CHECK_EQ(cornerResult.totalRetreat, repeatedResult.totalRetreat);
}

TEST_CASE("procgen.mesh.cave.surfaceSlopeReactivityIsObservableAndDefaultCompatible") {
    MeshRecipeRegistry::instance().registerBuiltins();
    Params params;
    params.setSeed(20260830);
    params.setString("style", "mixed");
    params.setInt("resolution", 24);
    params.setInt("chambers", 6);
    params.setInt("branches", 3);
    params.setInt("dripstones", 0);
    params.setInt("flowstones", 0);
    params.setInt("curtains", 0);
    params.setFloat("roughness", 0.18f);
    params.setFloat("multiscaleRoughness", 1.f);
    MeshBuild   omitted, disabled, reactive, repeated;
    std::string error;
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, omitted, error));
    params.setFloat("surfaceSlopeReactivity", 0.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, disabled, error));
    REQUIRE(omitted.positions() == disabled.positions());
    REQUIRE(omitted.indices() == disabled.indices());

    params.setFloat("surfaceSlopeReactivity", 1.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, reactive, error));
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, repeated, error));
    REQUIRE(disabled.positions() != reactive.positions());
    REQUIRE(reactive.positions() == repeated.positions());
    REQUIRE(reactive.indices() == repeated.indices());
    REQUIRE_EQ(reactive.getMeta("surfaceReactivityModel", ""), std::string("rotation-invariant-normal-dispersion-v1"));
    REQUIRE(std::stoi(reactive.getMeta("surfaceReactivityAffectedVoxels", "0")) > 0);
    REQUIRE(std::stof(reactive.getMeta("maximumSurfaceNormalDispersion", "0")) > 0.f);
    REQUIRE(std::stof(reactive.getMeta("maximumSurfaceReactivityRetreat", "0")) > 0.f);
    params.setFloat("surfaceSlopeReactivity", 1.1f);
    REQUIRE(!MeshRecipeRegistry::instance().generate("mesh.cave", params, repeated, error));
}

TEST_CASE("procgen.mesh.cave.roughnessTransferExposesRidgesAndSheltersRecesses") {
    const CaveRoughnessTransferSample disabled = sampleCaveRoughnessTransfer({0.8f, 1.f, 0.f});
    CHECK_EQ(disabled.massTransferMultiplier, 1.f);

    const CaveRoughnessTransferSample ridge         = sampleCaveRoughnessTransfer({0.8f, 1.f, 1.f});
    const CaveRoughnessTransferSample recess        = sampleCaveRoughnessTransfer({-0.8f, 1.f, 1.f});
    const CaveRoughnessTransferSample flushedRecess = sampleCaveRoughnessTransfer({-0.8f, 2.f, 1.f});
    CHECK(ridge.massTransferMultiplier > 1.f);
    CHECK(recess.massTransferMultiplier < 1.f);
    CHECK(flushedRecess.massTransferMultiplier > recess.massTransferMultiplier);
    CHECK(ridge.ridgeExposure > 0.f);
    CHECK(recess.recessShelter > 0.f);
    CHECK(ridge.massTransferMultiplier <= 1.45f);
    CHECK(recess.massTransferMultiplier >= 0.60f);
    CHECK_EQ(ridge.massTransferMultiplier, sampleCaveRoughnessTransfer({0.8f, 1.f, 1.f}).massTransferMultiplier);
}

TEST_CASE("procgen.mesh.cave.roughnessFlowCouplingIsObservableAndDefaultCompatible") {
    MeshRecipeRegistry::instance().registerBuiltins();
    Params params;
    params.setSeed(20260830);
    params.setString("style", "tunnels");
    params.setInt("resolution", 24);
    params.setInt("chambers", 5);
    params.setInt("branches", 3);
    params.setInt("dripstones", 0);
    params.setInt("flowstones", 0);
    params.setInt("curtains", 0);
    params.setFloat("roughness", 0.16f);
    params.setFloat("multiscaleRoughness", 1.f);
    params.setFloat("erosion", 1.f);
    MeshBuild   omitted, disabled, coupled, repeated;
    std::string error;
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, omitted, error));
    params.setFloat("roughnessFlowCoupling", 0.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, disabled, error));
    REQUIRE(omitted.positions() == disabled.positions());
    REQUIRE(omitted.indices() == disabled.indices());

    params.setFloat("roughnessFlowCoupling", 1.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, coupled, error));
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, repeated, error));
    REQUIRE(disabled.positions() != coupled.positions());
    REQUIRE(coupled.positions() == repeated.positions());
    REQUIRE(coupled.indices() == repeated.indices());
    REQUIRE_EQ(coupled.getMeta("roughnessMassTransferModel", ""), std::string("ridge-exposure-recess-shelter-v1"));
    REQUIRE(std::stoi(coupled.getMeta("roughnessTransferAffectedVoxels", "0")) > 0);
    REQUIRE(std::stof(coupled.getMeta("minimumRoughnessTransferMultiplier", "1")) < 1.f);
    REQUIRE(std::stof(coupled.getMeta("maximumRoughnessTransferMultiplier", "1")) > 1.f);

    params.setFloat("multiscaleRoughness", 0.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, repeated, error));
    REQUIRE_EQ(repeated.getMeta("roughnessMassTransferModel", ""), std::string("inactive-no-resolved-relief"));
    params.setFloat("roughnessFlowCoupling", 1.1f);
    REQUIRE(!MeshRecipeRegistry::instance().generate("mesh.cave", params, repeated, error));
}

TEST_CASE("procgen.mesh.cave.scallopHistoryPreservesOlderReliefAndAddsPartitionedYoungerFlow") {
    CaveScallopInput base;
    base.along              = 0.42f;
    base.angle              = 0.65f;
    base.distance           = 0.17f;
    base.radius             = 0.18f;
    base.hydraulicIntensity = 1.2f;
    base.baseScale          = 0.12f;
    base.flowSeparation     = 0.8f;
    base.seed               = 20260830u;

    const CaveScallopSample        legacy   = sampleCaveScallops(base);
    const CaveScallopHistorySample disabled = sampleCaveScallopHistory({base, 0.f});
    CHECK_EQ(disabled.erosion, legacy.erosion);
    CHECK_EQ(disabled.youngerErosion, 0.f);

    const CaveScallopHistorySample staged = sampleCaveScallopHistory({base, 1.f});
    CHECK(staged.erosion >= legacy.erosion);
    CHECK(staged.youngerCoverage > 0.f);
    CHECK(staged.youngerErosion > 0.f);
    CHECK(staged.secondaryScaleRatio < 1.f);
    CHECK_EQ(staged.erosion, sampleCaveScallopHistory({base, 1.f}).erosion);

    float minimumReversal = 1.f;
    float maximumReversal = 0.f;
    for (int i = 0; i <= 20; ++i) {
        base.along                           = float(i) * 0.08f;
        const CaveScallopHistorySample reach = sampleCaveScallopHistory({base, 1.f});
        minimumReversal                      = std::min(minimumReversal, reach.reversalMask);
        maximumReversal                      = std::max(maximumReversal, reach.reversalMask);
    }
    CHECK(maximumReversal - minimumReversal > 0.35f);
}

TEST_CASE("procgen.mesh.cave.scallopFlowHistoryIsObservableAndDefaultCompatible") {
    MeshRecipeRegistry::instance().registerBuiltins();
    Params params;
    params.setSeed(20260830);
    params.setString("style", "tunnels");
    params.setInt("resolution", 24);
    params.setInt("chambers", 5);
    params.setInt("branches", 3);
    params.setInt("dripstones", 0);
    params.setInt("flowstones", 0);
    params.setInt("curtains", 0);
    params.setFloat("roughness", 0.f);
    params.setFloat("erosion", 1.f);
    params.setFloat("bedding", 0.f);
    params.setFloat("fractureDissolution", 0.f);
    params.setFloat("scallopErosion", 1.f);
    params.setFloat("scallopFlowSeparation", 0.8f);

    MeshBuild   omitted, explicitLegacy, staged, repeated;
    std::string error;
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, omitted, error));
    params.setFloat("scallopFlowHistory", 0.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, explicitLegacy, error));
    REQUIRE(omitted.positions() == explicitLegacy.positions());
    REQUIRE(omitted.indices() == explicitLegacy.indices());

    params.setFloat("scallopFlowHistory", 0.85f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, staged, error));
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, repeated, error));
    REQUIRE(staged.positions() != explicitLegacy.positions());
    REQUIRE(staged.positions() == repeated.positions());
    REQUIRE(staged.indices() == repeated.indices());
    REQUIRE_EQ(staged.getMeta("scallopFlowHistoryModel", ""),
               std::string("partitioned-base-flood-reversal-overprint-v1"));
    REQUIRE(std::stoi(staged.getMeta("scallopHistoryAffectedVoxels", "0")) > 0);
    REQUIRE(std::stof(staged.getMeta("maximumYoungerScallopErosion", "0")) > 0.f);
    REQUIRE(std::stof(staged.getMeta("minimumSecondaryScallopScaleRatio", "1")) < 1.f);

    params.setFloat("scallopFlowHistory", 1.1f);
    REQUIRE(!MeshRecipeRegistry::instance().generate("mesh.cave", params, repeated, error));
}

TEST_CASE("procgen.mesh.cave.secondaryMineralArmoringIsPatchyProtectiveAndHydraulicallyStripped") {
    CaveMineralArmoringInput input;
    input.passageAlong       = 0.31f;
    input.passageAngle       = 0.72f;
    input.hydraulicIntensity = 0.35f;
    input.mineralSupply      = 1.f;
    input.seed               = 20260830u;

    const CaveMineralArmoringSample sheltered = sampleCaveMineralArmoring(input);
    CHECK(sheltered.coatingCoverage > 0.f);
    CHECK(sheltered.hydraulicRetention > 0.f);
    CHECK(sheltered.dissolutionRetention < 1.f);
    CHECK_EQ(sheltered.dissolutionRetention, sampleCaveMineralArmoring(input).dissolutionRetention);

    input.hydraulicIntensity                = 1.4f;
    const CaveMineralArmoringSample flushed = sampleCaveMineralArmoring(input);
    CHECK_EQ(flushed.coatingCoverage, sheltered.coatingCoverage);
    CHECK(flushed.hydraulicRetention < sheltered.hydraulicRetention);
    CHECK(flushed.dissolutionRetention > sheltered.dissolutionRetention);

    input.mineralSupply                    = 0.f;
    const CaveMineralArmoringSample absent = sampleCaveMineralArmoring(input);
    CHECK_EQ(absent.coatingCoverage, 0.f);
    CHECK_EQ(absent.dissolutionRetention, 1.f);
}

TEST_CASE("procgen.mesh.cave.mineralArmoringIsObservableAndDefaultCompatible") {
    MeshRecipeRegistry::instance().registerBuiltins();
    Params params;
    params.setSeed(20260830);
    params.setString("style", "tunnels");
    params.setString("genesis", "mixed");
    params.setInt("resolution", 24);
    params.setInt("chambers", 5);
    params.setInt("branches", 3);
    params.setInt("dripstones", 0);
    params.setInt("flowstones", 0);
    params.setInt("curtains", 0);
    params.setFloat("roughness", 0.f);
    params.setFloat("erosion", 1.f);
    params.setFloat("bedding", 0.7f);
    params.setFloat("fractureDissolution", 0.8f);

    MeshBuild   omitted, explicitLegacy, armored, repeated, noChemicalLegacy, inactive;
    std::string error;
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, omitted, error));
    params.setFloat("mineralArmoring", 0.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, explicitLegacy, error));
    REQUIRE(omitted.positions() == explicitLegacy.positions());
    REQUIRE(omitted.indices() == explicitLegacy.indices());

    params.setFloat("mineralArmoring", 1.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, armored, error));
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, repeated, error));
    REQUIRE(armored.positions() != explicitLegacy.positions());
    REQUIRE(armored.positions() == repeated.positions());
    REQUIRE(armored.indices() == repeated.indices());
    REQUIRE_EQ(armored.getMeta("mineralArmoringModel", ""),
               std::string("genesis-supplied-hydraulic-stripping-shield-v1"));
    REQUIRE(std::stoi(armored.getMeta("mineralArmoringAffectedVoxels", "0")) > 0);
    REQUIRE(std::stof(armored.getMeta("maximumMineralCoatingCoverage", "0")) > 0.f);
    REQUIRE(std::stof(armored.getMeta("minimumArmoredDissolutionRetention", "1")) < 1.f);

    params.setFloat("erosion", 0.f);
    params.setFloat("mineralArmoring", 0.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, noChemicalLegacy, error));
    params.setFloat("mineralArmoring", 1.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, inactive, error));
    REQUIRE(noChemicalLegacy.positions() == inactive.positions());
    REQUIRE(noChemicalLegacy.indices() == inactive.indices());
    REQUIRE_EQ(inactive.getMeta("mineralArmoringModel", ""), std::string("inactive-no-chemical-retreat"));

    params.setFloat("mineralArmoring", 1.1f);
    REQUIRE(!MeshRecipeRegistry::instance().generate("mesh.cave", params, repeated, error));
}

TEST_CASE("procgen.mesh.cave.breakdownBlocksDriveUpstreamHorseshoeAndDownstreamWakeScour") {
    CaveBreakdownSet   breakdown;
    CaveBreakdownEvent event;
    event.blocks.push_back({0.f, -0.15f, 0.f, 0.07f, 0.05f, 0.06f, 0.f});
    breakdown.events.push_back(event);
    breakdown.blockCount = 1;
    const std::vector<CaveHydrologyPoint>    trunk{{{-0.5f, 0.f, 0.f}, 0.2f}, {{0.5f, 0.f, 0.f}, 0.2f}};
    const std::vector<CaveObstacleScourSite> smooth =
        createCaveObstacleScourSites(breakdown, trunk, {1.2f}, 0.48f, 0.f);
    const std::vector<CaveObstacleScourSite> rough = createCaveObstacleScourSites(breakdown, trunk, {1.2f}, 0.48f, 1.f);
    REQUIRE_EQ(smooth.size(), size_t(1));
    REQUIRE_EQ(rough.size(), size_t(1));
    CHECK(smooth[0].frontCenter.x < smooth[0].blockCenter.x);
    CHECK(smooth[0].wakeCenter.x > smooth[0].blockCenter.x);
    CHECK(smooth[0].erosionPotential > rough[0].erosionPotential);

    CaveHydrologyVec3 frontLobe = smooth[0].frontCenter;
    frontLobe.z += smooth[0].frontLateralRadius * 0.42f;
    const CaveObstacleScourSample front = sampleCaveObstacleScour(frontLobe, smooth);
    const CaveObstacleScourSample wake  = sampleCaveObstacleScour(smooth[0].wakeCenter, smooth);
    CHECK(front.horseshoeScour > 0.f);
    CHECK(wake.wakeScour > 0.f);
    CHECK(front.horseshoeScour > wake.wakeScour);
    CHECK_EQ(front.erosion, sampleCaveObstacleScour(frontLobe, smooth).erosion);

    CHECK(createCaveObstacleScourSites({}, trunk, {1.2f}, 0.48f, 0.f).empty());
    CHECK(createCaveObstacleScourSites(breakdown, trunk, {1.2f}, 0.f, 0.f).empty());
}

TEST_CASE("procgen.mesh.cave.breakdownScourIsObservableAndDefaultCompatible") {
    MeshRecipeRegistry::instance().registerBuiltins();
    Params params;
    params.setSeed(9);  // Produces a mesh-visible effect at resolution 24.
    params.setString("style", "mixed");
    params.setString("genesis", "epigene");
    params.setInt("resolution", 24);
    params.setInt("chambers", 6);
    params.setInt("branches", 5);
    params.setInt("dripstones", 0);
    params.setInt("flowstones", 0);
    params.setInt("curtains", 0);
    params.setFloat("roughness", 0.f);
    params.setFloat("erosion", 0.f);
    params.setFloat("vadoseIncision", 0.f);
    params.setFloat("breakdown", 1.f);
    params.setInt("breakdownEvents", 5);
    params.setFloat("sedimentLoad", 0.48f);

    MeshBuild   omitted, explicitLegacy, eroded, repeated, hypogeneOff, hypogeneOn, noBlocksOff, noBlocksOn;
    std::string error;
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, omitted, error));
    params.setFloat("breakdownScour", 0.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, explicitLegacy, error));
    REQUIRE(omitted.positions() == explicitLegacy.positions());
    REQUIRE(omitted.indices() == explicitLegacy.indices());

    params.setFloat("breakdownScour", 1.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, eroded, error));
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, repeated, error));
    REQUIRE(eroded.positions() != explicitLegacy.positions());
    REQUIRE(eroded.positions() == repeated.positions());
    REQUIRE(eroded.indices() == repeated.indices());
    REQUIRE_EQ(eroded.getMeta("breakdownScourModel", ""), std::string("roughness-damped-horseshoe-wake-scour-v1"));
    REQUIRE(std::stoi(eroded.getMeta("breakdownScourSites", "0")) > 0);
    REQUIRE(std::stoi(eroded.getMeta("breakdownScourAffectedVoxels", "0")) > 0);
    REQUIRE(std::stof(eroded.getMeta("maximumBreakdownScourRetreat", "0")) > 0.f);
    REQUIRE(std::stof(eroded.getMeta("maximumHorseshoeScour", "0")) > 0.f);
    REQUIRE(std::stof(eroded.getMeta("maximumWakeScour", "0")) > 0.f);

    params.setString("genesis", "hypogene");
    params.setFloat("breakdownScour", 0.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, hypogeneOff, error));
    params.setFloat("breakdownScour", 1.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, hypogeneOn, error));
    REQUIRE(hypogeneOff.positions() == hypogeneOn.positions());
    REQUIRE(hypogeneOff.indices() == hypogeneOn.indices());
    REQUIRE_EQ(hypogeneOn.getMeta("breakdownScourModel", ""), std::string("inactive-hypogene"));

    params.setString("genesis", "epigene");
    params.setFloat("breakdown", 0.f);
    params.setFloat("breakdownScour", 0.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, noBlocksOff, error));
    params.setFloat("breakdownScour", 1.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, noBlocksOn, error));
    REQUIRE(noBlocksOff.positions() == noBlocksOn.positions());
    REQUIRE(noBlocksOff.indices() == noBlocksOn.indices());
    REQUIRE_EQ(noBlocksOn.getMeta("breakdownScourModel", ""), std::string("inactive-no-breakdown-blocks"));

    params.setFloat("breakdownScour", 1.1f);
    MeshBuild invalid;
    REQUIRE(!MeshRecipeRegistry::instance().generate("mesh.cave", params, invalid, error));
}

TEST_CASE("procgen.mesh.cave.eddyPotholesRequireCrossingFracturesAndResolveGravelBias") {
    const std::vector<CaveHydrologyPoint> trunk{{{-0.45f, 0.f, 0.f}, 0.2f}, {{0.45f, 0.f, 0.f}, 0.2f}};
    const std::vector<float>              hydraulic{1.2f};
    const std::vector<CaveFracture>       fractures{{1.f, 0.f, 0.f, 0.04f}, {0.f, 1.f, 0.f, 0.04f}};
    const std::vector<CavePotholeSite>    fine =
        createCavePotholeSites(trunk, hydraulic, fractures, 0.f, 0.f, 0.48f, 0.f, 20260830u);
    const std::vector<CavePotholeSite> coarse =
        createCavePotholeSites(trunk, hydraulic, fractures, 0.f, 0.f, 0.48f, 1.f, 20260830u);
    REQUIRE_EQ(fine.size(), size_t(1));
    REQUIRE_EQ(coarse.size(), size_t(1));
    CHECK(fine[0].center.y < 0.f);
    CHECK(fine[0].fractureIntersection > 0.f);

    CaveHydrologyVec3 downstream = fine[0].center;
    downstream.x += fine[0].alongRadius * 0.22f;
    const CavePotholeSample fineDownstream   = sampleCavePotholeErosion(downstream, fine);
    const CavePotholeSample coarseDownstream = sampleCavePotholeErosion(downstream, coarse);
    CHECK(fineDownstream.erosion > coarseDownstream.erosion);
    CHECK(fineDownstream.downstreamBias > 0.f);
    CHECK(coarseDownstream.downstreamBias < 0.f);

    CaveHydrologyVec3 deep = fine[0].center;
    deep.y -= fine[0].depthRadius * 0.72f;
    CHECK(sampleCavePotholeErosion(deep, fine).secondaryPothole > 0.f);
    CHECK(createCavePotholeSites(trunk, hydraulic, {fractures[0]}, 0.f, 0.f, 0.48f, 0.5f, 20260830u).empty());
    CHECK(createCavePotholeSites(trunk, hydraulic, fractures, 0.f, 0.f, 0.f, 0.5f, 20260830u).empty());
}

TEST_CASE("procgen.mesh.cave.eddyPotholesAreObservableAndDefaultCompatible") {
    MeshRecipeRegistry::instance().registerBuiltins();
    Params params;
    params.setSeed(1);  // Produces a mesh-visible effect at resolution 24.
    params.setString("style", "tunnels");
    params.setString("genesis", "epigene");
    params.setInt("resolution", 24);
    params.setInt("chambers", 5);
    params.setInt("branches", 5);
    params.setInt("fractureCount", 5);
    params.setInt("dripstones", 0);
    params.setInt("flowstones", 0);
    params.setInt("curtains", 0);
    params.setFloat("roughness", 0.f);
    params.setFloat("erosion", 0.f);
    params.setFloat("vadoseIncision", 0.f);
    params.setFloat("sedimentLoad", 0.48f);

    MeshBuild   omitted, explicitLegacy, eroded, repeated, hypogeneOff, hypogeneOn, noToolsOff, noToolsOn;
    std::string error;
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, omitted, error));
    params.setFloat("eddyPotholes", 0.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, explicitLegacy, error));
    REQUIRE(omitted.positions() == explicitLegacy.positions());
    REQUIRE(omitted.indices() == explicitLegacy.indices());

    params.setFloat("eddyPotholes", 1.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, eroded, error));
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, repeated, error));
    REQUIRE(eroded.positions() != explicitLegacy.positions());
    REQUIRE(eroded.positions() == repeated.positions());
    REQUIRE(eroded.indices() == repeated.indices());
    REQUIRE_EQ(eroded.getMeta("eddyPotholeModel", ""), std::string("gravel-size-dependent-compound-eddy-pothole-v1"));
    REQUIRE(std::stoi(eroded.getMeta("eddyPotholeSites", "0")) > 0);
    REQUIRE(std::stoi(eroded.getMeta("eddyPotholeAffectedVoxels", "0")) > 0);
    REQUIRE(std::stof(eroded.getMeta("maximumPotholeRetreat", "0")) > 0.f);
    REQUIRE(std::stof(eroded.getMeta("maximumPotholeSecondaryErosion", "0")) > 0.f);
    REQUIRE(std::stof(eroded.getMeta("maximumPotholeFractureIntersection", "0")) > 0.f);

    params.setString("genesis", "hypogene");
    params.setFloat("eddyPotholes", 0.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, hypogeneOff, error));
    params.setFloat("eddyPotholes", 1.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, hypogeneOn, error));
    REQUIRE(hypogeneOff.positions() == hypogeneOn.positions());
    REQUIRE(hypogeneOff.indices() == hypogeneOn.indices());
    REQUIRE_EQ(hypogeneOn.getMeta("eddyPotholeModel", ""), std::string("inactive-hypogene"));

    params.setString("genesis", "epigene");
    params.setFloat("sedimentLoad", 0.f);
    params.setFloat("eddyPotholes", 0.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, noToolsOff, error));
    params.setFloat("eddyPotholes", 1.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, noToolsOn, error));
    REQUIRE(noToolsOff.positions() == noToolsOn.positions());
    REQUIRE(noToolsOff.indices() == noToolsOn.indices());
    REQUIRE_EQ(noToolsOn.getMeta("eddyPotholeModel", ""), std::string("inactive-no-tools"));

    params.setFloat("eddyPotholes", 1.1f);
    MeshBuild invalid;
    REQUIRE(!MeshRecipeRegistry::instance().generate("mesh.cave", params, invalid, error));
}

TEST_CASE("procgen.mesh.cave.streamBedKarrenRequiresFloorFlowAndFractures") {
    const CaveKarrenSample floor    = sampleCaveStreamKarren({3.1415926535f, 1.2f, 0.9f, 0.65f});
    const CaveKarrenSample roof     = sampleCaveStreamKarren({0.f, 1.2f, 0.9f, 0.65f});
    const CaveKarrenSample dry      = sampleCaveStreamKarren({3.1415926535f, 0.2f, 0.9f, 0.65f});
    const CaveKarrenSample oneSet   = sampleCaveStreamKarren({3.1415926535f, 1.2f, 0.9f, 0.f});
    const CaveKarrenSample crossing = sampleCaveStreamKarren({3.1415926535f, 1.2f, 0.9f, 0.9f});
    CHECK(floor.erosion > 0.f);
    CHECK(floor.floorExposure > 0.99f);
    CHECK(roof.erosion == 0.f);
    CHECK(dry.erosion == 0.f);
    CHECK(crossing.erosion > oneSet.erosion);
    CHECK(crossing.intersectionPocket > 0.f);
    CHECK_EQ(floor.erosion, sampleCaveStreamKarren({3.1415926535f, 1.2f, 0.9f, 0.65f}).erosion);
}

TEST_CASE("procgen.mesh.cave.streamBedKarrenIsObservableAndDefaultCompatible") {
    MeshRecipeRegistry::instance().registerBuiltins();
    Params params;
    params.setSeed(20260830);
    params.setString("style", "tunnels");
    params.setString("genesis", "epigene");
    params.setInt("resolution", 24);
    params.setInt("chambers", 5);
    params.setInt("branches", 5);
    params.setInt("fractureCount", 5);
    params.setInt("dripstones", 0);
    params.setInt("flowstones", 0);
    params.setInt("curtains", 0);
    params.setFloat("roughness", 0.f);
    params.setFloat("erosion", 0.f);
    params.setFloat("vadoseIncision", 0.f);

    MeshBuild   omitted, explicitLegacy, eroded, repeated, hypogeneOff, hypogeneOn, noFracturesOff, noFracturesOn;
    std::string error;
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, omitted, error));
    params.setFloat("streamBedKarren", 0.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, explicitLegacy, error));
    REQUIRE(omitted.positions() == explicitLegacy.positions());
    REQUIRE(omitted.indices() == explicitLegacy.indices());

    params.setFloat("streamBedKarren", 1.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, eroded, error));
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, repeated, error));
    REQUIRE(eroded.positions() != explicitLegacy.positions());
    REQUIRE(eroded.positions() == repeated.positions());
    REQUIRE(eroded.indices() == repeated.indices());
    REQUIRE_EQ(eroded.getMeta("streamBedKarrenModel", ""),
               std::string("lidar-constrained-fracture-guided-bed-karren-v1"));
    REQUIRE(std::stoi(eroded.getMeta("streamBedKarrenAffectedVoxels", "0")) > 0);
    REQUIRE(std::stof(eroded.getMeta("maximumStreamBedKarrenRetreat", "0")) > 0.f);
    REQUIRE(std::stof(eroded.getMeta("maximumKarrenFractureGuidance", "0")) > 0.f);
    REQUIRE(std::stof(eroded.getMeta("maximumKarrenIntersectionPocket", "0")) > 0.f);

    params.setString("genesis", "hypogene");
    params.setFloat("streamBedKarren", 0.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, hypogeneOff, error));
    params.setFloat("streamBedKarren", 1.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, hypogeneOn, error));
    REQUIRE(hypogeneOff.positions() == hypogeneOn.positions());
    REQUIRE(hypogeneOff.indices() == hypogeneOn.indices());
    REQUIRE_EQ(hypogeneOn.getMeta("streamBedKarrenModel", ""), std::string("inactive-hypogene"));

    params.setString("genesis", "epigene");
    params.setInt("fractureCount", 1);
    params.setFloat("streamBedKarren", 0.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, noFracturesOff, error));
    params.setFloat("streamBedKarren", 1.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, noFracturesOn, error));
    REQUIRE(noFracturesOff.positions() == noFracturesOn.positions());
    REQUIRE(noFracturesOff.indices() == noFracturesOn.indices());
    REQUIRE_EQ(noFracturesOn.getMeta("streamBedKarrenModel", ""), std::string("inactive-no-crossing-fractures"));

    params.setFloat("streamBedKarren", 1.1f);
    MeshBuild invalid;
    REQUIRE(!MeshRecipeRegistry::instance().generate("mesh.cave", params, invalid, error));
}

TEST_CASE("procgen.mesh.cave.knickpointsFollowDownstreamSlopeBreaks") {
    const std::vector<CaveHydrologyPoint> trunk{{{-0.8f, 0.10f, 0.f}, 0.2f},
                                                {{-0.4f, 0.08f, 0.f}, 0.2f},
                                                {{0.f, 0.06f, 0.f}, 0.2f},
                                                {{0.4f, -0.20f, 0.f}, 0.2f},
                                                {{0.8f, -0.22f, 0.f}, 0.2f}};
    const std::vector<float>              hydraulic{0.5f, 0.7f, 1.1f, 1.2f, 1.2f};
    const std::vector<CaveKnickpointSite> sites = createCaveKnickpointSites(trunk, hydraulic, 0.48f);
    REQUIRE_EQ(sites.size(), size_t(1));
    CHECK(sites[0].poolCenter.x > sites[0].lip.x);
    CHECK(sites[0].poolCenter.y < sites[0].lip.y);
    CHECK(sites[0].slopeBreak > 0.4f);
    CHECK(sites[0].drop > 0.2f);

    const CaveKnickpointSample pool     = sampleCaveKnickpointErosion(sites[0].poolCenter, sites);
    const CaveKnickpointSample repeated = sampleCaveKnickpointErosion(sites[0].poolCenter, sites);
    const CaveKnickpointSample far      = sampleCaveKnickpointErosion({0.9f, 0.9f, 0.9f}, sites);
    CHECK_EQ(pool.erosion, repeated.erosion);
    CHECK(pool.verticalDrilling > pool.headwallUndercut);
    CHECK(pool.erosion > far.erosion);
    CHECK(createCaveKnickpointSites(trunk, hydraulic, 0.f).empty());

    std::vector<CaveHydrologyPoint> mild = trunk;
    for (size_t i = 0; i < mild.size(); ++i) mild[i].position.y = 0.1f - float(i) * 0.02f;
    CHECK(createCaveKnickpointSites(mild, hydraulic, 0.48f).empty());
    CHECK(createCaveKnickpointSites(trunk, hydraulic, 0.48f)[0].erosionPotential >
          createCaveKnickpointSites(trunk, hydraulic, 1.f)[0].erosionPotential);
}

TEST_CASE("procgen.mesh.cave.knickpointErosionIsObservableAndDefaultCompatible") {
    MeshRecipeRegistry::instance().registerBuiltins();
    Params params;
    params.setSeed(20260830);
    params.setString("style", "tunnels");
    params.setString("genesis", "epigene");
    params.setInt("resolution", 24);
    params.setInt("chambers", 5);
    params.setInt("branches", 5);
    params.setInt("dripstones", 0);
    params.setInt("flowstones", 0);
    params.setInt("curtains", 0);
    params.setFloat("roughness", 0.f);
    params.setFloat("erosion", 0.f);
    params.setFloat("vadoseIncision", 0.f);
    params.setFloat("sedimentLoad", 0.48f);

    MeshBuild   omitted, explicitLegacy, eroded, repeated, hypogeneOff, hypogeneOn, noToolsOff, noToolsOn;
    std::string error;
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, omitted, error));
    params.setFloat("knickpointErosion", 0.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, explicitLegacy, error));
    REQUIRE(omitted.positions() == explicitLegacy.positions());
    REQUIRE(omitted.indices() == explicitLegacy.indices());

    params.setFloat("knickpointErosion", 1.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, eroded, error));
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, repeated, error));
    REQUIRE(eroded.positions() != explicitLegacy.positions());
    REQUIRE(eroded.positions() == repeated.positions());
    REQUIRE(eroded.indices() == repeated.indices());
    REQUIRE_EQ(eroded.getMeta("knickpointErosionModel", ""), std::string("sediment-driven-headward-plunge-pool-v1"));
    REQUIRE(std::stoi(eroded.getMeta("knickpointSites", "0")) > 0);
    REQUIRE(std::stoi(eroded.getMeta("knickpointAffectedVoxels", "0")) > 0);
    REQUIRE(std::stof(eroded.getMeta("maximumKnickpointRetreat", "0")) > 0.f);
    REQUIRE(std::stof(eroded.getMeta("maximumKnickpointSlopeBreak", "0")) > 0.f);
    REQUIRE(std::stof(eroded.getMeta("maximumKnickpointDrop", "0")) > 0.f);

    params.setString("genesis", "hypogene");
    params.setFloat("knickpointErosion", 0.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, hypogeneOff, error));
    params.setFloat("knickpointErosion", 1.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, hypogeneOn, error));
    REQUIRE(hypogeneOff.positions() == hypogeneOn.positions());
    REQUIRE(hypogeneOff.indices() == hypogeneOn.indices());
    REQUIRE_EQ(hypogeneOn.getMeta("knickpointErosionModel", ""), std::string("inactive-hypogene"));

    params.setString("genesis", "epigene");
    params.setFloat("sedimentLoad", 0.f);
    params.setFloat("knickpointErosion", 0.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, noToolsOff, error));
    params.setFloat("knickpointErosion", 1.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, noToolsOn, error));
    REQUIRE(noToolsOff.positions() == noToolsOn.positions());
    REQUIRE(noToolsOff.indices() == noToolsOn.indices());
    REQUIRE_EQ(noToolsOn.getMeta("knickpointErosionModel", ""), std::string("inactive-no-tools"));

    params.setFloat("knickpointErosion", 1.1f);
    MeshBuild invalid;
    REQUIRE(!MeshRecipeRegistry::instance().generate("mesh.cave", params, invalid, error));
}

TEST_CASE("procgen.mesh.cave.constrictionScourFollowsRadiusMinimumDownstream") {
    const std::vector<CaveHydrologyPoint> trunk{{{-0.8f, 0.10f, 0.f}, 0.22f},
                                                {{-0.4f, 0.06f, 0.f}, 0.21f},
                                                {{0.f, 0.f, 0.f}, 0.11f},
                                                {{0.4f, -0.04f, 0.f}, 0.22f},
                                                {{0.8f, -0.08f, 0.f}, 0.24f}};
    CaveHydrologyWeights                  hydrology;
    hydrology.trunk                                    = {0.7f, 0.8f, 1.1f, 1.2f, 1.25f};
    const std::vector<CaveConstrictionScourSite> sites = createCaveConstrictionScourSites(trunk, {}, hydrology);
    REQUIRE_EQ(sites.size(), size_t(1));
    CHECK(sites[0].poolCenter.x > sites[0].constriction.x);
    CHECK(sites[0].poolCenter.y < sites[0].constriction.y);
    CHECK(sites[0].constrictionRatio > 0.4f);

    const CaveConstrictionScourSample pool     = sampleCaveConstrictionScour(sites[0].poolCenter, sites);
    const CaveConstrictionScourSample repeated = sampleCaveConstrictionScour(sites[0].poolCenter, sites);
    const CaveConstrictionScourSample far      = sampleCaveConstrictionScour({0.9f, 0.9f, 0.9f}, sites);
    CHECK_EQ(pool.erosion, repeated.erosion);
    CHECK(pool.bedScour > 0.f);
    CHECK(pool.erosion > far.erosion);

    std::vector<CaveHydrologyPoint> uniform = trunk;
    for (CaveHydrologyPoint& point : uniform) point.radius = 0.2f;
    CHECK(createCaveConstrictionScourSites(uniform, {}, hydrology).empty());
}

TEST_CASE("procgen.mesh.cave.plungingEfficiencyHasDischargeDependentOptimum") {
    auto makeSite = [&](float ratio, float hydraulicIntensity) {
        const float                           shoulderRadius = 0.2f;
        const std::vector<CaveHydrologyPoint> path{{{-0.8f, 0.f, 0.f}, shoulderRadius},
                                                   {{-0.4f, 0.f, 0.f}, shoulderRadius},
                                                   {{0.f, 0.f, 0.f}, shoulderRadius * (1.f - ratio)},
                                                   {{0.4f, 0.f, 0.f}, shoulderRadius},
                                                   {{0.8f, 0.f, 0.f}, shoulderRadius}};
        CaveHydrologyWeights                  hydrology;
        hydrology.trunk = {hydraulicIntensity, hydraulicIntensity, hydraulicIntensity, hydraulicIntensity,
                           hydraulicIntensity};
        const std::vector<CaveConstrictionScourSite> sites = createCaveConstrictionScourSites(path, {}, hydrology);
        REQUIRE_EQ(sites.size(), size_t(1));
        return sites.front();
    };
    const CaveConstrictionScourSite lowOptimal         = makeSite(0.35f, 0.25f);
    const CaveConstrictionScourSite lowOverConstricted = makeSite(0.75f, 0.25f);
    CHECK(lowOptimal.plungingEfficiency > lowOverConstricted.plungingEfficiency * 4.f);
    CHECK(std::fabs(lowOptimal.optimalConstriction - 0.35f) < 1e-5f);

    const CaveConstrictionScourSite highAtFifty      = makeSite(0.50f, 1.6f);
    const CaveConstrictionScourSite highAtThirtyFive = makeSite(0.35f, 1.6f);
    CHECK(highAtFifty.plungingEfficiency > highAtThirtyFive.plungingEfficiency);
    CHECK(std::fabs(highAtFifty.optimalConstriction - 0.50f) < 1e-5f);
}

TEST_CASE("procgen.mesh.cave.constrictionScourIsObservableAndDefaultCompatible") {
    MeshRecipeRegistry::instance().registerBuiltins();
    Params params;
    params.setSeed(20260830);
    params.setString("style", "tunnels");
    params.setString("genesis", "epigene");
    params.setInt("resolution", 24);
    params.setInt("chambers", 5);
    params.setInt("branches", 5);
    params.setInt("dripstones", 0);
    params.setInt("flowstones", 0);
    params.setInt("curtains", 0);
    params.setFloat("roughness", 0.f);
    params.setFloat("erosion", 0.f);
    params.setFloat("vadoseIncision", 0.f);

    MeshBuild   omitted, explicitLegacy, scoured, repeated, hypogeneOff, hypogeneOn;
    std::string error;
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, omitted, error));
    params.setFloat("constrictionScour", 0.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, explicitLegacy, error));
    REQUIRE(omitted.positions() == explicitLegacy.positions());
    REQUIRE(omitted.indices() == explicitLegacy.indices());

    params.setFloat("constrictionScour", 1.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, scoured, error));
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, repeated, error));
    REQUIRE(scoured.positions() != explicitLegacy.positions());
    REQUIRE(scoured.positions() == repeated.positions());
    REQUIRE(scoured.indices() == repeated.indices());
    REQUIRE_EQ(scoured.getMeta("constrictionScourModel", ""), std::string("discharge-optimal-plunging-flow-cpw-v2"));
    REQUIRE(std::stoi(scoured.getMeta("constrictionScourSites", "0")) > 0);
    REQUIRE(std::stoi(scoured.getMeta("constrictionScourAffectedVoxels", "0")) > 0);
    REQUIRE(std::stof(scoured.getMeta("maximumConstrictionScourRetreat", "0")) > 0.f);
    REQUIRE(std::stof(scoured.getMeta("maximumConstrictionRatio", "0")) > 0.f);
    REQUIRE(std::stof(scoured.getMeta("maximumPlungingEfficiency", "0")) > 0.f);

    params.setString("genesis", "hypogene");
    params.setFloat("constrictionScour", 0.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, hypogeneOff, error));
    params.setFloat("constrictionScour", 1.f);
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, hypogeneOn, error));
    REQUIRE(hypogeneOff.positions() == hypogeneOn.positions());
    REQUIRE(hypogeneOff.indices() == hypogeneOn.indices());
    REQUIRE_EQ(hypogeneOn.getMeta("constrictionScourModel", ""), std::string("inactive-hypogene"));

    params.setFloat("constrictionScour", 1.1f);
    MeshBuild invalid;
    REQUIRE(!MeshRecipeRegistry::instance().generate("mesh.cave", params, invalid, error));
}

TEST_CASE("procgen.mesh.cave.floodPluckingRequiresFracturesFlowAndSelectedBlocks") {
    CavePluckingInput  input{0.035f, -0.012f, 0.041f, 3.1415926535f, 1.2f, 1.f, 0.85f, 0.11f, 20260830u};
    CavePluckingSample selected;
    for (int z = -12; z <= 12 && selected.blockMask <= 0.1f; ++z) {
        for (int x = -12; x <= 12 && selected.blockMask <= 0.1f; ++x) {
            input.x                            = float(x) * 0.025f;
            input.z                            = float(z) * 0.025f;
            const CavePluckingSample candidate = sampleCaveFloodPlucking(input);
            if (candidate.blockMask > selected.blockMask) selected = candidate;
        }
    }
    REQUIRE(selected.blockMask > 0.1f);
    CHECK(selected.erosion > 0.f);
    CHECK(selected.fracturePredisposition > 0.f);
    CHECK(selected.hydraulicActivation > 0.f);

    const CavePluckingSample repeated = sampleCaveFloodPlucking(input);
    CHECK_EQ(repeated.erosion, sampleCaveFloodPlucking(input).erosion);

    CavePluckingInput unfractured     = input;
    unfractured.primaryFractureMask   = 0.f;
    unfractured.secondaryFractureMask = 0.f;
    CHECK_EQ(sampleCaveFloodPlucking(unfractured).erosion, 0.f);

    CavePluckingInput lowFlow  = input;
    lowFlow.hydraulicIntensity = 0.1f;
    CHECK_EQ(sampleCaveFloodPlucking(lowFlow).erosion, 0.f);
}
