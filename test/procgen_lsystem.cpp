#include "procgen/PointSet.h"
#include "procgen/Procgen.h"
#include "procgen/algorithms/LSystem.h"

#include "graphics/Graphics.h"
#include "graphics/Mesh.h"
#include "graphics/RenderSystem.h"
#include "graphics/RenderSystem3D.h"
#include "graphics/Texture.h"
#include "image/Image.h"
#include "window/Window.h"
#include "RenderImageAudit.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <memory>
#include <set>

using namespace eve::procgen;
using namespace eve::graphics;

TEST_CASE("procgen.lsystem.renderDump") {
    const char *outputPath = std::getenv("EVENGINE_LSYSTEM_RENDER_PNG");
    if (!outputPath || !outputPath[0]) return;

    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings ws;
    ws.width = 900;
    ws.height = 700;
    ws.centered = true;
    REQUIRE(win->setWindowSettings(ws));

    Params params;
    params.setSeed(20260826);
    const char *styleOverride = std::getenv("EVENGINE_LSYSTEM_STYLE");
    params.setString("style", styleOverride ? styleOverride : "tree");
    params.setInt("iterations", 5);
    params.setString("leafMode", "cards");
    params.setFloat("tropism", 0.05f);
    params.setFloat("leafSize", 0.5f);

    Procgen generator;
    auto paramsResult = Procgen::newParamsHandle();
    REQUIRE(paramsResult.ok());
    auto paramsHandle = std::move(paramsResult).takeValue();
    auto paramsView = Procgen::resolve(paramsHandle);
    REQUIRE(paramsView.isBound());
    *paramsView = params;
    auto lsMeshView = generator.generateMeshBorrowed("mesh.lsystem", paramsHandle, gfx);
    REQUIRE(lsMeshView.isBound());
    Mesh *lsMesh = lsMeshView.get();

    // 4px bark/foliage atlas; UVs are partitioned by the mesh recipe (left = bark).
    const uint8_t atlasPixels[] = {
        111, 70, 42, 255, 128, 82, 47, 255,
        64, 119, 57, 255, 82, 145, 67, 255,
    };
    Texture *atlas = gfx->newTexture(4, 1, atlasPixels);
    REQUIRE(atlas != nullptr);

    auto *plant = Renderable3D::create();
    plant->setMesh(lsMesh);
    plant->setTexture(atlas);
    plant->setTint(1.f, 1.f, 1.f, 1.f);
    plant->setRoughness(0.88f);

    auto *camera = Camera3D::createCamera();
    camera->setEye(7.5f, 5.2f, 9.5f);
    camera->setTarget(0.f, 3.2f, 0.f);
    camera->setUp(0.f, 1.f, 0.f);
    camera->setFov(36.f);
    camera->setAmbient(0.30f, 0.34f, 0.28f);
    camera->setActive(true);

    gfx->setScreenReadbackEnabled(true);
    gfx->setBackgroundColor(Color(0.075f, 0.105f, 0.095f, 1.f));
    RenderSystem3D::setDirectionalLight(-0.55f, -1.f, -0.35f, 1.45f, 1.32f, 1.08f);

    for (int frame = 0; frame < 4; ++frame) {
        RenderSystem3D::render(*gfx);
        RenderSystem::render(*gfx);
    }
    std::unique_ptr<eve::image::ImageData> image(gfx->newImageData());
    REQUIRE(image.get() != nullptr);
    REQUIRE(saveImagePng(*image, outputPath));
    std::printf("lsystem render saved: %s\n", outputPath);
    REQUIRE(Procgen::release(paramsHandle).ok());
    win->close();
}

TEST_CASE("procgen.lsystem.deterministicExpansion") {
    LSystem ls;
    ls.setAxiom("F");
    ls.addRule('F', "F[+F]F[-F]F");
    ls.setIterations(3);
    ls.setSeed(42);

    const std::string a = ls.derive();
    const std::string b = ls.derive();
    CHECK(!a.empty());
    CHECK_EQ(a, b);

    // More iterations must grow the word monotonically.
    ls.setIterations(4);
    const std::string c = ls.derive();
    CHECK(c.size() > a.size());
}

TEST_CASE("procgen.lsystem.stochasticRulesUseSeed") {
    LSystem ls;
    ls.setAxiom("A");
    // Branching grammar so different seeds produce a rich, distinct outcome space.
    ls.addRules('A', {"F[+A]A", "F[-A]A", "FA"}, {1.f, 1.f, 1.f});
    ls.setIterations(4);

    // Determinism for a fixed seed.
    ls.setSeed(7);
    const std::string a = ls.derive();
    ls.setSeed(7);
    const std::string b = ls.derive();
    CHECK_EQ(a, b);
    CHECK(!a.empty());

    // Across several seeds the stochastic grammar should not all collapse to one.
    std::set<std::string> seen;
    for (int s = 1; s <= 8; ++s) {
        ls.setSeed(s);
        seen.insert(ls.derive());
    }
    CHECK(seen.size() >= 2);
}

TEST_CASE("procgen.lsystem.turtleProducesSegments") {
    LSystem ls;
    ls.setAxiom("F[+F]F[-F]F");
    ls.addRule('F', "F");
    ls.setIterations(1);
    ls.setAngle(25.f);
    ls.setStep(1.f);
    ls.setSeed(1);

    LSystemResult result;
    ls.generate(result);
    CHECK(!result.segments.empty());
    CHECK(!result.derivation.empty());

    // Bracketed depth increases then returns: trunk segment is at depth 0.
    bool sawDepthZero = false;
    bool sawDeep      = false;
    for (const auto& seg : result.segments) {
        if (seg.depth == 0) sawDepthZero = true;
        if (seg.depth > 0) sawDeep = true;
        if (seg.leaf) CHECK(seg.leafSize > 0.f);
    }
    CHECK(sawDepthZero);
    CHECK(sawDeep);
}

TEST_CASE("procgen.lsystem.meshRecipeIsDeterministic") {
    Procgen proc;

    Params p1;
    p1.setSeed(123);
    p1.setString("style", "tree");
    p1.setInt("iterations", 4);
    p1.setString("leafMode", "cards");

    Params p2 = p1;

    auto p1Result = Procgen::newParamsHandle();
    auto p2Result = Procgen::newParamsHandle();
    REQUIRE(p1Result.ok());
    REQUIRE(p2Result.ok());
    auto p1Handle = std::move(p1Result).takeValue();
    auto p2Handle = std::move(p2Result).takeValue();
    *Procgen::resolve(p1Handle) = p1;
    *Procgen::resolve(p2Handle) = p2;
    auto m1Result = proc.buildMeshHandle("mesh.lsystem", p1Handle);
    auto m2Result = proc.buildMeshHandle("mesh.lsystem", p2Handle);
    REQUIRE(m1Result.ok());
    REQUIRE(m2Result.ok());
    auto m1Handle = std::move(m1Result).takeValue();
    auto m2Handle = std::move(m2Result).takeValue();
    auto m1 = proc.resolveMeshBuild(m1Handle);
    auto m2 = proc.resolveMeshBuild(m2Handle);
    REQUIRE(m1.isBound());
    REQUIRE(m2.isBound());
    CHECK(m1->getVertexCount() > 0);
    CHECK_EQ(m1->getVertexCount(), m2->getVertexCount());
    CHECK_EQ(m1->getIndexCount(), m2->getIndexCount());
    CHECK_EQ(m1->getMeta("recipe", ""), std::string("mesh.lsystem"));
    REQUIRE(proc.releaseMeshBuild(m2Handle).ok());
    REQUIRE(proc.releaseMeshBuild(m1Handle).ok());
    REQUIRE(Procgen::release(p2Handle).ok());
    REQUIRE(Procgen::release(p1Handle).ok());
}

TEST_CASE("procgen.lsystem.meshRecipeReportsErrors") {
    Procgen proc;
    Params  params;
    params.setSeed(1);
    params.setString("style", "bogus");
    auto paramsResult = Procgen::newParamsHandle();
    REQUIRE(paramsResult.ok());
    auto paramsHandle = std::move(paramsResult).takeValue();
    *Procgen::resolve(paramsHandle) = params;
    auto meshResult = proc.buildMeshHandle("mesh.lsystem", paramsHandle);
    REQUIRE(!meshResult.ok());
    CHECK(meshResult.status().describe().find("unknown style") != std::string::npos);
    REQUIRE(Procgen::release(paramsHandle).ok());
}

TEST_CASE("procgen.lsystem.scriptFacade") {
    Procgen proc;
    auto result = proc.newLSystemHandle();
    REQUIRE(result.ok());
    auto handle = std::move(result).takeValue();
    auto ls = proc.resolveLSystem(handle);
    REQUIRE(ls.isBound());
    ls->setAxiom("F");
    ls->addRule('F', "F[+F]F[-F]F");
    ls->setIterations(2);
    ls->setSeed(5);

    PointSet points;
    ls->toPointSet(points);
    CHECK(points.getCount() > 0);
    REQUIRE(proc.release(handle).ok());
}
