// clang-format off
/* eve-test-policy:
{"schema":"evengine.test-policy","version":1,"default":"render-window","module":"graphics","modules":["graphics","window"]}
*/
// clang-format on
#include "common/config.h"
#if !defined(EVENGINE_WEBGPU)
#include <array>
#include "Fixtures.h"
#include "ShaderBindingContractSpv.h"
#include "ShaderResourceSharingSpv.h"
#include "graphics/Graphics.h"
#include "graphics/Shader.h"
#include "graphics/ShaderResources.h"
#include "graphics/vulkan/GlslCompiler.h"
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

TEST_CASE("graphics.resources.bindingReloadRejectsSameSizeAbiChangesWithoutReplacingLiveShader") {
    using namespace eve::graphics;
    using namespace shader_binding_fixture;
    GfxFixture fixture;
    auto&      gfx = *fixture.gfx;
    // Set 1 is established by resource replacement, after an ordinary ABI bootstrap.
    auto* shader = gfx.newMeshShaderFromSpv(sharing_vert, sharing_frag);
    REQUIRE(shader != nullptr);
    const std::array<std::byte, 4> pixel{};
    const std::array<float, 4>     constants{1, 0, 0, 1};
    ShaderImageInput               image;
    image.binding = 3;
    image.width = image.height = 1;
    image.bytes                = pixel;
    auto uploaded              = gfx.replaceMeshShaderResources(*shader, vertex, fragment,
                                                                {{&image, 1}, std::as_bytes(std::span(constants)), {}});
#ifdef EVE_TEST_RESOURCE_VALIDATION
    REQUIRE(uploaded.ok());
    const auto originalGpu = shader->gpuHandle;
    if (glslRuntimeCompilationAvailable()) {
        auto compatibleGlsl = gfx.replaceShaderFromGlsl(*shader, {}, fragmentGlsl);
        REQUIRE(compatibleGlsl.ok());
        const auto installedFragment = shader->fragmentSpirv();
        auto       conflictingGlsl   = gfx.replaceShaderFromGlsl(*shader, {}, integerFragmentGlsl);
        REQUIRE(!conflictingGlsl.ok());
        CHECK(conflictingGlsl.diagnostics()[0].code() == eve::DiagnosticCode::Conflict);
        CHECK(shader->fragmentSpirv() == installedFragment);
        CHECK(shader->gpuHandle == originalGpu);
        auto malformed = gfx.replaceShaderFromGlsl(*shader, {}, "this is not GLSL");
        REQUIRE(!malformed.ok());
        CHECK(malformed.diagnostics()[0].code() == eve::DiagnosticCode::ParseError);
        CHECK(shader->fragmentSpirv() == installedFragment);
    } else {
        auto unavailable = gfx.replaceShaderFromGlsl(*shader, {}, fragmentGlsl);
        REQUIRE(!unavailable.ok());
        CHECK(unavailable.diagnostics()[0].code() == eve::DiagnosticCode::ParseError);
        CHECK(shader->fragmentSpirv() == fragment);
    }
    const auto originalFragment = shader->fragmentSpirv();
    auto       conflict         = gfx.replaceShaderFromSpv(*shader, {}, integerFragment);
    REQUIRE(!conflict.ok());
    CHECK(conflict.diagnostics()[0].code() == eve::DiagnosticCode::Conflict);
    CHECK(shader->gpuHandle == originalGpu);
    CHECK(shader->fragmentSpirv() == originalFragment);
    auto compatible = gfx.replaceShaderFromSpv(*shader, {}, imageOnlyFragment);
    REQUIRE(compatible.ok());
    auto reintroduced = gfx.replaceShaderFromSpv(*shader, {}, integerFragment);
    REQUIRE(!reintroduced.ok());
    CHECK(shader->fragmentSpirv() == imageOnlyFragment);
    // A new immutable resource snapshot establishes a new ABI.
    auto newSnapshot = gfx.replaceMeshShaderResources(*shader, {}, integerFragment,
                                                      {{&image, 1}, std::as_bytes(std::span(constants)), {}});
    REQUIRE(newSnapshot.ok());
    CHECK(shader->fragmentSpirv() == integerFragment);
#else
    REQUIRE(!uploaded.ok());
    CHECK(uploaded.status().code() == eve::StatusCode::Unsupported);
#endif
    REQUIRE(gfx.releaseShader(shader));
}
#endif
