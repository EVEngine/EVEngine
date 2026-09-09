#include "asset/ShaderAsset.h"
#include "asset/graphics/ShaderAssetValidation.h"
#include "graphics/shaders/textured_frag_spv.inc"
#include "graphics/shaders/textured_vert_spv.inc"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

TEST_CASE("asset.shader GPU admission validates both stage semantics and pipeline layout") {
    eve::asset::ShaderAsset shader;
    shader.interface = eve::asset::ShaderAssetInterface::Sprite2D;
    shader.vertex.assign(textured_vert_spv, textured_vert_spv + textured_vert_spv_count);
    shader.fragment.assign(textured_frag_spv, textured_frag_spv + textured_frag_spv_count);
    auto valid = eve::asset_graphics::validateShaderAssetGpu(shader);
#ifdef EVENGINE_SPIRV_ASSET_VALIDATION
    REQUIRE(valid.ok());
#else
    REQUIRE(!valid.ok());
#endif
    shader.interface = eve::asset::ShaderAssetInterface::Mesh3D;
    REQUIRE(!eve::asset_graphics::validateShaderAssetGpu(shader).ok());
    shader.interface = eve::asset::ShaderAssetInterface::Sprite2D;
    shader.fragment.pop_back();
    REQUIRE(!eve::asset_graphics::validateShaderAssetGpu(shader).ok());
}
TEST_CASE("asset.shader GPU admission rejects foreign descriptor slots") {
    eve::asset::ShaderAsset shader;
    shader.interface = eve::asset::ShaderAssetInterface::Sprite2D;
    shader.vertex.assign(textured_vert_spv, textured_vert_spv + textured_vert_spv_count);
    shader.fragment.assign(textured_frag_spv, textured_frag_spv + textured_frag_spv_count);
    // Change OpDecorate Binding (33) to a valid SPIR-V but unsupported descriptor slot.
    bool changed = false;
    for (std::size_t i = 5; i < shader.fragment.size();) {
        const auto length = shader.fragment[i] >> 16;
        if ((shader.fragment[i] & 65535) == 71 && length == 4 && shader.fragment[i + 2] == 33) {
            shader.fragment[i + 3] = 7;
            changed                = true;
        }
        i += length;
    }
    REQUIRE(changed);
    REQUIRE(!eve::asset_graphics::validateShaderAssetGpu(shader).ok());
}
