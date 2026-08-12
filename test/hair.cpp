#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "graphics/Graphics.h"
#include "graphics/HairShader.h"
#include "graphics/RenderSystem3D.h"
#include "graphics/Shader.h"
#include "graphics/shaders/mesh3d_hair_frag_spv.inc"
#include "graphics/shaders/mesh3d_hair_vert_spv.inc"

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

TEST_CASE("graphics.Renderable3D.hairFlag") {
    auto *r = Renderable3D::create();
    REQUIRE(r != nullptr);
    CHECK(!r->getHair());
    r->setHair(true);
    CHECK(r->getHair());
    r->setHair(false);
    CHECK(!r->getHair());
}
