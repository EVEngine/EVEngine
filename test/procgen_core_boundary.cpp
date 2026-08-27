#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "procgen/core/ProcgenCore.h"

#include <string>
#include <type_traits>

namespace {

static_assert(std::is_default_constructible_v<eve::procgen::Params>);
static_assert(std::is_default_constructible_v<eve::procgen::Grid2D>);
static_assert(std::is_default_constructible_v<eve::procgen::PointSet>);
static_assert(std::is_default_constructible_v<eve::procgen::MeshBuild>);

}  // namespace

TEST_CASE("procgen.core.cpuSurfaceIsBackendNeutral") {
    using namespace eve::procgen;

    Params params;
    params.setSeed(42);
    params.setSize(4, 3);
    params.setBool("decorations", true);

    const RecipeDescriptor recipe = RecipeDescriptor::grid("core.grid", "Core Grid", "test", 1, 1, 32, 32);
    recipe.applyDefaults(params);
    CHECK_EQ(params.getSeed(), uint32_t(42));
    CHECK_EQ(params.getWidth(), 4);
    CHECK_EQ(params.getHeight(), 3);

    Grid2D grid;
    grid.resize(params.getWidth(), params.getHeight());
    grid.fill(int(Semantic::Floor));
    grid.setMeta("buildKey", "core-grid:v1:42:4x3");
    CHECK_EQ(grid.getCell(2, 1), int(Semantic::Floor));
    CHECK_EQ(grid.getMeta("buildKey", ""), std::string("core-grid:v1:42:4x3"));

    PointSet points;
    points.add(1.f, 0.f, 2.f);
    const PointSet jittered = jitterPointPositions(points, params.getSeed(), 0.25f, 0.25f);
    CHECK_EQ(jittered.getCount(), 1);
    CHECK_EQ(deriveSeed(params.getSeed(), "terrain"), deriveSeed(params.getSeed(), "terrain"));
    CHECK(deriveSeed(params.getSeed(), "terrain") != deriveSeed(params.getSeed(), "props"));

    MeshBuild mesh;
    mesh.addVertex(0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f);
    mesh.addVertex(1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 1.f, 0.f);
    mesh.addVertex(0.f, 0.f, 1.f, 0.f, 1.f, 0.f, 0.f, 1.f);
    mesh.addTriangle(0, 1, 2);
    CHECK_EQ(mesh.getVertexCount(), 3);
    CHECK_EQ(mesh.getIndexCount(), 3);

    ProcgenContext context("core-system", params.getSeed(), "core-grid:v1:42:4x3");
    CHECK_EQ(context.getBuildKey(), std::string("core-grid:v1:42:4x3"));
    CHECK_EQ(context.seedFor("terrain"), deriveSeed(params.getSeed(), "terrain"));
}

TEST_CASE("procgen.params.typedStorageConversionsAndCanonicalEncoding") {
    using namespace eve::procgen;

    Params params;
    params.setInt("count", 7);
    params.setFloat("ratio", 1.5f);
    params.setBool("enabled", true);
    params.setString("label", "7");

    // Numeric conversion is explicit and does not go through text.
    CHECK_EQ(params.getInt("count", -1), 7);
    CHECK_EQ(params.getFloat("count", -1.f), 7.f);
    CHECK_EQ(params.getFloat("ratio", -1.f), 1.5f);
    CHECK_EQ(params.getInt("ratio", -1), -1);  // fractional Double is not truncated
    CHECK(params.getBool("enabled", false));
    CHECK_EQ(params.getInt("enabled", -1), 1);  // Bool has only 0/1 numeric conversion
    CHECK_EQ(params.getString("count", "wrong-kind"), "wrong-kind");
    CHECK_EQ(params.getInt("label", -1), -1);  // String is never parsed
    CHECK(params.getBool("label", true));      // invalid kind returns the caller default

    const std::string canonical = params.canonicalString();
    CHECK(canonical.find("int:7") != std::string::npos);
    CHECK(canonical.find("double:") != std::string::npos);
    CHECK(canonical.find("bool:1") != std::string::npos);
    CHECK(canonical.find("string:1:7") != std::string::npos);

    Params sameValues;
    sameValues.setString("label", "7");
    sameValues.setBool("enabled", true);
    sameValues.setFloat("ratio", 1.5f);
    sameValues.setInt("count", 7);
    CHECK_EQ(sameValues.canonicalString(), canonical);

    Params differentKind;
    differentKind.setInt("count", 7);
    differentKind.setFloat("ratio", 1.5f);
    differentKind.setBool("enabled", true);
    differentKind.setInt("label", 7);
    CHECK(differentKind.canonicalString() != canonical);
}

TEST_CASE("procgen.params.dimensionsHaveDedicatedSemantics") {
    using namespace eve::procgen;

    Params params;
    params.setSize(0, -4);
    CHECK_EQ(params.getWidth(), 1);
    CHECK_EQ(params.getHeight(), 1);
    params.setInt("width", 64);
    params.setInt("height", 48);
    params.setInt("seed", 123);
    CHECK_EQ(params.getWidth(), 64);
    CHECK_EQ(params.getHeight(), 48);
    CHECK_EQ(params.getSeed(), uint32_t(123));
    CHECK_EQ(params.getInt("width", -1), 64);
    CHECK_EQ(params.getInt("height", -1), 48);
    CHECK_EQ(params.getInt("seed", -1), 123);

    // Mesh recipes also use width/height as floating algorithm parameters;
    // those values remain separate from the grid dimensions.
    params.setFloat("width", 2.5f);
    params.setString("height", "building-height");
    CHECK_EQ(params.getWidth(), 64);
    CHECK_EQ(params.getHeight(), 48);
    CHECK_EQ(params.getFloat("width", -1.f), 2.5f);
    CHECK_EQ(params.getString("height", "missing"), "building-height");

    Params reordered;
    reordered.setString("height", "building-height");
    reordered.setFloat("width", 2.5f);
    reordered.setSeed(123);
    reordered.setSize(64, 48);
    CHECK_EQ(reordered.canonicalString(), params.canonicalString());
}
