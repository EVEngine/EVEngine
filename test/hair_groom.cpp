#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
#include "Fixtures.h"

#include "graphics/Graphics.h"
#include "graphics/Mesh.h"
#include "graphics/Shader.h"
#include "graphics/hair/ClusterGrid.h"
#include "graphics/hair/GroomAsset.h"
#include "graphics/hair/GroomInstance.h"
#include "graphics/hair/Procedural.h"
#include "graphics/hair/RibbonBuilder.h"
#include "graphics/hair/StrandsDatas.h"
#include "window/Window.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

using eve::graphics::Graphics;
using eve::graphics::hair::ClusterGrid;
using eve::graphics::hair::GroomAsset;
using eve::graphics::hair::GroomGroup;
using eve::graphics::hair::GroomInstance;
using eve::graphics::hair::GroomLod;
using eve::graphics::hair::ProceduralParams;
using eve::graphics::hair::Representation;
using eve::graphics::hair::RibbonMesh;
using eve::graphics::hair::RibbonParams;
using eve::graphics::hair::StrandCurve;
using eve::graphics::hair::StrandPoint;
using eve::graphics::hair::StrandsDatas;
using eve::graphics::hair::appendRibbonMesh;
using eve::graphics::hair::buildRibbons;
using eve::graphics::hair::filterStrandsByCurves;
using eve::graphics::hair::generateOnPlane;
using eve::graphics::hair::selectLodIndex;

TEST_CASE("graphics.hair.strandsValidateRejectsEmpty") {
    StrandsDatas empty;
    auto bad = empty.validate();
    CHECK(!bad.ok());
}

TEST_CASE("graphics.hair.strandsValidateAcceptsSimpleCurve") {
    StrandsDatas data;
    std::vector<StrandPoint> points(3);
    points[0] = {glm::vec3(0.f, 0.f, 0.f), 0.002f, 0.f};
    points[1] = {glm::vec3(0.f, 0.05f, 0.f), 0.0015f, 0.5f};
    points[2] = {glm::vec3(0.f, 0.1f, 0.f), 0.001f, 1.f};
    StrandCurve curve;
    curve.pointOffset = 0;
    curve.pointCount = 3;
    curve.length = 0.1f;
    data.setPoints(std::move(points));
    data.setCurves({curve});
    auto ok = data.validate();
    CHECK(ok.ok());
    CHECK_EQ(data.curveCount(), 1u);
    CHECK_EQ(data.pointCount(), 3u);
    CHECK_EQ(data.curvePoints(0).size(), 3u);
}

TEST_CASE("graphics.hair.proceduralPlaneDeterministic") {
    ProceduralParams params;
    params.strandCount = 64;
    params.pointsPerStrand = 6;
    params.seed = 42;
    auto a = generateOnPlane(0.4f, 0.4f, params);
    auto b = generateOnPlane(0.4f, 0.4f, params);
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    CHECK_EQ(a.value().curveCount(), 64u);
    CHECK_EQ(a.value().pointCount(), 64u * 6u);
    CHECK_EQ(a.value().curveCount(), b.value().curveCount());
    CHECK_EQ(a.value().points()[0].position.x, b.value().points()[0].position.x);
    CHECK_EQ(a.value().points()[0].position.y, b.value().points()[0].position.y);
}

TEST_CASE("graphics.hair.ribbonTopology") {
    ProceduralParams params;
    params.strandCount = 8;
    params.pointsPerStrand = 4;
    params.seed = 7;
    auto strands = generateOnPlane(0.3f, 0.3f, params);
    REQUIRE(strands.ok());
    RibbonParams ribbon;
    ribbon.widthScale = 2.f;
    auto mesh = buildRibbons(strands.value(), ribbon);
    REQUIRE(mesh.ok());
    CHECK(mesh.value().vertexCount() > 0);
    CHECK(mesh.value().indexCount() > 0);
    CHECK_EQ(mesh.value().indexCount() % 3, 0);
    CHECK_EQ(mesh.value().posXYZ.size(), mesh.value().nrmXYZ.size());
    CHECK_EQ(mesh.value().uvST.size(), size_t(mesh.value().vertexCount()) * 2u);
}

TEST_CASE("graphics.hair.groomAssetLodSelection") {
    GroomGroup group;
    group.name = "scalp";
    group.groupId = 1;

    ProceduralParams params;
    params.strandCount = 16;
    params.pointsPerStrand = 4;
    params.seed = 3;
    auto strands = generateOnPlane(0.2f, 0.2f, params);
    REQUIRE(strands.ok());
    group.strands = std::move(strands).value();

    GroomLod nearLod;
    nearLod.screenSize = 1.f;
    nearLod.curveFraction = 1.f;
    GroomLod farLod;
    farLod.screenSize = 0.3f;
    farLod.curveFraction = 0.25f;
    farLod.thicknessScale = 2.f;
    group.lods = {nearLod, farLod};

    GroomAsset asset;
    auto add = asset.addGroup(std::move(group));
    REQUIRE(add.ok());
    auto valid = asset.validate();
    CHECK(valid.ok());
    CHECK_EQ(selectLodIndex(asset.groupAt(0)->lods, 1.f), 0u);
    CHECK_EQ(selectLodIndex(asset.groupAt(0)->lods, 0.2f), 1u);
}

TEST_CASE("graphics.hair.groomAssetRejectsDuplicateId") {
    ProceduralParams params;
    params.strandCount = 4;
    params.pointsPerStrand = 3;
    auto strands = generateOnPlane(0.1f, 0.1f, params);
    REQUIRE(strands.ok());

    GroomGroup a;
    a.name = "a";
    a.groupId = 9;
    a.strands = strands.value();
    // Need a second independent copy — regenerate.
    auto strands2 = generateOnPlane(0.1f, 0.1f, params);
    REQUIRE(strands2.ok());
    GroomGroup b;
    b.name = "b";
    b.groupId = 9;
    b.strands = std::move(strands2).value();

    GroomAsset asset;
    REQUIRE(asset.addGroup(std::move(a)).ok());
    auto dup = asset.addGroup(std::move(b));
    CHECK(!dup.ok());
}

TEST_CASE("graphics.hair.clusterGridAabbAndCull") {
    StrandsDatas data;
    std::vector<StrandPoint> points;
    std::vector<StrandCurve> curves;

    auto addStrand = [&](glm::vec3 root) {
        StrandCurve c;
        c.pointOffset = uint32_t(points.size());
        c.pointCount = 2;
        c.length = 0.1f;
        points.push_back({root, 0.002f, 0.f});
        points.push_back({root + glm::vec3(0.f, 0.1f, 0.f), 0.001f, 1.f});
        curves.push_back(c);
    };
    // Two spatial clumps far apart so cellSize 0.5 yields ≥2 clusters.
    addStrand(glm::vec3(-2.f, 0.f, 0.f));
    addStrand(glm::vec3(-1.9f, 0.f, 0.05f));
    addStrand(glm::vec3(2.f, 0.f, 0.f));
    addStrand(glm::vec3(2.1f, 0.f, -0.05f));
    data.setPoints(std::move(points));
    data.setCurves(std::move(curves));
    REQUIRE(data.validate().ok());

    ClusterGrid grid;
    auto built = grid.build(data, 0.5f);
    REQUIRE(built.ok());
    CHECK(grid.clusterCount() >= 2u);

    glm::vec3 bmin, bmax;
    grid.computeBounds(bmin, bmax);
    CHECK(bmin.x < -1.f);
    CHECK(bmax.x > 1.f);

    // Camera looks at the left clump; right clump should cull out.
    const glm::mat4 view = glm::lookAtRH(glm::vec3(-2.f, 0.4f, 1.5f), glm::vec3(-2.f, 0.05f, 0.f),
                                         glm::vec3(0.f, 1.f, 0.f));
    const glm::mat4 proj = glm::perspectiveRH_ZO(glm::radians(35.f), 1.f, 0.1f, 4.f);
    const glm::mat4 viewProj = proj * view;
    auto visible = grid.cullClusters(reinterpret_cast<const float *>(&viewProj[0][0]));
    REQUIRE(visible.ok());
    CHECK(!visible.value().empty());
    CHECK(visible.value().size() < grid.clusterCount());

    auto curvesLeft = grid.collectCurveIndices(visible.value());
    REQUIRE(curvesLeft.ok());
    auto filtered = filterStrandsByCurves(data, curvesLeft.value());
    REQUIRE(filtered.ok());
    CHECK(filtered.value().curveCount() > 0u);
    CHECK(filtered.value().curveCount() < data.curveCount());
}

TEST_CASE("graphics.hair.groomAssetLodRepresentationNone") {
    GroomGroup group;
    group.name = "scalp";
    group.groupId = 2;

    ProceduralParams params;
    params.strandCount = 24;
    params.pointsPerStrand = 4;
    params.seed = 5;
    auto strands = generateOnPlane(0.2f, 0.2f, params);
    REQUIRE(strands.ok());
    group.strands = std::move(strands).value();

    GroomLod nearLod;
    nearLod.screenSize = 1.f;
    nearLod.representation = Representation::Strands;
    GroomLod farLod;
    farLod.screenSize = 0.2f;
    farLod.representation = Representation::None;
    group.lods = {nearLod, farLod};

    GroomAsset asset;
    REQUIRE(asset.addGroup(std::move(group)).ok());
    REQUIRE(asset.validate().ok());
    CHECK_EQ(selectLodIndex(asset.groupAt(0)->lods, 0.9f), 0u);
    CHECK_EQ(selectLodIndex(asset.groupAt(0)->lods, 0.1f), 1u);
    CHECK(asset.groupAt(0)->lods[1].representation == Representation::None);
}

TEST_CASE("graphics.hair.appendRibbonMeshRebasesIndices") {
    ProceduralParams params;
    params.strandCount = 4;
    params.pointsPerStrand = 3;
    params.seed = 11;
    auto a = generateOnPlane(0.15f, 0.15f, params);
    params.seed = 12;
    auto b = generateOnPlane(0.15f, 0.15f, params);
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    auto meshA = buildRibbons(a.value());
    auto meshB = buildRibbons(b.value());
    REQUIRE(meshA.ok());
    REQUIRE(meshB.ok());

    RibbonMesh combined = meshA.value();
    const int baseVerts = combined.vertexCount();
    const int baseIdx = combined.indexCount();
    appendRibbonMesh(combined, meshB.value());
    CHECK_EQ(combined.vertexCount(), baseVerts + meshB.value().vertexCount());
    CHECK_EQ(combined.indexCount(), baseIdx + meshB.value().indexCount());
    CHECK_EQ(combined.indexCount() % 3, 0);
    for (size_t i = size_t(baseIdx); i < combined.indices.size(); ++i) {
        CHECK(combined.indices[i] >= uint32_t(baseVerts));
        CHECK(combined.indices[i] < uint32_t(combined.vertexCount()));
    }
}

TEST_CASE("graphics.hair.groomAssetMultiGroup") {
    ProceduralParams params;
    params.strandCount = 8;
    params.pointsPerStrand = 4;
    params.seed = 21;
    auto scalp = generateOnPlane(0.2f, 0.2f, params);
    params.seed = 22;
    auto fringe = generateOnPlane(0.1f, 0.1f, params);
    REQUIRE(scalp.ok());
    REQUIRE(fringe.ok());

    GroomLod lod;
    lod.screenSize = 1.f;
    lod.representation = Representation::Strands;

    GroomGroup g0;
    g0.name = "scalp";
    g0.groupId = 1;
    g0.strands = std::move(scalp).value();
    g0.lods = {lod};

    GroomGroup g1;
    g1.name = "fringe";
    g1.groupId = 2;
    g1.strands = std::move(fringe).value();
    g1.lods = {lod};

    GroomAsset asset;
    REQUIRE(asset.addGroup(std::move(g0)).ok());
    REQUIRE(asset.addGroup(std::move(g1)).ok());
    REQUIRE(asset.validate().ok());
    CHECK_EQ(asset.groupCount(), 2u);
    CHECK_EQ(asset.groupAt(0)->name, std::string("scalp"));
    CHECK_EQ(asset.groupAt(1)->name, std::string("fringe"));
}

TEST_CASE("graphics.hair.groomInstanceMultiGroupAndMarschner") {
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 96, 64);

    ProceduralParams params;
    params.strandCount = 6;
    params.pointsPerStrand = 4;
    params.seed = 31;
    auto scalp = generateOnPlane(0.2f, 0.2f, params);
    params.seed = 32;
    auto fringe = generateOnPlane(0.12f, 0.12f, params);
    REQUIRE(scalp.ok());
    REQUIRE(fringe.ok());

    GroomLod lod;
    lod.screenSize = 1.f;
    lod.representation = Representation::Strands;

    GroomGroup g0;
    g0.name = "scalp";
    g0.groupId = 1;
    g0.strands = std::move(scalp).value();
    g0.lods = {lod};
    GroomGroup g1;
    g1.name = "fringe";
    g1.groupId = 2;
    g1.strands = std::move(fringe).value();
    g1.lods = {lod};

    GroomAsset asset;
    REQUIRE(asset.addGroup(std::move(g0)).ok());
    REQUIRE(asset.addGroup(std::move(g1)).ok());

    GroomInstance *groom = gfx->newGroomInstance();
    REQUIRE(groom != nullptr);
    auto set = groom->setAsset(asset);
    REQUIRE(set.ok());
    CHECK_EQ(groom->getGroupCount(), 2u);
    CHECK(groom->getCurveCount() == 12);
    REQUIRE(groom->getMesh() != nullptr);
    REQUIRE(groom->getShader() != nullptr);
    CHECK(groom->getShader()->hasUniform("marschnerR"));

    groom->setMarschnerLobes(1.5f, 0.6f, 0.4f);
    CHECK_EQ(groom->getMarschnerR(), 1.5f);
    CHECK_EQ(groom->getMarschnerTT(), 0.6f);
    CHECK_EQ(groom->getMarschnerTRT(), 0.4f);
    groom->setMarschnerLobes(-1.f, 0.2f, -0.5f);
    CHECK_EQ(groom->getMarschnerR(), 0.f);
    CHECK_EQ(groom->getMarschnerTT(), 0.2f);
    CHECK_EQ(groom->getMarschnerTRT(), 0.f);

    gfx->begin3DFrame();
    groom->draw(glm::mat4(1.f));
    gfx->present();
    win->close();
}
