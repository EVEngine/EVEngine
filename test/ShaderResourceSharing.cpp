#include "Fixtures.h"
#include "graphics/Shader.h"
#include "graphics/ShaderResources.h"
#include "zeroerr/unittest.h"
#ifndef EVENGINE_WEBGPU
#include <array>
#include "ShaderResourceSharingSpv.h"
#include "graphics/vulkan/ShaderResourceReload.h"
TEST_CASE("graphics.shader shared images retain ownership and replacement isolation") {
    GfxFixture fixture;
    auto*      gfx    = fixture.gfx;
    auto*      first  = gfx->newMeshShaderFromSpv(sharing_vert, sharing_frag);
    auto*      second = gfx->newMeshShaderFromSpv(sharing_vert, sharing_frag);
    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);
    auto                            owner = std::make_shared<const std::array<std::byte, 4>>();
    eve::graphics::ShaderImageInput image;
    image.width        = 1;
    image.height       = 1;
    image.bytes        = *owner;
    image.contentOwner = owner;
    auto replace       = [&](eve::graphics::Shader* shader) {
        return gfx->replaceMeshShaderResources(*shader, sharing_vert, sharing_frag, {{&image, 1}, {}, {}});
    };
    auto shared = [&] {
        return eve::graphics::vulkan::isMeshResourceImageShared(
            *static_cast<eve::graphics::vulkan::GpuShader*>(first->gpuHandle), 0,
            *static_cast<eve::graphics::vulkan::GpuShader*>(second->gpuHandle), 0);
    };
    REQUIRE(replace(first).ok());
    REQUIRE(replace(second).ok());
    REQUIRE(shared());
    image.sampler.repeatU = true;
    REQUIRE(replace(second).ok());
    REQUIRE(!shared());
    image.sampler.repeatU = false;
    REQUIRE(replace(second).ok());
    REQUIRE(shared());
    image.bytes = {};
    REQUIRE(!replace(second).ok());
    REQUIRE(shared());
    auto other         = std::make_shared<const std::array<std::byte, 4>>();
    image.bytes        = *other;
    image.contentOwner = other;
    REQUIRE(replace(second).ok());
    REQUIRE(!shared());
    image.bytes        = *owner;
    image.contentOwner = owner;
    REQUIRE(replace(second).ok());
    REQUIRE(shared());
    REQUIRE(gfx->releaseShader(first));
    // The second shader still owns the shared storage after the first is released.
    REQUIRE(replace(second).ok());
    REQUIRE(gfx->releaseShader(second));
}
#endif
