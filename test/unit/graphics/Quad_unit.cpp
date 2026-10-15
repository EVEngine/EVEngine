#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <SDL2/SDL.h>
#include <cmath>
#include <cstring>
#include <vector>

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
#include "image/ImageData.h"
#include "window/Window.h"

using namespace eve::graphics;

TEST_CASE("Quad.getUVMapsPixelRect") {
    Quad q(16, 8, 32, 16);
    float u0 = 0, v0 = 0, u1 = 0, v1 = 0;
    q.getUV(64, 64, u0, v0, u1, v1);
    CHECK(std::abs(u0 - 0.25f) < 1e-5f);
    CHECK(std::abs(v0 - 0.125f) < 1e-5f);
    CHECK(std::abs(u1 - 0.75f) < 1e-5f);
    CHECK(std::abs(v1 - 0.375f) < 1e-5f);

    q.setViewport(0, 0, 64, 64);
    q.getUV(64, 64, u0, v0, u1, v1);
    CHECK(std::abs(u0) < 1e-5f);
    CHECK(std::abs(v0) < 1e-5f);
    CHECK(std::abs(u1 - 1.f) < 1e-5f);
    CHECK(std::abs(v1 - 1.f) < 1e-5f);
}
