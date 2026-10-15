#include <array>
#include <limits>
#include <memory>
#include <vector>
#include "graphics/Canvas.h"
#include "graphics/Graphics.h"
#include "graphics/Light.h"
#include "graphics/Material.h"
#include "graphics/Mesh.h"
#include "graphics/PbrSurface.h"
#include "image/ImageData.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
using namespace eve::graphics;


TEST_CASE("graphics.shaderCustomMeshTexturesValidateSlotsAndPreserveBorrows") {
    Shader   shader;
    Texture* marker = reinterpret_cast<Texture*>(uintptr_t(16));
    REQUIRE(shader.setMeshTexture(3, marker).ok());
    REQUIRE_EQ(shader.meshTexture(3), marker);
    REQUIRE(!shader.setMeshTexture(4, nullptr).ok());
    REQUIRE_EQ(shader.meshTexture(3), marker);
    REQUIRE_EQ(shader.meshTexture(4), nullptr);
}
