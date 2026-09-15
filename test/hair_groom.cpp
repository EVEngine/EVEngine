#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
#include "Fixtures.h"

#include "graphics/Graphics.h"
#include "graphics/Mesh.h"
#include "graphics/Shader.h"
#include "graphics/hair/Binding.h"
#include "graphics/hair/ClusterGrid.h"
#include "graphics/hair/GroomAsset.h"
#include "graphics/hair/GroomInstance.h"
#include "graphics/hair/Guides.h"
#include "graphics/hair/Procedural.h"
#include "graphics/hair/RibbonBuilder.h"
#include "graphics/hair/Simulation.h"
#include "graphics/hair/StrandsDatas.h"
#include "window/Window.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

using eve::graphics::Graphics;
using eve::graphics::hair::ClusterGrid;
using eve::graphics::hair::BindingDeformMode;
using eve::graphics::hair::GroomBinding;
using eve::graphics::hair::GuideInfluence;
using eve::graphics::hair::InterpolationMode;
using eve::graphics::hair::SkinTriMesh;
using eve::graphics::hair::StrandGuideWeights;
using eve::graphics::hair::GuideSimParams;
using eve::graphics::hair::GuideSimulator;
using eve::graphics::hair::buildGuideWeights;
using eve::graphics::hair::extractGuides;
using eve::graphics::hair::interpolateStrands;
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

    CHECK(groom->getShader()->hasUniform("selfShadowStrength"));
    groom->setSelfShadow(0.6f, 0.2f, 0.5f);
    CHECK_EQ(groom->getSelfShadowStrength(), 0.6f);
    CHECK_EQ(groom->getSelfShadowBias(), 0.2f);
    CHECK_EQ(groom->getRootAoStrength(), 0.5f);
    groom->setSelfShadow(-1.f, 2.f, -0.5f);
    CHECK_EQ(groom->getSelfShadowStrength(), 0.f);
    CHECK_EQ(groom->getSelfShadowBias(), 1.f);
    CHECK_EQ(groom->getRootAoStrength(), 0.f);

    gfx->begin3DFrame();
    groom->draw(glm::mat4(1.f));
    gfx->present();
    win->close();
}

TEST_CASE("graphics.hair.groomBindingProjectsAndDeforms") {
    // Two strands rooted above a flat quad (two triangles) on XZ.
    StrandsDatas strands;
    std::vector<StrandPoint> points;
    std::vector<StrandCurve> curves;
    auto add = [&](glm::vec3 root) {
        StrandCurve c;
        c.pointOffset = uint32_t(points.size());
        c.pointCount = 3;
        points.push_back({root, 0.002f, 0.f});
        points.push_back({root + glm::vec3(0.f, 0.05f, 0.f), 0.0015f, 0.5f});
        points.push_back({root + glm::vec3(0.f, 0.1f, 0.f), 0.001f, 1.f});
        c.length = 0.1f;
        curves.push_back(c);
    };
    add(glm::vec3(-0.2f, 0.f, 0.f));
    add(glm::vec3(0.2f, 0.f, 0.f));
    strands.setPoints(std::move(points));
    strands.setCurves(std::move(curves));
    REQUIRE(strands.validate().ok());

    float restPos[] = {
        -0.5f, 0.f, -0.5f, 0.5f, 0.f, -0.5f, 0.5f, 0.f, 0.5f, -0.5f, 0.f, 0.5f,
    };
    uint32_t idx[] = {0, 1, 2, 0, 2, 3};
    SkinTriMesh rest{restPos, 4, idx, 6};

    GroomBinding binding;
    auto built = binding.build(strands, rest);
    REQUIRE(built.ok());
    CHECK_EQ(binding.rootCount(), 2u);
    REQUIRE(binding.rootAt(0) != nullptr);
    CHECK(binding.rootAt(0)->triangleIndex < 2u);

    // Lift the scalp by +0.25 on Y; Rigid deform should lift strand roots.
    float defPos[12];
    for (int i = 0; i < 4; ++i) {
        defPos[i * 3 + 0] = restPos[i * 3 + 0];
        defPos[i * 3 + 1] = restPos[i * 3 + 1] + 0.25f;
        defPos[i * 3 + 2] = restPos[i * 3 + 2];
    }
    SkinTriMesh deformed{defPos, 4, idx, 6};
    auto moved = binding.deform(strands, deformed, BindingDeformMode::Rigid);
    REQUIRE(moved.ok());
    CHECK_EQ(moved.value().curveCount(), 2u);
    const float y0 = moved.value().curvePoints(0)[0].position.y;
    CHECK(y0 > 0.2f);
    CHECK(y0 < 0.3f);
    // Tip moves by the same delta under Rigid.
    const float tipDelta =
        moved.value().curvePoints(0)[2].position.y - strands.curvePoints(0)[2].position.y;
    CHECK(tipDelta > 0.2f);

    auto offset = binding.deform(strands, deformed, BindingDeformMode::Offset);
    REQUIRE(offset.ok());
    CHECK_EQ(offset.value().curveCount(), 2u);
}

TEST_CASE("graphics.hair.guideWeightsAndInterpolate") {
    ProceduralParams params;
    params.strandCount = 24;
    params.pointsPerStrand = 5;
    params.seed = 9;
    auto strands = generateOnPlane(0.3f, 0.3f, params);
    REQUIRE(strands.ok());

    auto guides = extractGuides(strands.value(), 0.25f);
    REQUIRE(guides.ok());
    CHECK(guides.value().curveCount() >= 1u);
    CHECK(guides.value().curveCount() < strands.value().curveCount());

    auto weights = buildGuideWeights(strands.value(), guides.value(), 3, InterpolationMode::Offset);
    REQUIRE(weights.ok());
    CHECK_EQ(weights.value().size(), strands.value().curveCount());
    float wSum = 0.f;
    for (int i = 0; i < weights.value()[0].count; ++i) wSum += weights.value()[0].influencers[i].weight;
    CHECK(wSum > 0.99f);
    CHECK(wSum < 1.01f);

    // Translate all guide points by +0.1 X and interpolate.
    StrandsDatas guidesDef = guides.value();
    std::vector<StrandPoint> gp(guidesDef.points().begin(), guidesDef.points().end());
    for (auto &p : gp) p.position.x += 0.1f;
    guidesDef.setPoints(std::move(gp));

    auto rigid = interpolateStrands(strands.value(), guides.value(), guidesDef, weights.value(),
                                    InterpolationMode::Rigid);
    REQUIRE(rigid.ok());
    const float dx =
        rigid.value().curvePoints(0)[0].position.x - strands.value().curvePoints(0)[0].position.x;
    CHECK(dx > 0.05f);

    auto offset = interpolateStrands(strands.value(), guides.value(), guidesDef, weights.value(),
                                     InterpolationMode::Offset);
    REQUIRE(offset.ok());
    auto smooth = interpolateStrands(strands.value(), guides.value(), guidesDef, weights.value(),
                                     InterpolationMode::Smooth);
    REQUIRE(smooth.ok());
    CHECK_EQ(smooth.value().curveCount(), strands.value().curveCount());
}


TEST_CASE("graphics.hair.guideSimulatorPinsRootsAndFalls") {
    StrandsDatas guides;
    std::vector<StrandPoint> points;
    std::vector<StrandCurve> curves;
    auto add = [&](glm::vec3 root) {
        StrandCurve c;
        c.pointOffset = uint32_t(points.size());
        c.pointCount = 4;
        for (int i = 0; i < 4; ++i) {
            const float u = float(i) / 3.f;
            points.push_back({root + glm::vec3(0.f, 0.04f * float(i), 0.f), 0.001f, u});
        }
        c.length = 0.12f;
        curves.push_back(c);
    };
    add(glm::vec3(-0.1f, 0.5f, 0.f));
    add(glm::vec3(0.1f, 0.5f, 0.f));
    guides.setPoints(std::move(points));
    guides.setCurves(std::move(curves));
    REQUIRE(guides.validate().ok());

    GuideSimParams params;
    params.gravity = glm::vec3(0.f, -20.f, 0.f);
    params.damping = 0.98f;
    params.iterations = 6;
    params.compliance = 0.f;
    params.collisionY = 0.f;

    GuideSimulator sim;
    auto reset = sim.reset(guides, params);
    REQUIRE(reset.ok());
    CHECK(sim.isReady());
    CHECK_EQ(sim.curveCount(), 2u);
    CHECK_EQ(sim.particleCount(), 8u);

    const float rootY0 = guides.curvePoints(0)[0].position.y;
    for (int i = 0; i < 30; ++i) {
        auto stepped = sim.step(1.f / 60.f);
        REQUIRE(stepped.ok());
    }
    auto snap = sim.snapshot();
    REQUIRE(snap.ok());
    CHECK_EQ(snap.value().curvePoints(0)[0].position.y, rootY0);
    CHECK_EQ(snap.value().curvePoints(1)[0].position.y, rootY0);
    const float tipY = snap.value().curvePoints(0)[3].position.y;
    CHECK(tipY < rootY0 - 0.02f);
    CHECK(tipY >= -0.001f);

    CHECK(!sim.step(0.f).ok());
    CHECK(!sim.step(-0.1f).ok());
}

TEST_CASE("graphics.hair.groomInstanceCardsLodBakeAndDraw") {
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 96, 64);

    GroomInstance *groom = gfx->newGroomInstance();
    REQUIRE(groom != nullptr);
    ProceduralParams params;
    params.strandCount = 24;
    params.pointsPerStrand = 5;
    params.seed = 41;
    auto baked = groom->bakeProceduralPlane(0.2f, 0.2f, params);
    REQUIRE(baked.ok());
    REQUIRE(groom->getMesh() != nullptr);
    CHECK_EQ(groom->getActiveLodIndex(), 0);
    CHECK_EQ(groom->getActiveRepresentation(), int(Representation::Strands));
    const int strandsVerts = groom->getMesh()->getVertexCount();
    CHECK(strandsVerts > 0);

    groom->setForcedLod(1);
    auto cardsRebuild = groom->rebuild();
    REQUIRE(cardsRebuild.ok());
    REQUIRE(groom->getMesh() != nullptr);
    CHECK_EQ(groom->getActiveLodIndex(), 1);
    CHECK_EQ(groom->getActiveRepresentation(), int(Representation::Cards));
    const int cardsVerts = groom->getMesh()->getVertexCount();
    CHECK(cardsVerts > 0);
    // Cards expand per-segment quads; topology differs from near ribbon bake.
    CHECK(cardsVerts != strandsVerts);

    groom->setForcedLod(2);
    auto noneRebuild = groom->rebuild();
    REQUIRE(noneRebuild.ok());
    CHECK_EQ(groom->getActiveLodIndex(), 2);
    CHECK_EQ(groom->getActiveRepresentation(), int(Representation::None));
    CHECK(groom->getMesh() == nullptr);

    groom->setForcedLod(1);
    REQUIRE(groom->rebuild().ok());
    gfx->begin3DFrame();
    groom->draw(glm::mat4(1.f));
    gfx->present();
    win->close();
}

TEST_CASE("graphics.hair.groomInstanceGuideSimUpdate") {
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 96, 64);

    GroomInstance *groom = gfx->newGroomInstance();
    REQUIRE(groom != nullptr);
    ProceduralParams params;
    params.strandCount = 32;
    params.pointsPerStrand = 6;
    params.seed = 11;
    auto baked = groom->bakeProceduralPlane(0.25f, 0.25f, params);
    REQUIRE(baked.ok());
    CHECK(!groom->isGuideSimulationEnabled());

    GuideSimParams simParams;
    simParams.gravity = glm::vec3(0.f, -15.f, 0.f);
    simParams.damping = 0.97f;
    simParams.iterations = 4;
    auto enabled = groom->enableGuideSimulation(simParams, 0.2f, InterpolationMode::Offset);
    REQUIRE(enabled.ok());
    CHECK(groom->isGuideSimulationEnabled());
    REQUIRE(groom->getMesh() != nullptr);

    for (int i = 0; i < 12; ++i) {
        auto upd = groom->update(1.f / 60.f);
        REQUIRE(upd.ok());
    }
    CHECK(groom->getMesh() != nullptr);
    CHECK_EQ(groom->getCurveCount(), 32);

    auto disabled = groom->disableGuideSimulation();
    REQUIRE(disabled.ok());
    CHECK(!groom->isGuideSimulationEnabled());
    CHECK(groom->update(1.f / 60.f).ok());

    gfx->begin3DFrame();
    groom->draw(glm::mat4(1.f));
    gfx->present();
    win->close();
}
