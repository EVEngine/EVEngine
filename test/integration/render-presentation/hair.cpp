#include <glm/gtc/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include "Fixtures.h"
#include "graphics/AmbientOcclusion.h"
#include "graphics/AntiAliasing.h"
#include "graphics/Canvas.h"
#include "graphics/DrawItem2D.h"
#include "graphics/Font.h"
#include "graphics/GBuffer.h"
#include "graphics/GlobalIllumination.h"
#include "graphics/Graphics.h"
#include "graphics/Grass.h"
#include "graphics/HairCards.h"
#include "graphics/HairShader.h"
#include "graphics/Light.h"
#include "graphics/Material.h"
#include "graphics/Mesh.h"
#include "graphics/Outline.h"
#include "graphics/Quad.h"
#include "graphics/RenderControl.h"
#include "graphics/RenderSystem3D.h"
#include "graphics/ScreenSpaceReflection.h"
#include "graphics/Shader.h"
#include "graphics/Texture.h"
#include "graphics/Volumetric.h"
#include "graphics/Water.h"
#include "graphics/Waterfall.h"
#include "graphics/hair/Binding.h"
#include "graphics/hair/ClusterGrid.h"
#include "graphics/hair/GroomAsset.h"
#include "graphics/hair/GroomInstance.h"
#include "graphics/hair/Guides.h"
#include "graphics/hair/Procedural.h"
#include "graphics/hair/RibbonBuilder.h"
#include "graphics/hair/Simulation.h"
#include "graphics/hair/StrandsDatas.h"
#include "graphics/shaders/mesh3d_hair_frag_spv.inc"
#include "graphics/shaders/mesh3d_hair_vert_spv.inc"
#include "window/Window.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

// hair.cpp
using eve::graphics::Graphics;
using eve::graphics::Renderable3D;
using eve::graphics::Shader;


TEST_CASE("graphics.HairShader.createGpuPipeline") {
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 96, 64);
    Shader *shader = gfx->newHairShader();
    REQUIRE(shader != nullptr);
    REQUIRE(shader->gpuHandle != nullptr);
    CHECK(shader->hasUniform("specExp"));
    CHECK(shader->hasUniform("strandDir"));
    CHECK(shader->hasUniform("marschnerR"));
    shader->sendFloat("specStrength", 0.7f);
    shader->sendVec3("strandDir", 0.f, 1.f, 0.f);
    shader->sendFloat("marschnerR", 1.2f);
    shader->sendFloat("marschnerTT", 0.5f);
    shader->sendFloat("marschnerTRT", 0.3f);
    CHECK(shader->hasUniform("selfShadowStrength"));
    shader->sendFloat("selfShadowStrength", 0.5f);
    shader->sendFloat("selfShadowBias", 0.2f);
    shader->sendFloat("rootAoStrength", 0.4f);
    win->close();
}


// hair_cards.cpp
using eve::graphics::Graphics;
using eve::graphics::Material;
using eve::graphics::Mesh;
using eve::graphics::Renderable3D;


TEST_CASE("graphics.HairCards.uploadMaterialAndLod") {
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 96, 64);
    REQUIRE(gfx != nullptr);

    Mesh *nearMesh = gfx->newHairCardMesh(0.15f, 0.4f);
    REQUIRE(nearMesh != nullptr);
    Mesh *farMesh = gfx->newHairCardMesh(0.25f, 0.35f);
    REQUIRE(farMesh != nullptr);

    Material *mat = gfx->newHairCardMaterial(nullptr);
    REQUIRE(mat != nullptr);
    CHECK(mat->getHair());
    CHECK(mat->getDoubleSided());
    CHECK(!mat->getDepthWrite());
    CHECK(!mat->getCastShadow());
    CHECK_EQ(mat->getSortPriority(), 10);

    auto *r = Renderable3D::create();
    REQUIRE(r != nullptr);
    eve::graphics::hair::configureCardLod(r, nearMesh, farMesh, 6.f);
    CHECK(r->getHair());
    CHECK_EQ(r->getMeshLodCount(), 2);

    delete mat;
    win->close();
}

// hair_groom.cpp
using eve::graphics::Graphics;
using eve::graphics::hair::appendRibbonMesh;
using eve::graphics::hair::BindingDeformMode;
using eve::graphics::hair::buildGuideWeights;
using eve::graphics::hair::buildRibbons;
using eve::graphics::hair::ClusterGrid;
using eve::graphics::hair::extractGuides;
using eve::graphics::hair::filterStrandsByCurves;
using eve::graphics::hair::generateOnPlane;
using eve::graphics::hair::GroomAsset;
using eve::graphics::hair::GroomBinding;
using eve::graphics::hair::GroomGroup;
using eve::graphics::hair::GroomInstance;
using eve::graphics::hair::GroomLod;
using eve::graphics::hair::GuideInfluence;
using eve::graphics::hair::GuideSimParams;
using eve::graphics::hair::GuideSimulator;
using eve::graphics::hair::interpolateStrands;
using eve::graphics::hair::InterpolationMode;
using eve::graphics::hair::ProceduralParams;
using eve::graphics::hair::Representation;
using eve::graphics::hair::RibbonMesh;
using eve::graphics::hair::RibbonParams;
using eve::graphics::hair::selectLodIndex;
using eve::graphics::hair::SkinTriMesh;
using eve::graphics::hair::StrandCurve;
using eve::graphics::hair::StrandGuideWeights;
using eve::graphics::hair::StrandPoint;
using eve::graphics::hair::StrandsDatas;


TEST_CASE("graphics.hair.groomInstanceMultiGroupAndMarschner") {
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 96, 64);

    ProceduralParams params;
    params.strandCount     = 6;
    params.pointsPerStrand = 4;
    params.seed            = 31;
    auto scalp             = generateOnPlane(0.2f, 0.2f, params);
    params.seed            = 32;
    auto fringe            = generateOnPlane(0.12f, 0.12f, params);
    REQUIRE(scalp.ok());
    REQUIRE(fringe.ok());

    GroomLod lod;
    lod.screenSize     = 1.f;
    lod.representation = Representation::Strands;

    GroomGroup g0;
    g0.name    = "scalp";
    g0.groupId = 1;
    g0.strands = std::move(scalp).value();
    g0.lods    = {lod};
    GroomGroup g1;
    g1.name    = "fringe";
    g1.groupId = 2;
    g1.strands = std::move(fringe).value();
    g1.lods    = {lod};

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


TEST_CASE("graphics.hair.groomInstanceCardsLodBakeAndDraw") {
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 96, 64);

    GroomInstance *groom = gfx->newGroomInstance();
    REQUIRE(groom != nullptr);
    ProceduralParams params;
    params.strandCount     = 24;
    params.pointsPerStrand = 5;
    params.seed            = 41;
    auto baked             = groom->bakeProceduralPlane(0.2f, 0.2f, params);
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
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 96, 64);

    GroomInstance *groom = gfx->newGroomInstance();
    REQUIRE(groom != nullptr);
    ProceduralParams params;
    params.strandCount     = 32;
    params.pointsPerStrand = 6;
    params.seed            = 11;
    auto baked             = groom->bakeProceduralPlane(0.25f, 0.25f, params);
    REQUIRE(baked.ok());
    CHECK(!groom->isGuideSimulationEnabled());

    GuideSimParams simParams;
    simParams.gravity    = glm::vec3(0.f, -15.f, 0.f);
    simParams.damping    = 0.97f;
    simParams.iterations = 4;
    auto enabled         = groom->enableGuideSimulation(simParams, 0.2f, InterpolationMode::Offset);
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
