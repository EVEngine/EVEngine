#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "graphics/GBuffer.h"
#include "graphics/Material.h"
#include "graphics/Mesh.h"
#include "graphics/RenderControl.h"
#include "graphics/RenderSystem3D.h"

#include <cmath>

using namespace eve::graphics;

TEST_CASE("material.shadingModelAndParams") {
    Material mat;
    CHECK(mat.getShadingModel() == "pbr");
    mat.setShadingModel("unlit");
    CHECK(mat.getShadingModel() == "unlit");
    CHECK(mat.getReceiveLight() == false);

    mat.setShadingModel("hair");
    CHECK(mat.getShadingModel() == "hair");
    CHECK(mat.isTransparentHair() == true);

    mat.setShadingModel("nope");
    CHECK(mat.getShadingModel() == "pbr");

    mat.setMetallic(1.5f);
    CHECK(mat.getMetallic() == 1.f);
    mat.setRoughness(0.01f);
    CHECK(mat.getRoughness() >= 0.04f);

    mat.setFloat("outlineWidth", 1.25f);
    CHECK(mat.hasParam("outlineWidth"));
    CHECK(std::fabs(mat.getFloat("outlineWidth") - 1.25f) < 1e-5f);
}

TEST_CASE("material.surfaceModesAndTransparencyPolicies") {
    Material mat;
    CHECK(mat.getSurfaceMode() == "opaque");
    mat.setSurfaceMode("masked");
    mat.setAlphaCutoff(0.37f);
    mat.setDoubleSided(true);
    CHECK(mat.getSurfaceMode() == "masked");
    CHECK(std::fabs(mat.getAlphaCutoff() - 0.37f) < 1e-5f);
    CHECK(mat.getDoubleSided());
    mat.setSurfaceMode("transparent");
    mat.setBlendMode("premultiplied");
    mat.setSortPriority(12);
    CHECK(mat.getBlendMode() == "premultiplied");
    CHECK(mat.getSortPriority() == 12);
    mat.setAlphaTechnique("dither");
    CHECK(mat.getAlphaTechnique() == "dither");
}

TEST_CASE("renderControl.compileFeaturesToPasses") {
    RenderControl rc;
    CHECK(rc.supports("gbuffer"));
    CHECK(rc.supports("ao"));
    CHECK(rc.supports("gi"));
    CHECK(rc.supports("aa"));
    CHECK(rc.supports("msaa"));
    CHECK(rc.supports("shadow"));
    CHECK(!rc.supports("deferred"));

    rc.compile();
    CHECK(rc.isCompiled());
    CHECK(rc.hasPass("shadow"));
    CHECK(rc.hasPass("forward"));
    CHECK(rc.hasPass("hair"));
    CHECK(rc.hasPass("gbuffer"));
    CHECK(rc.isEnabled("ao"));
    CHECK(rc.isEnabled("gi"));
    CHECK(rc.isEnabled("aa"));
    CHECK(rc.isEnabled("msaa"));

    rc.disable("msaa");
    CHECK(!rc.isEnabled("msaa"));
    rc.enable("msaa");
    CHECK(rc.isEnabled("msaa"));

    rc.disable("ao");
    rc.disable("gi");
    rc.disable("gbuffer");
    CHECK(rc.isDirty());
    rc.compile();
    CHECK(!rc.hasPass("gbuffer"));
    CHECK(!rc.isEnabled("ao"));
    CHECK(!rc.isEnabled("gi"));

    rc.enable("gbuffer");
    CHECK(rc.isDirty());
    rc.compile();
    CHECK(rc.hasPass("gbuffer"));
    CHECK(rc.getPassCount() >= 3);

    // Pass order: shadow → gbuffer → forward → hair
    int shadowIdx = -1, gbIdx = -1, fwdIdx = -1;
    for (int i = 0; i < rc.getPassCount(); ++i) {
        const std::string n = rc.getPassName(i);
        if (n == "shadow") shadowIdx = i;
        if (n == "gbuffer") gbIdx = i;
        if (n == "forward") fwdIdx = i;
    }
    CHECK(shadowIdx >= 0);
    CHECK(gbIdx > shadowIdx);
    CHECK(fwdIdx > gbIdx);

    rc.enable("gbufferAlbedo");
    CHECK(rc.isEnabled("gbuffer"));
    rc.disable("gbuffer");
    CHECK(!rc.isEnabled("gbufferAlbedo"));
}

TEST_CASE("renderControl.atmospherePassDependencies") {
    RenderControl rc;
    CHECK(rc.supports("atmosphere"));
    CHECK(rc.supports("volumetricFog"));
    rc.enable("fogTemporal");
    rc.compile();
    CHECK(rc.isEnabled("gbuffer"));
    CHECK(rc.isEnabled("atmosphere"));
    CHECK(rc.isEnabled("volumetricFog"));
    CHECK(rc.hasPass("atmosphere"));
    CHECK(rc.hasPass("fogMedia"));
    CHECK(rc.hasPass("fogLighting"));
    CHECK(rc.hasPass("fogTemporal"));
    CHECK(rc.hasPass("fogIntegrate"));
    CHECK(rc.hasPass("fogComposite"));
    rc.disable("volumetricFog");
    CHECK(!rc.isEnabled("fogTemporal"));
}

TEST_CASE("renderControl.reflectionChainDependenciesAndQuality") {
    RenderControl rc;
    CHECK(rc.getPostProcessQuality() == "high");
    CHECK(rc.getReflectionQuality() == "high");

    rc.setPostProcessQuality("ultra");
    CHECK(rc.getPostProcessQuality() == "ultra");
    CHECK(rc.getReflectionQuality() == "ultra");
    rc.setReflectionQuality("invalid");
    CHECK(rc.getPostProcessQuality() == "high");

    rc.enable("reflectionChain");
    CHECK(rc.isEnabled("reflectionChain"));
    CHECK(rc.isEnabled("gbuffer"));
    CHECK(rc.isEnabled("aa"));
    CHECK(rc.isEnabled("taa"));
    CHECK(rc.isEnabled("rtgi"));
    CHECK(rc.isEnabled("ssr"));
    CHECK(!rc.isEnabled("msaa"));

    rc.disable("ssr");
    CHECK(!rc.isEnabled("ssr"));
    CHECK(!rc.isEnabled("reflectionChain"));

    rc.enable("reflectionChain");
    rc.disable("reflectionChain");
    CHECK(!rc.isEnabled("reflectionChain"));
    CHECK(!rc.isEnabled("taa"));
    CHECK(!rc.isEnabled("rtgi"));
    CHECK(!rc.isEnabled("ssr"));
    CHECK(rc.isEnabled("aa"));
}

TEST_CASE("camera3d.boxProjectedEnvironmentProbeState") {
    Camera3D *camera = Camera3D::createCamera();
    REQUIRE(camera != nullptr);
    CHECK(!camera->hasEnvProbe());
    camera->setEnvProbe(1.f, 2.f, 3.f, 4.f, -5.f, 6.f);
    CHECK(!camera->hasEnvProbe());
    CHECK(std::fabs(camera->getEnvProbeCenterX() - 1.f) < 1e-5f);
    CHECK(std::fabs(camera->getEnvProbeCenterY() - 2.f) < 1e-5f);
    CHECK(std::fabs(camera->getEnvProbeCenterZ() - 3.f) < 1e-5f);
    CHECK(camera->getEnvProbeExtentY() == 0.f);
    camera->setEnvProbe(1.f, 2.f, 3.f, 4.f, 5.f, 6.f);
    CHECK(camera->hasEnvProbe());
    CHECK(std::fabs(camera->getEnvProbeExtentX() - 4.f) < 1e-5f);
    CHECK(std::fabs(camera->getEnvProbeExtentY() - 5.f) < 1e-5f);
    CHECK(std::fabs(camera->getEnvProbeExtentZ() - 6.f) < 1e-5f);
    camera->clearEnvProbe();
    CHECK(!camera->hasEnvProbe());
}

TEST_CASE("renderable3d.materialAndParts") {
    auto *ent = Renderable3D::create();
    REQUIRE(ent != nullptr);
    Material body;
    body.setShadingModel("pbr");
    body.setMetallic(0.8f);
    Material hair;
    hair.setShadingModel("hair");

    ent->setMaterial(&body);
    CHECK(ent->getMaterial() == &body);
    CHECK(ent->meshRenderer()->effectiveHair() == false);

    ent->setPart(0, "body", nullptr, &body);
    // null mesh does not grow partCount
    CHECK(ent->getPartCount() == 0);

    // Use a fake non-null mesh pointer only for slot bookkeeping (not drawn here).
    Mesh dummyMesh;
    ent->setPart(0, "body", &dummyMesh, &body);
    ent->setPart(1, "hair", &dummyMesh, &hair);
    CHECK(ent->getPartCount() == 2);
    CHECK(ent->getPartName(0) == "body");
    CHECK(ent->getPartName(1) == "hair");
    CHECK(ent->getPartMaterial(1) == &hair);
    CHECK(ent->getPartMaterial(1)->isTransparentHair());

    ent->clearParts();
    CHECK(ent->getPartCount() == 0);
}

TEST_CASE("gbuffer.bufferQueries") {
    GBuffer gb;
    CHECK(!gb.isValid());
    CHECK(!gb.hasBuffer("depth"));

    Texture depth, normal, albedo;
    gb.setTargets(128, 96, &depth, &normal, &albedo);
    CHECK(gb.isValid());
    CHECK(gb.getWidth() == 128);
    CHECK(gb.hasBuffer("depth"));
    CHECK(gb.hasBuffer("normal"));
    CHECK(gb.hasBuffer("albedo"));
    CHECK(gb.getBuffer("depth") == &depth);
    CHECK(!gb.hasBuffer("hwDepth"));
    CHECK(gb.getBuffer("missing") == nullptr);

    Texture hw;
    gb.setTargets(128, 96, &depth, &normal, &albedo, &hw);
    CHECK(gb.hasBuffer("hwDepth"));
    CHECK(gb.getHwDepthTexture() == &hw);
    CHECK(gb.getBuffer("hwDepth") == &hw);

    gb.clear();
    CHECK(!gb.isValid());
}
