// clang-format off
/* eve-test-policy:
{"schema":"evengine.test-policy","version":1,"default":"render-window","module":"graphics","modules":["graphics","window"]}
*/
// clang-format on
#include "common/config.h"
#if !defined(EVENGINE_WEBGPU)
#include <array>
#include "Fixtures.h"
#include "ShaderResourceSharingSpv.h"
#include "graphics/Graphics.h"
#include "graphics/ShaderResources.h"
#include "zeroerr/unittest.h"

TEST_CASE("graphics.resources.customProgramsDoNotReserveUnusedMaterialSamplers") {
    GfxFixture fixture;
    auto*      gfx    = fixture.gfx;
    auto*      shader = gfx->newMeshShaderFromSpv(sharing_vert, sharing_frag);
    REQUIRE(shader != nullptr);
    const std::array<std::byte, 4>                  pixel{std::byte{255}, std::byte{0}, std::byte{0}, std::byte{255}};
    std::array<eve::graphics::ShaderImageInput, 13> images;
    for (std::size_t i = 0; i < images.size(); ++i) {
        images[i].binding = static_cast<std::uint32_t>(i);
        images[i].width = images[i].height = 1;
        images[i].bytes                    = pixel;
    }
    auto replaced = gfx->replaceMeshShaderResources(*shader, sharing_vert, sharing_frag, {images, {}, {}});
#ifdef EVE_TEST_RESOURCE_VALIDATION
    // Core Vulkan guarantees at least 16 samplers. Thirteen custom images plus
    // albedo and scene color fit; adding all legacy material slots does not.
    REQUIRE(replaced.ok());
#else
    REQUIRE(!replaced.ok());
    REQUIRE(replaced.status().describe().find("SPIR-V validation provider is unavailable") != std::string::npos);
#endif
    REQUIRE(gfx->releaseShader(shader));
}
#endif
