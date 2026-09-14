#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "graphics/hair/GroomAsset.h"
#include "graphics/hair/Procedural.h"
#include "graphics/hair/RibbonBuilder.h"
#include "graphics/hair/StrandsDatas.h"

#include <glm/vec3.hpp>

using eve::graphics::hair::GroomAsset;
using eve::graphics::hair::GroomGroup;
using eve::graphics::hair::GroomLod;
using eve::graphics::hair::ProceduralParams;
using eve::graphics::hair::Representation;
using eve::graphics::hair::RibbonParams;
using eve::graphics::hair::StrandCurve;
using eve::graphics::hair::StrandPoint;
using eve::graphics::hair::StrandsDatas;
using eve::graphics::hair::buildRibbons;
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
