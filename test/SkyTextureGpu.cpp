#include <array>
#include <cmath>
#include <memory>
#include "Fixtures.h"
#include "ShaderResourceSharingSpv.h"
#include "SkyTextureGpuSpv.h"
#include "graphics/Canvas.h"
#include "graphics/Mesh.h"
#include "graphics/Shader.h"
#include "graphics/ShaderResources.h"
#include "image/Image.h"
#include "image/ImageData.h"
#include "zeroerr/unittest.h"

// sky_volume_vert: #version 450
// layout(location=0) in vec3 position;
// void main(){gl_Position=vec4(position,1.0);}
// sky_volume_frag: #version 450
// layout(set=1,binding=0) uniform sampler3D skyVolume;
// layout(location=0) out vec4 color;
// void main(){color=textureLod(skyVolume,vec3(0.5,0.5,0.5),0.0);}

TEST_CASE("graphics.sky HDR volume uploads preserve interpolation and failed replacement") {
    using namespace eve::graphics;
    REQUIRE(eve::image::Image::create() != nullptr);
    GfxFixture fixture(32, 32, true);
    auto*      gfx = fixture.gfx;
#ifdef EVENGINE_WEBGPU
    Shader shader;
    auto   unavailable = gfx->replaceMeshShaderResources(shader, sky_volume_vert, sky_volume_frag, {});
    REQUIRE(!unavailable.ok());
    REQUIRE(unavailable.error()->code() == eve::DiagnosticCode::Unsupported);
#else
    auto* shader = gfx->newMeshShaderFromSpv(sharing_vert, sharing_frag);
    REQUIRE(shader != nullptr);
    // Two Z slices: values above one and negative values must remain linear HDR.
    std::array<uint16_t, 8> pixels{0x4000, 0x3c00, 0xbc00, 0x3c00, 0x4400, 0x4200, 0x3c00, 0x3c00};
    ShaderImageInput        image;
    image.dimension = ShaderImageDimension::Image3D;
    image.format    = ShaderImageFormat::RGBA16Float;
    image.width = image.height = 1;
    image.depth                = 2;
    image.bytes                = std::as_bytes(std::span(pixels));
    image.sampler              = TextureSampler::linear();
#ifndef EVE_TEST_RESOURCE_VALIDATION
    auto* original = shader->gpuHandle;
#endif
    auto uploaded = gfx->replaceMeshShaderResources(*shader, sky_volume_vert, sky_volume_frag, {{&image, 1}, {}, {}});
#ifndef EVE_TEST_RESOURCE_VALIDATION
    REQUIRE(!uploaded.ok());
    REQUIRE(uploaded.error()->code() == eve::DiagnosticCode::Unsupported);
    REQUIRE(shader->gpuHandle == original);
#else
    REQUIRE(uploaded.ok());
    auto* committed = shader->gpuHandle;
    image.depth     = 1;
    REQUIRE(!gfx->replaceMeshShaderResources(*shader, sky_volume_vert, sky_volume_frag, {{&image, 1}, {}, {}}).ok());
    REQUIRE(shader->gpuHandle == committed);
    const float    positions[] = {-1, -1, .5f, 3, -1, .5f, -1, 3, .5f};
    const float    normals[]   = {0, 0, 1, 0, 0, 1, 0, 0, 1};
    const float    uv[]        = {0, 0, 2, 0, 0, 2};
    const uint32_t indices[]   = {0, 1, 2};
    auto*          mesh        = gfx->newMeshFromArrays(positions, normals, uv, 3, indices, 3);
    auto*          canvas      = gfx->newHDRCanvas(16, 16);
    REQUIRE(mesh != nullptr);
    REQUIRE(canvas != nullptr);
    REQUIRE(gfx->configureMeshShaderSurface(*shader, BlendMode::Opaque, false, true).ok());
    gfx->setMesh3DViewProj(glm::mat4(1));
    gfx->setMesh3DView(glm::mat4(1));
    gfx->begin3DFrameToCanvas(canvas);
    gfx->drawMeshShader(mesh, glm::mat4(1), nullptr, Color(1, 1, 1, 1), shader);
    gfx->end3DFrameToCanvas();
    std::unique_ptr<eve::image::ImageData> readback(canvas->newHDRImageData());
    REQUIRE(readback != nullptr);
    const auto sample = readback->getPixel(8, 8);
    REQUIRE(std::abs(sample.r - 3.f) < .01f);
    REQUIRE(std::abs(sample.g - 2.f) < .01f);
    REQUIRE(std::abs(sample.b) < .01f);
    REQUIRE(gfx->releaseMesh(mesh));
    delete mesh;
#endif
    REQUIRE(gfx->releaseShader(shader));
    delete shader;
#endif
}
