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

TEST_CASE("renderControl.compileFeaturesToPasses") {
    RenderControl rc;
    CHECK(rc.supports("gbuffer"));
    CHECK(rc.supports("shadow"));
    CHECK(!rc.supports("deferred"));

    rc.compile();
    CHECK(rc.isCompiled());
    CHECK(rc.hasPass("shadow"));
    CHECK(rc.hasPass("forward"));
    CHECK(rc.hasPass("hair"));
    CHECK(!rc.hasPass("gbuffer"));

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
    CHECK(gb.getBuffer("missing") == nullptr);

    gb.clear();
    CHECK(!gb.isValid());
}
