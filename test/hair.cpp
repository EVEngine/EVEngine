#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
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
#include "graphics/shaders/mesh3d_hair_frag_spv.inc"
#include "graphics/shaders/mesh3d_hair_vert_spv.inc"
#include "window/Window.h"


using eve::graphics::Graphics;
using eve::graphics::Renderable3D;
using eve::graphics::Shader;

TEST_CASE("graphics.HairShader.bindDefaults") {
    Shader sh;
    sh.setKind(Shader::Kind::eMesh3D);
    eve::graphics::hair::bindDefaults(&sh);
    CHECK(sh.hasUniform("specExp"));
    CHECK(sh.hasUniform("strandDir"));
    CHECK_EQ(sh.getUniformIndex("specExp"), 0);
    CHECK_EQ(sh.getUniformIndex("strandDir"), 6);
    CHECK_EQ(sh.usedFloats(), 9);
}

TEST_CASE("graphics.HairShader.paramNames") {
    CHECK_EQ(eve::graphics::hair::paramCount(), 9);
    CHECK_EQ(eve::graphics::hair::paramName(0), std::string("specExp"));
    CHECK_EQ(eve::graphics::hair::paramName(8), std::string("strandDirZ"));
}

TEST_CASE("graphics.HairShader.spvMagic") {
    CHECK(mesh3d_hair_vert_spv_count > 0);
    CHECK(mesh3d_hair_frag_spv_count > 0);
    CHECK_EQ(mesh3d_hair_vert_spv[0], 0x07230203u);
    CHECK_EQ(mesh3d_hair_frag_spv[0], 0x07230203u);
}

TEST_CASE("graphics.HairShader.createGpuPipeline") {
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 96, 64);
    Shader *shader = gfx->newHairShader();
    REQUIRE(shader != nullptr);
    REQUIRE(shader->gpuHandle != nullptr);
    CHECK(shader->hasUniform("specExp"));
    CHECK(shader->hasUniform("strandDir"));
    shader->sendFloat("specStrength", 0.7f);
    shader->sendVec3("strandDir", 0.f, 1.f, 0.f);
    win->close();
}

TEST_CASE("graphics.Renderable3D.hairFlag") {
    auto *r = Renderable3D::create();
    REQUIRE(r != nullptr);
    CHECK(!r->getHair());
    r->setHair(true);
    CHECK(r->getHair());
    r->setHair(false);
    CHECK(!r->getHair());
}
