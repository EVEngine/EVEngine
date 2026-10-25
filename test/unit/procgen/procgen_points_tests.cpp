// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "origins": [
    "procgen_attribute_table.cpp",
    "procgen_foliage_clusters.cpp",
    "procgen_grid_graph.cpp",
    "procgen_point_delta.cpp",
    "procgen_point_graph.cpp",
    "procgen_pointset.cpp",
    "procgen_poisson.cpp"
  ]
}
*/
// clang-format on

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_procgen_attribute_table)
#include "PrivateTestScope.h"
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
namespace test_procgen_attribute_table {
// procgen_attribute_table.cpp
using namespace eve;
using namespace eve::procgen;

TEST_CASE("procgen.attributeTable.spec.typedColumnsAndAtomicOperations") {
    SUB_CASE("sparse typed values -> column reads preserve each declared type") {
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
    };
    SUB_CASE("conflicting column type -> error and unchanged table") {
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
    };
    SUB_CASE("valid rows then invalid append -> all-or-nothing row count") {
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
    };
    SUB_CASE("self-copy of a column -> values remain stable") {
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
    };
    SUB_CASE("point set operations -> typed attributes preserved") {
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
    };
    SUB_CASE("conflicting point attribute schema -> error without mutation") {
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
    };
    SUB_CASE("rename, copy, remove -> target columns and values reflect each operation") {
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
    };
}


// procgen_biome.cpp
using namespace eve::procgen;

TEST_CASE("procgen.biome.spec.priorityExclusionAndValidation") {
    SUB_CASE("priority and exclusion rules -> selected biome and asset metadata") {
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
    };
    SUB_CASE("invalid rules or zero density -> explicit validation result") {
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
    };
}


// procgen_build_layer_stack.cpp
using namespace eve::procgen;

TEST_CASE("procgen.buildLayers.spec.orderSerializationAndDamage") {
    SUB_CASE("ordered tile and object layers -> composed output in definition order") {
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
    };
    SUB_CASE("damaged layer serialization -> rejected without partial replacement") {
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
    };
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

TEST_CASE("procgen.bushMesh.spec.outwardFoliageWinding") {
    SUB_CASE("bush triangle vertices -> outward-facing foliage normals") {
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
            const int  ia   = mesh.getIndex(i);
            const int  ib   = mesh.getIndex(i + 1);
            const int  ic   = mesh.getIndex(i + 2);
            const Vec3 face = cross(subtract(position(mesh, ib), position(mesh, ia)),
                                    subtract(position(mesh, ic), position(mesh, ia)));
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
    };
}


// procgen_cable_rope.cpp
// Cable, chain, and rope mesh and texture contracts.


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

TEST_CASE("procgen.cableRope.spec.meshRecipesAndGeometry") {
    SUB_CASE("registered cable and rope recipes -> discoverable IDs") {
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
    };
    SUB_CASE("cable, chain, rope recipe inputs -> finite nonempty indexed meshes") {
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
    };
    SUB_CASE("same seed and scale -> reproducible scalable mesh") {
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
    };
    SUB_CASE("more segments -> corresponding vertex count increase") {
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
    };
    SUB_CASE("direct cable mesh API -> valid geometry") {
        Params p;
        p.setInt("segments", 2);
        p.setFloat("segLength", 1.f);
        MeshBuild   m;
        std::string err;
        REQUIRE(generateCableChainRope("mesh.chain", p, m, err));
        CHECK_GT(m.getVertexCount(), 0);
        CHECK(!generateCableChainRope("mesh.missing", p, m, err));
    };
}

TEST_CASE("procgen.cableRope.spec.textureSeedParametersAndPbr") {
    SUB_CASE("same texture seed -> identical pixels") {
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
    };
    SUB_CASE("changed texture parameters -> changed output") {
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
    };
    SUB_CASE("result API with defaults -> valid texture or explicit error") {
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
    };
    SUB_CASE("PBR recipe -> complete material map set") {
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
    };
}


// procgen_cave.cpp
TEST_CASE("procgen.cave.spec.reactivePatchesAndSlope") {
    SUB_CASE("seeded reactive cave patch input -> coherent bounded surface relief") {
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
        const auto         result =
            evolveCaveSurfaceByCorrelatedReactivity(first, rates, flow, size, size, size, 1.f, 20260830u);
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
    };
    SUB_CASE("default cave patch parameters -> compatible base output") {
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
    };
    SUB_CASE("edge versus plane orientation -> distinct slope reaction independent of rotation") {
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
    };
    SUB_CASE("slope reactivity setting -> observable change with default compatibility") {
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
        REQUIRE_EQ(reactive.getMeta("surfaceReactivityModel", ""),
                   std::string("rotation-invariant-normal-dispersion-v1"));
        REQUIRE(std::stoi(reactive.getMeta("surfaceReactivityAffectedVoxels", "0")) > 0);
        REQUIRE(std::stof(reactive.getMeta("maximumSurfaceNormalDispersion", "0")) > 0.f);
        REQUIRE(std::stof(reactive.getMeta("maximumSurfaceReactivityRetreat", "0")) > 0.f);
        params.setFloat("surfaceSlopeReactivity", 1.1f);
        REQUIRE(!MeshRecipeRegistry::instance().generate("mesh.cave", params, repeated, error));
    };
}

TEST_CASE("procgen.cave.spec.roughnessAndScallopHistory") {
    SUB_CASE("roughness transfer -> ridge and shelter response") {
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
    };
    SUB_CASE("flow coupling parameter -> observable roughness change") {
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
    };
    SUB_CASE("older scallop relief plus younger flow -> partitioned combined history") {
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
    };
    SUB_CASE("scallop flow history switch -> observable output and default baseline") {
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
    };
}

TEST_CASE("procgen.cave.spec.mineralsAndBreakdownScour") {
    SUB_CASE("secondary minerals and flow -> patchy armor and selective stripping") {
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
    };
    SUB_CASE("armor parameter switch -> observable output and default baseline") {
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
    };
    SUB_CASE("breakdown blocks in stream -> upstream horseshoe and downstream wake scour") {
        CaveBreakdownSet   breakdown;
        CaveBreakdownEvent event;
        event.blocks.push_back({0.f, -0.15f, 0.f, 0.07f, 0.05f, 0.06f, 0.f});
        breakdown.events.push_back(event);
        breakdown.blockCount = 1;
        const std::vector<CaveHydrologyPoint>    trunk{{{-0.5f, 0.f, 0.f}, 0.2f}, {{0.5f, 0.f, 0.f}, 0.2f}};
        const std::vector<CaveObstacleScourSite> smooth =
            createCaveObstacleScourSites(breakdown, trunk, {1.2f}, 0.48f, 0.f);
        const std::vector<CaveObstacleScourSite> rough =
            createCaveObstacleScourSites(breakdown, trunk, {1.2f}, 0.48f, 1.f);
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
    };
    SUB_CASE("breakdown scour switch -> observable output and default baseline") {
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
    };
}

TEST_CASE("procgen.cave.spec.potholesAndStreamBed") {
    SUB_CASE("crossing fractures and gravel -> potholes where both conditions hold") {
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
    };
    SUB_CASE("eddy pothole switch -> observable output and default baseline") {
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
        REQUIRE_EQ(eroded.getMeta("eddyPotholeModel", ""),
                   std::string("gravel-size-dependent-compound-eddy-pothole-v1"));
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
    };
    SUB_CASE("floor flow with fractures -> stream-bed karren only there") {
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
    };
    SUB_CASE("karren switch -> observable output and default baseline") {
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
    };
}

TEST_CASE("procgen.cave.spec.knickpointsAndConstrictedFlow") {
    SUB_CASE("downstream slope break -> localized knickpoint erosion") {
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
    };
    SUB_CASE("knickpoint switch -> observable output and default baseline") {
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
        REQUIRE_EQ(eroded.getMeta("knickpointErosionModel", ""),
                   std::string("sediment-driven-headward-plunge-pool-v1"));
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
    };
    SUB_CASE("radius minimum -> downstream constriction scour") {
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
    };
    SUB_CASE("varying discharge -> nonmonotonic plunging efficiency") {
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
    };
    SUB_CASE("constriction switch -> observable output and default baseline") {
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
        REQUIRE_EQ(scoured.getMeta("constrictionScourModel", ""),
                   std::string("discharge-optimal-plunging-flow-cpw-v2"));
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
    };
}

TEST_CASE("procgen.cave.spec.floodPlucking") {
    SUB_CASE("fracture, flow, selected blocks -> flood plucking only when eligible") {
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
    };
}


}  // namespace test_procgen_attribute_table
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_procgen_foliage_clusters)
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>
#include "PrivateTestScope.h"
#include "procgen/MeshBuild.h"
#include "procgen/Params.h"
#include "procgen/Procgen.h"
#include "procgen/algorithms/FoliageCluster.h"
#include "procgen/algorithms/MarchingCubes.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
namespace test_procgen_foliage_clusters_unit {
// Geometry properties remain here; the visual construction walk lives in
// examples/procedural-render-gallery/foliage_stages.cpp.

using namespace eve::procgen;
namespace {

[[maybe_unused]] bool meshIndicesInRange(const MeshBuild& m) {
    const int vertexCount = m.getVertexCount();
    for (int i = 0; i < m.getIndexCount(); ++i) {
        const int id = m.getIndex(i);
        if (id < 0 || id >= vertexCount) return false;
    }
    return true;
}

[[maybe_unused]] bool meshPositionsFinite(const MeshBuild& m) {
    for (int i = 0; i < m.getVertexCount(); ++i) {
        if (!std::isfinite(m.getPositionX(i)) || !std::isfinite(m.getPositionY(i)) || !std::isfinite(m.getPositionZ(i)))
            return false;
    }
    return true;
}

[[maybe_unused]] bool meshNormalsFiniteUnit(const MeshBuild& m, float tolerance = 0.15f) {
    for (int i = 0; i < m.getVertexCount(); ++i) {
        const float nx = m.getNormalX(i);
        const float ny = m.getNormalY(i);
        const float nz = m.getNormalZ(i);
        if (!std::isfinite(nx) || !std::isfinite(ny) || !std::isfinite(nz)) return false;
        const float length = std::sqrt(nx * nx + ny * ny + nz * nz);
        if (std::fabs(length - 1.f) > tolerance) return false;
    }
    return true;
}

/** @brief One plane, no jitter and no size variation: the sampler is measurable. */
[[maybe_unused]] FoliageClusterDesc singlePlaneDesc() {
    FoliageClusterDesc desc;
    desc.seed               = 4242;
    desc.radius             = 1.f;
    desc.leafSize           = 0.25f;
    desc.leafSpacing        = 1.f;
    desc.planes             = 1;
    desc.capPlanes          = 0;
    desc.leafJitter         = 0.f;
    desc.leafScaleVariation = 0.f;
    desc.maxLeavesPerPlane  = 256;
    return desc;
}

}  // namespace

TEST_CASE("procgen.foliage.spec.clusterDistributionAndTreeInputs") {
    SUB_CASE("blue-noise foliage sampling -> spaced cluster and sphere normals") {
        const FoliageClusterDesc desc = singlePlaneDesc();

        MeshBuild first, second;
        const int leaves = addFoliageCluster(first, desc);
        CHECK(leaves > 0);
        // zeroerr re-evaluates CHECK_EQ operands when it formats a message, so
        // anything that appends to a mesh has to be hoisted into a variable first.
        const int repeated = addFoliageCluster(second, desc);
        CHECK_EQ(repeated, leaves);
        CHECK(first.positions() == second.positions());
        CHECK(first.normals() == second.normals());
        CHECK(first.indices() == second.indices());
        CHECK(meshIndicesInRange(first));
        CHECK(meshPositionsFinite(first));
        CHECK(meshNormalsFiniteUnit(first));

        // Six vertices per petal, four front triangles plus the four back-facing
        // ones the default two-sided emission adds.
        CHECK_EQ(first.getVertexCount(), leaves * 6);
        CHECK_EQ(first.getIndexCount(), leaves * 8 * 3);

        // Petal centroids sit 0.06 * leafSize off the sampled centre because the
        // lanceolate outline is not symmetric about its own mid-line.
        constexpr float    kCentroidOffset = 0.06f;
        const float        minDistance     = desc.leafSize * desc.leafSpacing;
        std::vector<float> cx, cy, cz;
        for (int leaf = 0; leaf < leaves; ++leaf) {
            float x = 0.f, y = 0.f, z = 0.f;
            for (int v = 0; v < 6; ++v) {
                const int i = leaf * 6 + v;
                x += first.getPositionX(i);
                y += first.getPositionY(i);
                z += first.getPositionZ(i);
            }
            cx.push_back(x / 6.f);
            cy.push_back(y / 6.f);
            cz.push_back(z / 6.f);
        }
        const float allowed = minDistance - 2.f * kCentroidOffset * desc.leafSize;
        float       closest = std::numeric_limits<float>::max();
        for (size_t i = 0; i < cx.size(); ++i) {
            for (size_t j = i + 1; j < cx.size(); ++j) {
                const float dx = cx[i] - cx[j], dy = cy[i] - cy[j], dz = cz[i] - cz[j];
                closest = std::min(closest, std::sqrt(dx * dx + dy * dy + dz * dz));
            }
        }
        // Blue noise guarantees a floor on the nearest-neighbour distance; an
        // uncorrelated draw of ~58 points in a unit disk does not.
        CHECK(closest >= allowed);
        // A sampler that bailed out early would leave the disk mostly empty. The
        // hexagonal-packing bound is 2A / (sqrt(3) d^2) centres; the disk is only
        // an inscribed region of the square the module sampler fills, and Bridson
        // reaches well under the ideal packing, so the floor sits near half of it.
        const float packingBound =
            2.f * 3.14159265f * desc.radius * desc.radius / (1.7320508f * minDistance * minDistance);
        CHECK(float(leaves) >= packingBound * 0.4f);
        CHECK(float(leaves) <= packingBound * 1.05f);

        // Spherical normal transfer: away from the cluster centre every vertex
        // normal has to point outward, which is what makes the flat cards shade as
        // a rounded mass.
        const float roundingDistance = desc.radius * 0.5f;
        int         outward          = 0;
        for (int i = 0; i < first.getVertexCount(); ++i) {
            const float px = first.getPositionX(i), py = first.getPositionY(i), pz = first.getPositionZ(i);
            const float length = std::sqrt(px * px + py * py + pz * pz);
            if (length < roundingDistance) continue;
            const float alignment =
                (px * first.getNormalX(i) + py * first.getNormalY(i) + pz * first.getNormalZ(i)) / length;
            CHECK(alignment > 0.99f);
            ++outward;
        }
        CHECK(outward > 0);

        // Two-sided emission doubles the index stream without duplicating vertices.
        FoliageClusterDesc singleSided = desc;
        singleSided.doubleSided        = false;
        MeshBuild oneSided;
        const int singleCount = addFoliageCluster(oneSided, singleSided);
        CHECK_EQ(singleCount, leaves);
        CHECK_EQ(oneSided.getVertexCount(), first.getVertexCount());
        CHECK_EQ(oneSided.getIndexCount() * 2, first.getIndexCount());

        // Flat card normals (rounding off) must reproduce the plane normal instead.
        FoliageClusterDesc flat = desc;
        flat.normalRounding     = 0.f;
        MeshBuild faceted;
        const int facetedCount = addFoliageCluster(faceted, flat);
        CHECK_EQ(facetedCount, leaves);
        CHECK(faceted.normals() != first.normals());

        // More planes fill the same volume with more leaves.
        FoliageClusterDesc many = desc;
        many.planes             = 6;
        many.capPlanes          = 2;
        MeshBuild full;
        const int fullCount = addFoliageCluster(full, many);
        CHECK(fullCount > leaves);
    };
    SUB_CASE("tree cluster settings -> authored foliage mesh") {
        MeshRecipeRegistry::instance().registerBuiltins();
        Params p;
        p.setSeed(90210);
        p.setString("style", "lowpoly");
        p.setString("leafMode", "clusters");
        p.setString("branchAlgorithm", "spaceColonization");
        p.setFloat("leafDensity", 0.8f);
        p.setFloat("height", 6.2f);
        p.setFloat("crownRadius", 2.15f);

        MeshBuild   a, b;
        std::string err;
        REQUIRE(MeshRecipeRegistry::instance().generate("mesh.tree", p, a, err));
        REQUIRE(MeshRecipeRegistry::instance().generate("mesh.tree", p, b, err));
        CHECK(a.getVertexCount() > 0);
        CHECK(a.positions() == b.positions());
        CHECK(a.indices() == b.indices());
        CHECK(meshIndicesInRange(a));
        CHECK(meshPositionsFinite(a));
        CHECK(meshNormalsFiniteUnit(a));
        CHECK_EQ(a.getMeta("leafMode", ""), "clusters");
        CHECK(std::stoi(a.getMeta("clusters", "0")) > 0);

        // Bare skeleton for comparison.
        Params bare = p;
        bare.setString("leafMode", "none");
        MeshBuild skeleton;
        REQUIRE(MeshRecipeRegistry::instance().generate("mesh.tree", bare, skeleton, err));
        CHECK(skeleton.getVertexCount() < a.getVertexCount());

        // Density widens the blue-noise spacing, so the leaves get sparser.
        Params sparse = p;
        sparse.setFloat("leafDensity", 0.25f);
        MeshBuild sparseMesh;
        REQUIRE(MeshRecipeRegistry::instance().generate("mesh.tree", sparse, sparseMesh, err));
        CHECK(sparseMesh.getVertexCount() < a.getVertexCount());

        // The cluster budget caps the foliage cost.
        Params limited = p;
        limited.setInt("clusterLimit", 4);
        MeshBuild fewClusters;
        REQUIRE(MeshRecipeRegistry::instance().generate("mesh.tree", limited, fewClusters, err));
        CHECK(std::stoi(fewClusters.getMeta("clusters", "0")) <= 4);
        CHECK(fewClusters.getVertexCount() < a.getVertexCount());

        // Plane count must change the leaf budget monotonically.
        Params dense = p;
        dense.setInt("clusterPlanes", 14);
        dense.setInt("clusterCaps", 3);
        MeshBuild denseMesh;
        REQUIRE(MeshRecipeRegistry::instance().generate("mesh.tree", dense, denseMesh, err));
        CHECK(denseMesh.getVertexCount() > a.getVertexCount());
        CHECK(meshNormalsFiniteUnit(denseMesh));

        // Every vertex must sit inside the crown envelope. The floor is the trunk
        // radius, not zero: the first trunk ring is perpendicular to an already
        // bent trunk axis, so its lowest vertices dip slightly below the root.
        const float crownTop    = p.getFloat("height", 6.2f) + p.getFloat("crownRadius", 2.15f);
        const float trunkRadius = p.getFloat("height", 6.2f) * 0.055f;
        int         outside     = 0;
        for (int i = 0; i < a.getVertexCount(); ++i) {
            const float y = a.getPositionY(i);
            if (y < -trunkRadius || y > crownTop) ++outside;
        }
        CHECK_EQ(outside, 0);
    };
    SUB_CASE("Weber-Penn tree settings -> valid cluster foliage") {
        // The cluster mode has to work over both skeleton algorithms.
        MeshRecipeRegistry::instance().registerBuiltins();
        Params p;
        p.setSeed(5150);
        p.setString("leafMode", "clusters");
        p.setString("branchAlgorithm", "weberPenn");
        p.setFloat("leafDensity", 0.7f);
        p.setInt("branchLevels", 3);
        p.setInt("branchCount", 8);
        MeshBuild   mesh;
        std::string err;
        REQUIRE(MeshRecipeRegistry::instance().generate("mesh.tree", p, mesh, err));
        CHECK(meshIndicesInRange(mesh));
        CHECK(meshPositionsFinite(mesh));
        CHECK(meshNormalsFiniteUnit(mesh));
        CHECK(std::stoi(mesh.getMeta("clusters", "0")) > 0);
        CHECK_EQ(mesh.getMeta("branchAlgorithm", ""), "weberPenn");
    };
    SUB_CASE("changed cluster parameters -> corresponding geometry change") {
        MeshRecipeRegistry::instance().registerBuiltins();
        const RecipeDescriptor* recipe = MeshRecipeRegistry::instance().descriptor("mesh.tree");
        REQUIRE(recipe != nullptr);

        // Every knob the cluster generator reads must be reachable from the recipe
        // schema, otherwise it can never be set from Squirrel or the editor.
        const char* keys[] = {"clusterSize",   "clusterLeafScale", "clusterSpacing", "clusterSeparation", "clusterTilt",
                              "clusterPlanes", "clusterCaps",      "clusterLeaves",  "clusterLimit"};
        for (const char* key : keys) {
            const ParamDescriptor* param = recipe->find(key);
            REQUIRE(param != nullptr);
            CHECK_EQ(param->key, key);
            CHECK(param->advanced);
            CHECK(!param->displayName.empty());
        }
        // The schema is the source of truth for `applyMeshRecipeDefaults`, so its
        // defaults have to agree with the generator's own fallbacks.
        Params defaults;
        defaults.setSeed(1);
        recipe->applyDefaults(defaults);
        CHECK_EQ(defaults.getFloat("clusterSize", -1.f), 0.30f);
        CHECK_EQ(defaults.getFloat("clusterLeafScale", -1.f), 0.85f);
        CHECK_EQ(defaults.getFloat("clusterSpacing", -1.f), 0.80f);
        CHECK_EQ(defaults.getFloat("clusterSeparation", -1.f), 0.55f);
        CHECK_EQ(defaults.getFloat("clusterTilt", -1.f), 26.f);
        CHECK_EQ(defaults.getInt("clusterPlanes", -1), 10);
        CHECK_EQ(defaults.getInt("clusterCaps", -1), 2);
        CHECK_EQ(defaults.getInt("clusterLeaves", -1), 28);
        CHECK_EQ(defaults.getInt("clusterLimit", -1), 120);

        Params base;
        base.setSeed(4711);
        base.setString("style", "lowpoly");
        base.setString("leafMode", "clusters");
        base.setString("branchAlgorithm", "weberPenn");
        base.setFloat("leafDensity", 0.8f);
        base.setFloat("height", 6.2f);
        base.setFloat("crownRadius", 2.15f);
        base.setInt("branchLevels", 3);
        base.setInt("branchCount", 8);
        MeshBuild   reference;
        std::string err;
        REQUIRE(MeshRecipeRegistry::instance().generate("mesh.tree", base, reference, err));
        const int referenceClusters = std::stoi(reference.getMeta("clusters", "0"));
        REQUIRE(referenceClusters > 0);

        // Each knob must actually move the geometry or the cluster count; a
        // parameter that is accepted but ignored is worse than a missing one.
        const auto differs = [&](const Params& variant) {
            MeshBuild mesh;
            if (!MeshRecipeRegistry::instance().generate("mesh.tree", variant, mesh, err)) return false;
            return mesh.positions() != reference.positions();
        };

        Params p = base;
        p.setFloat("clusterSize", 0.45f);
        CHECK(differs(p));
        p = base;
        p.setFloat("clusterLeafScale", 1.6f);
        CHECK(differs(p));
        p = base;
        p.setFloat("clusterSpacing", 0.35f);
        CHECK(differs(p));
        p = base;
        p.setFloat("clusterSeparation", 1.4f);
        CHECK(differs(p));
        p = base;
        p.setFloat("clusterTilt", 70.f);
        CHECK(differs(p));
        p = base;
        p.setInt("clusterPlanes", 3);
        CHECK(differs(p));
        p = base;
        p.setInt("clusterCaps", 6);
        CHECK(differs(p));

        // The two budget knobs are observable through the cluster count / cost.
        p = base;
        p.setInt("clusterLeaves", 4);
        MeshBuild cheap;
        REQUIRE(MeshRecipeRegistry::instance().generate("mesh.tree", p, cheap, err));
        CHECK(cheap.getVertexCount() < reference.getVertexCount());

        p = base;
        p.setInt("clusterLimit", 6);
        MeshBuild few;
        REQUIRE(MeshRecipeRegistry::instance().generate("mesh.tree", p, few, err));
        CHECK(std::stoi(few.getMeta("clusters", "0")) <= 6);
        CHECK(std::stoi(few.getMeta("clusters", "0")) < referenceClusters);

        // Out-of-range values are clamped rather than rejected, so a hostile or
        // stale preset can never blow the geometry budget.
        Params extreme = base;
        extreme.setInt("clusterPlanes", 9999);
        extreme.setInt("clusterCaps", 9999);
        extreme.setInt("clusterLeaves", 99999);
        extreme.setInt("clusterLimit", 99999);
        extreme.setFloat("clusterSize", -5.f);
        extreme.setFloat("clusterSpacing", 0.f);
        MeshBuild clamped;
        REQUIRE(MeshRecipeRegistry::instance().generate("mesh.tree", extreme, clamped, err));
        CHECK(meshIndicesInRange(clamped));
        CHECK(meshNormalsFiniteUnit(clamped));
        CHECK(meshPositionsFinite(clamped));
    };
}


}  // namespace test_procgen_foliage_clusters_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_procgen_grid_graph)
#include "PrivateTestScope.h"
#include "procgen/GeneratorRegistry.h"
#include "procgen/GridGraph.h"
#include "procgen/MeshGraph.h"
#include "procgen/Params.h"
#include "procgen/PointGraph.h"
#include "procgen/Semantic.h"
#include "procgen/TilePreset.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// procgen_grid_graph.cpp
namespace test_procgen_grid_graph {
using namespace eve::procgen;

TEST_CASE("procgen.gridGraph.spec.presetCompositionAndFailure") {
    SUB_CASE("standard grid preset -> reference table dimensions and values") {
        const auto isolated = resolveTilePreset(0);
        CHECK_EQ(isolated.configuration, 16);
        CHECK_EQ(isolated.kind, TilePresetKind::Single);
        CHECK_EQ(isolated.rotationDegrees, 0);
        CHECK(!isolated.mirrorX);

        const auto north = resolveTilePreset(1u << 0u);
        CHECK_EQ(north.configuration, 18);
        CHECK_EQ(north.kind, TilePresetKind::DeadEnd);
        CHECK_EQ(north.rotationDegrees, 90);

        const auto east = resolveTilePreset(1u << 1u);
        CHECK_EQ(east.configuration, 48);
        CHECK_EQ(east.kind, TilePresetKind::DeadEnd);
        CHECK_EQ(east.rotationDegrees, 180);

        const auto asymmetric = resolveTilePreset(139);
        CHECK_EQ(asymmetric.configuration, 59);
        CHECK_EQ(asymmetric.kind, TilePresetKind::EdgeCornerFill);
        CHECK_EQ(asymmetric.rotationDegrees, 90);
        CHECK(asymmetric.mirrorX);

        const auto filled = resolveTilePreset(255);
        CHECK_EQ(filled.configuration, 511);
        CHECK_EQ(filled.kind, TilePresetKind::Fill);

        // The source 4.3.5 table omits configuration 350; preserve that compatibility edge case.
        const auto unclassified = resolveTilePreset(121);
        CHECK_EQ(unclassified.configuration, 350);
        CHECK_EQ(unclassified.kind, TilePresetKind::None);

        for (int mask = 0; mask <= 255; ++mask) {
            const auto variant = resolveTilePreset(std::uint8_t(mask));
            CHECK((variant.configuration & 16u) != 0u);
            const bool validRotation = variant.rotationDegrees == 0 || variant.rotationDegrees == 90 ||
                                       variant.rotationDegrees == 180 || variant.rotationDegrees == 270;
            CHECK(validRotation);
        }
    };
    SUB_CASE("grid and point nodes -> composed graph output") {
        Grid2D source;
        source.resize(5, 5);
        source.setCell(2, 2, int(Semantic::Grass));

        PointGraph points;
        REQUIRE(points.addNode("input", "input"));
        REQUIRE(points.addNode("move", "transform"));
        REQUIRE(points.connect("input", "move"));
        REQUIRE(points.setNodeFloat("move", "y", 3.f));

        GridGraph graph;
        REQUIRE(graph.addNode("source", "grid.input").ok());
        REQUIRE(graph.addNode("grow", "grid.expand").ok());
        REQUIRE(graph.addNode("points", "convert.grid_to_points").ok());
        REQUIRE(graph.addNode("point_pass", "point.subgraph").ok());
        REQUIRE(graph.connect("source", "grow").ok());
        REQUIRE(graph.connect("grow", "points").ok());
        REQUIRE(graph.connect("points", "point_pass").ok());
        REQUIRE(graph.setNodeGrid("source", source).ok());
        REQUIRE(graph.setNodeInt("grow", "radius", 1).ok());
        REQUIRE(graph.setNodeFloat("points", "cellSize", 2.f).ok());
        REQUIRE(graph.setNodePointSubgraph("point_pass", points, "input", "move").ok());

        auto result = graph.execute("point_pass");
        REQUIRE(result.ok());
        const auto* output = std::get_if<PointSet>(&result.value());
        REQUIRE(output != nullptr);
        CHECK_EQ(output->getCount(), 9);
        CHECK_EQ(output->getY(0), 3.f);
        CHECK(output->hasIntAttribute(0, "cell_x"));
        CHECK(output->hasIntAttribute(0, "semantic"));

        auto again = graph.execute("point_pass");
        REQUIRE(again.ok());
        CHECK_EQ(std::get<PointSet>(again.value()).getCount(), 9);
    };
    SUB_CASE("type mismatch, cycle, shape mismatch -> graph errors") {
        GridGraph graph;
        REQUIRE(graph.addNode("a", "grid.input").ok());
        REQUIRE(graph.addNode("b", "grid.input").ok());
        REQUIRE(graph.addNode("merge", "grid.union").ok());
        REQUIRE(graph.addNode("points", "convert.grid_to_points").ok());
        REQUIRE(graph.connect("a", "merge", 0).ok());
        REQUIRE(graph.connect("b", "merge", 1).ok());
        REQUIRE(graph.connect("merge", "points").ok());

        auto badType = graph.connect("points", "merge", 0);
        CHECK(!badType.ok());

        Grid2D small;
        small.resize(2, 2);
        Grid2D large;
        large.resize(3, 3);
        REQUIRE(graph.setNodeGrid("a", small).ok());
        REQUIRE(graph.setNodeGrid("b", large).ok());
        auto mismatch = graph.execute("merge");
        CHECK(!mismatch.ok());

        GridGraph cycle;
        REQUIRE(cycle.addNode("left", "grid.invert").ok());
        REQUIRE(cycle.addNode("right", "grid.invert").ok());
        REQUIRE(cycle.connect("left", "right").ok());
        REQUIRE(cycle.connect("right", "left").ok());
        auto cyclic = cycle.validate("left");
        CHECK(!cyclic.ok());
    };
}

TEST_CASE("procgen.gridGraph.spec.generatorsRoguelikeAndMesh") {
    SUB_CASE("generator, selector, path, serialization -> expected graph result") {
        GridGraph graph;
        REQUIRE(graph.addNode("maze", "generate.maze").ok());
        REQUIRE(graph.setNodeInt("maze", "width", 17).ok());
        REQUIRE(graph.setNodeInt("maze", "height", 17).ok());
        REQUIRE(graph.setNodeInt("maze", "seed", 42).ok());
        REQUIRE(graph.addNode("border", "select.border").ok());
        REQUIRE(graph.connect("maze", "border").ok());

        auto first = graph.execute("border");
        REQUIRE(first.ok());
        const auto* selected = std::get_if<Grid2D>(&first.value());
        REQUIRE(selected != nullptr);
        CHECK_EQ(selected->getWidth(), 17);

        GridGraph restored;
        REQUIRE(restored.deserializeDefinition(graph.serializeDefinition()).ok());
        auto second = restored.execute("border");
        REQUIRE(second.ok());
        CHECK_EQ(std::get<Grid2D>(second.value()).cells(), selected->cells());

        Grid2D start;
        start.resize(17, 17);
        start.setCell(1, 1, int(Semantic::Floor));
        Grid2D target;
        target.resize(17, 17);
        target.setCell(15, 15, int(Semantic::Floor));
        REQUIRE(restored.addNode("start", "grid.input").ok());
        REQUIRE(restored.addNode("target", "grid.input").ok());
        REQUIRE(restored.addNode("path", "grid.path").ok());
        REQUIRE(restored.setNodeGrid("start", start).ok());
        REQUIRE(restored.setNodeGrid("target", target).ok());
        REQUIRE(restored.connect("maze", "path", 0).ok());
        REQUIRE(restored.connect("start", "path", 1).ok());
        REQUIRE(restored.connect("target", "path", 2).ok());
        CHECK(restored.execute("path").ok());
    };
    SUB_CASE("registered roguelike source -> semantic layer extraction") {
        GeneratorRegistry::instance().registerBuiltins();
        for (const int seed : {7, 42, 436}) {
            Params params;
            params.setSize(31, 23);
            params.setSeed(std::uint32_t(seed));
            params.setInt("roomCount", 9);
            params.setString("layoutStyle", "clustered");
            params.setString("connectionStyle", "nearest");
            params.setString("corridorStyle", "l");

            Grid2D      expected;
            std::string error;
            REQUIRE(GeneratorRegistry::instance().generate("level.roguelike", params, expected, error));

            GridGraph graph;
            REQUIRE(graph.addNode("level", "generate.registry").ok());
            REQUIRE(graph.setNodeString("level", "algorithm", "level.roguelike").ok());
            REQUIRE(graph.setNodeInt("level", "width", 31).ok());
            REQUIRE(graph.setNodeInt("level", "height", 23).ok());
            REQUIRE(graph.setNodeInt("level", "seed", seed).ok());
            REQUIRE(graph.setNodeInt("level", "roomCount", 9).ok());
            REQUIRE(graph.setNodeString("level", "layoutStyle", "clustered").ok());
            REQUIRE(graph.setNodeString("level", "connectionStyle", "nearest").ok());
            REQUIRE(graph.setNodeString("level", "corridorStyle", "l").ok());
            REQUIRE(graph.addNode("floor", "select.semantic").ok());
            REQUIRE(graph.addNode("corridor", "select.semantic").ok());
            REQUIRE(graph.setNodeInt("floor", "semantic", int(Semantic::Floor)).ok());
            REQUIRE(graph.setNodeInt("corridor", "semantic", int(Semantic::Corridor)).ok());
            REQUIRE(graph.connect("level", "floor").ok());
            REQUIRE(graph.connect("level", "corridor").ok());

            auto generated = graph.execute("level");
            REQUIRE(generated.ok());
            const auto& actual = std::get<Grid2D>(generated.value());
            CHECK_EQ(actual.cells(), expected.cells());
            CHECK_EQ(actual.detail(), expected.detail());
            CHECK_EQ(actual.getObjectCount(), expected.getObjectCount());

            auto floor    = graph.execute("floor");
            auto corridor = graph.execute("corridor");
            REQUIRE(floor.ok());
            REQUIRE(corridor.ok());
            int selected = 0;
            for (const auto cell : std::get<Grid2D>(floor.value()).cells()) {
                const bool valid = cell == Semantic::Empty || cell == Semantic::Floor;
                CHECK(valid);
                if (cell == Semantic::Floor) ++selected;
            }
            for (const auto cell : std::get<Grid2D>(corridor.value()).cells()) {
                const bool valid = cell == Semantic::Empty || cell == Semantic::Corridor;
                CHECK(valid);
                if (cell == Semantic::Corridor) ++selected;
            }
            CHECK(selected > 0);

            GridGraph restored;
            REQUIRE(restored.deserializeDefinition(graph.serializeDefinition()).ok());
            auto restoredLevel = restored.execute("level");
            REQUIRE(restoredLevel.ok());
            CHECK_EQ(std::get<Grid2D>(restoredLevel.value()).cells(), expected.cells());
        }

        GridGraph missing;
        REQUIRE(missing.addNode("level", "generate.registry").ok());
        CHECK(!missing.execute("level").ok());
        REQUIRE(missing.setNodeString("level", "algorithm", "missing.generator").ok());
        CHECK(!missing.execute("level").ok());
    };
    SUB_CASE("grid and point inputs -> composed mesh graph output") {
        Grid2D grid;
        grid.resize(2, 1);
        grid.setCell(0, 0, int(Semantic::Floor));
        grid.setCell(1, 0, int(Semantic::Floor));
        GridGraph autotile;
        REQUIRE(autotile.addNode("grid", "grid.input").ok());
        REQUIRE(autotile.addNode("autotile", "grid.autotile").ok());
        REQUIRE(autotile.setNodeGrid("grid", grid).ok());
        REQUIRE(autotile.connect("grid", "autotile").ok());
        auto autotiled = autotile.execute("autotile");
        REQUIRE(autotiled.ok());
        grid = std::get<Grid2D>(autotiled.value());

        MeshGraph tiles;
        REQUIRE(tiles.addNode("grid", "grid.input").ok());
        REQUIRE(tiles.addNode("tiles", "mesh.grid_tiles").ok());
        REQUIRE(tiles.setNodeGrid("grid", grid).ok());
        REQUIRE(tiles.connect("grid", "tiles").ok());
        auto tiled = tiles.execute("tiles");
        REQUIRE(tiled.ok());
        CHECK_EQ(tiled.value().getIndexCount(), 48);
        CHECK_EQ(tiled.value().getGroupCount(), 2);
        CHECK(tiled.value().getGroupName(0).find("/dead_end/r180/normal/48") != std::string::npos);
        CHECK(tiled.value().getGroupName(1).find("/dead_end/r0/normal/24") != std::string::npos);

        MeshGraph restoredTiles;
        REQUIRE(restoredTiles.deserializeDefinition(tiles.serializeDefinition()).ok());
        REQUIRE(restoredTiles.setNodeGrid("grid", grid).ok());
        auto restoredMesh = restoredTiles.execute("tiles");
        REQUIRE(restoredMesh.ok());
        CHECK_EQ(restoredMesh.value().getIndexCount(), tiled.value().getIndexCount());
        CHECK(!restoredTiles.setNodeFloat("tiles", "misspelled", 1.f).ok());

        PointSet points;
        points.add(0.f, 0.f, 0.f);
        points.add(4.f, 0.f, 0.f);
        MeshGraph instances;
        REQUIRE(instances.addNode("mesh", "mesh.input").ok());
        REQUIRE(instances.addNode("points", "point.input").ok());
        REQUIRE(instances.addNode("instances", "mesh.instance_points").ok());
        REQUIRE(instances.setNodeMesh("mesh", tiled.value()).ok());
        REQUIRE(instances.setNodePoints("points", points).ok());
        REQUIRE(instances.connect("mesh", "instances", 0).ok());
        REQUIRE(instances.connect("points", "instances", 1).ok());
        auto result = instances.execute("instances");
        REQUIRE(result.ok());
        CHECK_EQ(result.value().getIndexCount(), tiled.value().getIndexCount() * 2);
    };
}


}  // namespace test_procgen_grid_graph
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_procgen_point_delta)
#include "PrivateTestScope.h"
#include <cstdint>
#include <memory>
#include <string>
#include "procgen/Biome.h"
#include "procgen/PointDelta.h"
#include "procgen/PointGraph.h"
#include "procgen/ShapeGrammar.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// procgen_point_delta.cpp
namespace test_procgen_point_delta {
// procgen_point_delta.cpp
using namespace eve::procgen;

namespace {

PointSet makePointSet(int count, std::uint64_t identityNamespace = 91) {
    PointSet points;
    for (int index = 0; index < count; ++index) {
        ProcgenPoint point;
        point.x            = float(index);
        point.seed         = std::uint32_t(index + 1);
        const int appended = points.appendPoint(point);
        (void)appended;
        points.trySetIntAttribute(std::size_t(index), "kind", index % 3).expect("point delta fixture metadata");
    }
    points.assignPointIds(identityNamespace).expect("point delta fixture ids");
    return points;
}

}  // namespace

TEST_CASE("procgen.pointIdentity.spec.assignmentAndSetOperations") {
    SUB_CASE("seed and transforms -> stable point identities") {
        const PointSet first  = sampleGridPoints(8, 8, 1.f, 42, 0.1f);
        const PointSet second = sampleGridPoints(8, 8, 1.f, 42, 0.1f);
        REQUIRE_EQ(first.getCount(), second.getCount());
        for (int index = 0; index < first.getCount(); ++index) {
            CHECK_NE(first.getPointId(index), std::uint64_t(0));
            CHECK_EQ(first.getPointId(index), second.getPointId(index));
        }
        const PointSet transformed = transformPointSet3D(first, 10.f, 2.f, -4.f, 0.f, 30.f, 0.f, 2.f, 2.f, 2.f);
        REQUIRE_EQ(transformed.getCount(), first.getCount());
        for (int index = 0; index < first.getCount(); ++index)
            CHECK_EQ(transformed.getPointId(index), first.getPointId(index));
    };
    SUB_CASE("invalid identity assignment -> original set unchanged") {
        PointSet     legacy = makePointSet(3);
        ProcgenPoint duplicate;
        duplicate.id             = legacy.getPointId(0);
        const int duplicateIndex = legacy.appendPoint(duplicate);
        (void)duplicateIndex;
        const auto before = legacy.points();
        auto       failed = legacy.assignPointIds(33);
        CHECK(!failed.ok());
        REQUIRE_EQ(legacy.points().size(), before.size());
        for (std::size_t index = 0; index < before.size(); ++index)
            CHECK_EQ(legacy.points()[index].id, before[index].id);

        PointSet unassigned;
        unassigned.add(0.f, 0.f, 0.f);
        unassigned.add(1.f, 0.f, 0.f);
        PointSet repeated = unassigned;
        unassigned.assignPointIds(44).expect("assign legacy ids");
        repeated.assignPointIds(44).expect("repeat legacy ids");
        CHECK_EQ(unassigned.getPointId(0), repeated.getPointId(0));
        CHECK_EQ(unassigned.getPointId(1), repeated.getPointId(1));
    };
    SUB_CASE("set operations with shared IDs -> stable identity preference") {
        PointSet first = makePointSet(2);
        PointSet moved = first;
        moved.setPosition(0, 100.f, 0.f, 0.f);
        CHECK_EQ(unionPointSets(first, moved).getCount(), 2);
        CHECK_EQ(intersectPointSets(first, moved).getCount(), 2);

        PointSet     distinct;
        ProcgenPoint samePosition = first.points()[0];
        samePosition.id           = derivePointId(999, 1);
        const int distinctIndex   = distinct.appendPoint(samePosition);
        (void)distinctIndex;
        CHECK_EQ(unionPointSets(first, distinct).getCount(), 3);
    };
}

TEST_CASE("procgen.pointDelta.spec.roundtripStaleBaseAndLargeUnion") {
    SUB_CASE("add, update, remove, reorder delta -> reconstructed target") {
        PointSet before = makePointSet(4);
        PointSet after;
        after.appendPointFrom(before, 2).expect("reorder point");
        after.appendPointFrom(before, 0).expect("reorder point");
        after.setPosition(1, 20.f, 3.f, -2.f);
        after.trySetStringAttribute(1, "asset", "oak").expect("updated metadata");
        ProcgenPoint added;
        added.id             = derivePointId(92, 10);
        added.x              = 9.f;
        added.seed           = 88;
        const int addedIndex = after.appendPoint(added);
        (void)addedIndex;
        after.trySetStringAttribute(2, "asset", "rock").expect("added metadata");

        auto deltaResult = diffPointSets(before, after);
        REQUIRE(deltaResult.ok());
        PointDelta delta = std::move(deltaResult).takeValue();
        CHECK_EQ(delta.added.getCount(), 1);
        CHECK_EQ(delta.updated.getCount(), 2);
        CHECK_EQ(delta.removed.size(), std::size_t(2));
        auto applied = applyPointDelta(before, delta);
        REQUIRE(applied.ok());
        auto expectedFingerprint = fingerprintPointSet(after);
        auto actualFingerprint   = fingerprintPointSet(applied.value());
        REQUIRE(expectedFingerprint.ok());
        REQUIRE(actualFingerprint.ok());
        CHECK_EQ(actualFingerprint.value(), expectedFingerprint.value());
    };
    SUB_CASE("stale delta base -> error without target mutation") {
        PointSet before = makePointSet(3);
        PointSet after  = before;
        after.setPosition(0, 5.f, 0.f, 0.f);
        auto delta = diffPointSets(before, after);
        REQUIRE(delta.ok());
        before.setPosition(1, 7.f, 0.f, 0.f);
        auto applied = applyPointDelta(before, delta.value());
        CHECK(!applied.ok());
        CHECK_EQ(before.getX(1), 7.f);
    };
    SUB_CASE("large identity union -> bounded work and correct count") {
        PointSet first  = makePointSet(10000, 700);
        PointSet second = makePointSet(10000, 701);
        CHECK_EQ(unionPointSets(first, second).getCount(), 20000);
    };
}


// procgen_point_graph_plan.cpp
using namespace eve::procgen;

namespace {
void makePlanFork(PointGraph& graph) {
    graph.setComputePolicy("cpu");
    PointSet points;
    points.add(1.f, 0.f, 0.f);
    REQUIRE(graph.addNode("source", "input"));
    REQUIRE(graph.setNodePoints("source", &points));
    for (const auto* name : {"left", "right"}) {
        REQUIRE(graph.addNode(name, "transform"));
        REQUIRE(graph.connect("source", name));
    }
}

void requirePlanOutput(PointGraph& graph, const std::string& id) {
    std::unique_ptr<PointSet> result(graph.execute(id));
    REQUIRE(bool(result));
    REQUIRE_EQ(result->getCount(), 1);
}
}  // namespace

TEST_CASE("procgen.pointGraphPlan.spec.cacheInvalidationAndEviction") {
    SUB_CASE("alternating outputs and parameters -> execution plan reuse") {
        PointGraph graph;
        makePlanFork(graph);
        requirePlanOutput(graph, "left");
        requirePlanOutput(graph, "right");
        requirePlanOutput(graph, "left");
        REQUIRE_EQ(graph.getExecutionPlanBuildCount(), uint64_t(2));
        REQUIRE(graph.setNodeFloat("left", "x", 5.f));
        requirePlanOutput(graph, "left");
        requirePlanOutput(graph, "right");
        REQUIRE_EQ(graph.getExecutionPlanBuildCount(), uint64_t(2));
    };
    SUB_CASE("changed branch or failure -> only affected plans invalidated") {
        PointGraph graph;
        makePlanFork(graph);
        requirePlanOutput(graph, "left");
        requirePlanOutput(graph, "right");
        REQUIRE(graph.disconnect("left", 0));
        requirePlanOutput(graph, "right");
        REQUIRE_EQ(graph.getExecutionPlanBuildCount(), uint64_t(2));
        std::unique_ptr<PointSet> invalid(graph.execute("left"));
        REQUIRE(!invalid);
        REQUIRE_EQ(graph.getCompiledSegmentCount(), 0);
        requirePlanOutput(graph, "right");
        REQUIRE_EQ(graph.getExecutionPlanBuildCount(), uint64_t(2));
        REQUIRE(graph.connect("source", "left"));
        requirePlanOutput(graph, "left");
        REQUIRE_EQ(graph.getExecutionPlanBuildCount(), uint64_t(3));
        REQUIRE(graph.removeNode("left"));
        requirePlanOutput(graph, "right");
        REQUIRE_EQ(graph.getExecutionPlanBuildCount(), uint64_t(3));
        REQUIRE(graph.removeNode("source"));
        std::unique_ptr<PointSet> stale(graph.execute("right"));
        REQUIRE(!stale);
        REQUIRE_EQ(graph.getExecutionPlanBuildCount(), uint64_t(3));
    };
    SUB_CASE("bounded cache -> least recently used plan evicted") {
        PointGraph graph;
        graph.setComputePolicy("cpu");
        PointSet points;
        points.add(0.f, 0.f, 0.f);
        for (int index = 0; index < 16; ++index) {
            const auto name = "output" + std::to_string(index);
            REQUIRE(graph.addNode(name, "input"));
            REQUIRE(graph.setNodePoints(name, &points));
            requirePlanOutput(graph, name);
        }
        requirePlanOutput(graph, "output0");
        REQUIRE_EQ(graph.getExecutionPlanBuildCount(), uint64_t(16));
        REQUIRE(graph.addNode("extra", "input"));
        REQUIRE(graph.setNodePoints("extra", &points));
        requirePlanOutput(graph, "extra");
        requirePlanOutput(graph, "output0");
        REQUIRE_EQ(graph.getExecutionPlanBuildCount(), uint64_t(17));
        requirePlanOutput(graph, "output1");
        REQUIRE_EQ(graph.getExecutionPlanBuildCount(), uint64_t(18));
    };
    SUB_CASE("copy or restore -> independent plan cache and dropped stale plans") {
        PointGraph graph;
        makePlanFork(graph);
        requirePlanOutput(graph, "left");
        requirePlanOutput(graph, "right");
        PointGraph copy = graph;
        REQUIRE(copy.disconnect("left", 0));
        requirePlanOutput(graph, "left");
        REQUIRE_EQ(graph.getExecutionPlanBuildCount(), uint64_t(2));
        requirePlanOutput(copy, "right");
        REQUIRE_EQ(copy.getExecutionPlanBuildCount(), uint64_t(2));
        const auto definition = graph.serializeDefinition();
        REQUIRE(copy.deserializeDefinition(definition));
        REQUIRE_EQ(copy.getExecutionPlanBuildCount(), uint64_t(0));
        REQUIRE_EQ(copy.getCompiledSegmentCount(), 0);
        PointSet points;
        points.add(1.f, 0.f, 0.f);
        REQUIRE(copy.setNodePoints("source", &points));
        requirePlanOutput(copy, "left");
        REQUIRE_EQ(copy.getExecutionPlanBuildCount(), uint64_t(1));
    };
    SUB_CASE("compiled output plan -> reused execution on repeat") {
        PointSet points;
        points.add(1.f, 0.f, 0.f);
        PointGraph graph;
        REQUIRE(graph.addNode("source", "input"));
        REQUIRE(graph.addNode("first", "transform"));
        REQUIRE(graph.addNode("second", "transform"));
        REQUIRE(graph.connect("source", "first"));
        REQUIRE(graph.connect("first", "second"));
        REQUIRE(graph.setNodePoints("source", &points));
        graph.setComputePolicy("cpu");

        std::unique_ptr<PointSet> first(graph.execute("second"));
        REQUIRE(bool(first));
        REQUIRE_EQ(graph.getCompiledSegmentCount(), 2);
        REQUIRE_EQ(graph.getExecutionPlanBuildCount(), uint64_t(1));

        REQUIRE(graph.setNodeFloat("second", "x", 4.f));
        std::unique_ptr<PointSet> parameterChange(graph.execute("second"));
        REQUIRE(bool(parameterChange));
        REQUIRE_EQ(graph.getExecutionPlanBuildCount(), uint64_t(1));

        REQUIRE(graph.addNode("third", "transform"));
        REQUIRE(graph.connect("second", "third"));
        std::unique_ptr<PointSet> topologyChange(graph.execute("third"));
        REQUIRE(bool(topologyChange));
        REQUIRE_EQ(graph.getExecutionPlanBuildCount(), uint64_t(2));
        REQUIRE_EQ(graph.getCompiledSegmentCount(), 2);
    };
}

TEST_CASE("procgen.pointGraphPlan.spec.gpuBranchBoundary") {
    SUB_CASE("GPU boundary input -> planned branch split before execution") {
        PointSet points;
        points.add(1.f, 0.f, 0.f);
        PointGraph graph;
        REQUIRE(graph.addNode("source", "input"));
        REQUIRE(graph.addNode("shared", "transform"));
        REQUIRE(graph.addNode("left", "transform"));
        REQUIRE(graph.addNode("right", "transform"));
        REQUIRE(graph.addNode("merged", "merge"));
        REQUIRE(graph.connect("source", "shared"));
        REQUIRE(graph.connect("shared", "left"));
        REQUIRE(graph.connect("shared", "right"));
        REQUIRE(graph.connect("left", "merged", 0));
        REQUIRE(graph.connect("right", "merged", 1));
        graph.setComputePolicy("cpu");
        REQUIRE(graph.setNodePoints("source", &points));

        std::unique_ptr<PointSet> output(graph.execute("merged"));
        REQUIRE(bool(output));
        REQUIRE_EQ(output->getCount(), 2);
        REQUIRE_EQ(graph.getCompiledSegmentCount(), 5);
    };
}


// procgen_point_graph_stress.cpp
using namespace eve::procgen;

namespace {

bool pointSetsEqual(const PointSet& first, const PointSet& second) {
    if (first.getCount() != second.getCount()) return false;
    if (first.attributes().columnCount() != second.attributes().columnCount()) return false;
    for (int index = 0; index < first.getCount(); ++index) {
        const ProcgenPoint& a = first.points()[size_t(index)];
        const ProcgenPoint& b = second.points()[size_t(index)];
        if (a.x != b.x || a.y != b.y || a.z != b.z || a.normalX != b.normalX || a.normalY != b.normalY ||
            a.normalZ != b.normalZ || a.yaw != b.yaw || a.scaleX != b.scaleX || a.scaleY != b.scaleY ||
            a.scaleZ != b.scaleZ || a.density != b.density || a.seed != b.seed)
            return false;
        for (size_t column = 0; column < first.attributes().columnCount(); ++column) {
            const std::string name(first.attributes().columnName(column));
            if (first.attributes().typeOf(name) != second.attributes().typeOf(name) ||
                first.getAttributeType(index, name) != second.getAttributeType(index, name))
                return false;
            switch (*first.attributes().typeOf(name)) {
                case ProcgenAttributeType::Float:
                    if (first.attributes().getFloat(index, name) != second.attributes().getFloat(index, name))
                        return false;
                    break;
                case ProcgenAttributeType::Int:
                    if (first.attributes().getInt(index, name) != second.attributes().getInt(index, name)) return false;
                    break;
                case ProcgenAttributeType::Bool:
                    if (first.attributes().getBool(index, name) != second.attributes().getBool(index, name))
                        return false;
                    break;
                case ProcgenAttributeType::String:
                    if (first.getStringAttribute(index, name, {}) != second.getStringAttribute(index, name, {}))
                        return false;
                    break;
                case ProcgenAttributeType::Vector: {
                    const auto firstValue  = first.attributes().getVector(index, name);
                    const auto secondValue = second.attributes().getVector(index, name);
                    if (firstValue.has_value() != secondValue.has_value()) return false;
                    if (firstValue && (firstValue->x != secondValue->x || firstValue->y != secondValue->y ||
                                       firstValue->z != secondValue->z))
                        return false;
                    break;
                }
            }
        }
    }
    return true;
}

}  // namespace

TEST_CASE("procgen.pointGraphStress.spec.budgetDeterminismAndRecovery") {
    SUB_CASE("deep graph and small budget -> resumable deterministic completion") {
        constexpr int nodeCount = 64;
        PointSet      input;
        input.add(0.f, 0.f, 0.f);
        PointGraph graph;
        CHECK(graph.addNode("source", "input"));
        CHECK(graph.setNodePoints("source", &input));
        std::string previous = "source";
        for (int index = 0; index < nodeCount; ++index) {
            const std::string id = "move-" + std::to_string(index);
            CHECK(graph.addNode(id, "transform"));
            CHECK(graph.connect(previous, id));
            CHECK(graph.setNodeFloat(id, "x", 0.25f));
            previous = id;
        }
        CHECK(graph.validate());
        graph.setExecutionNodeBudget(17);

        std::unique_ptr<PointSet> output;
        int                       budgetWindows = 0;
        while (!output && budgetWindows < 20) {
            output.reset(graph.execute(previous));
            ++budgetWindows;
            if (!output) CHECK(graph.wasCancelled());
        }
        REQUIRE(bool(output));
        CHECK_EQ(output->getX(0), 16.f);
        CHECK_EQ(budgetWindows, 4);
        CHECK(graph.getCacheHitCount() > 0);
    };
    SUB_CASE("compute policy selection -> deterministic CPU result") {
        PointSet input;
        input.add(2.f, 3.f, 4.f);
        PointGraph graph;
        CHECK(graph.addNode("source", "input"));
        CHECK(graph.addNode("move", "transform"));
        CHECK(graph.connect("source", "move"));
        CHECK(graph.setNodePoints("source", &input));
        CHECK(graph.setNodeFloat("move", "x", 5.f));
        CHECK(!graph.setComputePolicy("invalid"));
        CHECK(graph.setComputePolicy("cpu"));
        graph.setComputeMinimumPoints(0);

        std::unique_ptr<PointSet> output(graph.execute("move"));
        REQUIRE(bool(output));
        CHECK_EQ(output->getX(0), 7.f);
        CHECK_EQ(graph.getComputePolicy(), "cpu");
        CHECK_EQ(graph.getComputeMinimumPoints(), 0);
        CHECK_EQ(graph.getMetricBackend(graph.getMetricCount() - 1), "cpu");
        CHECK(graph.getComputeFallbackReason().empty());
        CHECK_EQ(graph.getComputeUploadCount(), uint64_t(0));
        CHECK_EQ(graph.getComputeDispatchCount(), uint64_t(0));
        CHECK_EQ(graph.getComputeReadbackCount(), uint64_t(0));
        CHECK_EQ(graph.getComputeBufferReuseCount(), uint64_t(0));
        CHECK_EQ(graph.getComputePeakBufferBytes(), uint64_t(0));
        CHECK_EQ(graph.getLastFusedTransformCount(), 0);
        CHECK(graph.debugReport().find("backend=cpu") != std::string::npos);
    };
    SUB_CASE("independent biome instances with same seed -> bit-stable points") {
        SpatialData domain = SpatialData::box(-32.f, 0.f, -32.f, 32.f, 0.f, 32.f);
        SpatialData center = SpatialData::sphere(0.f, 0.f, 0.f, 12.f);
        BiomeRules  rules;
        CHECK(rules.addLayer("forest", &domain, 1, 0.85f));
        CHECK(rules.addLayer("clearing", &center, 5, 0.35f));
        CHECK(rules.addAsset("forest", "oak", 3.f, 0.8f, 1.3f, true));
        CHECK(rules.addAsset("forest", "pine", 1.f, 0.9f, 1.4f, true));
        CHECK(rules.addAsset("clearing", "rock", 1.f, 0.7f, 1.1f, true));

        PointGraph asset;
        CHECK(asset.addNode("biome", "biome.generate"));
        CHECK(asset.setNodeFloat("biome", "spacing", 1.f));
        CHECK(asset.setNodeFloat("biome", "jitter", 0.75f));
        CHECK(asset.setNodeInt("biome", "seed", 0x5a17));
        std::unique_ptr<PointGraph> first(asset.instantiate());
        std::unique_ptr<PointGraph> second(asset.instantiate());
        REQUIRE(bool(first));
        REQUIRE(bool(second));
        CHECK(first->setNodeSpatial("biome", &domain));
        CHECK(first->setNodeBiomeRules("biome", &rules));
        CHECK(second->setNodeSpatial("biome", &domain));
        CHECK(second->setNodeBiomeRules("biome", &rules));

        std::unique_ptr<PointSet> firstOutput(first->execute("biome"));
        std::unique_ptr<PointSet> secondOutput(second->execute("biome"));
        REQUIRE(bool(firstOutput));
        REQUIRE(bool(secondOutput));
        CHECK(firstOutput->getCount() > 3000);
        CHECK(firstOutput->getCount() < 4225);
        CHECK(pointSetsEqual(*firstOutput, *secondOutput));
    };
    SUB_CASE("external rule or data error -> recoverable subsequent run") {
        PointSet path;
        path.add(0.f, 0.f, 0.f);
        path.add(10.f, 0.f, 0.f);
        ShapeGrammar grammar;
        CHECK(grammar.addModule("A", "wall", 2.f));
        PointGraph graph;
        CHECK(graph.addNode("path", "input"));
        CHECK(graph.addNode("facade", "grammar.generate"));
        CHECK(graph.connect("path", "facade"));
        CHECK(graph.setNodeString("facade", "grammar", "Missing+"));
        CHECK(!graph.execute("facade"));
        CHECK(graph.getError().find("no points") != std::string::npos);

        CHECK(graph.setNodePoints("path", &path));
        CHECK(graph.setNodeShapeGrammar("facade", &grammar));
        CHECK(!graph.execute("facade"));
        CHECK(graph.getError().find("unknown module") != std::string::npos);
        CHECK(graph.setNodeString("facade", "grammar", "A+"));
        std::unique_ptr<PointSet> recovered(graph.execute("facade"));
        REQUIRE(bool(recovered));
        CHECK_EQ(recovered->getCount(), 5);
        CHECK_EQ(recovered->getStringAttribute(0, "asset", ""), std::string("wall"));
    };
}

TEST_CASE("procgen.pointGraphStress.spec.outputAndMergePreflight") {
    SUB_CASE("oversized output request -> rejection before spatial allocation") {
        SpatialData domain = SpatialData::box(0.f, 0.f, 0.f, 100.f, 0.f, 100.f);
        PointGraph  graph;
        CHECK(graph.addNode("sample", "spatial.sample"));
        CHECK(graph.setNodeSpatial("sample", &domain));
        CHECK(graph.setNodeFloat("sample", "spacing", 1.f));
        graph.setMaxNodeOutputPoints(1000);
        CHECK_EQ(graph.getMaxNodeOutputPoints(), 1000);
        CHECK(!graph.execute("sample"));
        CHECK(graph.getError().find("point budget") != std::string::npos);
        CHECK(!graph.getNodeOutput("sample"));

        graph.setMaxNodeOutputPoints(11000);
        std::unique_ptr<PointSet> output(graph.execute("sample"));
        REQUIRE(bool(output));
        CHECK_EQ(output->getCount(), 10201);

        graph.setMaxNodeOutputPoints(1000);
        CHECK(!graph.execute("sample"));
        CHECK(graph.getError().find("point budget") != std::string::npos);

        Heightmap heightmap(11, 11);
        heightmap.setHeight(10, 10, 100.f);
        SpatialData terrain = SpatialData::heightfield(heightmap, 0.f, 0.f, 1.f, 1.f);
        PointGraph  terrainGraph;
        CHECK(terrainGraph.addNode("sample", "spatial.sample"));
        CHECK(terrainGraph.setNodeSpatial("sample", &terrain));
        terrainGraph.setMaxNodeOutputPoints(121);
        output.reset(terrainGraph.execute("sample"));
        REQUIRE(bool(output));
        CHECK_EQ(output->getCount(), 121);
    };
    SUB_CASE("merge and copy allocations above limit -> preflight failure") {
        PointSet first;
        PointSet second;
        for (int index = 0; index < 60; ++index) {
            first.add(float(index), 0.f, 0.f);
            second.add(0.f, 0.f, float(index));
        }
        PointGraph graph;
        CHECK(graph.addNode("first", "input"));
        CHECK(graph.addNode("second", "input"));
        CHECK(graph.addNode("merge", "merge"));
        CHECK(graph.addNode("copy", "copy.points"));
        graph.setMaxNodeOutputPoints(100);
        CHECK(graph.setNodePoints("first", &first));
        CHECK(graph.setNodePoints("second", &second));
        CHECK(graph.connect("first", "merge", 0));
        CHECK(graph.connect("second", "merge", 1));
        CHECK(graph.connect("first", "copy", 0));
        CHECK(graph.connect("second", "copy", 1));
        CHECK(graph.setNodeInt("copy", "maxPoints", 10000));
        CHECK(!graph.execute("merge"));
        CHECK(graph.getError().find("point budget") != std::string::npos);
        CHECK(!graph.execute("copy"));
        CHECK(graph.getError().find("point budget") != std::string::npos);

        PointSet oversized;
        for (int index = 0; index < 101; ++index) oversized.add(float(index), 0.f, 0.f);
        CHECK(!graph.setNodePoints("first", &oversized));
        CHECK(graph.getError().find("input exceeds") != std::string::npos);
    };
}


}  // namespace test_procgen_point_delta
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_procgen_point_graph)
#include "PrivateTestScope.h"
#include <cmath>
#include <memory>
#include "procgen/Biome.h"
#include "procgen/MeshBuild.h"
#include "procgen/PointGraph.h"
#include "procgen/ShapeGrammar.h"
#include "procgen/heightmap/Heightmap.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// procgen_point_graph.cpp
namespace test_procgen_point_graph {
using namespace eve::procgen;

TEST_CASE("procgen.pointGraph.spec.dagTypingBranchesAndSerialization") {
    SUB_CASE("DAG input -> executed output with cached repeat") {
        PointGraph graph;
        CHECK(graph.addNode("sample", "spatial.sample"));
        CHECK(graph.addNode("exclude", "spatial.filter"));
        CHECK(graph.addNode("prune", "self.prune"));
        CHECK(graph.connect("sample", "exclude"));
        CHECK(graph.connect("exclude", "prune"));

        SpatialData domain = SpatialData::box(0.f, 0.f, 0.f, 4.f, 0.f, 4.f);
        SpatialData hole   = SpatialData::sphere(2.f, 0.f, 2.f, 0.75f);
        CHECK(graph.setNodeSpatial("sample", &domain));
        CHECK(graph.setNodeFloat("sample", "spacing", 2.f));
        CHECK(graph.setNodeInt("sample", "seed", 42));
        CHECK(graph.setNodeSpatial("exclude", &hole));
        CHECK(graph.setNodeInt("exclude", "invert", 1));
        CHECK(graph.setNodeFloat("prune", "radius", 1.f));
        CHECK(graph.validate());

        PointSet* first = graph.execute("prune");
        REQUIRE(bool(first));
        CHECK_EQ(first->getCount(), 8);
        CHECK(graph.getMetricCount() >= 3);
        PointSet* debug = graph.getNodeOutput("sample");
        REQUIRE(bool(debug));
        CHECK_EQ(debug->getCount(), 9);

        PointSet* second = graph.execute("prune");
        REQUIRE(bool(second));
        CHECK_EQ(second->getCount(), 8);
        CHECK_EQ(graph.getCacheHitCount(), 1);
        CHECK(graph.isMetricCacheHit(0));

        delete second;
        delete debug;
        delete first;
    };
    SUB_CASE("cycle or bad input -> rejected graph") {
        PointGraph graph;
        CHECK(graph.addNode("a", "self.prune"));
        CHECK(graph.addNode("b", "jitter"));
        CHECK(graph.connect("a", "b"));
        CHECK(graph.connect("b", "a"));
        CHECK(!graph.validate());
        CHECK(graph.getError().find("cycle") != std::string::npos);
        CHECK(!graph.execute("a"));
        CHECK(graph.getError().find("cycle") != std::string::npos);
        CHECK(!graph.addNode("unknown", "not-an-operation"));
    };
    SUB_CASE("terrain slope projection -> filtered points") {
        PointSet source;
        source.add(0.f, 0.f, 0.f);
        source.add(1.f, 0.f, 0.f);
        source.setNormal(0, 0.f, 1.f, 0.f);
        source.setNormal(1, 1.f, 0.f, 0.f);
        PointGraph graph;
        CHECK(graph.addNode("source", "input"));
        CHECK(graph.addNode("slope", "filter.slope"));
        CHECK(graph.setNodePoints("source", &source));
        CHECK(graph.connect("source", "slope"));
        CHECK(graph.setNodeFloat("slope", "minDegrees", 0.f));
        CHECK(graph.setNodeFloat("slope", "maxDegrees", 45.f));
        CHECK(graph.validate());
        std::unique_ptr<PointSet> result(graph.execute("slope"));
        REQUIRE(bool(result));
        CHECK_EQ(result->getCount(), 1);
        CHECK_EQ(result->getNormalY(0), 1.f);
    };
    SUB_CASE("branch and nested graph -> composed outputs") {
        PointSet source;
        source.add(1.f, 0.f, 0.f);

        PointGraph nested;
        CHECK(nested.addNode("in", "input"));
        CHECK(nested.addNode("move", "transform"));
        CHECK(nested.setNodePoints("in", &source));
        CHECK(nested.connect("in", "move"));
        CHECK(nested.setNodeFloat("move", "x", 5.f));

        PointGraph graph;
        CHECK(graph.addNode("source", "input"));
        CHECK(graph.addNode("nested", "subgraph"));
        CHECK(graph.addNode("fallback", "transform"));
        CHECK(graph.addNode("choose", "branch"));
        CHECK(graph.setNodePoints("source", &source));
        CHECK(graph.connect("source", "nested"));
        CHECK(graph.setNodeSubgraph("nested", &nested, "in", "move"));
        CHECK(graph.connect("source", "fallback"));
        CHECK(graph.setNodeFloat("fallback", "x", -5.f));
        CHECK(graph.connect("nested", "choose", 0));
        CHECK(graph.connect("fallback", "choose", 1));
        CHECK(graph.setNodeInt("choose", "condition", 1));

        PointSet* result = graph.execute("choose");
        REQUIRE(bool(result));
        CHECK_EQ(result->getCount(), 1);
        CHECK_EQ(result->getX(0), 6.f);
        delete result;
    };
    SUB_CASE("node reflection -> editor-visible operations") {
        CHECK(PointGraph::getOperationCount() >= 15);
        CHECK_EQ(PointGraph::getOperationInputCount("merge"), 2);
        CHECK_EQ(PointGraph::getOperationInputCount("spatial.sample"), 0);
        CHECK_EQ(PointGraph::getOperationParamCount("transform"), 7);
        CHECK_EQ(PointGraph::getOperationParamKey("spatial.sample", 0), std::string("spacing"));
        CHECK_EQ(PointGraph::getOperationParamKind("spatial.sample", 1), std::string("int"));
        CHECK_EQ(PointGraph::getOperationParamDefault("branch", 0), std::string("false"));
        CHECK_EQ(PointGraph::getOperationInputCount("filter.slope"), 1);
        CHECK_EQ(PointGraph::getOperationInputCount("missing"), -1);
    };
    SUB_CASE("undeclared pin or bad external input -> validation error") {
        PointGraph nested;
        CHECK(nested.addNode("in", "input"));
        CHECK(nested.addNode("move", "transform"));
        CHECK(nested.connect("in", "move"));

        PointGraph graph;
        CHECK(graph.addNode("source", "input"));
        CHECK(graph.addNode("nested", "subgraph"));
        CHECK(graph.connect("source", "nested"));
        CHECK(!graph.connect("source", "nested", 1));
        CHECK(!graph.connect("source", "source", 0));
        CHECK(graph.setNodeSubgraph("nested", &nested, "in", "move"));

        PointSet input;
        input.add(1.f, 2.f, 3.f);
        CHECK(graph.setNodePoints("source", &input));
        CHECK(graph.validate());
        PointSet* output = graph.execute("nested");
        REQUIRE(bool(output));
        CHECK_EQ(output->getCount(), 1);
        delete output;
    };
    SUB_CASE("nested graph definition serialization -> equivalent topology and parameters") {
        PointSet source;
        source.add(1.f, 0.f, 0.f);

        PointGraph nested;
        nested.addNode("in", "input");
        nested.addNode("move", "transform");
        nested.setNodePoints("in", &source);
        nested.connect("in", "move");
        nested.setNodeFloat("move", "x", 5.f);

        PointGraph original;
        original.addNode("source", "input");
        original.addNode("nested", "subgraph");
        original.addNode("tag", "attribute.set.string");
        original.setNodePoints("source", &source);
        original.connect("source", "nested");
        original.setNodeSubgraph("nested", &nested, "in", "move");
        original.connect("nested", "tag");
        original.setNodeString("tag", "attribute", "asset");
        original.setNodeString("tag", "value", "oak \"old\"");

        const std::string definition = original.serializeDefinition();
        CHECK(definition.find("EVPCG_POINT_GRAPH 1") == 0);
        PointGraph loaded;
        CHECK(loaded.deserializeDefinition(definition));
        CHECK_EQ(loaded.getNodeCount(), 3);
        CHECK_EQ(loaded.getInputNode("tag", 0), std::string("nested"));
        CHECK(loaded.setNodePoints("source", &source));
        PointSet* result = loaded.execute("tag");
        REQUIRE(bool(result));
        CHECK_EQ(result->getX(0), 6.f);
        CHECK_EQ(result->getStringAttribute(0, "asset", ""), std::string("oak \"old\""));
        delete result;

        CHECK(!loaded.deserializeDefinition("broken"));
        CHECK(loaded.hasNode("source"));
        CHECK(!loaded.deserializeDefinition(
            "EVPCG_POINT_GRAPH 1\nNODE \"move\" \"transform\"\nS \"move\" \"x\" \"bad\"\nEND\n"));
        CHECK(loaded.hasNode("source"));
    };
    SUB_CASE("changed node -> only descendant cache invalidation") {
        PointSet points;
        points.add(1.f, 0.f, 0.f);
        PointGraph graph;
        CHECK(graph.addNode("source", "input"));
        CHECK(graph.addNode("left", "transform"));
        CHECK(graph.addNode("right", "transform"));
        CHECK(graph.connect("source", "left"));
        CHECK(graph.connect("source", "right"));
        CHECK(graph.setNodePoints("source", &points));
        PointSet* left  = graph.execute("left");
        PointSet* right = graph.execute("right");
        REQUIRE(bool(left));
        REQUIRE(bool(right));
        delete right;
        delete left;

        const uint64_t revision = graph.getRevision();
        CHECK(graph.setNodeFloat("left", "x", 5.f));
        CHECK(graph.getRevision() > revision);
        PointSet* sourceCache = graph.getNodeOutput("source");
        PointSet* rightCache  = graph.getNodeOutput("right");
        CHECK(bool(sourceCache));
        CHECK(bool(rightCache));
        CHECK(!graph.getNodeOutput("left"));
        delete rightCache;
        delete sourceCache;
    };
    SUB_CASE("parameter insertion order -> stable definition serialization") {
        PointGraph first;
        PointGraph second;
        CHECK(first.addNode("move", "transform"));
        CHECK(second.addNode("move", "transform"));
        CHECK(first.addNode("tag", "attribute.set.string"));
        CHECK(second.addNode("tag", "attribute.set.string"));
        CHECK(first.setNodeFloat("move", "z", 3.f));
        CHECK(first.setNodeFloat("move", "x", 1.f));
        CHECK(first.setNodeString("tag", "value", "oak"));
        CHECK(first.setNodeString("tag", "attribute", "asset"));
        CHECK(second.setNodeString("tag", "attribute", "asset"));
        CHECK(second.setNodeString("tag", "value", "oak"));
        CHECK(second.setNodeFloat("move", "x", 1.f));
        CHECK(second.setNodeFloat("move", "z", 3.f));
        CHECK_EQ(first.serializeDefinition(), second.serializeDefinition());
        CHECK(!first.setNodeString("move", "typo", "rejected"));
        CHECK(!first.setNodeInt("move", "x", 1));
        CHECK(!first.setNodeFloat("tag", "value", 1.f));
    };
}

TEST_CASE("procgen.pointGraph.spec.parametersBudgetAndRuntime") {
    SUB_CASE("exposed typed parameter -> reflected value and type") {
        PointSet points;
        points.add(1.f, 0.f, 0.f);
        PointGraph graph;
        CHECK(graph.addNode("source", "input"));
        CHECK(graph.addNode("move", "transform"));
        CHECK(graph.connect("source", "move"));
        CHECK(graph.setNodePoints("source", &points));
        CHECK(graph.setNodeFloat("move", "x", 2.f));
        CHECK(graph.exposeParameter("offset", "move", "x"));
        CHECK_EQ(graph.getParameterCount(), 1);
        CHECK_EQ(graph.getParameterName(0), std::string("offset"));
        CHECK_EQ(graph.getParameterKind("offset"), std::string("float"));
        CHECK_EQ(graph.getParameterFloat("offset", -1.f), 2.f);
        CHECK(!graph.exposeParameter("alias", "move", "x"));
        CHECK(!graph.exposeParameter("bad", "move", "missing"));
        CHECK(!graph.setParameterInt("offset", 7));

        PointSet* result = graph.execute("move");
        REQUIRE(bool(result));
        CHECK_EQ(result->getX(0), 3.f);
        delete result;

        CHECK(graph.setParameterFloat("offset", 5.f));
        CHECK(graph.hasParameterOverride("offset"));
        CHECK_EQ(graph.getParameterFloat("offset", -1.f), 5.f);
        result = graph.execute("move");
        REQUIRE(bool(result));
        CHECK_EQ(result->getX(0), 6.f);
        delete result;

        const std::string definition = graph.serializeDefinition();
        PointGraph        loaded;
        CHECK(loaded.deserializeDefinition(definition));
        CHECK_EQ(loaded.getParameterKind("offset"), std::string("float"));
        CHECK(!loaded.hasParameterOverride("offset"));
        CHECK_EQ(loaded.getParameterFloat("offset", -1.f), 2.f);
        CHECK(loaded.setNodePoints("source", &points));
        result = loaded.execute("move");
        REQUIRE(bool(result));
        CHECK_EQ(result->getX(0), 3.f);
        delete result;

        CHECK(graph.clearParameterOverride("offset"));
        CHECK(!graph.hasParameterOverride("offset"));
        CHECK_EQ(graph.getParameterFloat("offset", -1.f), 2.f);
        CHECK(graph.removeNode("move"));
        CHECK_EQ(graph.getParameterCount(), 0);
    };
    SUB_CASE("exposed parameters -> deterministic serialized form") {
        PointGraph first;
        PointGraph second;
        for (PointGraph* graph : {&first, &second}) {
            CHECK(graph->addNode("move", "transform"));
            CHECK(graph->addNode("tag", "attribute.set.string"));
        }
        CHECK(first.exposeParameter("zOffset", "move", "z"));
        CHECK(first.exposeParameter("asset", "tag", "value"));
        CHECK(second.exposeParameter("asset", "tag", "value"));
        CHECK(second.exposeParameter("zOffset", "move", "z"));
        CHECK_EQ(first.serializeDefinition(), second.serializeDefinition());
    };
    SUB_CASE("small work budget then cancellation -> cooperative status") {
        PointSet points;
        points.add(0.f, 0.f, 0.f);
        PointGraph graph;
        CHECK(graph.addNode("source", "input"));
        CHECK(graph.addNode("first", "transform"));
        CHECK(graph.addNode("second", "transform"));
        CHECK(graph.connect("source", "first"));
        CHECK(graph.connect("first", "second"));
        CHECK(graph.setNodePoints("source", &points));
        graph.setExecutionNodeBudget(2);
        CHECK_EQ(graph.getExecutionNodeBudget(), 2);
        CHECK(!graph.execute("second"));
        CHECK(graph.wasCancelled());
        CHECK(graph.getError().find("budget exceeded") != std::string::npos);
        PointSet* partial = graph.getNodeOutput("first");
        REQUIRE(bool(partial));
        delete partial;

        graph.setExecutionNodeBudget(3);
        PointSet* completed = graph.execute("second");
        REQUIRE(bool(completed));
        CHECK(!graph.wasCancelled());
        CHECK(graph.getCacheHitCount() > 0);
        delete completed;

        graph.requestCancel();
        CHECK(!graph.execute("second"));
        CHECK(graph.wasCancelled());
        CHECK(graph.getError().find("cancelled") != std::string::npos);
        graph.resetCancellation();
        CHECK(!graph.wasCancelled());
        completed = graph.execute("second");
        REQUIRE(bool(completed));
        delete completed;
    };
    SUB_CASE("source points across targets -> bounded copied output") {
        PointSet  source;
        const int sourcePoint = source.add(1.f, 2.f, 0.f);
        source.setPointSeed(sourcePoint, 11);
        source.setFloatAttribute(sourcePoint, "source", 2.f);
        source.setStringAttribute(sourcePoint, "shared", "source");
        PointSet  targets;
        const int firstTarget = targets.add(10.f, 20.f, 30.f);
        targets.setYaw(firstTarget, 90.f);
        targets.setScale(firstTarget, 2.f, 3.f, 4.f);
        targets.setDensity(firstTarget, 0.5f);
        targets.setPointSeed(firstTarget, 101);
        targets.setFloatAttribute(firstTarget, "target", 3.f);
        targets.setStringAttribute(firstTarget, "shared", "target");
        targets.add(-10.f, 0.f, 0.f);

        PointGraph graph;
        CHECK(graph.addNode("source", "input"));
        CHECK(graph.addNode("targets", "input"));
        CHECK(graph.addNode("copies", "copy.points"));
        CHECK(graph.setNodePoints("source", &source));
        CHECK(graph.setNodePoints("targets", &targets));
        CHECK(graph.connect("source", "copies", 0));
        CHECK(graph.connect("targets", "copies", 1));
        CHECK(graph.setNodeInt("copies", "maxPoints", 2));
        PointSet* result = graph.execute("copies");
        REQUIRE(bool(result));
        CHECK_EQ(result->getCount(), 2);
        CHECK(std::abs(result->getX(0) - 10.f) < 0.0001f);
        CHECK_EQ(result->getY(0), 26.f);
        CHECK(std::abs(result->getZ(0) - 32.f) < 0.0001f);
        CHECK_EQ(result->getYaw(0), 90.f);
        CHECK_EQ(result->getScaleX(0), 2.f);
        CHECK_EQ(result->getDensity(0), 0.5f);
        CHECK_EQ(result->getFloatAttribute(0, "target", -1.f), 3.f);
        CHECK_EQ(result->getFloatAttribute(0, "source", -1.f), 2.f);
        CHECK_EQ(result->getStringAttribute(0, "shared", ""), std::string("source"));
        delete result;

        CHECK(graph.setNodeInt("copies", "maxPoints", 1));
        CHECK(!graph.execute("copies"));
        CHECK(graph.getError().find("maxPoints") != std::string::npos);
    };
    SUB_CASE("missing required input -> error for each pin") {
        PointSet   points;
        PointGraph graph;
        CHECK(graph.addNode("source", "input"));
        CHECK(graph.addNode("copies", "copy.points"));
        CHECK(graph.setNodePoints("source", &points));
        CHECK(graph.connect("source", "copies", 0));
        CHECK(!graph.validate());
        CHECK(graph.getError().find("input 1") != std::string::npos);
    };
    SUB_CASE("remapped density and float metadata -> computed channels") {
        PointSet  points;
        const int first  = points.add(0.f, 0.f, 0.f);
        const int second = points.add(1.f, 0.f, 0.f);
        points.setDensity(first, -1.f);
        points.setDensity(second, 0.5f);
        points.setFloatAttribute(first, "age", 3.f);

        PointGraph graph;
        CHECK(graph.addNode("source", "input"));
        CHECK(graph.addNode("density", "density.remap"));
        CHECK(graph.addNode("math", "attribute.math.float"));
        CHECK(graph.setNodePoints("source", &points));
        CHECK(graph.connect("source", "density"));
        CHECK(graph.connect("density", "math"));
        CHECK(graph.setNodeFloat("density", "inputMin", 0.f));
        CHECK(graph.setNodeFloat("density", "inputMax", 1.f));
        CHECK(graph.setNodeFloat("density", "outputMin", 10.f));
        CHECK(graph.setNodeFloat("density", "outputMax", 20.f));
        CHECK(graph.setNodeString("math", "attribute", "age"));
        CHECK(graph.setNodeString("math", "outputAttribute", "score"));
        CHECK(graph.setNodeString("math", "operation", "multiply"));
        CHECK(graph.setNodeFloat("math", "operand", 2.f));
        CHECK(graph.setNodeFloat("math", "defaultValue", 4.f));
        PointSet* result = graph.execute("math");
        REQUIRE(bool(result));
        CHECK_EQ(result->getDensity(0), 10.f);
        CHECK_EQ(result->getDensity(1), 15.f);
        CHECK_EQ(result->getFloatAttribute(0, "score", -1.f), 6.f);
        CHECK_EQ(result->getFloatAttribute(1, "score", -1.f), 8.f);
        delete result;

        CHECK(graph.setNodeString("math", "operation", "divide"));
        CHECK(graph.setNodeFloat("math", "operand", 0.f));
        CHECK(!graph.execute("math"));
        CHECK(graph.getError().find("divide by zero") != std::string::npos);
        CHECK(graph.setNodeFloat("density", "inputMax", 0.f));
        CHECK(!graph.execute("density"));
        CHECK(graph.getError().find("non-zero input range") != std::string::npos);
    };
    SUB_CASE("spatial metrics request -> cached visualization data") {
        PointSet  points;
        const int first  = points.add(-2.f, 3.f, 4.f);
        const int second = points.add(6.f, -1.f, 8.f);
        points.setDensity(first, 0.25f);
        points.setDensity(second, 0.75f);
        PointGraph graph;
        CHECK(graph.addNode("source", "input"));
        CHECK(graph.setNodePoints("source", &points));
        PointSet* output = graph.execute("source");
        REQUIRE(bool(output));
        delete output;
        CHECK_EQ(graph.getMetricCount(), 1);
        CHECK_EQ(graph.getMetricMinX(0), -2.f);
        CHECK_EQ(graph.getMetricMinY(0), -1.f);
        CHECK_EQ(graph.getMetricMinZ(0), 4.f);
        CHECK_EQ(graph.getMetricMaxX(0), 6.f);
        CHECK_EQ(graph.getMetricMaxY(0), 3.f);
        CHECK_EQ(graph.getMetricMaxZ(0), 8.f);
        CHECK_EQ(graph.getMetricAverageDensity(0), 0.5f);

        output = graph.execute("source");
        REQUIRE(bool(output));
        delete output;
        CHECK(graph.isMetricCacheHit(0));
        CHECK_EQ(graph.getMetricMinX(0), -2.f);
        CHECK_EQ(graph.getMetricAverageDensity(0), 0.5f);
    };
    SUB_CASE("separate graph instances -> independent runtime state") {
        PointSet points;
        points.add(1.f, 0.f, 0.f);
        PointGraph asset;
        CHECK(asset.addNode("source", "input"));
        CHECK(asset.addNode("move", "transform"));
        CHECK(asset.connect("source", "move"));
        CHECK(asset.setNodePoints("source", &points));
        CHECK(asset.setNodeFloat("move", "x", 2.f));
        CHECK(asset.exposeParameter("offset", "move", "x"));
        CHECK(asset.setParameterFloat("offset", 9.f));

        PointGraph* instance = asset.instantiate();
        REQUIRE(bool(instance));
        CHECK(!instance->hasParameterOverride("offset"));
        CHECK_EQ(instance->getParameterFloat("offset", -1.f), 2.f);
        CHECK(!instance->execute("move"));
        CHECK(instance->getError().find("no points") != std::string::npos);
        CHECK(instance->setNodePoints("source", &points));
        CHECK(instance->setParameterFloat("offset", 4.f));
        PointSet* output = instance->execute("move");
        REQUIRE(bool(output));
        CHECK_EQ(output->getX(0), 5.f);
        delete output;
        CHECK_EQ(asset.getParameterFloat("offset", -1.f), 9.f);
        delete instance;
    };
}

TEST_CASE("procgen.pointGraph.spec.biomeShapeAndSamplingNodes") {
    SUB_CASE("biome node input -> generated biome output") {
        SpatialData domain = SpatialData::box(0.f, 0.f, 0.f, 4.f, 0.f, 4.f);
        BiomeRules  rules;
        CHECK(rules.addLayer("forest", &domain, 1, 1.f));
        CHECK(rules.addAsset("forest", "oak", 1.f, 1.f, 1.f, false));

        PointGraph graph;
        CHECK(graph.addNode("biome", "biome.generate"));
        CHECK(graph.setNodeSpatial("biome", &domain));
        CHECK(graph.setNodeBiomeRules("biome", &rules));
        CHECK(graph.setNodeFloat("biome", "spacing", 2.f));
        CHECK(graph.setNodeInt("biome", "seed", 42));
        CHECK(graph.validate());
        PointSet* output = graph.execute("biome");
        REQUIRE(bool(output));
        CHECK_EQ(output->getCount(), 9);
        CHECK_EQ(output->getStringAttribute(0, "biome", ""), std::string("forest"));
        CHECK_EQ(output->getStringAttribute(0, "asset", ""), std::string("oak"));
        delete output;
    };
    SUB_CASE("shape grammar and external asset rebinding -> expanded output") {
        PointSet controlPoints;
        controlPoints.add(0.f, 0.f, 0.f);
        controlPoints.add(6.f, 0.f, 0.f);
        ShapeGrammar grammar;
        CHECK(grammar.addModule("A", "wall", 2.f, 1.f));

        PointGraph asset;
        CHECK(asset.addNode("path", "input"));
        CHECK(asset.addNode("facade", "grammar.generate"));
        CHECK(asset.connect("path", "facade"));
        CHECK(asset.setNodePoints("path", &controlPoints));
        CHECK(asset.setNodeShapeGrammar("facade", &grammar));
        CHECK(asset.setNodeString("facade", "grammar", "A+"));
        CHECK(asset.validate());
        PointSet* output = asset.execute("facade");
        REQUIRE(bool(output));
        CHECK_EQ(output->getCount(), 3);
        CHECK_EQ(output->getStringAttribute(0, "asset", ""), std::string("wall"));
        delete output;

        PointGraph* instance = asset.instantiate();
        REQUIRE(bool(instance));
        CHECK(instance->setNodePoints("path", &controlPoints));
        CHECK(!instance->validate());
        CHECK(instance->getError().find("shape grammar") != std::string::npos);
        CHECK(instance->setNodeShapeGrammar("facade", &grammar));
        CHECK(instance->validate());
        output = instance->execute("facade");
        REQUIRE(bool(output));
        CHECK_EQ(output->getCount(), 3);
        delete output;
        delete instance;
    };
    SUB_CASE("breadth node catalog -> discoverable operations") {
        CHECK(PointGraph::getOperationInputCount("mesh.sample") == 0);
        CHECK(PointGraph::getOperationInputCount("grid.sample") == 0);
        CHECK(PointGraph::getOperationInputCount("poisson.sample") == 0);
        CHECK(PointGraph::getOperationInputCount("spline.sample") == 1);
        CHECK(PointGraph::getOperationInputCount("spline.filter.distance") == 2);
        CHECK(PointGraph::getOperationInputCount("points.union") == 2);
        CHECK(PointGraph::getOperationInputCount("points.intersect") == 2);
        CHECK(PointGraph::getOperationInputCount("points.difference") == 2);
        CHECK(PointGraph::getOperationInputCount("density.from.normal") == 1);
        CHECK(PointGraph::getOperationInputCount("bounds.modify") == 1);
        CHECK(PointGraph::getOperationInputCount("spawn.mesh") == 1);
        CHECK(PointGraph::getOperationInputCount("debug.disable") == 1);
        CHECK(PointGraph::getOperationInputCount("debug.inspect") == 1);
        CHECK_EQ(PointGraph::getOperationParamKey("spline.sample", 2), std::string("lateralJitter"));
        CHECK_EQ(PointGraph::getOperationParamKey("spawn.mesh", 1), std::string("attribute"));
        CHECK_EQ(PointGraph::getOperationParamKey("grid.sample", 0), std::string("width"));
        CHECK_EQ(PointGraph::getOperationParamKey("poisson.sample", 2), std::string("radius"));
        CHECK(PointGraph::getOperationInputCount("landscape.sample") == 1);
        CHECK(PointGraph::getOperationInputCount("texture.sample") == 1);
        CHECK_EQ(PointGraph::getOperationParamKey("landscape.sample", 1), std::string("project"));
        CHECK_EQ(PointGraph::getOperationParamKey("texture.sample", 1), std::string("attribute"));
    };
    SUB_CASE("grid and Poisson inputs -> sampled points") {
        PointGraph gridGraph;
        CHECK(gridGraph.addNode("grid", "grid.sample"));
        CHECK(gridGraph.setNodeInt("grid", "width", 4));
        CHECK(gridGraph.setNodeInt("grid", "depth", 3));
        CHECK(gridGraph.setNodeFloat("grid", "spacing", 2.f));
        CHECK(gridGraph.setNodeInt("grid", "seed", 11));
        CHECK(gridGraph.setNodeFloat("grid", "jitter", 0.f));
        CHECK(gridGraph.setNodeFloat("grid", "originX", 10.f));
        CHECK(gridGraph.setNodeFloat("grid", "originY", 1.f));
        CHECK(gridGraph.setNodeFloat("grid", "originZ", -5.f));
        CHECK(gridGraph.validate());
        std::unique_ptr<PointSet> grid(gridGraph.execute("grid"));
        REQUIRE(bool(grid));
        CHECK_EQ(grid->getCount(), 12);
        CHECK_EQ(grid->getX(0), 10.f);
        CHECK_EQ(grid->getY(0), 1.f);
        CHECK_EQ(grid->getZ(0), -5.f);

        PointGraph again;
        CHECK(again.addNode("grid", "grid.sample"));
        CHECK(again.setNodeInt("grid", "width", 4));
        CHECK(again.setNodeInt("grid", "depth", 3));
        CHECK(again.setNodeFloat("grid", "spacing", 2.f));
        CHECK(again.setNodeInt("grid", "seed", 11));
        CHECK(again.setNodeFloat("grid", "originX", 10.f));
        CHECK(again.setNodeFloat("grid", "originY", 1.f));
        CHECK(again.setNodeFloat("grid", "originZ", -5.f));
        std::unique_ptr<PointSet> gridB(again.execute("grid"));
        REQUIRE(bool(gridB));
        CHECK_EQ(gridB->getCount(), grid->getCount());
        for (int i = 0; i < grid->getCount(); ++i) {
            CHECK_EQ(gridB->getX(i), grid->getX(i));
            CHECK_EQ(gridB->getZ(i), grid->getZ(i));
            CHECK_EQ(gridB->getPointSeed(i), grid->getPointSeed(i));
        }

        PointGraph badGrid;
        CHECK(badGrid.addNode("grid", "grid.sample"));
        CHECK(badGrid.setNodeInt("grid", "width", 0));
        CHECK(!badGrid.validate());

        PointGraph poissonGraph;
        CHECK(poissonGraph.addNode("scatter", "poisson.sample"));
        CHECK(poissonGraph.setNodeInt("scatter", "width", 20));
        CHECK(poissonGraph.setNodeInt("scatter", "depth", 20));
        CHECK(poissonGraph.setNodeFloat("scatter", "radius", 2.f));
        CHECK(poissonGraph.setNodeInt("scatter", "seed", 99));
        CHECK(poissonGraph.setNodeInt("scatter", "maxPoints", 80));
        CHECK(poissonGraph.setNodeFloat("scatter", "originX", 100.f));
        CHECK(poissonGraph.validate());
        std::unique_ptr<PointSet> scatter(poissonGraph.execute("scatter"));
        REQUIRE(bool(scatter));
        CHECK(scatter->getCount() > 0);
        CHECK(scatter->getCount() <= 80);
        CHECK(scatter->getX(0) >= 100.f);

        PointGraph poissonBudget;
        CHECK(poissonBudget.addNode("scatter", "poisson.sample"));
        CHECK(poissonBudget.setNodeInt("scatter", "width", 50));
        CHECK(poissonBudget.setNodeInt("scatter", "depth", 50));
        CHECK(poissonBudget.setNodeFloat("scatter", "radius", 0.5f));
        CHECK(poissonBudget.setNodeInt("scatter", "maxPoints", 200));
        poissonBudget.setMaxNodeOutputPoints(50);
        CHECK(poissonBudget.validate());
        CHECK(!poissonBudget.execute("scatter"));
        CHECK(poissonBudget.getError().find("budget") != std::string::npos);
    };
    SUB_CASE("landscape and texture sources -> sampled attribute channels") {
        Heightmap heights(3, 3);
        for (int z = 0; z < 3; ++z)
            for (int x = 0; x < 3; ++x) heights.setHeight(x, z, float(x));
        SpatialData landscape = SpatialData::heightfield(heights, 0.f, 0.f, 1.f, 1.f);

        PointSet candidates;
        candidates.add(0.f, 10.f, 1.f);
        candidates.add(2.f, 10.f, 1.f);
        candidates.setDensity(0, 0.f);
        candidates.setDensity(1, 0.f);

        PointGraph landscapeGraph;
        CHECK(landscapeGraph.addNode("in", "input"));
        CHECK(landscapeGraph.addNode("sample", "landscape.sample"));
        CHECK(landscapeGraph.setNodePoints("in", &candidates));
        CHECK(landscapeGraph.connect("in", "sample"));
        CHECK(landscapeGraph.setNodeSpatial("sample", &landscape));
        CHECK(landscapeGraph.setNodeInt("sample", "project", 1));
        CHECK(landscapeGraph.setNodeString("sample", "attribute", "$Density"));
        CHECK(landscapeGraph.setNodeFloat("sample", "inputMin", 0.f));
        CHECK(landscapeGraph.setNodeFloat("sample", "inputMax", 2.f));
        CHECK(landscapeGraph.validate());
        std::unique_ptr<PointSet> projected(landscapeGraph.execute("sample"));
        REQUIRE(bool(projected));
        CHECK_EQ(projected->getCount(), 2);
        CHECK_EQ(projected->getY(0), 0.f);
        CHECK_EQ(projected->getY(1), 2.f);
        CHECK_EQ(projected->getDensity(0), 0.f);
        CHECK_EQ(projected->getDensity(1), 1.f);

        Heightmap mask(2, 2);
        mask.setHeight(0, 0, 0.f);
        mask.setHeight(1, 0, 1.f);
        mask.setHeight(0, 1, 0.25f);
        mask.setHeight(1, 1, 0.75f);
        SpatialData texture = SpatialData::textureMask(mask, 0.f, 0.f, 1.f, 0.f, 1.f, -1.f, 1.f);

        PointSet maskPoints;
        maskPoints.add(0.f, 0.f, 0.f);
        maskPoints.add(1.f, 0.f, 0.f);

        PointGraph textureGraph;
        CHECK(textureGraph.addNode("in", "input"));
        CHECK(textureGraph.addNode("sample", "texture.sample"));
        CHECK(textureGraph.setNodePoints("in", &maskPoints));
        CHECK(textureGraph.connect("in", "sample"));
        CHECK(textureGraph.setBindingSpatial("mask", &texture).ok());
        CHECK(textureGraph.setNodeString("sample", "binding", "mask"));
        CHECK(textureGraph.setNodeString("sample", "attribute", "coverage"));
        CHECK(textureGraph.validate());
        std::unique_ptr<PointSet> covered(textureGraph.execute("sample"));
        REQUIRE(bool(covered));
        CHECK_EQ(covered->getFloatAttribute(0, "coverage", -1.f), 0.f);
        CHECK_EQ(covered->getFloatAttribute(1, "coverage", -1.f), 1.f);

        PointGraph wrongKind;
        CHECK(wrongKind.addNode("in", "input"));
        CHECK(wrongKind.addNode("sample", "texture.sample"));
        CHECK(wrongKind.setNodePoints("in", &maskPoints));
        CHECK(wrongKind.connect("in", "sample"));
        CHECK(wrongKind.setNodeSpatial("sample", &landscape));
        CHECK(wrongKind.validate());
        CHECK(!wrongKind.execute("sample"));
        CHECK(wrongKind.getError().find("texture_mask") != std::string::npos);
    };
    SUB_CASE("spline distance and boolean nodes -> filtered point outputs") {
        PointSet controls;
        controls.add(0.f, 0.f, 0.f);
        controls.add(10.f, 0.f, 0.f);

        PointGraph graph;
        CHECK(graph.addNode("path", "input"));
        CHECK(graph.addNode("lamps", "spline.sample"));
        CHECK(graph.addNode("candidates", "input"));
        CHECK(graph.addNode("near", "spline.filter.distance"));
        CHECK(graph.addNode("keep", "points.difference"));
        CHECK(graph.setNodePoints("path", &controls));
        CHECK(graph.connect("path", "lamps"));
        CHECK(graph.setNodeFloat("lamps", "spacing", 5.f));
        CHECK(graph.setNodeInt("lamps", "seed", 7));

        PointSet candidates;
        candidates.add(0.f, 0.f, 0.f);
        candidates.add(5.f, 0.f, 0.5f);
        candidates.add(5.f, 0.f, 8.f);
        candidates.add(20.f, 0.f, 0.f);
        CHECK(graph.setNodePoints("candidates", &candidates));
        CHECK(graph.connect("candidates", "near", 0));
        CHECK(graph.connect("path", "near", 1));
        CHECK(graph.setNodeFloat("near", "minDistance", 0.f));
        CHECK(graph.setNodeFloat("near", "maxDistance", 2.f));
        CHECK(graph.connect("candidates", "keep", 0));
        CHECK(graph.connect("near", "keep", 1));
        CHECK(graph.validate());

        std::unique_ptr<PointSet> lamps(graph.execute("lamps"));
        REQUIRE(bool(lamps));
        CHECK(lamps->getCount() >= 2);

        std::unique_ptr<PointSet> near(graph.execute("near"));
        REQUIRE(bool(near));
        CHECK_EQ(near->getCount(), 2);

        std::unique_ptr<PointSet> keep(graph.execute("keep"));
        REQUIRE(bool(keep));
        CHECK_EQ(keep->getCount(), 2);
    };
    SUB_CASE("normal, bounds, spawn, debug nodes -> mapped metadata") {
        PointSet  source;
        const int flat  = source.add(0.f, 0.f, 0.f);
        const int steep = source.add(1.f, 0.f, 0.f);
        source.setNormal(flat, 0.f, 1.f, 0.f);
        source.setNormal(steep, 1.f, 0.f, 0.f);
        source.setBounds(flat, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f);
        source.setPointSeed(flat, 11);
        source.setPointSeed(steep, 22);

        PointGraph graph;
        CHECK(graph.addNode("source", "input"));
        CHECK(graph.addNode("density", "density.from.normal"));
        CHECK(graph.addNode("bounds", "bounds.modify"));
        CHECK(graph.addNode("spawn", "spawn.mesh"));
        CHECK(graph.addNode("inspect", "debug.inspect"));
        CHECK(graph.addNode("disable", "debug.disable"));
        CHECK(graph.setNodePoints("source", &source));
        CHECK(graph.connect("source", "density"));
        CHECK(graph.setNodeFloat("density", "minDegrees", 0.f));
        CHECK(graph.setNodeFloat("density", "maxDegrees", 90.f));
        CHECK(graph.connect("density", "bounds"));
        CHECK(graph.setNodeFloat("bounds", "scaleX", 2.f));
        CHECK(graph.setNodeFloat("bounds", "scaleY", 2.f));
        CHECK(graph.setNodeFloat("bounds", "scaleZ", 2.f));
        CHECK(graph.connect("bounds", "spawn"));
        CHECK(graph.setNodeString("spawn", "mesh0", "tree.oak"));
        CHECK(graph.setNodeFloat("spawn", "weight0", 1.f));
        CHECK(graph.setNodeString("spawn", "mesh1", "tree.pine"));
        CHECK(graph.setNodeFloat("spawn", "weight1", 1.f));
        CHECK(graph.setNodeInt("spawn", "seed", 99));
        CHECK(graph.connect("spawn", "inspect"));
        CHECK(graph.connect("inspect", "disable"));
        CHECK(graph.setNodeInt("disable", "enabled", 1));
        CHECK(graph.validate());

        std::unique_ptr<PointSet> enabled(graph.execute("disable"));
        REQUIRE(bool(enabled));
        CHECK_EQ(enabled->getCount(), 2);
        CHECK(enabled->getDensity(0) < enabled->getDensity(1));
        CHECK(enabled->getBoundsMaxX(0) > 0.f);
        CHECK(enabled->hasStringAttribute(0, "mesh"));
        CHECK(enabled->hasStringAttribute(1, "mesh"));

        CHECK(graph.setNodeInt("disable", "enabled", 0));
        std::unique_ptr<PointSet> disabled(graph.execute("disable"));
        REQUIRE(bool(disabled));
        CHECK_EQ(disabled->getCount(), 0);
    };
    SUB_CASE("mesh sample without mesh surface -> validation failure") {
        PointGraph graph;
        CHECK(graph.addNode("sample", "mesh.sample"));
        SpatialData box = SpatialData::box(0.f, 0.f, 0.f, 2.f, 0.f, 2.f);
        CHECK(graph.setNodeSpatial("sample", &box));
        CHECK(graph.setNodeFloat("sample", "spacing", 1.f));
        CHECK(graph.validate());
        PointSet* rejected = graph.execute("sample");
        CHECK(!rejected);
        CHECK(graph.getError().find("surface.mesh") != std::string::npos);

        MeshBuild mesh;
        mesh.addVertex(0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f);
        mesh.addVertex(4.f, 0.f, 0.f, 0.f, 1.f, 0.f, 1.f, 0.f);
        mesh.addVertex(4.f, 0.f, 4.f, 0.f, 1.f, 0.f, 1.f, 1.f);
        mesh.addVertex(0.f, 0.f, 4.f, 0.f, 1.f, 0.f, 0.f, 1.f);
        mesh.addTriangle(0, 1, 2);
        mesh.addTriangle(0, 2, 3);
        SpatialData surface = SpatialData::meshSurface(mesh, 0.25f);
        CHECK(graph.setNodeSpatial("sample", &surface));
        std::unique_ptr<PointSet> sampled(graph.execute("sample"));
        REQUIRE(bool(sampled));
        CHECK(sampled->getCount() > 0);
    };
}

TEST_CASE("procgen.pointGraph.spec.attributeAndNamedBindingNodes") {
    SUB_CASE("attribute operation catalog -> exposed node types") {
        CHECK(PointGraph::getOperationInputCount("attribute.copy") == 1);
        CHECK(PointGraph::getOperationInputCount("attribute.rename") == 1);
        CHECK(PointGraph::getOperationInputCount("attribute.delete") == 1);
        CHECK(PointGraph::getOperationInputCount("attribute.transfer") == 2);
        CHECK(PointGraph::getOperationInputCount("attribute.set.int") == 1);
        CHECK(PointGraph::getOperationInputCount("attribute.set.bool") == 1);
        CHECK(PointGraph::getOperationInputCount("attribute.set.vector") == 1);
        CHECK(PointGraph::getOperationInputCount("attribute.compare.float") == 1);
        CHECK(PointGraph::getOperationInputCount("attribute.select.float") == 1);
        CHECK(PointGraph::getOperationInputCount("filter.int") == 1);
        CHECK(PointGraph::getOperationInputCount("filter.bool") == 1);
        CHECK_EQ(PointGraph::getOperationParamKey("attribute.transfer", 2), std::string("mode"));
        CHECK_EQ(PointGraph::getOperationParamKey("attribute.compare.float", 1), std::string("comparison"));
    };
    SUB_CASE("copy, rename, transfer, select -> expected attribute columns") {
        PointSet  source;
        const int a = source.add(0.f, 0.f, 0.f);
        const int b = source.add(10.f, 0.f, 0.f);
        source.setDensity(a, 0.25f);
        source.setDensity(b, 0.75f);
        CHECK(source.trySetFloatAttribute(a, "age", 3.f).ok());
        CHECK(source.trySetFloatAttribute(b, "age", 9.f).ok());
        CHECK(source.trySetPointId(a, 11).ok());
        CHECK(source.trySetPointId(b, 22).ok());

        PointGraph graph;
        CHECK(graph.addNode("source", "input"));
        CHECK(graph.addNode("copyDensity", "attribute.copy"));
        CHECK(graph.addNode("renameAge", "attribute.rename"));
        CHECK(graph.addNode("tagLayer", "attribute.set.int"));
        CHECK(graph.addNode("mark", "attribute.set.bool"));
        CHECK(graph.addNode("compare", "attribute.compare.float"));
        CHECK(graph.addNode("select", "attribute.select.float"));
        CHECK(graph.addNode("filterHot", "filter.bool"));
        CHECK(graph.setNodePoints("source", &source));
        CHECK(graph.connect("source", "copyDensity"));
        CHECK(graph.setNodeString("copyDensity", "source", "$Density"));
        CHECK(graph.setNodeString("copyDensity", "target", "densityCopy"));
        CHECK(graph.connect("copyDensity", "renameAge"));
        CHECK(graph.setNodeString("renameAge", "from", "age"));
        CHECK(graph.setNodeString("renameAge", "to", "years"));
        CHECK(graph.connect("renameAge", "tagLayer"));
        CHECK(graph.setNodeString("tagLayer", "attribute", "layer"));
        CHECK(graph.setNodeInt("tagLayer", "value", 4));
        CHECK(graph.connect("tagLayer", "mark"));
        CHECK(graph.setNodeString("mark", "attribute", "keep"));
        CHECK(graph.setNodeInt("mark", "value", 1));
        CHECK(graph.connect("mark", "compare"));
        CHECK(graph.setNodeString("compare", "attribute", "$Density"));
        CHECK(graph.setNodeString("compare", "comparison", "gt"));
        CHECK(graph.setNodeFloat("compare", "operand", 0.5f));
        CHECK(graph.setNodeString("compare", "outputAttribute", "hot"));
        CHECK(graph.connect("compare", "select"));
        CHECK(graph.setNodeString("select", "conditionAttribute", "hot"));
        CHECK(graph.setNodeString("select", "trueAttribute", "years"));
        CHECK(graph.setNodeString("select", "falseAttribute", "densityCopy"));
        CHECK(graph.setNodeString("select", "outputAttribute", "chosen"));
        CHECK(graph.connect("select", "filterHot"));
        CHECK(graph.setNodeString("filterHot", "attribute", "hot"));
        CHECK(graph.setNodeInt("filterHot", "value", 1));
        CHECK(graph.validate());

        std::unique_ptr<PointSet> filtered(graph.execute("filterHot"));
        REQUIRE(bool(filtered));
        CHECK_EQ(filtered->getCount(), 1);
        CHECK_EQ(filtered->getFloatAttribute(0, "densityCopy", -1.f), 0.75f);
        CHECK_EQ(filtered->getFloatAttribute(0, "years", -1.f), 9.f);
        CHECK_EQ(filtered->getIntAttribute(0, "layer", -1), 4);
        CHECK(filtered->getBoolAttribute(0, "hot", false));
        CHECK_EQ(filtered->getFloatAttribute(0, "chosen", -1.f), 9.f);
        CHECK(!filtered->hasFloatAttribute(0, "age"));

        PointSet  donors;
        const int d0 = donors.add(0.f, 0.f, 0.f);
        const int d1 = donors.add(10.f, 0.f, 0.f);
        CHECK(donors.trySetPointId(d0, 11).ok());
        CHECK(donors.trySetPointId(d1, 22).ok());
        CHECK(donors.trySetStringAttribute(d0, "biome", "meadow").ok());
        CHECK(donors.trySetStringAttribute(d1, "biome", "forest").ok());

        PointGraph transfer;
        CHECK(transfer.addNode("targets", "input"));
        CHECK(transfer.addNode("donors", "input"));
        CHECK(transfer.addNode("paint", "attribute.transfer"));
        CHECK(transfer.addNode("drop", "attribute.delete"));
        CHECK(transfer.setNodePoints("targets", &source));
        CHECK(transfer.setNodePoints("donors", &donors));
        CHECK(transfer.connect("targets", "paint", 0));
        CHECK(transfer.connect("donors", "paint", 1));
        CHECK(transfer.setNodeString("paint", "attribute", "biome"));
        CHECK(transfer.setNodeString("paint", "mode", "id"));
        CHECK(transfer.connect("paint", "drop"));
        CHECK(transfer.setNodeString("drop", "attribute", "age"));
        CHECK(transfer.validate());
        std::unique_ptr<PointSet> painted(transfer.execute("drop"));
        REQUIRE(bool(painted));
        CHECK_EQ(painted->getCount(), 2);
        CHECK_EQ(painted->getStringAttribute(0, "biome", ""), std::string("meadow"));
        CHECK_EQ(painted->getStringAttribute(1, "biome", ""), std::string("forest"));
        CHECK(!painted->hasFloatAttribute(0, "age"));
    };
    SUB_CASE("built-in density selector -> written density channel") {
        PointSet points;
        points.add(1.f, 2.f, 3.f);
        PointGraph graph;
        CHECK(graph.addNode("source", "input"));
        CHECK(graph.addNode("set", "attribute.set.float"));
        CHECK(graph.addNode("math", "attribute.math.float"));
        CHECK(graph.setNodePoints("source", &points));
        CHECK(graph.connect("source", "set"));
        CHECK(graph.setNodeString("set", "attribute", "$Density"));
        CHECK(graph.setNodeFloat("set", "value", 0.4f));
        CHECK(graph.connect("set", "math"));
        CHECK(graph.setNodeString("math", "attribute", "$Density"));
        CHECK(graph.setNodeString("math", "outputAttribute", "scaled"));
        CHECK(graph.setNodeString("math", "operation", "multiply"));
        CHECK(graph.setNodeFloat("math", "operand", 2.f));
        CHECK(graph.validate());
        std::unique_ptr<PointSet> result(graph.execute("math"));
        REQUIRE(bool(result));
        CHECK_EQ(result->getDensity(0), 0.4f);
        CHECK_EQ(result->getFloatAttribute(0, "scaled", -1.f), 0.8f);
    };
    SUB_CASE("named binding and Get nodes -> resolved data and inspection") {
        PointGraph graph;
        CHECK(graph.addNode("landscape", "get.landscape"));
        CHECK(graph.addNode("actors", "get.actor"));
        CHECK(graph.setNodeFloat("landscape", "spacing", 2.f));
        CHECK(graph.setNodeString("landscape", "binding", "terrain"));
        CHECK(graph.setNodeString("actors", "binding", "proxies"));

        Heightmap heightmap(4, 4);
        for (int z = 0; z < 4; ++z)
            for (int x = 0; x < 4; ++x) heightmap.setHeight(x, z, float(x + z) * 0.25f);
        SpatialData terrain = SpatialData::heightfield(heightmap, 0.f, 0.f, 1.f, 1.f);
        CHECK(graph.setBindingSpatial("terrain", &terrain).ok());

        PointSet proxies;
        proxies.add(1.f, 0.f, 2.f);
        proxies.add(3.f, 0.f, 4.f);
        CHECK(graph.setBindingPoints("proxies", &proxies).ok());
        CHECK_EQ(graph.getBindingCount(), 2);
        CHECK_EQ(graph.getBindingType("terrain"), std::string("spatial"));
        CHECK_EQ(graph.getBindingType("proxies"), std::string("points"));

        CHECK(graph.validate());
        std::unique_ptr<PointSet> landscape(graph.execute("landscape"));
        REQUIRE(bool(landscape));
        CHECK(landscape->getCount() > 0);

        std::unique_ptr<PointSet> actors(graph.execute("actors"));
        REQUIRE(bool(actors));
        CHECK_EQ(actors->getCount(), 2);

        auto inspect = graph.inspectNode("actors", 2);
        REQUIRE(inspect.ok());
        CHECK_EQ(inspect.value().pointCount, 2);
        CHECK(inspect.value().sampleCount > 0);
        bool sawBuiltin = false;
        for (const auto& column : inspect.value().columns)
            if (column.domain == "builtin") sawBuiltin = true;
        CHECK(sawBuiltin);
    };
    SUB_CASE("data domain, partition, noise, selector -> filtered attributes") {
        PointSet points;
        points.add(0.f, 0.f, 0.f);
        points.add(1.f, 0.f, 0.f);
        points.setYaw(0, 90.f);
        auto written = setPointDataFloatAttribute(points, "biomeSeed", 7.5f);
        REQUIRE(written.ok());
        CHECK_EQ(points.dataAttributes().rowCount(), 1);
        CHECK_EQ(points.dataAttributes().getFloat(0, "biomeSeed").value_or(-1.f), 7.5f);
        CHECK(points.trySetStringAttribute(0, "biome", "forest").ok());
        CHECK(points.trySetStringAttribute(1, "biome", "forest").ok());

        PointSet partitioned = partitionPointAttribute(points, "biome", "partition", "hash");
        CHECK(partitioned.hasIntAttribute(0, "partition"));
        PointSet noisy = noisePointFloatAttribute(partitioned, "noise", 11u, 1.f, 1.f, 0.f);
        CHECK(noisy.hasFloatAttribute(0, "noise"));

        PointGraph graph;
        CHECK(graph.addNode("input", "input"));
        CHECK(graph.addNode("swizzle", "attribute.math.float"));
        CHECK(graph.connect("input", "swizzle"));
        CHECK(graph.setNodePoints("input", &noisy));
        CHECK(graph.setNodeString("swizzle", "attribute", "$Position.ZYX.X"));
        CHECK(graph.setNodeString("swizzle", "outputAttribute", "swizzledZ"));
        CHECK(graph.setNodeString("swizzle", "operation", "add"));
        CHECK(graph.setNodeFloat("swizzle", "operand", 0.f));
        CHECK(graph.validate());
        std::unique_ptr<PointSet> result(graph.execute("swizzle"));
        REQUIRE(bool(result));
        CHECK_EQ(result->getFloatAttribute(0, "swizzledZ", -1.f), 0.f);
        // Data domain survives graph copies from the input PointSet.
        CHECK_EQ(result->dataAttributes().rowCount(), 1);
        auto dataValue = readPointFloatChannel(*result, 0, "@Data.biomeSeed", -1.f);
        REQUIRE(dataValue.ok());
        CHECK_EQ(dataValue.value(), 7.5f);
        auto lastNoise = readPointFloatChannel(*result, 0, "@Last.noise", -1.f);
        REQUIRE(lastNoise.ok());
        CHECK_EQ(lastNoise.value(), result->getFloatAttribute(0, "noise", -2.f));
        auto forward = readPointFloatChannel(*result, 0, "$Rotation.Forward.X", 0.f);
        REQUIRE(forward.ok());
    };
    SUB_CASE("named spatial sample binding -> accepted and sampled") {
        PointGraph graph;
        CHECK(graph.addNode("sample", "spatial.sample"));
        CHECK(graph.setNodeString("sample", "binding", "volume"));
        CHECK(graph.setNodeFloat("sample", "spacing", 2.f));
        SpatialData volume = SpatialData::box(0.f, 0.f, 0.f, 2.f, 0.f, 2.f);
        CHECK(graph.setBindingSpatial("volume", &volume).ok());
        CHECK(graph.validate());
        std::unique_ptr<PointSet> result(graph.execute("sample"));
        REQUIRE(bool(result));
        CHECK(result->getCount() > 0);
    };
}


}  // namespace test_procgen_point_graph
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_procgen_pointset)
#include "PrivateTestScope.h"
#include "procgen/PointSet.h"
#include "procgen/Procgen.h"
#include "procgen/heightmap/Heightmap.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// procgen_pointset.cpp
namespace test_procgen_pointset {
using namespace eve::procgen;

namespace {

using PointSetHandle = ProcgenPointSetHandleRef;
using ContextHandle  = ProcgenContextHandleRef;

/** @brief Owns one module point-set handle for the duration of a test. */
struct PointSetLease {
    Procgen*       owner = nullptr;
    PointSetHandle handle{};

    PointSetLease() = default;
    PointSetLease(Procgen& proc, PointSetHandle value) : owner(&proc), handle(value) {}
    PointSetLease(const PointSetLease&)            = delete;
    PointSetLease& operator=(const PointSetLease&) = delete;
    PointSetLease(PointSetLease&& other) noexcept : owner(other.owner), handle(other.handle) {
        other.owner  = nullptr;
        other.handle = {};
    }
    PointSetLease& operator=(PointSetLease&& other) noexcept {
        if (this == &other) return *this;
        reset();
        owner        = other.owner;
        handle       = other.handle;
        other.owner  = nullptr;
        other.handle = {};
        return *this;
    }
    ~PointSetLease() { reset(); }

    void reset() noexcept {
        if (!owner || !handle.isValid()) return;
        auto result = owner->releasePointSet(handle);
        result.ignore("test point-set lease cleanup");
        owner  = nullptr;
        handle = {};
    }

    [[nodiscard]] eve::script::Borrowed<PointSet> view() const noexcept {
        return owner ? owner->resolvePointSet(handle) : eve::script::Borrowed<PointSet>();
    }
};

/** @brief Converts a successful point-set allocation into an owned test lease. */
PointSetLease requirePointSet(Procgen& proc, eve::Result<PointSetHandle>&& result) {
    const bool ok = result.ok();
    if (!ok) {
        const eve::Diagnostic* diagnostic = result.error();
        REQUIRE(diagnostic != nullptr);
        if (diagnostic) CHECK(diagnostic->isError());
    }
    REQUIRE(ok);
    return PointSetLease(proc, std::move(result).takeValue());
}

/** @brief Converts a successful rebuild-context allocation into a test lease. */
struct ContextLease {
    Procgen*      owner = nullptr;
    ContextHandle handle{};

    ContextLease() = default;
    ContextLease(Procgen& proc, ContextHandle value) : owner(&proc), handle(value) {}
    ContextLease(const ContextLease&)            = delete;
    ContextLease& operator=(const ContextLease&) = delete;
    ContextLease(ContextLease&& other) noexcept : owner(other.owner), handle(other.handle) {
        other.owner  = nullptr;
        other.handle = {};
    }
    ContextLease& operator=(ContextLease&& other) noexcept {
        if (this == &other) return *this;
        reset();
        owner        = other.owner;
        handle       = other.handle;
        other.owner  = nullptr;
        other.handle = {};
        return *this;
    }
    ~ContextLease() { reset(); }

    void reset() noexcept {
        if (!owner || !handle.isValid()) return;
        auto result = Procgen::release(handle);
        result.ignore("test context lease cleanup");
        owner  = nullptr;
        handle = {};
    }

    [[nodiscard]] eve::script::Borrowed<ProcgenContext> view() const noexcept {
        return owner ? Procgen::resolve(handle) : eve::script::Borrowed<ProcgenContext>();
    }
};

ContextLease requireContext(Procgen& proc, eve::Result<ContextHandle>&& result) {
    const bool ok = result.ok();
    if (!ok) {
        const eve::Diagnostic* diagnostic = result.error();
        REQUIRE(diagnostic != nullptr);
        if (diagnostic) CHECK(diagnostic->isError());
    }
    REQUIRE(ok);
    return ContextLease(proc, std::move(result).takeValue());
}

template <typename T>
void checkResult(const eve::Result<T>& result, eve::DiagnosticCode expected = eve::DiagnosticCode::None) {
    const bool ok = result.ok();
    CHECK(ok);
    if (!ok) {
        const eve::Diagnostic* diagnostic = result.error();
        REQUIRE(diagnostic != nullptr);
        if (diagnostic && expected != eve::DiagnosticCode::None) CHECK_EQ(diagnostic->code(), expected);
    }
}

}  // namespace

TEST_CASE("procgen.pointSet.spec.pipelineValidationAndSpatialQueries") {
    SUB_CASE("same seed through composed point pipeline -> same point set") {
        Procgen        proc;
        const uint32_t forestSeed = proc.deriveSeed(42, "forest");
        CHECK_NE(forestSeed, proc.deriveSeed(42, "rocks"));
        CHECK_EQ(forestSeed, proc.deriveSeed(42, "forest"));

        auto source     = requirePointSet(proc, proc.sampleGridHandle(8, 6, 2.f, forestSeed, 0.f));
        auto sourceView = source.view();
        REQUIRE(sourceView.isBound());
        PointSet* sourcePtr = sourceView.get();
        CHECK_EQ(sourcePtr->getCount(), 48);
        CHECK_EQ(sourcePtr->getX(1), 2.f);
        CHECK_EQ(sourcePtr->getZ(8), 2.f);

        for (int i = 0; i < sourcePtr->getCount(); ++i) {
            sourcePtr->setPosition(i, sourcePtr->getX(i), float(i % 4), sourcePtr->getZ(i));
            sourcePtr->setDensity(i, float(i % 5) / 4.f);
            sourcePtr->setStringAttribute(i, "biome", "forest");
        }

        auto height     = requirePointSet(proc, proc.filterHeightHandle(source.handle, 1.f, 2.f));
        auto heightView = height.view();
        REQUIRE(heightView.isBound());
        CHECK_EQ(heightView->getCount(), 24);
        auto dense     = requirePointSet(proc, proc.filterDensityHandle(height.handle, 0.5f, 1.f));
        auto denseView = dense.view();
        REQUIRE(denseView.isBound());
        CHECK(denseView->getCount() < heightView->getCount());
        CHECK(denseView->getCount() > 0);
        CHECK_EQ(denseView->getStringAttribute(0, "biome", ""), std::string("forest"));

        auto excluded     = requirePointSet(proc, proc.excludeRadiusHandle(dense.handle, 0.f, 0.f, 3.f));
        auto excludedView = excluded.view();
        REQUIRE(excludedView.isBound());
        auto pruned     = requirePointSet(proc, proc.selfPruneHandle(excluded.handle, 2.1f));
        auto prunedView = pruned.view();
        REQUIRE(prunedView.isBound());
        CHECK(prunedView->getCount() <= excludedView->getCount());

        const uint32_t jitterSeed = proc.deriveSeed(forestSeed, "jitter");
        auto           jitterA = requirePointSet(proc, proc.jitterPointsHandle(pruned.handle, jitterSeed, 0.5f, 0.5f));
        auto           jitterB = requirePointSet(proc, proc.jitterPointsHandle(pruned.handle, jitterSeed, 0.5f, 0.5f));
        auto           jitterAView = jitterA.view();
        auto           jitterBView = jitterB.view();
        REQUIRE(jitterAView.isBound());
        REQUIRE(jitterBView.isBound());
        CHECK_EQ(jitterAView->getCount(), jitterBView->getCount());
        for (int i = 0; i < jitterAView->getCount(); ++i) {
            CHECK_EQ(jitterAView->getX(i), jitterBView->getX(i));
            CHECK_EQ(jitterAView->getZ(i), jitterBView->getZ(i));
        }
    };
    SUB_CASE("invalid point pipeline input -> explicit failure") {
        Procgen proc;
        auto    failed = proc.filterHeightHandle({}, 0.f, 1.f);
        CHECK(!failed.ok());
        const eve::Diagnostic* diagnostic = failed.error();
        REQUIRE(diagnostic != nullptr);
        CHECK_EQ(diagnostic->code(), eve::DiagnosticCode::StaleHandle);
    };
    SUB_CASE("spatial filter and heightmap -> projected subset") {
        Procgen  proc;
        PointSet source;
        source.add(0.f, 0.f, 0.f);
        source.add(1.f, 0.f, 1.f);
        source.add(2.f, 0.f, 2.f);

        auto sourceResult = proc.newPointSetHandle();
        auto sourceHandle = std::move(sourceResult).takeValue();
        auto sourceView   = proc.resolvePointSet(sourceHandle);
        REQUIRE(sourceView.isBound());
        *sourceView     = source;
        auto inside     = requirePointSet(proc, proc.filterBoxHandle(sourceHandle, 0.5f, -1.f, 0.5f, 1.5f, 1.f, 1.5f));
        auto insideView = inside.view();
        REQUIRE(insideView.isBound());
        CHECK_EQ(insideView->getCount(), 1);
        auto outside =
            requirePointSet(proc, proc.filterBoxHandle(sourceHandle, 0.5f, -1.f, 0.5f, 1.5f, 1.f, 1.5f, true));
        auto outsideView = outside.view();
        REQUIRE(outsideView.isBound());
        CHECK_EQ(outsideView->getCount(), 2);

        Heightmap heightmap(3, 3);
        for (int z = 0; z < 3; ++z)
            for (int x = 0; x < 3; ++x) heightmap.setHeight(x, z, float(x));
        auto heightmapResult = proc.newHeightmapHandle(3, 3);
        REQUIRE(heightmapResult.ok());
        auto heightmapHandle = std::move(heightmapResult).takeValue();
        auto heightmapView   = proc.resolveHeightmap(heightmapHandle);
        REQUIRE(heightmapView.isBound());
        for (int z = 0; z < 3; ++z)
            for (int x = 0; x < 3; ++x) heightmapView->setHeight(x, z, float(x));
        auto projected =
            requirePointSet(proc, proc.projectToHeightmapHandle(sourceHandle, heightmapHandle, 0.f, 0.f, 1.f, 2.f));
        auto projectedView = projected.view();
        REQUIRE(projectedView.isBound());
        CHECK_EQ(projectedView->getY(0), 0.f);
        CHECK_EQ(projectedView->getY(1), 2.f);
        CHECK_EQ(projectedView->getY(2), 4.f);
        CHECK(projectedView->getNormalX(1) < 0.f);
        CHECK(projectedView->getNormalY(1) > 0.f);

        auto gentle     = requirePointSet(proc, proc.filterSlopeHandle(projected.handle, 0.f, 80.f));
        auto gentleView = gentle.view();
        REQUIRE(gentleView.isBound());
        CHECK_EQ(gentleView->getCount(), 3);
        auto flat     = requirePointSet(proc, proc.filterSlopeHandle(projected.handle, 0.f, 1.f));
        auto flatView = flat.view();
        REQUIRE(flatView.isBound());
        CHECK_EQ(flatView->getCount(), 0);
        auto heightmapRelease = proc.releaseHeightmap(heightmapHandle);
        heightmapRelease.ignore("test heightmap cleanup");
        auto sourceCleanup = proc.releasePointSet(sourceHandle);
        sourceCleanup.ignore("test point-set setup cleanup");
    };
    SUB_CASE("polygon and spline queries -> selected points") {
        Procgen  proc;
        PointSet polygon;
        polygon.add(0.f, 0.f, 0.f);
        polygon.add(10.f, 0.f, 0.f);
        polygon.add(10.f, 0.f, 10.f);
        polygon.add(0.f, 0.f, 10.f);

        PointSet candidates;
        candidates.add(5.f, 0.f, 5.f);
        candidates.add(15.f, 0.f, 5.f);
        auto polygonResult = proc.newPointSetHandle();
        REQUIRE(polygonResult.ok());
        auto polygonHandle = std::move(polygonResult).takeValue();
        auto polygonView   = proc.resolvePointSet(polygonHandle);
        REQUIRE(polygonView.isBound());
        *polygonView          = polygon;
        auto candidatesResult = proc.newPointSetHandle();
        REQUIRE(candidatesResult.ok());
        auto candidatesHandle = std::move(candidatesResult).takeValue();
        auto candidatesView   = proc.resolvePointSet(candidatesHandle);
        REQUIRE(candidatesView.isBound());
        *candidatesView  = candidates;
        auto inside      = requirePointSet(proc, proc.filterPolygonHandle(candidatesHandle, polygonHandle));
        auto outside     = requirePointSet(proc, proc.filterPolygonHandle(candidatesHandle, polygonHandle, true));
        auto insideView  = inside.view();
        auto outsideView = outside.view();
        REQUIRE(insideView.isBound());
        REQUIRE(outsideView.isBound());
        CHECK_EQ(insideView->getCount(), 1);
        CHECK_EQ(outsideView->getCount(), 1);
        CHECK_EQ(insideView->getX(0), 5.f);
        CHECK_EQ(outsideView->getX(0), 15.f);

        PointSet control;
        control.add(0.f, 0.f, 0.f);
        control.add(10.f, 0.f, 0.f);
        control.add(10.f, 0.f, 10.f);
        auto controlResult = proc.newPointSetHandle();
        REQUIRE(controlResult.ok());
        auto controlHandle = std::move(controlResult).takeValue();
        auto controlView   = proc.resolvePointSet(controlHandle);
        REQUIRE(controlView.isBound());
        *controlView     = control;
        auto samples     = requirePointSet(proc, proc.sampleSplineHandle(controlHandle, 5.f, 42, 0.f));
        auto samplesView = samples.view();
        REQUIRE(samplesView.isBound());
        CHECK_EQ(samplesView->getCount(), 5);
        CHECK_EQ(samplesView->getX(1), 5.f);
        CHECK_EQ(samplesView->getZ(3), 5.f);
        CHECK_EQ(samplesView->getYaw(0), 0.f);
        CHECK_EQ(samplesView->getYaw(3), 90.f);

        PointSet nearby;
        nearby.add(5.f, 0.f, 2.f);
        nearby.add(5.f, 0.f, 5.f);
        nearby.add(12.f, 0.f, 5.f);
        auto nearbyResult = proc.newPointSetHandle();
        REQUIRE(nearbyResult.ok());
        auto nearbyHandle = std::move(nearbyResult).takeValue();
        auto nearbyView   = proc.resolvePointSet(nearbyHandle);
        REQUIRE(nearbyView.isBound());
        *nearbyView   = nearby;
        auto corridor = requirePointSet(proc, proc.filterSplineDistanceHandle(nearbyHandle, controlHandle, 0.f, 2.1f));
        auto corridorView = corridor.view();
        REQUIRE(corridorView.isBound());
        CHECK_EQ(corridorView->getCount(), 2);
        auto polygonCleanup = proc.releasePointSet(polygonHandle);
        polygonCleanup.ignore("test polygon cleanup");
        auto candidatesCleanup = proc.releasePointSet(candidatesHandle);
        candidatesCleanup.ignore("test candidates cleanup");
        auto controlCleanup = proc.releasePointSet(controlHandle);
        controlCleanup.ignore("test control cleanup");
        auto nearbyCleanup = proc.releasePointSet(nearbyHandle);
        nearbyCleanup.ignore("test nearby cleanup");
    };
}

TEST_CASE("procgen.system.spec.atomicCacheTraceAndRevision") {
    SUB_CASE("failed commit after success -> previous snapshot retained") {
        // Static handle factories resolve against the manager-owned singleton.
        // Keep commit/read operations on that same owner and epoch.
        Procgen& proc = *Procgen::create();
        PointSet first;
        first.add(1.f, 2.f, 3.f);

        auto initial     = requireContext(proc, Procgen::beginSystemHandle("forest", 42));
        auto initialView = initial.view();
        REQUIRE(initialView.isBound());
        CHECK(initialView->publish("trees", &first));
        CHECK(initialView->captureDebug("candidates", &first));
        CHECK_EQ(initialView->getDebugStageCount(), 1);
        CHECK_EQ(initialView->getDebugStageName(0), std::string("candidates"));
        initialView->trace("sample", 0, 1, 0.25f);
        auto initialCommit = proc.commitSystem(initial.handle);
        CHECK(initialCommit.ok());
        CHECK(proc.hasSystem("forest"));
        CHECK_EQ(proc.getSystemRevision("forest"), uint64_t(1));
        CHECK_EQ(proc.getSystemDebugStageCount("forest"), 1);
        CHECK_EQ(proc.getSystemDebugStageName("forest", 0), std::string("candidates"));

        auto debug     = requirePointSet(proc, proc.getSystemDebugStageHandle("forest", "candidates"));
        auto debugView = debug.view();
        REQUIRE(debugView.isBound());
        CHECK_EQ(debugView->getCount(), 1);
        auto committed     = requirePointSet(proc, proc.getSystemOutputHandle("forest", "trees"));
        auto committedView = committed.view();
        REQUIRE(committedView.isBound());
        CHECK_EQ(committedView->getCount(), 1);

        PointSet replacement;
        replacement.add(9.f, 9.f, 9.f);
        replacement.add(8.f, 8.f, 8.f);
        auto failed     = requireContext(proc, Procgen::beginSystemHandle("forest", 99));
        auto failedView = failed.view();
        REQUIRE(failedView.isBound());
        CHECK(failedView->publish("trees", &replacement));
        failedView->fail("script error");
        auto failedCommit = proc.commitSystem(failed.handle);
        CHECK(!failedCommit.ok());
        const eve::Diagnostic* failure = failedCommit.error();
        REQUIRE(failure != nullptr);
        CHECK(failure->isError());
        CHECK_EQ(proc.getSystemRevision("forest"), uint64_t(1));
        CHECK_EQ(proc.getSystemSeed("forest"), uint32_t(42));
        CHECK_EQ(proc.getSystemDebugStageCount("forest"), 1);

        auto committedAgain     = requirePointSet(proc, proc.getSystemOutputHandle("forest", "trees"));
        auto committedAgainView = committedAgain.view();
        REQUIRE(committedAgainView.isBound());
        CHECK_EQ(committedAgainView->getCount(), 1);
        CHECK(proc.getSystemDebugReport("forest").find("sample") != std::string::npos);
    };
    SUB_CASE("same build input -> committed cached snapshot reused") {
        Procgen& proc = *Procgen::create();
        PointSet points;
        points.add(1.f, 0.f, 2.f);

        auto miss     = requireContext(proc, Procgen::beginCachedSystemHandle("rocks", 7, "layout-v1:size=32"));
        auto missView = miss.view();
        REQUIRE(missView.isBound());
        CHECK(!missView->isCacheHit());
        CHECK(missView->isActive());
        CHECK_EQ(missView->getBuildKey(), std::string("layout-v1:size=32"));
        CHECK(missView->publish("rocks", &points));
        auto missCommit = proc.commitSystem(miss.handle);
        CHECK(missCommit.ok());
        CHECK_EQ(proc.getSystemRevision("rocks"), uint64_t(1));
        CHECK_EQ(proc.getSystemBuildKey("rocks"), std::string("layout-v1:size=32"));

        auto hit     = requireContext(proc, Procgen::beginCachedSystemHandle("rocks", 7, "layout-v1:size=32"));
        auto hitView = hit.view();
        REQUIRE(hitView.isBound());
        CHECK(hitView->isCacheHit());
        CHECK(!hitView->isActive());
        CHECK_EQ(proc.getSystemRevision("rocks"), uint64_t(1));

        auto changed     = requireContext(proc, Procgen::beginCachedSystemHandle("rocks", 7, "layout-v1:size=64"));
        auto changedView = changed.view();
        REQUIRE(changedView.isBound());
        CHECK(!changedView->isCacheHit());
        CHECK(changedView->isActive());
    };
    SUB_CASE("stage failure -> previous stage cache retained") {
        Procgen& proc = *Procgen::create();
        PointSet first;
        first.add(3.f, 0.f, 4.f);

        auto initial     = requireContext(proc, Procgen::beginSystemHandle("village", 11));
        auto initialView = initial.view();
        REQUIRE(initialView.isBound());
        CHECK(initialView->cacheStage("lots", "lots-v1:size=16", &first));
        CHECK(initialView->publish("lots", &first));
        auto initialCommit = proc.commitSystem(initial.handle);
        CHECK(initialCommit.ok());

        auto rebuild     = requireContext(proc, Procgen::beginSystemHandle("village", 11));
        auto rebuildView = rebuild.view();
        REQUIRE(rebuildView.isBound());
        PointSet* reused = rebuildView->reuseStage("lots", "lots-v1:size=16");
        REQUIRE(bool(reused));
        CHECK_EQ(reused->getCount(), 1);
        CHECK_EQ(rebuildView->getStageCacheHitCount(), 1);
        CHECK(!rebuildView->reuseStage("lots", "lots-v1:size=32"));
        CHECK_EQ(rebuildView->getStageCacheMissCount(), 1);

        PointSet replacement;
        replacement.add(8.f, 0.f, 8.f);
        replacement.add(9.f, 0.f, 9.f);
        CHECK(rebuildView->cacheStage("lots", "lots-v1:size=32", &replacement));
        rebuildView->fail("downstream failed");
        auto rebuildCommit = proc.commitSystem(rebuild.handle);
        CHECK(!rebuildCommit.ok());

        auto retry     = requireContext(proc, Procgen::beginSystemHandle("village", 11));
        auto retryView = retry.view();
        REQUIRE(retryView.isBound());
        PointSet* preserved = retryView->reuseStage("lots", "lots-v1:size=16");
        REQUIRE(bool(preserved));
        CHECK_EQ(preserved->getCount(), 1);
        CHECK(!retryView->reuseStage("lots", "lots-v1:size=32"));
    };
    SUB_CASE("automatic trace -> balanced start and finish timing") {
        Procgen& proc = *Procgen::create();
        PointSet points;
        points.add(1.f, 0.f, 1.f);

        auto measured     = requireContext(proc, Procgen::beginSystemHandle("timed", 5));
        auto measuredView = measured.view();
        REQUIRE(measuredView.isBound());
        CHECK(measuredView->beginTrace("outer", 0));
        CHECK(measuredView->beginTrace("inner", 1));
        CHECK_EQ(measuredView->getOpenTraceCount(), 2);
        CHECK(measuredView->endTrace(1));
        CHECK(measuredView->endTrace(2));
        CHECK_EQ(measuredView->getOpenTraceCount(), 0);
        CHECK_EQ(measuredView->getTraceCount(), 2);
        CHECK_EQ(measuredView->getTraceName(0), std::string("inner"));
        CHECK(measuredView->getTraceMilliseconds(0) >= 0.f);
        CHECK(measuredView->publish("points", &points));
        auto measuredCommit = proc.commitSystem(measured.handle);
        CHECK(measuredCommit.ok());

        auto unfinished     = requireContext(proc, Procgen::beginSystemHandle("timed", 5));
        auto unfinishedView = unfinished.view();
        REQUIRE(unfinishedView.isBound());
        CHECK(unfinishedView->beginTrace("forgotten", 1));
        CHECK(unfinishedView->publish("points", &points));
        auto unfinishedCommit = proc.commitSystem(unfinished.handle);
        CHECK(!unfinishedCommit.ok());
        const eve::Diagnostic* unfinishedDiagnostic = unfinishedCommit.error();
        REQUIRE(unfinishedDiagnostic != nullptr);
        CHECK_EQ(unfinishedDiagnostic->code(), eve::DiagnosticCode::PreconditionViolation);
        CHECK_EQ(proc.getSystemRevision("timed"), uint64_t(1));

        auto unmatched     = requireContext(proc, Procgen::beginSystemHandle("unmatched", 5));
        auto unmatchedView = unmatched.view();
        REQUIRE(unmatchedView.isBound());
        CHECK(!unmatchedView->endTrace(0));
        CHECK_EQ(unmatchedView->getError(), std::string("endTrace: no stage timer is active"));
    };
    SUB_CASE("hot reload revision -> previous snapshot available for diff") {
        Procgen& proc = *Procgen::create();
        PointSet first;
        first.add(1.f, 0.f, 1.f);
        auto revision1     = requireContext(proc, Procgen::beginSystemHandle("diff", 1));
        auto revision1View = revision1.view();
        REQUIRE(revision1View.isBound());
        CHECK(revision1View->captureDebug("trees", &first));
        auto revision1Commit = proc.commitSystem(revision1.handle);
        CHECK(revision1Commit.ok());
        CHECK_EQ(proc.getPreviousSystemRevision("diff"), uint64_t(0));
        auto missingPrevious = proc.getPreviousSystemDebugStageHandle("diff", "trees");
        CHECK(!missingPrevious.ok());
        const eve::Diagnostic* missingDiagnostic = missingPrevious.error();
        REQUIRE(missingDiagnostic != nullptr);
        CHECK_EQ(missingDiagnostic->code(), eve::DiagnosticCode::NotFound);

        PointSet second = first;
        second.add(2.f, 0.f, 2.f);
        auto revision2     = requireContext(proc, Procgen::beginSystemHandle("diff", 2));
        auto revision2View = revision2.view();
        REQUIRE(revision2View.isBound());
        CHECK(revision2View->captureDebug("trees", &second));
        CHECK(revision2View->captureDebug("roads", &first));
        auto revision2Commit = proc.commitSystem(revision2.handle);
        CHECK(revision2Commit.ok());
        CHECK_EQ(proc.getPreviousSystemRevision("diff"), uint64_t(1));
        auto previousTrees     = requirePointSet(proc, proc.getPreviousSystemDebugStageHandle("diff", "trees"));
        auto previousTreesView = previousTrees.view();
        REQUIRE(previousTreesView.isBound());
        CHECK_EQ(previousTreesView->getCount(), 1);
        const std::string report = proc.getSystemDebugDiffReport("diff");
        CHECK(report.find("revision=1 -> 2") != std::string::npos);
        CHECK(report.find("debug trees points=2 delta=+1") != std::string::npos);
        CHECK(report.find("debug roads points=1 delta=+1") != std::string::npos);

        auto failed     = requireContext(proc, Procgen::beginSystemHandle("diff", 3));
        auto failedView = failed.view();
        REQUIRE(failedView.isBound());
        failedView->fail("broken reload");
        auto failedCommit = proc.commitSystem(failed.handle);
        CHECK(!failedCommit.ok());
        CHECK_EQ(proc.getPreviousSystemRevision("diff"), uint64_t(1));
        auto removeResult = proc.removeSystem("diff");
        CHECK(removeResult.ok());
        CHECK_EQ(proc.getPreviousSystemRevision("diff"), uint64_t(0));
    };
}


}  // namespace test_procgen_pointset
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_procgen_poisson)
#include <algorithm>
#include <cmath>
#include <utility>
#include "PrivateTestScope.h"
#include "procgen/PointSet.h"
#include "procgen/Procgen.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// procgen_poisson.cpp
namespace test_procgen_poisson {
using namespace eve::procgen;

namespace {

/** @brief Owns a Procgen point-set handle for one test's lifetime. */
class PointSetLease {
public:
    PointSetLease() = default;
    PointSetLease(Procgen& owner, ProcgenPointSetHandleRef handle) : owner_(&owner), handle_(handle) {}

    PointSetLease(const PointSetLease&)            = delete;
    PointSetLease& operator=(const PointSetLease&) = delete;
    PointSetLease(PointSetLease&& other) noexcept : owner_(other.owner_), handle_(other.handle_) {
        other.owner_  = nullptr;
        other.handle_ = {};
    }
    PointSetLease& operator=(PointSetLease&& other) noexcept {
        if (this == &other) return *this;
        reset();
        owner_        = other.owner_;
        handle_       = other.handle_;
        other.owner_  = nullptr;
        other.handle_ = {};
        return *this;
    }
    ~PointSetLease() { reset(); }

    /** @brief Releases the owned handle and explicitly observes the cleanup Result. */
    void reset() noexcept {
        if (!owner_ || !handle_.isValid()) return;
        auto result = owner_->releasePointSet(handle_);
        result.ignore("test point-set cleanup");
        owner_  = nullptr;
        handle_ = {};
    }

    /** @brief Resolves the handle for the duration of the current test operation. */
    [[nodiscard]] eve::script::Borrowed<PointSet> view() const noexcept {
        return owner_ ? owner_->resolvePointSet(handle_) : eve::script::Borrowed<PointSet>();
    }

private:
    Procgen*                 owner_ = nullptr;
    ProcgenPointSetHandleRef handle_{};
};

/** @brief Converts a checked point-set allocation Result into a test lease. */
PointSetLease requirePointSet(Procgen& proc, eve::Result<ProcgenPointSetHandleRef>&& result) {
    const bool ok = result.ok();
    if (!ok) {
        const eve::Diagnostic* diagnostic = result.error();
        REQUIRE(diagnostic != nullptr);
    }
    REQUIRE(ok);
    return PointSetLease(proc, std::move(result).takeValue());
}

}  // namespace

TEST_CASE("procgen.poisson.spec.seedSpacingLimitAndBounds") {
    SUB_CASE("same Poisson seed -> identical samples") {
        Procgen proc;

        auto a     = requirePointSet(proc, proc.poissonDiskHandle(40, 30, 2.5f, 99, 300));
        auto b     = requirePointSet(proc, proc.poissonDiskHandle(40, 30, 2.5f, 99, 300));
        auto c     = requirePointSet(proc, proc.poissonDiskHandle(40, 30, 2.5f, 100, 300));
        auto aView = a.view();
        auto bView = b.view();
        auto cView = c.view();
        REQUIRE(aView.isBound());
        REQUIRE(bView.isBound());
        REQUIRE(cView.isBound());
        CHECK_EQ(aView->getCount(), bView->getCount());
        CHECK(aView->getCount() > 0);
        for (int i = 0; i < aView->getCount(); ++i) {
            CHECK_EQ(aView->getX(i), bView->getX(i));
            CHECK_EQ(aView->getZ(i), bView->getZ(i));
        }
        bool seedChangedOutput = aView->getCount() != cView->getCount();
        for (int i = 0; i < std::min(aView->getCount(), cView->getCount()); ++i) {
            if (aView->getX(i) != cView->getX(i) || aView->getZ(i) != cView->getZ(i)) seedChangedOutput = true;
        }
        REQUIRE(seedChangedOutput);
    };
    SUB_CASE("negative area, zero radius, or negative limit -> InvalidArgument") {
        Procgen proc;
        REQUIRE(!proc.poissonDiskHandle(-1, 10, 1.f, 1, 10).ok());
        REQUIRE(!proc.poissonDiskHandle(10, 10, 0.f, 1, 10).ok());
        REQUIRE(!proc.poissonDiskHandle(10, 10, 1.f, 1, -1).ok());
    };
    SUB_CASE("minimum spacing -> every pair separated") {
        Procgen proc;
        auto    points = requirePointSet(proc, proc.poissonDiskHandle(50, 50, 2.f, 7, 400));
        auto    view   = points.view();
        REQUIRE(view.isBound());
        CHECK(view->getCount() > 1);

        const float minDist = 1.9f;  // allow a little float slack
        for (int i = 0; i < view->getCount(); ++i) {
            for (int j = i + 1; j < view->getCount(); ++j) {
                const float dx = view->getX(i) - view->getX(j);
                const float dz = view->getZ(i) - view->getZ(j);
                const float d  = std::sqrt(dx * dx + dz * dz);
                CHECK(d >= minDist);
            }
        }
    };
    SUB_CASE("maxPoints limit -> capped sample count") {
        Procgen proc;
        auto    capped     = requirePointSet(proc, proc.poissonDiskHandle(100, 100, 0.5f, 3, 50));
        auto    cappedView = capped.view();
        REQUIRE(cappedView.isBound());
        CHECK(cappedView->getCount() <= 50);

        auto tiny     = requirePointSet(proc, proc.poissonDiskHandle(0, 0, 1.f, 3, 100));
        auto tinyView = tiny.view();
        REQUIRE(tinyView.isBound());
        CHECK_EQ(tinyView->getCount(), 0);
    };
    SUB_CASE("sampling area -> all positions inside bounds") {
        Procgen proc;
        auto    points = requirePointSet(proc, proc.poissonDiskHandle(20, 20, 1.5f, 11, 200));
        auto    view   = points.view();
        REQUIRE(view.isBound());
        for (int i = 0; i < view->getCount(); ++i) {
            CHECK(view->getX(i) >= 0.f);
            CHECK(view->getX(i) <= 20.f);
            CHECK(view->getZ(i) >= 0.f);
            CHECK(view->getZ(i) <= 20.f);
            CHECK_EQ(view->getY(i), 0.f);
        }
    };
}


}  // namespace test_procgen_poisson
#endif
