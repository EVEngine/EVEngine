#include <zeroerr/unittest.h>
#include <cmath>
#include <limits>
#include <memory>
#include <simplesquirrel/simplesquirrel.hpp>
#include "camera/CameraController.h"
#include "camera/PcgCarCameraSetup.h"
#include "camera/PcgFreeCamera.h"
#include "common/Capability.h"
#include "graphics/Canvas.h"
#include "graphics/Graphics.h"
#include "graphics/Light.h"
#include "graphics/PcgWaterPhotoMode.h"
#include "graphics/RenderSystem3D.h"
#include "image/ImageData.h"
#include "procgen/PcgMeshLod.h"
#include "procgen/PcgMeshLodBackup.h"
#include "procgen/Procgen.h"
#include "scene/Scene.h"
#include "scene/SceneNodeRef.h"
#include "ui/PcgPhotoModeApplyPlan.h"
#include "zeroerr/unittest.h"

// pcg_car_camera.cpp
using namespace eve::camera;
using eve::graphics::Camera3D;
using namespace eve::scene;
TEST_CASE("camera.pcgCar.appliesExactDefaultsAndInput") {
    Scene* scene = Scene::create();
    scene->beginBuild();
    scene->beginNode("root");
    scene->addNode("car");
    scene->end();
    REQUIRE(scene->mountBuildAs("pcg-car-test"));
    SceneNodeRef        car("pcg-car-test", "car");
    auto*               camera = Camera3D::createCamera();
    CameraController    controller;
    PcgCarCameraSetup   setup;
    PcgCarCameraProfile profile;
    REQUIRE(setup.configure(profile).ok());
    REQUIRE(setup.apply(&controller, camera, &car, 15.f, 10.f).ok());
    CHECK_EQ(controller.getMode(), "orbit");
    CHECK_EQ(controller.getRadius(), 6.f);
    CHECK_EQ(controller.getMinimumRadius(), .6f);
    CHECK_EQ(controller.getMaximumRadius(), 20.f);
    REQUIRE(setup.update(&controller, 1.f, 100.f, 1.f, .25f, false, 0.f).ok());
    CHECK_EQ(setup.getYaw(), 19.f);
    CHECK_EQ(setup.getPitch(), -80.f);
    CHECK_EQ(controller.getRadius(), .6f);
}
TEST_CASE("camera.pcgCar.rejectsBadProfilesWithoutMutation") {
    PcgCarCameraSetup   setup;
    PcgCarCameraProfile profile;
    REQUIRE(setup.configure(profile).ok());
    profile.minimumDistance = 30.f;
    CHECK(!setup.configure(profile).ok());
    CameraController controller;
    REQUIRE(controller.setRadiusLimits(1.f, 10.f).ok());
    controller.setRadius(5.f);
    CHECK(!controller.setRadiusLimits(8.f, 2.f).ok());
    CHECK_EQ(controller.getRadius(), 5.f);
    CHECK_EQ(controller.getMinimumRadius(), 1.f);
    CHECK(!setup.update(nullptr, 0, 0, 0, .1f, false, 0).ok());
    CHECK(!setup.update(&controller, std::numeric_limits<float>::quiet_NaN(), 0, 0, .1f, false, 0).ok());
}
TEST_CASE("camera.pcgCar.rearLockUsesInjectedTargetYaw") {
    PcgCarCameraProfile profile;
    profile.lockToRearOfTarget = true;
    profile.rotationDamping    = 2.f;
    PcgCarCameraSetup setup;
    REQUIRE(setup.configure(profile).ok());
    Scene* scene = Scene::create();
    scene->beginBuild();
    scene->beginNode("root");
    scene->addNode("car");
    scene->end();
    REQUIRE(scene->mountBuildAs("pcg-car-rear-test"));
    SceneNodeRef     car("pcg-car-rear-test", "car");
    auto*            camera = Camera3D::createCamera();
    CameraController controller;
    REQUIRE(setup.apply(&controller, camera, &car, 0, 0).ok());
    REQUIRE(setup.update(&controller, 0, 0, 0, .5f, false, 90).ok());
    CHECK_GT(setup.getYaw(), 50.f);
    CHECK_LT(setup.getYaw(), 90.f);
}

// pcg_free_camera.cpp
using namespace eve::camera;
using eve::graphics::Camera3D;
TEST_CASE("camera.pcgFree.captureMoveSprintScrollAndRelease") {
    auto* c = Camera3D::createCamera();
    c->setEye(0, 0, 0);
    c->setTarget(0, 0, 1);
    PcgFreeCamera f;
    REQUIRE(f.configure(true, true, true, 5, 5, 50, true, 100, 15).ok());
    PcgFreeCameraInput i;
    i.rightPressed = true;
    i.forward      = 1;
    i.sprint       = true;
    i.scroll       = 500;
    REQUIRE(f.update(c, i, 1).ok());
    CHECK(f.getCaptured());
    CHECK_EQ(f.getSprintSpeed(), 150.f);
    CHECK_GT(c->getEyeZ(), 149.f);
    CHECK_EQ(f.getRoll(), 15.f);
    i               = {};
    i.rightReleased = true;
    REQUIRE(f.update(c, i, .1f).ok());
    CHECK(!f.getCaptured());
}
TEST_CASE("camera.pcgFree.focusAndInvalidInputAreSafe") {
    auto* c = Camera3D::createCamera();
    c->setEye(1, 2, 3);
    c->setTarget(1, 2, 4);
    PcgFreeCamera f;
    REQUIRE(f.capture(c).ok());
    PcgFreeCameraInput i;
    i.focused = false;
    REQUIRE(f.update(c, i, .1f).ok());
    CHECK(!f.getCaptured());
    float x  = c->getEyeX();
    i.mouseX = std::numeric_limits<float>::quiet_NaN();
    CHECK(!f.update(c, i, .1f).ok());
    CHECK_EQ(c->getEyeX(), x);
    CHECK(!f.configure(true, false, true, 5, 10, 5, true, 100, 0).ok());
}

// pcg_mesh_lod.cpp
using namespace eve::procgen;
using namespace eve::graphics;

namespace {
MeshBuild pcgLodGrid() {
    MeshBuild mesh;
    mesh.setActiveGroup("surface");
    for (int z = 0; z < 4; ++z)
        for (int x = 0; x < 4; ++x)
            mesh.addVertex(float(x), (x == 1 && z == 2) ? 0.35F : 0.F, float(z), 0.F, 1.F, 0.F, float(x) / 3.F,
                           float(z) / 3.F);
    for (int z = 0; z < 3; ++z)
        for (int x = 0; x < 3; ++x) {
            const std::uint32_t a = std::uint32_t(z * 4 + x), b = a + 1, c = a + 4, d = c + 1;
            mesh.addTriangle(a, b, d);
            mesh.addTriangle(a, d, c);
        }
    return mesh;
}

TEST_CASE("graphics.pcgMeshLod.speedTreeFadeUsesComplementaryOpaqueCoverage") {
    auto* gfx = Graphics::create();
    REQUIRE(gfx);
    gfx->initHeadless(96, 96);
    auto* canvas = gfx->newCanvas(96, 96);
    REQUIRE(canvas);
    const float    positions[] = {-.9f, -.9f, .5f, .9f, -.9f, .5f, .9f, .9f, .5f, -.9f, .9f, .5f};
    const float    normals[]   = {0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1};
    const float    uv[]        = {0, 0, 1, 0, 1, 1, 0, 1};
    const uint32_t indices[]   = {0, 2, 1, 0, 3, 2};
    auto*          mesh        = gfx->newMeshFromArrays(positions, normals, uv, 4, indices, 6);
    REQUIRE(mesh);
    Lighting3DPack lighting{};
    lighting.ambient = glm::vec4(1.f);
    gfx->setMesh3DViewProj(glm::mat4(1.f));
    gfx->setMesh3DView(glm::mat4(1.f));
    gfx->setMesh3DCameraPos({0.f, 0.f, 2.f});
    gfx->begin3DFrameToCanvas(canvas);
    gfx->setMesh3DLighting(lighting);
    gfx->setMesh3DSurface(SurfaceMode::Masked, BlendMode::Alpha, true, true, .5f);
    gfx->setMesh3DLodDither(.5f, false, true);
    gfx->drawMesh(mesh, glm::mat4(1.f), nullptr, Color(1.f, 0.f, 0.f, 1.f));
    gfx->setMesh3DLodDither(.5f, true, true);
    gfx->drawMesh(mesh, glm::mat4(1.f), nullptr, Color(0.f, 0.f, 1.f, 1.f));
    gfx->end3DFrameToCanvas();
    std::unique_ptr<eve::image::ImageData> pixels(canvas->newImageData());
    REQUIRE(pixels);
    const auto* data = static_cast<const unsigned char*>(pixels->getData());
    size_t      red = 0, blue = 0, holes = 0;
    for (int y = 12; y < 84; ++y)
        for (int x = 12; x < 84; ++x) {
            const size_t i = static_cast<size_t>(y * 96 + x) * 4;
            if (data[i] > data[i + 2] + 20)
                ++red;
            else if (data[i + 2] > data[i] + 20)
                ++blue;
            else if (data[i] < 10 && data[i + 2] < 10)
                ++holes;
        }
    CHECK(red > 500);
    CHECK(blue > 500);
    CHECK(holes < 100);
}

MeshBuild pcgTriangle(const std::string& material) {
    MeshBuild mesh;
    mesh.setActiveGroup(material);
    mesh.addVertex(0.F, 0.F, 0.F, 1.F, 0.F, 0.F, 0.F, 0.F);
    mesh.addVertex(1.F, 0.F, 0.F, 1.F, 0.F, 0.F, 1.F, 0.F);
    mesh.addVertex(0.F, 1.F, 0.F, 1.F, 0.F, 0.F, 0.F, 1.F);
    mesh.addTriangle(0, 1, 2);
    return mesh;
}
}  // namespace

TEST_CASE("procgen.pcgMeshCombiner.matchesStaticRendererOrderingAndStreams") {
    MeshBuild first = pcgTriangle("shared");
    MeshBuild second;
    second.addVertex(0.F, 0.F, 0.F, 1.F, 0.F, 0.F, 0.F, 0.F);
    second.addVertex(1.F, 0.F, 0.F, 1.F, 0.F, 0.F, 1.F, 0.F);
    second.addVertex(1.F, 1.F, 0.F, 1.F, 0.F, 0.F, 1.F, 1.F);
    second.addVertex(0.F, 1.F, 0.F, 1.F, 0.F, 0.F, 0.F, 1.F);
    second.setActiveGroup("unique");
    second.addTriangle(0, 1, 2);
    second.setActiveGroup("shared");
    second.addTriangle(0, 2, 3);
    REQUIRE(
        second
            .setVertexColors({0.F, 0.1F, 0.2F, 1.F, 0.3F, 0.4F, 0.5F, 1.F, 0.6F, 0.7F, 0.8F, 1.F, 0.9F, 1.F, 0.F, 1.F})
            .ok());
    PcgMeshTransform identity, transformed;
    REQUIRE(transformed.setElement(0, 0, 2.F).ok());
    REQUIRE(transformed.setElement(0, 3, 10.F).ok());
    PcgMeshCombinePlan plan;
    REQUIRE(plan.appendSource(first, identity, "fallback-a").ok());
    REQUIRE(plan.appendSource(second, transformed, "fallback-b").ok());
    MeshBuild output;
    auto      combined = combinePcgStaticMeshesInto(output, plan);
    REQUIRE(combined.ok());
    CHECK_EQ(combined.value(), 3);
    CHECK_EQ(output.getVertexCount(), 7);
    CHECK_EQ(output.getIndexCount(), 9);
    CHECK_EQ(output.getPositionX(3), 10.F);
    CHECK_EQ(output.getPositionX(4), 12.F);
    CHECK_EQ(output.getNormalX(3), 2.F);
    REQUIRE(output.hasVertexColors());
    CHECK_EQ(output.getColor(0, 0), 1.F);
    CHECK_EQ(output.getColor(3, 1), 0.1F);
    CHECK_EQ(output.getGroupCount(), 2);
    CHECK_EQ(output.getGroupName(0), std::string("shared"));
    CHECK_EQ(output.getGroupName(1), std::string("unique"));
    CHECK_EQ(output.getTriangleGroup(0), 0);
    CHECK_EQ(output.getTriangleGroup(1), 0);
    CHECK_EQ(output.getTriangleGroup(2), 1);
    CHECK_EQ(output.getIndex(3), 3);
    CHECK_EQ(output.getIndex(4), 5);
    CHECK_EQ(output.getIndex(5), 6);
}

TEST_CASE("procgen.pcgMeshCombiner.failuresAreAtomic") {
    PcgMeshTransform transform;
    CHECK(!transform.setElement(4, 0, 1.F).ok());
    CHECK(!transform.setElement(0, 0, std::numeric_limits<float>::infinity()).ok());
    PcgMeshCombinePlan empty;
    MeshBuild          output = pcgTriangle("preserved");
    CHECK(!combinePcgStaticMeshesInto(output, empty).ok());
    CHECK_EQ(output.getVertexCount(), 3);
    CHECK_EQ(output.getGroupName(0), std::string("preserved"));
    PcgMeshLodProfile profile;
    REQUIRE(profile.appendLevel(0.5F, 0.F, 1.F, false, false).ok());
    PcgMeshLodSet lods;
    CHECK(!buildPcgCombinedMeshLodsInto(lods, empty, profile).ok());
    CHECK_EQ(lods.getLevelCount(), 0);
}

TEST_CASE("procgen.pcgMeshLod.generatesEveryLevelFromOriginalMesh") {
    PcgMeshLodProfile profile;
    REQUIRE(profile.appendLevel(0.8F, 0.1F, 0.5F, false, false).ok());
    REQUIRE(profile.appendLevel(0.4F, 0.2F, 0.5F, false, false).ok());
    REQUIRE(profile.setFadePolicy(2, false, 0.75F).ok());
    REQUIRE(profile.setLevelRendererState(0, 4, 2, false, 2, false, 4, 2).ok());
    REQUIRE(profile.levelAt(0));
    CHECK(!profile.levelAt(0)->simplification.preserveBorderEdges);
    CHECK_EQ(profile.levelAt(0)->simplification.vertexLinkDistance, std::numeric_limits<double>::denorm_min());
    PcgMeshLodSet output;
    REQUIRE(buildPcgMeshLodsInto(output, pcgLodGrid(), profile).ok());
    CHECK_EQ(output.getLevelCount(), 2);
    REQUIRE(output.meshAt(0));
    REQUIRE(output.meshAt(1));
    REQUIRE(output.rendererStateAt(0));
    CHECK_EQ(output.rendererStateAt(0)->skinQuality, 4);
    CHECK_EQ(output.rendererStateAt(0)->shadowCastingMode, 2);
    CHECK(!output.rendererStateAt(0)->receiveShadows);
    CHECK_EQ(output.rendererStateAt(0)->lightProbeUsage, 4);
    CHECK_EQ(output.meshAt(0)->getIndexCount(), output.meshAt(1)->getIndexCount());
    CHECK_EQ(output.meshAt(0)->positions(), output.meshAt(1)->positions());
    CHECK_EQ(output.getFadeWidth(1), 0.2F);
    CHECK_EQ(output.getFadeMode(), 2);
    CHECK(!output.getAnimateCrossFading());
    CHECK_EQ(output.getCrossFadeAnimationDuration(), 0.75F);
    CHECK_EQ(output.selectLevel(0.9F), 0);
    CHECK_EQ(output.selectLevel(0.5F), 1);
    CHECK_EQ(output.selectLevel(0.1F), -1);
    CHECK(std::abs(output.getSwitchDistance(0, 2.F, 90.F) - 1.25F) < 0.0001F);
}

TEST_CASE("procgen.pcgMeshLod.profileAndBuildFailuresAreAtomic") {
    PcgMeshLodProfile profile;
    REQUIRE(profile.appendLevel(0.8F, 0.F, 1.F).ok());
    CHECK(!profile.appendLevel(0.9F, 0.F, 0.5F).ok());
    CHECK(!profile.appendLevel(0.4F, 0.F, 0.5F, false, true).ok());
    CHECK(!profile.setLevelRendererState(0, 3, 1, true, 1, true, 1, 1).ok());
    CHECK(!profile.setLevelRendererState(0, 4, 1, true, 1, true, 3, 1).ok());
    CHECK(!profile.setFadePolicy(3, false, .5F).ok());
    CHECK(!profile.setFadePolicy(0, true, .5F).ok());
    CHECK_EQ(profile.getLevelCount(), 1);
    PcgMeshLodSet output;
    REQUIRE(buildPcgMeshLodsInto(output, pcgLodGrid(), profile).ok());
    const int         before = output.meshAt(0)->getIndexCount();
    PcgMeshLodProfile emptyProfile;
    CHECK(!buildPcgMeshLodsInto(output, pcgLodGrid(), emptyProfile).ok());
    CHECK_EQ(output.getLevelCount(), 1);
    CHECK_EQ(output.meshAt(0)->getIndexCount(), before);
    MeshBuild empty;
    CHECK(!buildPcgMeshLodsInto(output, empty, profile).ok());
    CHECK_EQ(output.meshAt(0)->getIndexCount(), before);
}

TEST_CASE("procgen.pcgMeshLod.realVmBuildsAndCopiesLevel") {
    MeshBuild             source = pcgLodGrid();
    MeshBuild             combined;
    eve::procgen::Procgen procgen;
    ssq::VM               vm(1024);
    auto                  table = vm.addTable("eve");
    procgen.expose(table);
    vm.addFunc("pcgLodSource", [&]() { return &source; });
    vm.addFunc("pcgCombineOutput", [&]() { return &combined; });
    vm.run(vm.compileSource(R"(
        local profile=eve.PcgMeshLodProfile();
        assert(profile.appendLevel(0.8,0.1,1.0,false,false).ok);
        assert(profile.appendLevel(0.3,0.2,0.5,false,false).ok);
        assert(profile.setLevelRendererState(1,2,0,false,2,false,0,0).ok);
        assert(profile.setFadePolicy(2,false,0.75).ok);
        assert(profile.getFadeMode()==2 && !profile.getAnimateCrossFading());
        assert(profile.getCrossFadeAnimationDuration()==0.75);
        assert(profile.getLevelRendererState(1,0)==2);
        local output=eve.PcgMeshLodSet();
        assert(eve.buildPcgMeshLods(output,pcgLodSource(),profile).ok);
        assert(output.getLevelCount()==2 && output.selectLevel(0.5)==1);
        local mesh=output.copyLevelMesh(1);
        assert(mesh.getIndexCount()>0);
        local transform=eve.PcgMeshTransform();
        assert(transform.setElement(0,3,2.0).ok);
        local plan=eve.PcgMeshCombinePlan();
        assert(plan.appendSource(pcgLodSource(),transform,"surface").ok);
        local combined=pcgCombineOutput();
        local combineResult=eve.combinePcgStaticMeshes(combined,plan);
        if(!combineResult.ok)throw combineResult.status.summary;
        assert(combined.getIndexCount()==54);
        local combinedProfile=eve.PcgMeshLodProfile();
        assert(combinedProfile.appendLevel(0.5,0.0,1.0,true,false).ok);
        local combinedLods=eve.PcgMeshLodSet();
        assert(eve.buildPcgCombinedMeshLods(combinedLods,plan,combinedProfile).ok);
        assert(combinedLods.getLevelCount()==1);
    )"));
}

TEST_CASE("procgen.pcgMeshLodBackup.restoresExactEntityGenerationAtomically") {
    using eve::graphics::Mesh;
    using eve::graphics::Renderable3D;
    auto* target = Renderable3D::create();
    auto* other  = Renderable3D::create();
    REQUIRE(target != nullptr);
    REQUIRE(other != nullptr);
    Mesh* original  = reinterpret_cast<Mesh*>(uintptr_t(0x1000));
    Mesh* generated = reinterpret_cast<Mesh*>(uintptr_t(0x2000));
    target->setMesh(original);
    target->setVisible(true);
    PcgMeshLodBackup backup;
    REQUIRE(backup.capture(*target).ok());
    CHECK(backup.isCaptured());
    CHECK_EQ(backup.getEntityId(), target->getEntityId());
    CHECK_EQ(backup.getEntityGeneration(), target->getEntityGeneration());

    target->setMeshLod(0, generated);
    target->setVisible(false);
    REQUIRE(target->setMeshLodRendererState(0, 4, 2, false, 2, false, 4, 2).ok());
    const auto otherBefore = *other->meshRenderer();
    CHECK(!backup.restore(*other).ok());
    CHECK_EQ(other->meshRenderer()->mesh, otherBefore.mesh);
    CHECK(backup.isCaptured());

    REQUIRE(backup.restore(*target).ok());
    CHECK_EQ(target->getMesh(), original);
    CHECK_EQ(target->getMeshLodCount(), 0);
    CHECK(target->getVisible());
    CHECK(!backup.isCaptured());
    CHECK(!backup.restore(*target).ok());
    ecs::DestroyEntity(target);
    ecs::DestroyEntity(other);
}

// pcg_photo_mode_water.cpp
using namespace eve;
using namespace eve::graphics;
using namespace eve::ui;
TEST_CASE("graphics.pcgWaterPhotoMode.routesSevenFieldsIntoRealPlanners") {
    cap::detail::clearAllRaw();
    PcgWaterPhotoModeAuthority water;
    water.setAuthority(true);
    PcgPhotoModeValues before, after;
    after.setBool("m_pcgWaterReflectionEnabled", false).value();
    after.setFloat("m_pcgWaterReflectionDistance", 100).value();
    after.setInt("m_pcgWaterReflectionResolution", 3).value();
    after.setFloat("m_pcgReflectionsLODBias", 2).value();
    after.setColor("m_pcgUnderwaterFogColor", 0.1f, 0.2f, 0.3f, 1).value();
    after.setFloat("m_pcgUnderwaterFogDensity", 0.08f).value();
    after.setFloat("m_pcgUnderwaterFogDistance", 70).value();
    PcgPhotoModeApplyPlan plan;
    REQUIRE(plan.compile(before, after).ok());
    REQUIRE_EQ(plan.getCommandCount(), 7u);
    REQUIRE(plan.executeRegistered().ok());
    WaterPlanarReflectionSettings reflection;
    REQUIRE(water.applyToPlanarSettings(reflection).ok());
    WaterPlanarReflectionInput input;
    input.cameraPosition = {0, 5, 0};
    input.cameraForward  = {0, -0.2f, -1};
    auto capture         = buildWaterPlanarReflectionPlan(reflection, input);
    REQUIRE(capture.ok());
    CHECK(!capture.value().shouldRender);
    CHECK_EQ(capture.value().textureWidth, 128);
    CHECK_EQ(capture.value().layerCullDistances[0], 1200.0f);
    WaterUnderwaterSettings settings;
    settings.supportPostFx = false;
    REQUIRE(water.applyToUnderwaterSettings(settings).ok());
    WaterUnderwaterState    state;
    WaterUnderwaterOutput   output;
    UnderwaterColorGradient emptyColor;
    UnderwaterScalarCurve   emptyCurve;
    WaterUnderwaterInput    frame;
    frame.cameraY  = -5;
    frame.seaLevel = 0;
    REQUIRE(
        advanceWaterUnderwaterEffects(state, output, settings, frame, emptyColor, emptyColor, emptyCurve, emptyColor)
            .ok());
    CHECK_EQ(output.fogColor.x, 0.1f);
    CHECK_EQ(output.fogColor.y, 0.2f);
    CHECK_EQ(output.fogColor.z, 0.3f);
    CHECK_EQ(output.fogDensity, 0.08f);
    CHECK_EQ(output.fogEnd, 70.0f);
}
TEST_CASE("graphics.pcgWaterPhotoMode.rollsBackBeforeMissingAudioUnderwaterProvider") {
    cap::detail::clearAllRaw();
    PcgWaterPhotoModeAuthority water;
    water.setAuthority(true);
    PcgPhotoModeValues before, after;
    after.setColor("m_pcgUnderwaterFogColor", 0.4f, 0.5f, 0.6f, 1).value();
    after.setFloat("m_pcgUnderwaterVolume", 0.2f).value();
    PcgPhotoModeApplyPlan plan;
    REQUIRE(plan.compile(before, after).ok());
    CHECK(!plan.executeRegistered().ok());
    CHECK(plan.getRollbackComplete());
    CHECK_EQ(water.getFogRed(), 0.0f);
    CHECK_EQ(water.getFogGreen(), 0.0f);
    CHECK_EQ(water.getFogBlue(), 0.0f);
}
TEST_CASE("graphics.pcgWaterPhotoMode.authorityLeaseRevokesOnDestruction") {
    cap::detail::clearAllRaw();
    PcgPhotoModeValues before, after;
    after.setFloat("m_pcgWaterReflectionDistance", 5).value();
    PcgPhotoModeApplyPlan plan;
    REQUIRE(plan.compile(before, after).ok());
    {
        PcgWaterPhotoModeAuthority water;
        water.setAuthority(true);
        REQUIRE(plan.executeRegistered().ok());
    }
    CHECK(!plan.executeRegistered().ok());
}
