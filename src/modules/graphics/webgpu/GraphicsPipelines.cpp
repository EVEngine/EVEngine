#include "common/FrameCapture.h"
#include "graphics/AntiAliasing.h"
#include "graphics/Batcher.h"
#include "graphics/ClusteredLight.h"
#include "graphics/CubemapPrefilter.h"
#include "graphics/GBuffer.h"
#include "graphics/Light.h"
#include "graphics/Mesh.h"
#include "graphics/PrimitiveDrawList.h"
#include "graphics/PrimitiveTessellator.h"
#include "graphics/ReflectionProbeUpload.h"
#include "graphics/RenderControl.h"
#include "graphics/Shader.h"
#include "graphics/Shadow.h"
#include "graphics/Texture.h"
#include "graphics/TextureSampler.h"
#include "graphics/shaders/ReflectionProbeWgsl.h"
#include "graphics/webgpu/BindGroupLayoutBuilder.h"
#include "graphics/webgpu/Canvas.h"
#include "graphics/webgpu/Graphics.h"
#include "graphics/webgpu/GraphicsPipelineSupport.h"
#include "graphics/webgpu/InitFlow.h"
#include "graphics/webgpu/PipelineBuilder.h"
#include "graphics/webgpu/wgsl_shaders.h"

#include "common/Exception.h"
#include "common/Resource.h"
#include "common/config.h"
#include "filesystem/FileData.h"
#include "filesystem/Filesystem.h"
#include "image/Image.h"
#include "image/ImageData.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

#include <glm/gtc/constants.hpp>

namespace eve::graphics::webgpu {
using namespace detail;

void Graphics::createPipelineResources() {
    // Only layouts are required during initialization. Native Dawn compiles
    // render pipelines synchronously, so eagerly materializing every blend,
    // depth, cull, target and MSAA variant made each isolated CTest spend
    // roughly a minute compiling pipelines it never used.
    create2DPipelines();
    createMesh3DPipelines();
}

// ---------------------------------------------------------------------------
// Bind group layouts
// ---------------------------------------------------------------------------

wgpu::BindGroupLayout Graphics::make2DBindGroupLayout() {
    BindGroupLayoutBuilder b;
    // 0: color texture
    b.texture(0, wgpu::ShaderStage::Fragment, wgpu::TextureSampleType::Float, wgpu::TextureViewDimension::e2D);
    // 1: depth texture
    b.texture(1, wgpu::ShaderStage::Fragment, wgpu::TextureSampleType::Float, wgpu::TextureViewDimension::e2D);
    // 2: color sampler
    b.sampler(2, wgpu::ShaderStage::Fragment, wgpu::SamplerBindingType::Filtering);
    // 3: depth sampler
    b.sampler(3, wgpu::ShaderStage::Fragment, wgpu::SamplerBindingType::Filtering);
    // 4: Externals UBO (push-constant replacement, dynamic offset). Sized for
    //    the largest consumer: custom 2D shaders (128 B) and lit2D (Lighting2DUBO).
    b.buffer(4, wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment, wgpu::BufferBindingType::Uniform, true,
             std::max<uint32_t>(Shader::kPushConstantBytes, uint32_t(sizeof(Lighting2DUBO))));
    b.texture(5, wgpu::ShaderStage::Fragment, wgpu::TextureSampleType::Float, wgpu::TextureViewDimension::e2D);
    b.sampler(6, wgpu::ShaderStage::Fragment, wgpu::SamplerBindingType::Filtering);
    b.texture(7, wgpu::ShaderStage::Fragment, wgpu::TextureSampleType::Float, wgpu::TextureViewDimension::e2D);
    b.sampler(8, wgpu::ShaderStage::Fragment, wgpu::SamplerBindingType::Filtering);
    b.texture(9, wgpu::ShaderStage::Fragment, wgpu::TextureSampleType::Float, wgpu::TextureViewDimension::e2D);
    b.sampler(10, wgpu::ShaderStage::Fragment, wgpu::SamplerBindingType::Filtering);
    return b.build(device, "eve_2d");
}

wgpu::BindGroupLayout Graphics::makeMesh3DBindGroupLayout() {
    BindGroupLayoutBuilder b;
    // 0: Frame UBO (dynamic)
    b.buffer(0, wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment, wgpu::BufferBindingType::Uniform, true,
             sizeof(Mesh3DUBO));
    // 1: albedo
    b.texture(1, wgpu::ShaderStage::Fragment, wgpu::TextureSampleType::Float, wgpu::TextureViewDimension::e2D);
    // 2: normal
    b.texture(2, wgpu::ShaderStage::Fragment, wgpu::TextureSampleType::Float, wgpu::TextureViewDimension::e2D);
    // 3: env cubemap
    b.texture(3, wgpu::ShaderStage::Fragment, wgpu::TextureSampleType::Float, wgpu::TextureViewDimension::Cube);
    // 4: Shadow UBO (dynamic)
    b.buffer(4, wgpu::ShaderStage::Fragment, wgpu::BufferBindingType::Uniform, true, sizeof(ShadowUBO));
    // 5: shadow depth array
    b.texture(5, wgpu::ShaderStage::Fragment, wgpu::TextureSampleType::Depth, wgpu::TextureViewDimension::e2DArray);
    // 6: height
    b.texture(6, wgpu::ShaderStage::Fragment, wgpu::TextureSampleType::Float, wgpu::TextureViewDimension::e2D);
    // 7: shared filtering sampler (WGSL `mainSamp`)
    b.sampler(7, wgpu::ShaderStage::Fragment, wgpu::SamplerBindingType::Filtering);
    // 8: shadow comparison sampler
    b.sampler(8, wgpu::ShaderStage::Fragment, wgpu::SamplerBindingType::Comparison);
    // 9: scene depth (G-buffer hwDepth; X-ray shaders sample it). Depth view,
    // sampled via textureSampleLevel with the shared mainSamp (binding 7).
    b.texture(9, wgpu::ShaderStage::Fragment, wgpu::TextureSampleType::Depth, wgpu::TextureViewDimension::e2D);
    // 10: SSAO occlusion texture (white when AO is disabled)
    b.texture(10, wgpu::ShaderStage::Fragment, wgpu::TextureSampleType::Float, wgpu::TextureViewDimension::e2D);
    // 11: shared AO sampler
    b.sampler(11, wgpu::ShaderStage::Fragment, wgpu::SamplerBindingType::Filtering);
    for (uint32_t i = 12; i <= 14; ++i) {
        b.texture(i, wgpu::ShaderStage::Fragment, wgpu::TextureSampleType::Float, wgpu::TextureViewDimension::e2D);
    }
    // Custom mesh shaders may use their external parameters for vertex
    // deformation as well as fragment shading (for example grass billboards).
    b.buffer(15, wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment, wgpu::BufferBindingType::Uniform, true,
             Shader::kPushConstantBytes);
    b.texture(16, wgpu::ShaderStage::Fragment, wgpu::TextureSampleType::Float, wgpu::TextureViewDimension::Cube);
    b.texture(17, wgpu::ShaderStage::Fragment, wgpu::TextureSampleType::Float, wgpu::TextureViewDimension::Cube);
    b.texture(18, wgpu::ShaderStage::Fragment, wgpu::TextureSampleType::Float, wgpu::TextureViewDimension::e2D);
    b.sampler(19, wgpu::ShaderStage::Fragment, wgpu::SamplerBindingType::Filtering);
    b.buffer(21, wgpu::ShaderStage::Vertex, wgpu::BufferBindingType::ReadOnlyStorage, false, 64);
    return b.build(device, "eve_mesh3d");
}

wgpu::BindGroupLayout Graphics::makeShadowBindGroupLayout() {
    BindGroupLayoutBuilder b;
    b.buffer(0, wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment, wgpu::BufferBindingType::Uniform, true,
             sizeof(SkinPassUBO));
    b.texture(1, wgpu::ShaderStage::Fragment, wgpu::TextureSampleType::Float, wgpu::TextureViewDimension::e2D);
    b.sampler(2, wgpu::ShaderStage::Fragment, wgpu::SamplerBindingType::Filtering);
    b.buffer(3, wgpu::ShaderStage::Vertex, wgpu::BufferBindingType::ReadOnlyStorage, false, 64);
    return b.build(device, "eve_shadow");
}

wgpu::BindGroupLayout Graphics::makeGbufferBindGroupLayout() {
    BindGroupLayoutBuilder b;
    // 0: pass and skinning UBO (dynamic)
    b.buffer(0, wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment, wgpu::BufferBindingType::Uniform, true,
             sizeof(SkinPassUBO));
    // 1: albedo
    b.texture(1, wgpu::ShaderStage::Fragment, wgpu::TextureSampleType::Float, wgpu::TextureViewDimension::e2D);
    // 2: sampler
    b.sampler(2, wgpu::ShaderStage::Fragment, wgpu::SamplerBindingType::Filtering);
    b.buffer(3, wgpu::ShaderStage::Vertex, wgpu::BufferBindingType::ReadOnlyStorage, false, 64);
    return b.build(device, "eve_gbuffer");
}

wgpu::BindGroupLayout Graphics::makeDecalBindGroupLayout() {
    BindGroupLayoutBuilder b;
    b.buffer(0, wgpu::ShaderStage::Fragment, wgpu::BufferBindingType::Uniform, true, 256);
    for (uint32_t i = 1; i <= 3; ++i) {
        b.texture(i, wgpu::ShaderStage::Fragment, wgpu::TextureSampleType::Float, wgpu::TextureViewDimension::e2D);
    }
    b.texture(4, wgpu::ShaderStage::Fragment, wgpu::TextureSampleType::Depth, wgpu::TextureViewDimension::e2D);
    b.texture(5, wgpu::ShaderStage::Fragment, wgpu::TextureSampleType::Float, wgpu::TextureViewDimension::e2D);
    b.sampler(6, wgpu::ShaderStage::Fragment, wgpu::SamplerBindingType::Filtering);
    return b.build(device, "eve_decal");
}

wgpu::BindGroupLayout Graphics::makeVoxelBindGroupLayout() {
    BindGroupLayoutBuilder b;
    // 0: PC UBO (dynamic)
    b.buffer(0, wgpu::ShaderStage::Vertex, wgpu::BufferBindingType::Uniform, true, 112);
    // 1: atlas texture
    b.texture(1, wgpu::ShaderStage::Fragment, wgpu::TextureSampleType::Float, wgpu::TextureViewDimension::e2D);
    // 2: atlas sampler
    b.sampler(2, wgpu::ShaderStage::Fragment, wgpu::SamplerBindingType::Filtering);
    return b.build(device, "eve_voxel");
}

wgpu::PipelineLayout Graphics::make2DPipelineLayout() {
    wgpu::PipelineLayoutDescriptor d{};
    d.label                = "eve_2d_layout";
    d.bindGroupLayoutCount = 1;
    d.bindGroupLayouts     = &tex2DSetLayout;
    return device.CreatePipelineLayout(&d);
}

wgpu::PipelineLayout Graphics::makeMesh3DPipelineLayout() {
    wgpu::PipelineLayoutDescriptor d{};
    d.label                = "eve_mesh3d_layout";
    d.bindGroupLayoutCount = 1;
    d.bindGroupLayouts     = &mesh3dSetLayout;
    return device.CreatePipelineLayout(&d);
}

wgpu::BindGroupLayout Graphics::makeMesh3DClusteredBindGroupLayout() {
    BindGroupLayoutBuilder b;
    // 0: Frame UBO (dynamic; clustered layout)
    b.buffer(0, wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment, wgpu::BufferBindingType::Uniform, true,
             sizeof(Mesh3DClusteredUBO));
    // 1: albedo
    b.texture(1, wgpu::ShaderStage::Fragment, wgpu::TextureSampleType::Float, wgpu::TextureViewDimension::e2D);
    // 2: normal map
    b.texture(2, wgpu::ShaderStage::Fragment, wgpu::TextureSampleType::Float, wgpu::TextureViewDimension::e2D);
    // 3: env cubemap
    b.texture(3, wgpu::ShaderStage::Fragment, wgpu::TextureSampleType::Float, wgpu::TextureViewDimension::Cube);
    // 4: shadow UBO (dynamic)
    b.buffer(4, wgpu::ShaderStage::Fragment, wgpu::BufferBindingType::Uniform, true, sizeof(ShadowUBO));
    // 5: shadow depth array
    b.texture(5, wgpu::ShaderStage::Fragment, wgpu::TextureSampleType::Depth, wgpu::TextureViewDimension::e2DArray);
    // 6: height/parallax (unused fallback)
    b.texture(6, wgpu::ShaderStage::Fragment, wgpu::TextureSampleType::Float, wgpu::TextureViewDimension::e2D);
    // 7: shared filtering sampler
    b.sampler(7, wgpu::ShaderStage::Fragment, wgpu::SamplerBindingType::Filtering);
    // 8: shadow comparison sampler
    b.sampler(8, wgpu::ShaderStage::Fragment, wgpu::SamplerBindingType::Comparison);
    // 9: scene depth (G-buffer hwDepth; sampled by X-ray variants)
    b.texture(9, wgpu::ShaderStage::Fragment, wgpu::TextureSampleType::Depth, wgpu::TextureViewDimension::e2D);
    // 10..12: clustered-forward SSBOs
    b.buffer(10, wgpu::ShaderStage::Fragment, wgpu::BufferBindingType::ReadOnlyStorage, false,
             sizeof(ClusteredLightGpu));
    b.buffer(11, wgpu::ShaderStage::Fragment, wgpu::BufferBindingType::ReadOnlyStorage, false,
             sizeof(ClusterTableEntry));
    b.buffer(12, wgpu::ShaderStage::Fragment, wgpu::BufferBindingType::ReadOnlyStorage, false, sizeof(uint32_t));
    // 13/14: SSAO occlusion texture + sampler
    b.texture(13, wgpu::ShaderStage::Fragment, wgpu::TextureSampleType::Float, wgpu::TextureViewDimension::e2D);
    b.sampler(14, wgpu::ShaderStage::Fragment, wgpu::SamplerBindingType::Filtering);
    for (uint32_t i = 15; i <= 17; ++i) {
        b.texture(i, wgpu::ShaderStage::Fragment, wgpu::TextureSampleType::Float, wgpu::TextureViewDimension::e2D);
    }

    b.texture(18, wgpu::ShaderStage::Fragment, wgpu::TextureSampleType::Float, wgpu::TextureViewDimension::Cube);
    b.texture(19, wgpu::ShaderStage::Fragment, wgpu::TextureSampleType::Float, wgpu::TextureViewDimension::Cube);
    return b.build(device, "eve_mesh3d_clustered");
}

wgpu::PipelineLayout Graphics::makeMesh3DClusteredPipelineLayout() {
    wgpu::PipelineLayoutDescriptor d{};
    d.label                = "eve_mesh3d_clustered_layout";
    d.bindGroupLayoutCount = 1;
    d.bindGroupLayouts     = &mesh3dClusteredSetLayout;
    return device.CreatePipelineLayout(&d);
}

wgpu::PipelineLayout Graphics::makeShadowPipelineLayout() {
    wgpu::PipelineLayoutDescriptor d{};
    d.label                = "eve_shadow_layout";
    d.bindGroupLayoutCount = 1;
    d.bindGroupLayouts     = &shadowSetLayout;
    return device.CreatePipelineLayout(&d);
}

wgpu::PipelineLayout Graphics::makeGbufferPipelineLayout() {
    wgpu::PipelineLayoutDescriptor d{};
    d.label                = "eve_gbuffer_layout";
    d.bindGroupLayoutCount = 1;
    d.bindGroupLayouts     = &gbufferSetLayout;
    return device.CreatePipelineLayout(&d);
}

wgpu::PipelineLayout Graphics::makeDecalPipelineLayout() {
    wgpu::PipelineLayoutDescriptor d{};
    d.label                = "eve_decal_layout";
    d.bindGroupLayoutCount = 1;
    d.bindGroupLayouts     = &decalSetLayout;
    return device.CreatePipelineLayout(&d);
}

wgpu::PipelineLayout Graphics::makeVoxelPipelineLayout() {
    wgpu::PipelineLayoutDescriptor d{};
    d.label                = "eve_voxel_layout";
    d.bindGroupLayoutCount = 1;
    d.bindGroupLayouts     = &voxelSetLayout;
    return device.CreatePipelineLayout(&d);
}

// ---------------------------------------------------------------------------
// Pipelines
// ---------------------------------------------------------------------------

namespace {

wgpu::RenderPipeline make2DColorPipeline(wgpu::Device& dev, WGPUTextureFormat format, BlendMode mode) {
    wgpu::VertexAttribute attrs[2] = {};
    attrs[0].format                = wgpu::VertexFormat::Float32x2;
    attrs[0].offset                = 0;
    attrs[0].shaderLocation        = 0;
    attrs[1].format                = wgpu::VertexFormat::Float32x4;
    attrs[1].offset                = 8;
    attrs[1].shaderLocation        = 1;

    PipelineBuilder b;
    // The solid-color shader declares no bindings, so it must use an empty
    // (auto-derived) pipeline layout. Reusing the textured 2D layout here would
    // require a bind group for every solid draw.
    b.vertexLayout(24, wgpu::VertexStepMode::Vertex, attrs, 2);
    b.shader(makeWgslModule(dev, kColorVertWgsl), "vs_main", makeWgslModule(dev, kColorFragWgsl), "fs_main");
    b.colorTarget(format, mode);
    // The WGSL vertex shader mirrors clip Y to match Vulkan, which flips
    // object-space CCW winding in framebuffer space.
    b.frontFace(wgpu::FrontFace::CW);
    b.cull(wgpu::CullMode::Back);
    return b.build(dev);
}

wgpu::RenderPipeline make2DTexturedPipeline(wgpu::Device& dev, wgpu::PipelineLayout layout, WGPUTextureFormat format,
                                            BlendMode mode, bool toneMapScene = false) {
    WGPUVertexAttribute attrs[3] = {};
    attrs[0].format              = WGPUVertexFormat_Float32x2;
    attrs[0].offset              = 0;
    attrs[0].shaderLocation      = 0;
    attrs[1].format              = WGPUVertexFormat_Float32x4;
    attrs[1].offset              = 8;
    attrs[1].shaderLocation      = 1;
    attrs[2].format              = WGPUVertexFormat_Float32x2;
    attrs[2].offset              = 24;
    attrs[2].shaderLocation      = 2;
    WGPUVertexBufferLayout vb{};
    fillVertexLayout(vb, 32, attrs, 3);

    WGPUColorTargetState target{};
    target.format     = format;
    target.writeMask  = WGPUColorWriteMask_All;
    WGPUBlendState bs = alphaBlend();
    if (mode == BlendMode::Additive)
        bs = additiveBlend();
    else if (mode == BlendMode::Premultiplied)
        bs = premultipliedBlend();
    else if (mode == BlendMode::Multiply)
        bs = multiplyBlend();
    else if (mode == BlendMode::Opaque)
        bs = noBlend();
    if (mode != BlendMode::Opaque) target.blend = &bs;

    WGPURenderPipelineDescriptor pd{};
    pd.label                                           = sv("eve_textured2d");
    pd.layout                                          = layout.Get();
    wgpu::ShaderModule           vertModule            = makeWgslModule(dev, kTexturedVertWgsl);
    static constexpr const char* kSceneTonemapFragWgsl = R"wgsl(
struct FSIn {
    @location(0) color: vec4f,
    @location(1) uv: vec2f,
};
@group(0) @binding(0) var colorTex: texture_2d<f32>;
@group(0) @binding(2) var colorSampler: sampler;
@group(0) @binding(1) var rawSceneTex: texture_2d<f32>;
@group(0) @binding(3) var rawSceneSampler: sampler;
fn acesFitted(c0: vec3f) -> vec3f {
    let c = max(c0, vec3f(0.0));
    return clamp((c * (2.51 * c + vec3f(0.03))) /
                 (c * (2.43 * c + vec3f(0.59)) + vec3f(0.14)),
                 vec3f(0.0), vec3f(1.0));
}
fn linearToSrgb(c: vec3f) -> vec3f {
    let low = c * 12.92;
    let high = 1.055 * pow(max(c, vec3f(0.0)), vec3f(1.0 / 2.4)) - vec3f(0.055);
    return select(low, high, c > vec3f(0.0031308));
}
fn automaticExposure(packedLimits: f32, raw: bool) -> f32 {
    var logLuminance: array<f32, 16>;
    var sampleIndex = 0;
    for (var y = 0; y < 4; y = y + 1) {
        for (var x = 0; x < 4; x = x + 1) {
            let uv = (vec2f(f32(x), f32(y)) + vec2f(0.5)) * 0.25;
            var sampleColor = textureSampleLevel(colorTex, colorSampler, uv, 0.0).rgb;
            if (raw) {
                sampleColor = textureSampleLevel(rawSceneTex, rawSceneSampler, uv, 0.0).rgb;
            }
            sampleColor = max(sampleColor, vec3f(0.0));
            let luminance = dot(sampleColor, vec3f(0.2126, 0.7152, 0.0722));
            logLuminance[sampleIndex] = log2(clamp(luminance, 0.0001, 65504.0));
            sampleIndex += 1;
        }
    }
    for (var i = 1; i < 16; i = i + 1) {
        let value = logLuminance[i];
        var j = i - 1;
        while (j >= 0 && logLuminance[j] > value) {
            logLuminance[j + 1] = logLuminance[j];
            j -= 1;
        }
        logLuminance[j + 1] = value;
    }
    var trimmedLogSum = 0.0;
    for (var i = 2; i < 14; i = i + 1) {
        trimmedLogSum += logLuminance[i];
    }
    let geometricMean = exp2(trimmedLogSum * (1.0 / 12.0));
    let packed = round(packedLimits);
    let minEV = (packed - floor(packed * (1.0 / 256.0)) * 256.0) *
                (1.0 / 8.0) - 16.0;
    let maxEV = floor(packed * (1.0 / 256.0)) * (1.0 / 8.0) - 16.0;
    return clamp(0.18 / max(geometricMean, 0.0001), exp2(minEV), exp2(maxEV));
}
fn bloomPrefilter(color: vec3f, threshold: f32) -> vec3f {
    let brightness = max(color.r, max(color.g, color.b));
    let knee = max(threshold * 0.1, 0.01);
    let soft = clamp((brightness - threshold + knee) / (2.0 * knee), 0.0, 1.0);
    let contribution = max(brightness - threshold, soft * soft * knee);
    return color * (contribution / max(brightness, 0.0001));
}
fn sampleBloom(uv: vec2f, threshold: f32) -> vec3f {
    let texel = 1.0 / vec2f(textureDimensions(colorTex));
    var bloom = bloomPrefilter(textureSampleLevel(colorTex, colorSampler, uv, 0.0).rgb,
                               threshold) * 0.2;
    let offsets = array<vec2f, 12>(
        vec2f(2.00, 0.00), vec2f(-1.47, 1.35), vec2f(0.17, -1.99), vec2f(1.22, 1.58),
        vec2f(4.22, -2.68), vec2f(-4.92, -0.87), vec2f(3.04, 3.97), vec2f(-0.44, -4.98),
        vec2f(-5.28, 7.28), vec2f(8.89, -1.41), vec2f(-7.82, -4.46), vec2f(2.07, 8.76));
    let weights = array<f32, 12>(
        0.10, 0.10, 0.10, 0.10,
        0.06, 0.06, 0.06, 0.06,
        0.04, 0.04, 0.04, 0.04);
    for (var q = 0; q < 12; q = q + 1) {
        bloom += bloomPrefilter(textureSampleLevel(colorTex, colorSampler,
                                uv + offsets[q] * texel, 0.0).rgb, threshold) * weights[q];
    }
    return bloom;
}
@fragment
fn fs_main(in: FSIn) -> @location(0) vec4f {
    let hdr = textureSample(colorTex, colorSampler, in.uv);
    let exposure = 1.0; // Applied once by the dedicated HDR pre-exposure pass.
    let encodeSrgb = in.color.a >= 65536.0;
    let bloomPacked = round(in.color.a) - floor(round(in.color.a) * (1.0 / 65536.0)) * 65536.0;
    let bloomIntensity = (bloomPacked - floor(bloomPacked * (1.0 / 256.0)) * 256.0) *
                         (1.0 / 32.0);
    let bloomThreshold = floor(bloomPacked * (1.0 / 256.0)) * (1.0 / 16.0);
    var bloom = vec3f(0.0);
    if (bloomIntensity > 0.0) {
        bloom = sampleBloom(in.uv, bloomThreshold);
    }
    var displayColor = acesFitted((hdr.rgb + bloom * bloomIntensity) * exposure);
    if (encodeSrgb) {
        displayColor = linearToSrgb(displayColor);
    }
    return vec4f(displayColor, hdr.a);
}
)wgsl";
    wgpu::ShaderModule fragModule = makeWgslModule(dev, toneMapScene ? kSceneTonemapFragWgsl : kTexturedFragWgsl);
    pd.vertex.module              = vertModule.Get();
    pd.vertex.entryPoint          = sv("vs_main");
    pd.vertex.bufferCount         = 1;
    pd.vertex.buffers             = &vb;
    WGPUFragmentState fs{};
    fs.module                     = fragModule.Get();
    fs.entryPoint                 = sv("fs_main");
    fs.targetCount                = 1;
    fs.targets                    = &target;
    pd.fragment                   = &fs;
    pd.primitive.topology         = WGPUPrimitiveTopology_TriangleList;
    pd.primitive.frontFace        = WGPUFrontFace_CCW;
    pd.primitive.cullMode         = WGPUCullMode_None;
    pd.primitive.stripIndexFormat = WGPUIndexFormat_Undefined;
    pd.depthStencil               = nullptr;
    pd.multisample.count          = 1;
    // Zero-init would leave mask=0, which discards every fragment
    // (sampleMask=0). The WebGPU default is 0xFFFFFFFF (all samples).
    pd.multisample.mask = 0xFFFFFFFFu;
    return dev.CreateRenderPipeline(reinterpret_cast<const wgpu::RenderPipelineDescriptor*>(&pd));
}

wgpu::RenderPipeline make2DLitPipeline(wgpu::Device& dev, wgpu::PipelineLayout layout, WGPUTextureFormat format,
                                       BlendMode blend) {
    wgpu::VertexAttribute attrs[3] = {};
    attrs[0].format                = wgpu::VertexFormat::Float32x2;
    attrs[0].offset                = 0;
    attrs[0].shaderLocation        = 0;
    attrs[1].format                = wgpu::VertexFormat::Float32x4;
    attrs[1].offset                = 8;
    attrs[1].shaderLocation        = 1;
    attrs[2].format                = wgpu::VertexFormat::Float32x2;
    attrs[2].offset                = 24;
    attrs[2].shaderLocation        = 2;

    PipelineBuilder b;
    b.vertexLayout(32, wgpu::VertexStepMode::Vertex, attrs, 3);
    b.shader(makeWgslModule(dev, kLit2DVertWgsl), "vs_main", makeWgslModule(dev, kLit2DFragWgsl), "fs_main");
    b.layout(layout);
    b.colorTarget(format, blend);
    return b.build(dev);
}

}  // namespace

void Graphics::create2DPipelines() {
    tex2DSetLayout      = make2DBindGroupLayout();
    tex2DPipelineLayout = make2DPipelineLayout();

    colorPipeline            = make2DColorPipeline(device, surfaceFormat, BlendMode::Alpha);
    texturedPipeline         = make2DTexturedPipeline(device, tex2DPipelineLayout, surfaceFormat, BlendMode::Alpha);
    colorAdditivePipeline    = make2DColorPipeline(device, surfaceFormat, BlendMode::Additive);
    texturedAdditivePipeline = make2DTexturedPipeline(device, tex2DPipelineLayout, surfaceFormat, BlendMode::Additive);
    colorPremultipliedPipeline = make2DColorPipeline(device, surfaceFormat, BlendMode::Premultiplied);
    texturedPremultipliedPipeline =
        make2DTexturedPipeline(device, tex2DPipelineLayout, surfaceFormat, BlendMode::Premultiplied);
    colorMultiplyPipeline    = make2DColorPipeline(device, surfaceFormat, BlendMode::Multiply);
    texturedMultiplyPipeline = make2DTexturedPipeline(device, tex2DPipelineLayout, surfaceFormat, BlendMode::Multiply);
    colorOpaquePipeline      = make2DColorPipeline(device, surfaceFormat, BlendMode::Opaque);
    texturedOpaquePipeline   = make2DTexturedPipeline(device, tex2DPipelineLayout, surfaceFormat, BlendMode::Opaque);
    sceneTonemapPipeline  = make2DTexturedPipeline(device, tex2DPipelineLayout, surfaceFormat, BlendMode::Opaque, true);
    lit2dPipeline         = make2DLitPipeline(device, tex2DPipelineLayout, surfaceFormat, BlendMode::Alpha);
    lit2dAdditivePipeline = make2DLitPipeline(device, tex2DPipelineLayout, surfaceFormat, BlendMode::Additive);
    lit2dPremultipliedPipeline =
        make2DLitPipeline(device, tex2DPipelineLayout, surfaceFormat, BlendMode::Premultiplied);
    lit2dMultiplyPipeline = make2DLitPipeline(device, tex2DPipelineLayout, surfaceFormat, BlendMode::Multiply);
    lit2dOpaquePipeline   = make2DLitPipeline(device, tex2DPipelineLayout, surfaceFormat, BlendMode::Opaque);

    offscreenColorPipeline = make2DColorPipeline(device, WGPUTextureFormat_RGBA8Unorm, BlendMode::Alpha);
    offscreenTexturedPipeline =
        make2DTexturedPipeline(device, tex2DPipelineLayout, WGPUTextureFormat_RGBA8Unorm, BlendMode::Alpha);
    offscreenColorAdditivePipeline = make2DColorPipeline(device, WGPUTextureFormat_RGBA8Unorm, BlendMode::Additive);
    offscreenTexturedAdditivePipeline =
        make2DTexturedPipeline(device, tex2DPipelineLayout, WGPUTextureFormat_RGBA8Unorm, BlendMode::Additive);
    offscreenColorPremultipliedPipeline =
        make2DColorPipeline(device, WGPUTextureFormat_RGBA8Unorm, BlendMode::Premultiplied);
    offscreenTexturedPremultipliedPipeline =
        make2DTexturedPipeline(device, tex2DPipelineLayout, WGPUTextureFormat_RGBA8Unorm, BlendMode::Premultiplied);
    offscreenColorMultiplyPipeline = make2DColorPipeline(device, WGPUTextureFormat_RGBA8Unorm, BlendMode::Multiply);
    offscreenTexturedMultiplyPipeline =
        make2DTexturedPipeline(device, tex2DPipelineLayout, WGPUTextureFormat_RGBA8Unorm, BlendMode::Multiply);
    offscreenColorOpaquePipeline = make2DColorPipeline(device, WGPUTextureFormat_RGBA8Unorm, BlendMode::Opaque);
    offscreenTexturedOpaquePipeline =
        make2DTexturedPipeline(device, tex2DPipelineLayout, WGPUTextureFormat_RGBA8Unorm, BlendMode::Opaque);
    hdrOffscreenTexturedPipeline =
        make2DTexturedPipeline(device, tex2DPipelineLayout, WGPUTextureFormat_RGBA16Float, BlendMode::Alpha);
    hdrOffscreenTexturedOpaquePipeline =
        make2DTexturedPipeline(device, tex2DPipelineLayout, WGPUTextureFormat_RGBA16Float, BlendMode::Opaque);
    offscreenLitPipeline =
        make2DLitPipeline(device, tex2DPipelineLayout, WGPUTextureFormat_RGBA8Unorm, BlendMode::Alpha);
    offscreenLitAdditivePipeline =
        make2DLitPipeline(device, tex2DPipelineLayout, WGPUTextureFormat_RGBA8Unorm, BlendMode::Additive);
    offscreenLitPremultipliedPipeline =
        make2DLitPipeline(device, tex2DPipelineLayout, WGPUTextureFormat_RGBA8Unorm, BlendMode::Premultiplied);
    offscreenLitMultiplyPipeline =
        make2DLitPipeline(device, tex2DPipelineLayout, WGPUTextureFormat_RGBA8Unorm, BlendMode::Multiply);
    offscreenLitOpaquePipeline =
        make2DLitPipeline(device, tex2DPipelineLayout, WGPUTextureFormat_RGBA8Unorm, BlendMode::Opaque);
    hdrOffscreenLitPipeline =
        make2DLitPipeline(device, tex2DPipelineLayout, WGPUTextureFormat_RGBA16Float, BlendMode::Alpha);
    hdrOffscreenLitAdditivePipeline =
        make2DLitPipeline(device, tex2DPipelineLayout, WGPUTextureFormat_RGBA16Float, BlendMode::Additive);
    hdrOffscreenLitPremultipliedPipeline =
        make2DLitPipeline(device, tex2DPipelineLayout, WGPUTextureFormat_RGBA16Float, BlendMode::Premultiplied);
    hdrOffscreenLitMultiplyPipeline =
        make2DLitPipeline(device, tex2DPipelineLayout, WGPUTextureFormat_RGBA16Float, BlendMode::Multiply);
    hdrOffscreenLitOpaquePipeline =
        make2DLitPipeline(device, tex2DPipelineLayout, WGPUTextureFormat_RGBA16Float, BlendMode::Opaque);
}

wgpu::RenderPipeline Graphics::get2DColorPipeline(BlendMode blend, bool offscreen) {
    switch (blend) {
        case BlendMode::Additive: return offscreen ? offscreenColorAdditivePipeline : colorAdditivePipeline;
        case BlendMode::Premultiplied:
            return offscreen ? offscreenColorPremultipliedPipeline : colorPremultipliedPipeline;
        case BlendMode::Multiply: return offscreen ? offscreenColorMultiplyPipeline : colorMultiplyPipeline;
        case BlendMode::Opaque: return offscreen ? offscreenColorOpaquePipeline : colorOpaquePipeline;
        case BlendMode::Alpha:
        default: return offscreen ? offscreenColorPipeline : colorPipeline;
    }
}

wgpu::RenderPipeline Graphics::get2DTexturedPipeline(BlendMode blend, bool offscreen) {
    switch (blend) {
        case BlendMode::Additive: return offscreen ? offscreenTexturedAdditivePipeline : texturedAdditivePipeline;
        case BlendMode::Premultiplied:
            return offscreen ? offscreenTexturedPremultipliedPipeline : texturedPremultipliedPipeline;
        case BlendMode::Multiply: return offscreen ? offscreenTexturedMultiplyPipeline : texturedMultiplyPipeline;
        case BlendMode::Opaque: return offscreen ? offscreenTexturedOpaquePipeline : texturedOpaquePipeline;
        case BlendMode::Alpha:
        default: return offscreen ? offscreenTexturedPipeline : texturedPipeline;
    }
}

wgpu::RenderPipeline Graphics::get2DLitPipeline(BlendMode blend, WGPUTextureFormat format) {
    const bool hdr       = format == WGPUTextureFormat_RGBA16Float;
    const bool offscreen = hdr || uint32_t(format) != uint32_t(surfaceFormat);
    auto       pick      = [](BlendMode mode, wgpu::RenderPipeline alpha, wgpu::RenderPipeline additive,
                   wgpu::RenderPipeline premultiplied, wgpu::RenderPipeline multiply,
                   wgpu::RenderPipeline opaque) -> wgpu::RenderPipeline {
        switch (mode) {
            case BlendMode::Additive: return additive;
            case BlendMode::Premultiplied: return premultiplied;
            case BlendMode::Multiply: return multiply;
            case BlendMode::Opaque: return opaque;
            case BlendMode::Alpha:
            default: return alpha;
        }
    };
    if (hdr) {
        return pick(blend, hdrOffscreenLitPipeline, hdrOffscreenLitAdditivePipeline,
                    hdrOffscreenLitPremultipliedPipeline, hdrOffscreenLitMultiplyPipeline,
                    hdrOffscreenLitOpaquePipeline);
    }
    if (offscreen) {
        return pick(blend, offscreenLitPipeline, offscreenLitAdditivePipeline, offscreenLitPremultipliedPipeline,
                    offscreenLitMultiplyPipeline, offscreenLitOpaquePipeline);
    }
    return pick(blend, lit2dPipeline, lit2dAdditivePipeline, lit2dPremultipliedPipeline, lit2dMultiplyPipeline,
                lit2dOpaquePipeline);
}

void Graphics::createMesh3DPipelines() {
    mesh3dSetLayout = makeMesh3DBindGroupLayout();
    // Bind groups reference the layout; drop cached groups when it changes.
    clearMeshBindGroupCache();
    mesh3dPipelineLayout = makeMesh3DPipelineLayout();
}

wgpu::RenderPipeline Graphics::getMesh3DPipeline(BlendMode blend, bool depthWrite, bool doubleSided,
                                                 bool canvasTarget) {
    const size_t          index    = meshPipelineIndex(blend, depthWrite, doubleSided);
    wgpu::RenderPipeline& pipeline = canvasTarget ? mesh3dCanvasPipelines[index] : mesh3dPipelines[index];
    if (pipeline) return pipeline;

    WGPUVertexAttribute attrs[6] = {};
    fillMeshAttributes(attrs);
    WGPUVertexBufferLayout vb{};
    fillVertexLayout(vb, sizeof(MeshVertex), attrs, 6);

    WGPUDepthStencilState ds{};
    ds.format            = WGPUTextureFormat_Depth32Float;
    ds.depthWriteEnabled = depthWrite ? WGPUOptionalBool_True : WGPUOptionalBool_False;
    // Built-in opaque ForwardPlus replaces the equal-depth Hybrid result;
    // alpha surfaces keep strict depth and their existing blend behavior.
    ds.depthCompare =
        blend == BlendMode::Opaque && depthWrite ? WGPUCompareFunction_LessEqual : WGPUCompareFunction_Less;

    WGPUBlendState       blendDesc = blendState(blend);
    WGPUColorTargetState target{};
    target.format    = sceneColorFormat;
    target.blend     = blend == BlendMode::Opaque ? nullptr : &blendDesc;
    target.writeMask = WGPUColorWriteMask_All;

    wgpu::ShaderModule vertModule = makeWgslModule(device, kMesh3DVertWgsl);
    wgpu::ShaderModule fragModule = makeWgslModule(device, kMesh3DFragWgsl);
    WGPUFragmentState  fs{};
    fs.module      = fragModule.Get();
    fs.entryPoint  = sv("fs_main");
    fs.targetCount = 1;
    fs.targets     = &target;

    WGPURenderPipelineDescriptor pd{};
    pd.label                      = canvasTarget ? sv("eve_mesh3d_canvas_surface") : sv("eve_mesh3d_surface");
    pd.layout                     = mesh3dPipelineLayout.Get();
    pd.vertex.module              = vertModule.Get();
    pd.vertex.entryPoint          = sv("vs_main");
    pd.vertex.bufferCount         = 1;
    pd.vertex.buffers             = &vb;
    pd.fragment                   = &fs;
    pd.primitive.topology         = WGPUPrimitiveTopology_TriangleList;
    pd.primitive.frontFace        = WGPUFrontFace_CCW;
    pd.primitive.cullMode         = doubleSided ? WGPUCullMode_None : WGPUCullMode_Back;
    pd.primitive.stripIndexFormat = WGPUIndexFormat_Undefined;
    pd.depthStencil               = &ds;
    pd.multisample.count          = canvasTarget ? 1 : sceneColorSamples;
    pd.multisample.mask           = 0xFFFFFFFFu;
    pipeline = device.CreateRenderPipeline(reinterpret_cast<const wgpu::RenderPipelineDescriptor*>(&pd));

    if (!canvasTarget && blend == BlendMode::Opaque && depthWrite && !doubleSided) mesh3dPipeline = pipeline;
    if (!canvasTarget && blend == BlendMode::Alpha && !depthWrite && !doubleSided) mesh3dTransparentPipeline = pipeline;
    if (canvasTarget && blend == BlendMode::Opaque && depthWrite && !doubleSided) mesh3dCanvasPipeline = pipeline;
    return pipeline;
}


void Graphics::createMesh3DClusteredPipeline() {
    mesh3dClusteredSetLayout      = makeMesh3DClusteredBindGroupLayout();
    mesh3dClusteredPipelineLayout = makeMesh3DClusteredPipelineLayout();

    WGPUVertexAttribute attrs[6] = {};
    fillMeshAttributes(attrs);
    attrs[0].format         = WGPUVertexFormat_Float32x3;  // pos
    attrs[0].offset         = 0;
    attrs[0].shaderLocation = 0;
    attrs[1].format         = WGPUVertexFormat_Float32x3;  // normal
    attrs[1].offset         = 12;
    attrs[1].shaderLocation = 1;
    attrs[2].format         = WGPUVertexFormat_Float32x2;  // uv
    attrs[2].offset         = 24;
    attrs[2].shaderLocation = 2;
    WGPUVertexBufferLayout vb{};
    fillVertexLayout(vb, sizeof(MeshVertex), attrs, 6);

    WGPUDepthStencilState ds{};
    ds.format            = WGPUTextureFormat_Depth32Float;
    ds.depthWriteEnabled = WGPUOptionalBool_True;
    ds.depthCompare      = WGPUCompareFunction_LessEqual;
    ds.stencilReadMask   = 0;
    ds.stencilWriteMask  = 0;

    WGPUColorTargetState target{};
    target.format    = sceneColorFormat;
    target.blend     = nullptr;
    target.writeMask = WGPUColorWriteMask_All;

    WGPURenderPipelineDescriptor pd{};
    pd.label                      = sv("eve_mesh3d_clustered");
    pd.layout                     = mesh3dClusteredPipelineLayout.Get();
    wgpu::ShaderModule vertModule = makeWgslModule(device, kMesh3DClusteredVertWgsl);
    wgpu::ShaderModule fragModule = makeWgslModule(device, kMesh3DClusteredFragWgsl);
    pd.vertex.module              = vertModule.Get();
    pd.vertex.entryPoint          = sv("vs_main");
    pd.vertex.bufferCount         = 1;
    pd.vertex.buffers             = &vb;
    WGPUFragmentState fs{};
    fs.module                     = fragModule.Get();
    fs.entryPoint                 = sv("fs_main");
    fs.targetCount                = 1;
    fs.targets                    = &target;
    pd.fragment                   = &fs;
    pd.primitive.topology         = WGPUPrimitiveTopology_TriangleList;
    pd.primitive.frontFace        = WGPUFrontFace_CCW;
    pd.primitive.cullMode         = WGPUCullMode_Back;
    pd.primitive.stripIndexFormat = WGPUIndexFormat_Undefined;
    pd.depthStencil               = &ds;
    pd.multisample.count          = sceneColorSamples;
    pd.multisample.mask           = 0xFFFFFFFFu;
    mesh3dClusteredPipeline = device.CreateRenderPipeline(reinterpret_cast<const wgpu::RenderPipelineDescriptor*>(&pd));
}

void Graphics::createShadowPipelines() {
    shadowSetLayout      = makeShadowBindGroupLayout();
    shadowPipelineLayout = makeShadowPipelineLayout();

    WGPUVertexAttribute attrs[6] = {};
    fillMeshAttributes(attrs);
    attrs[0].format         = WGPUVertexFormat_Float32x3;
    attrs[0].offset         = 0;
    attrs[0].shaderLocation = 0;
    attrs[1].format         = WGPUVertexFormat_Float32x3;
    attrs[1].offset         = 12;
    attrs[1].shaderLocation = 1;
    attrs[2].format         = WGPUVertexFormat_Float32x2;
    attrs[2].offset         = 24;
    attrs[2].shaderLocation = 2;
    WGPUVertexBufferLayout vb{};
    fillVertexLayout(vb, sizeof(MeshVertex), attrs, 6);

    WGPUDepthStencilState ds{};
    ds.format            = WGPUTextureFormat_Depth32Float;
    ds.depthWriteEnabled = WGPUOptionalBool_True;
    ds.depthCompare      = WGPUCompareFunction_Less;
    ds.stencilReadMask   = 0;
    ds.stencilWriteMask  = 0;

    WGPURenderPipelineDescriptor pd{};
    pd.label                      = sv("eve_shadow");
    pd.layout                     = shadowPipelineLayout.Get();
    wgpu::ShaderModule vertModule = makeWgslModule(device, kMesh3DShadowVertWgsl);
    pd.vertex.module              = vertModule.Get();
    pd.vertex.entryPoint          = sv("vs_main");
    pd.vertex.bufferCount         = 1;
    pd.vertex.buffers             = &vb;
    pd.fragment                   = nullptr;
    pd.primitive.topology         = WGPUPrimitiveTopology_TriangleList;
    pd.primitive.frontFace        = WGPUFrontFace_CCW;
    pd.primitive.cullMode         = WGPUCullMode_None;
    pd.primitive.stripIndexFormat = WGPUIndexFormat_Undefined;
    pd.depthStencil               = &ds;
    pd.multisample.count          = 1;
    // Zero-init would leave mask=0, which discards every fragment
    // (sampleMask=0). The WebGPU default is 0xFFFFFFFF (all samples).
    pd.multisample.mask   = 0xFFFFFFFFu;
    mesh3dShadowPipeline  = device.CreateRenderPipeline(reinterpret_cast<const wgpu::RenderPipelineDescriptor*>(&pd));
    pd.label              = sv("eve_shadow_single_sided");
    pd.primitive.cullMode = WGPUCullMode_Back;
    mesh3dShadowSingleSidedPipeline =
        device.CreateRenderPipeline(reinterpret_cast<const wgpu::RenderPipelineDescriptor*>(&pd));

    wgpu::ShaderModule alphaVertModule = makeWgslModule(device, kMesh3DShadowAlphaVertWgsl);
    wgpu::ShaderModule alphaFragModule = makeWgslModule(device, kMesh3DShadowAlphaFragWgsl);
    pd.label                           = sv("eve_shadow_alpha");
    pd.primitive.cullMode              = WGPUCullMode_None;
    pd.vertex.module                   = alphaVertModule.Get();
    WGPUFragmentState alphaFs{};
    alphaFs.module      = alphaFragModule.Get();
    alphaFs.entryPoint  = sv("fs_main");
    alphaFs.targetCount = 0;
    alphaFs.targets     = nullptr;
    pd.fragment         = &alphaFs;
    mesh3dShadowAlphaPipeline =
        device.CreateRenderPipeline(reinterpret_cast<const wgpu::RenderPipelineDescriptor*>(&pd));
    pd.label              = sv("eve_shadow_alpha_single_sided");
    pd.primitive.cullMode = WGPUCullMode_Back;
    mesh3dShadowAlphaSingleSidedPipeline =
        device.CreateRenderPipeline(reinterpret_cast<const wgpu::RenderPipelineDescriptor*>(&pd));
}

void Graphics::createGbufferPipelines() {
    gbufferSetLayout      = makeGbufferBindGroupLayout();
    gbufferPipelineLayout = makeGbufferPipelineLayout();

    WGPUVertexAttribute attrs[6] = {};
    fillMeshAttributes(attrs);
    attrs[0].format         = WGPUVertexFormat_Float32x3;
    attrs[0].offset         = 0;
    attrs[0].shaderLocation = 0;
    attrs[1].format         = WGPUVertexFormat_Float32x3;
    attrs[1].offset         = 12;
    attrs[1].shaderLocation = 1;
    attrs[2].format         = WGPUVertexFormat_Float32x2;
    attrs[2].offset         = 24;
    attrs[2].shaderLocation = 2;
    WGPUVertexBufferLayout vb{};
    fillVertexLayout(vb, sizeof(MeshVertex), attrs, 6);

    WGPUDepthStencilState ds{};
    ds.format            = WGPUTextureFormat_Depth32Float;
    ds.depthWriteEnabled = WGPUOptionalBool_True;
    ds.depthCompare      = WGPUCompareFunction_Less;
    ds.stencilReadMask   = 0;
    ds.stencilWriteMask  = 0;

    WGPUColorTargetState targets[5] = {};
    for (int i = 0; i < 5; ++i) {
        targets[i].format    = WGPUTextureFormat_RGBA8Unorm;
        targets[i].blend     = nullptr;
        targets[i].writeMask = WGPUColorWriteMask_All;
    }

    WGPURenderPipelineDescriptor pd{};
    targets[4].format             = WGPUTextureFormat_RGBA16Float;
    pd.label                      = sv("eve_gbuffer");
    pd.layout                     = gbufferPipelineLayout.Get();
    wgpu::ShaderModule vertModule = makeWgslModule(device, kMesh3DGbufferVertWgsl);
    wgpu::ShaderModule fragModule = makeWgslModule(device, kMesh3DGbufferFragWgsl);
    pd.vertex.module              = vertModule.Get();
    pd.vertex.entryPoint          = sv("vs_main");
    pd.vertex.bufferCount         = 1;
    pd.vertex.buffers             = &vb;
    WGPUFragmentState fs{};
    fs.module                     = fragModule.Get();
    fs.entryPoint                 = sv("fs_main");
    fs.targetCount                = 5;
    fs.targets                    = targets;
    pd.fragment                   = &fs;
    pd.primitive.topology         = WGPUPrimitiveTopology_TriangleList;
    pd.primitive.frontFace        = WGPUFrontFace_CCW;
    pd.primitive.cullMode         = WGPUCullMode_None;
    pd.primitive.stripIndexFormat = WGPUIndexFormat_Undefined;
    pd.depthStencil               = &ds;
    pd.multisample.count          = 1;
    // Zero-init would leave mask=0, which discards every fragment
    // (sampleMask=0). The WebGPU default is 0xFFFFFFFF (all samples).
    pd.multisample.mask   = 0xFFFFFFFFu;
    mesh3dGbufferPipeline = device.CreateRenderPipeline(reinterpret_cast<const wgpu::RenderPipelineDescriptor*>(&pd));

    wgpu::ShaderModule alphaFragModule = makeWgslModule(device, kMesh3DGbufferAlphaFragWgsl);
    pd.label                           = sv("eve_gbuffer_alpha");
    fs.module                          = alphaFragModule.Get();
    mesh3dGbufferAlphaPipeline =
        device.CreateRenderPipeline(reinterpret_cast<const wgpu::RenderPipelineDescriptor*>(&pd));
}

void Graphics::createDecalPipeline() {
    decalSetLayout             = makeDecalBindGroupLayout();
    decalPipelineLayout        = makeDecalPipelineLayout();
    wgpu::ShaderModule   vert  = makeWgslModule(device, kDecalVertWgsl);
    wgpu::ShaderModule   frag  = makeWgslModule(device, kDecalFragWgsl);
    WGPUBlendState       blend = alphaBlend();
    WGPUColorTargetState targets[3]{};
    for (auto& target : targets) {
        target.format    = WGPUTextureFormat_RGBA8Unorm;
        target.blend     = &blend;
        target.writeMask = WGPUColorWriteMask_All;
    }
    WGPUFragmentState fs{};
    fs.module      = frag.Get();
    fs.entryPoint  = sv("fs_main");
    fs.targetCount = 3;
    fs.targets     = targets;
    WGPURenderPipelineDescriptor pd{};
    pd.label              = sv("eve_decal");
    pd.layout             = decalPipelineLayout.Get();
    pd.vertex.module      = vert.Get();
    pd.vertex.entryPoint  = sv("vs_main");
    pd.fragment           = &fs;
    pd.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    // kMesh3DVertWgsl mirrors clip-space Y, so object-space CCW becomes
    // clockwise in framebuffer space (the Vulkan pipeline uses Clockwise).
    pd.primitive.frontFace = WGPUFrontFace_CW;
    pd.primitive.cullMode  = WGPUCullMode_None;
    pd.multisample.count   = 1;
    pd.multisample.mask    = 0xFFFFFFFFu;
    decalPipeline          = device.CreateRenderPipeline(reinterpret_cast<const wgpu::RenderPipelineDescriptor*>(&pd));
}


// ---------------------------------------------------------------------------
// SSAO (screen-space ambient occlusion)
// ---------------------------------------------------------------------------

void Graphics::ensureAOResources(int width, int height) {
    if (!device || width <= 0 || height <= 0) return;
    if (!aoSetLayout) {
        BindGroupLayoutBuilder b;
        b.buffer(0, wgpu::ShaderStage::Fragment, wgpu::BufferBindingType::Uniform, false, 32);
        b.texture(1, wgpu::ShaderStage::Fragment, wgpu::TextureSampleType::Depth, wgpu::TextureViewDimension::e2D);
        b.sampler(2, wgpu::ShaderStage::Fragment, wgpu::SamplerBindingType::Filtering);
        aoSetLayout                             = b.build(device, "eve_ao");
        WGPUBindGroupLayout          layouts[1] = {aoSetLayout.Get()};
        WGPUPipelineLayoutDescriptor pl{};
        pl.label                = sv("eve_ao_layout");
        pl.bindGroupLayoutCount = 1;
        pl.bindGroupLayouts     = layouts;
        aoPipelineLayout = device.CreatePipelineLayout(reinterpret_cast<const wgpu::PipelineLayoutDescriptor*>(&pl));

        WGPUVertexAttribute attrs[3]{};
        attrs[0].format         = WGPUVertexFormat_Float32x2;
        attrs[0].offset         = 0;
        attrs[0].shaderLocation = 0;
        attrs[1].format         = WGPUVertexFormat_Float32x4;
        attrs[1].offset         = 8;
        attrs[1].shaderLocation = 1;
        attrs[2].format         = WGPUVertexFormat_Float32x2;
        attrs[2].offset         = 24;
        attrs[2].shaderLocation = 2;
        WGPUVertexBufferLayout vb{};
        fillVertexLayout(vb, 32, attrs, 3);

        WGPUColorTargetState target{};
        target.format    = WGPUTextureFormat_RGBA8Unorm;
        target.blend     = nullptr;
        target.writeMask = WGPUColorWriteMask_All;

        WGPURenderPipelineDescriptor pd{};
        pd.label                      = sv("eve_ssao");
        pd.layout                     = aoPipelineLayout.Get();
        wgpu::ShaderModule vertModule = makeWgslModule(device, kTexturedVertWgsl);
        wgpu::ShaderModule fragModule = makeWgslModule(device, kSSAOFragWgsl);
        pd.vertex.module              = vertModule.Get();
        pd.vertex.entryPoint          = sv("vs_main");
        pd.vertex.bufferCount         = 1;
        pd.vertex.buffers             = &vb;
        WGPUFragmentState fs{};
        fs.module                     = fragModule.Get();
        fs.entryPoint                 = sv("fs_main");
        fs.targetCount                = 1;
        fs.targets                    = &target;
        pd.fragment                   = &fs;
        pd.primitive.topology         = WGPUPrimitiveTopology_TriangleList;
        pd.primitive.frontFace        = WGPUFrontFace_CCW;
        pd.primitive.cullMode         = WGPUCullMode_None;
        pd.primitive.stripIndexFormat = WGPUIndexFormat_Undefined;
        pd.multisample.count          = 1;
        pd.multisample.mask           = 0xFFFFFFFFu;
        aoPipeline = device.CreateRenderPipeline(reinterpret_cast<const wgpu::RenderPipelineDescriptor*>(&pd));
    }
    if (!aoUbo) {
        WGPUBufferDescriptor bd{};
        bd.label            = sv("eve_ao_ubo");
        bd.size             = 64;
        bd.usage            = WGPUBufferUsage_CopyDst | WGPUBufferUsage_Uniform;
        bd.mappedAtCreation = false;
        aoUbo               = device.CreateBuffer(reinterpret_cast<const wgpu::BufferDescriptor*>(&bd));
    }
    if (!aoTex[0]) {
        for (int i = 0; i < 2; ++i) {
            WGPUTextureDescriptor td{};
            td.label     = sv("eve_ao");
            td.dimension = WGPUTextureDimension_2D;
            // Half-resolution AO: the bilinear upsample in the mesh pass blurs
            // the per-pixel sampling grain that otherwise reads as noise.
            td.size          = {static_cast<uint32_t>((width + 1) / 2), static_cast<uint32_t>((height + 1) / 2), 1};
            td.sampleCount   = 1;
            td.format        = WGPUTextureFormat_RGBA8Unorm;
            td.mipLevelCount = 1;
            td.usage         = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_RenderAttachment;
            aoTex[i]         = device.CreateTexture(reinterpret_cast<const wgpu::TextureDescriptor*>(&td));
            aoView[i]        = aoTex[i].CreateView();
        }
        aoReady = true;
        // New AO binding �?rebuild cached mesh bind groups.
        clearMeshBindGroupCache();
    }
    if (!fullscreenQuadReady) {
        float verts[32] = {
            -1.f, -1.f, 1.f, 1.f, 1.f, 1.f, 0.f, 0.f, 1.f,  -1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 0.f,
            1.f,  1.f,  1.f, 1.f, 1.f, 1.f, 1.f, 1.f, -1.f, 1.f,  1.f, 1.f, 1.f, 1.f, 0.f, 1.f,
        };
        uint32_t             indices[6] = {0, 1, 2, 2, 3, 0};
        WGPUBufferDescriptor vbd{};
        vbd.label        = sv("eve_fullscreen_vb");
        vbd.size         = sizeof(verts);
        vbd.usage        = WGPUBufferUsage_CopyDst | WGPUBufferUsage_Vertex;
        fullscreenQuadVb = device.CreateBuffer(reinterpret_cast<const wgpu::BufferDescriptor*>(&vbd));
        queue.WriteBuffer(fullscreenQuadVb, 0, verts, sizeof(verts));
        WGPUBufferDescriptor ibd{};
        ibd.label        = sv("eve_fullscreen_ib");
        ibd.size         = sizeof(indices);
        ibd.usage        = WGPUBufferUsage_CopyDst | WGPUBufferUsage_Index;
        fullscreenQuadIb = device.CreateBuffer(reinterpret_cast<const wgpu::BufferDescriptor*>(&ibd));
        queue.WriteBuffer(fullscreenQuadIb, 0, indices, sizeof(indices));
        fullscreenQuadReady = true;
    }
}

wgpu::BindGroup Graphics::makeAOBindGroup(wgpu::TextureView depthView) {
    WGPUBindGroupEntry entries[3]{};
    entries[0].binding     = 0;
    entries[0].buffer      = aoUbo.Get();
    entries[0].size        = 32;
    entries[1].binding     = 1;
    entries[1].textureView = depthView.Get();
    entries[2].binding     = 2;
    entries[2].sampler     = mainSampler.Get();
    WGPUBindGroupDescriptor desc{};
    desc.label      = sv("eve_ao_group");
    desc.layout     = aoSetLayout.Get();
    desc.entryCount = 3;
    desc.entries    = entries;
    return device.CreateBindGroup(reinterpret_cast<const wgpu::BindGroupDescriptor*>(&desc));
}

// ---------------------------------------------------------------------------
// Arena helpers
// ---------------------------------------------------------------------------


}  // namespace eve::graphics::webgpu
