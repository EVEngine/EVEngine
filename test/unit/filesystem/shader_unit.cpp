#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <SDL2/SDL.h>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <vector>

#include "common/Exception.h"
#include "filesystem/Filesystem.h"
#include "graphics/AmbientOcclusion.h"
#include "graphics/AntiAliasing.h"
#include "graphics/Canvas.h"
#include "graphics/DrawItem2D.h"
#include "graphics/Font.h"
#include "graphics/GBuffer.h"
#include "graphics/GlobalIllumination.h"
#include "graphics/Graphics.h"
#include "graphics/Grass.h"
#include "graphics/Light.h"
#include "graphics/Material.h"
#include "graphics/Mesh.h"
#include "graphics/Outline.h"
#include "graphics/Quad.h"
#include "graphics/RenderControl.h"
#include "graphics/RenderSystem.h"
#include "graphics/ScreenSpaceReflection.h"
#include "graphics/Shader.h"
#include "graphics/Texture.h"
#include "graphics/Volumetric.h"
#include "graphics/Water.h"
#include "graphics/Waterfall.h"
#include "graphics/shaders/custom2d_frag_spv.inc"
#include "graphics/shaders/textured_vert_spv.inc"
#include "window/Window.h"
// Color lives in eve::graphics (see graphics/Canvas.h); keep the unqualified form.
using eve::graphics::Color;

using namespace eve::graphics;

TEST_CASE("graphics.Shader.declareAndSendUniforms") {
    Shader sh;
    CHECK_EQ(sh.declareFloat("time"), 0);
    CHECK_EQ(sh.declareVec4("tint"), 1);
    CHECK_EQ(sh.usedFloats(), 5);
    CHECK(sh.hasUniform("time"));
    CHECK(sh.hasUniform("tint"));

    sh.sendFloat("time", 1.5f);
    sh.sendVec4("tint", 0.2f, 0.4f, 0.6f, 1.f);

    float t = 0.f;
    REQUIRE_EQ(sh.getFromVar("time", &t, sizeof(t)), int(sizeof(t)));
    CHECK(std::abs(t - 1.5f) < 1e-5f);

    float tint[4] = {};
    REQUIRE_EQ(sh.getFromVar("tint", tint, sizeof(tint)), int(sizeof(tint)));
    CHECK(std::abs(tint[0] - 0.2f) < 1e-5f);
    CHECK(std::abs(tint[3] - 1.f) < 1e-5f);

    bool threw = false;
    try {
        sh.sendFloat("missing", 1.f);
    } catch (const eve::Exception &) {
        threw = true;
    }
    CHECK(threw);

    threw = false;
    try {
        sh.declareFloat("time");  // same size OK
        sh.declareVec2("time");   // different size → error
    } catch (const eve::Exception &) {
        threw = true;
    }
    CHECK(threw);
}
