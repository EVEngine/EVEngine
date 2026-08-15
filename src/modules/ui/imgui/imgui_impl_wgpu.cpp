// dear imgui: Renderer Backend for WebGPU (wgpu), adapted for EVEngine's
// vendored imgui v1.83. Uses the stable webgpu.h C API so the same file
// compiles against Dawn (native) and Emscripten (browser).
//
// This is a self-contained renderer: imgui draws are issued into an existing
// WGPURenderPassEncoder supplied by the host (EVEngine's present overlay).

// The source scanner compiles every *.cpp under ui/imgui/ on all platforms;
// keep this TU empty when the WebGPU backend is not active.
// config.h must be included before the guard: EVENGINE_WEBGPU is defined by
// config.h, not by a -D flag, so including it inside the guard would make this
// TU always empty.
#include "common/config.h"

#if defined(EVENGINE_WEBGPU)

#include "imgui_impl_wgpu.h"

#include <limits.h>
#include <string.h>

// Avoid clashes with Windows headers.
#if defined(WIN32) && defined(min)
#undef min
#undef max
#endif

//-------------------------------------------------------------------------
// Configuration
//-------------------------------------------------------------------------
#ifndef IMGUI_IMPL_WEBGPU_DEFAULT_FONT_SAMPLER
#define IMGUI_IMPL_WEBGPU_DEFAULT_FONT_SAMPLER 0
#endif

//-----------------------------------------------------------------------------
// Implementation
//-----------------------------------------------------------------------------

struct ImGui_ImplWGPU_Data {
    WGPUDevice wgpuDevice = nullptr;
    WGPUTextureFormat rtFormat = WGPUTextureFormat_BGRA8Unorm;

    WGPUQueue defaultQueue = nullptr;

    WGPUBindGroupLayout bindGroupLayout = nullptr;
    WGPUBindGroup bindGroup = nullptr;
    WGPUBuffer uniformBuffer = nullptr;
    uint32_t uniformBufferSize = 0;
    WGPURenderPipeline renderPipeline = nullptr;

    WGPUTexture fontTexture = nullptr;
    WGPUTextureView fontTextureView = nullptr;
    WGPUSampler fontSampler = nullptr;

    int numFramesInFlight = 1;
    int frameIndex = 0;
};

// Backend data stored in io.BackendRendererUserData to allow support for multiple
// Dear ImGui contexts.
static ImGui_ImplWGPU_Data *ImGui_ImplWGPU_GetBackendData() {
    return ImGui::GetCurrentContext() ? (ImGui_ImplWGPU_Data *)ImGui::GetIO().BackendRendererUserData
                                      : nullptr;
}

static const char *ImGui_ImplWGPU_GetWGSLVertexShader() {
    return R"(
struct Uniforms {
    mvp: mat4x4f,
};
struct VertexInput {
    @location(0) position: vec2f,
    @location(1) uv: vec2f,
    @location(2) color: vec4f,
};
struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
    @location(1) color: vec4f,
};
@group(0) @binding(0) var<uniform> uniforms: Uniforms;
@vertex
fn vs_main(in: VertexInput) -> VertexOutput {
    var out: VertexOutput;
    out.position = uniforms.mvp * vec4f(in.position, 0.0, 1.0);
    out.uv = in.uv;
    out.color = in.color;
    return out;
}
)";
}

static const char *ImGui_ImplWGPU_GetWGSLFragmentShader() {
    return R"(
struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
    @location(1) color: vec4f,
};
@group(0) @binding(1) var fontSampler: sampler;
@group(0) @binding(2) var fontTexture: texture_2d<f32>;
@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    return textureSample(fontTexture, fontSampler, in.uv) * in.color;
}
)";
}

static void ImGui_ImplWGPU_SetupRenderState(ImDrawData *draw_data,
                                            WGPURenderPassEncoder pass_encoder) {
    ImGui_ImplWGPU_Data *bd = ImGui_ImplWGPU_GetBackendData();

    // Setup orthographic projection matrix into our constant buffer.
    {
        float L = draw_data->DisplayPos.x;
        float R = draw_data->DisplayPos.x + draw_data->DisplaySize.x;
        float T = draw_data->DisplayPos.y;
        float B = draw_data->DisplayPos.y + draw_data->DisplaySize.y;
        float mvp[4][4] = {
            {2.0f / (R - L), 0.0f, 0.0f, 0.0f},
            {0.0f, 2.0f / (T - B), 0.0f, 0.0f},
            {0.0f, 0.0f, 0.5f, 0.0f},
            {(R + L) / (L - R), (T + B) / (B - T), 0.5f, 1.0f},
        };
        wgpuQueueWriteBuffer(bd->defaultQueue, bd->uniformBuffer, 0, mvp, sizeof(mvp));
    }

    wgpuRenderPassEncoderSetPipeline(pass_encoder, bd->renderPipeline);
    wgpuRenderPassEncoderSetBindGroup(pass_encoder, 0, bd->bindGroup, 0, nullptr);
}

static void ImGui_ImplWGPU_CreatePipeline() {
    ImGui_ImplWGPU_Data *bd = ImGui_ImplWGPU_GetBackendData();
    WGPUDevice device = bd->wgpuDevice;

    // Shader modules.
    WGPUShaderSourceWGSL wgslVert{};
    wgslVert.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgslVert.code = WGPUStringView{ImGui_ImplWGPU_GetWGSLVertexShader(),
                                   strlen(ImGui_ImplWGPU_GetWGSLVertexShader())};
    WGPUShaderModuleDescriptor vsDesc{};
    vsDesc.nextInChain = &wgslVert.chain;
    WGPUShaderModule vsModule = wgpuDeviceCreateShaderModule(device, &vsDesc);

    WGPUShaderSourceWGSL wgslFrag{};
    wgslFrag.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgslFrag.code = WGPUStringView{ImGui_ImplWGPU_GetWGSLFragmentShader(),
                                   strlen(ImGui_ImplWGPU_GetWGSLFragmentShader())};
    WGPUShaderModuleDescriptor fsDesc{};
    fsDesc.nextInChain = &wgslFrag.chain;
    WGPUShaderModule fsModule = wgpuDeviceCreateShaderModule(device, &fsDesc);

    // Bind group layout.
    WGPUBindGroupLayoutEntry entries[3] = {};
    entries[0].binding = 0;
    entries[0].visibility = WGPUShaderStage_Vertex;
    entries[0].buffer.type = WGPUBufferBindingType_Uniform;
    entries[0].buffer.minBindingSize = 64;
    entries[1].binding = 1;
    entries[1].visibility = WGPUShaderStage_Fragment;
    entries[1].sampler.type = WGPUSamplerBindingType_Filtering;
    entries[2].binding = 2;
    entries[2].visibility = WGPUShaderStage_Fragment;
    entries[2].texture.sampleType = WGPUTextureSampleType_Float;
    entries[2].texture.viewDimension = WGPUTextureViewDimension_2D;
    WGPUBindGroupLayoutDescriptor bglDesc{};
    bglDesc.entryCount = 3;
    bglDesc.entries = entries;
    bd->bindGroupLayout = wgpuDeviceCreateBindGroupLayout(device, &bglDesc);

    // Pipeline layout.
    WGPUPipelineLayoutDescriptor plDesc{};
    plDesc.bindGroupLayoutCount = 1;
    plDesc.bindGroupLayouts = &bd->bindGroupLayout;
    WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(device, &plDesc);

    // Vertex layout: ImDrawVert { vec2 pos; vec2 uv; u32 col; }
    WGPUVertexAttribute attrs[3] = {};
    attrs[0].format = WGPUVertexFormat_Float32x2;
    attrs[0].offset = IM_OFFSETOF(ImDrawVert, pos);
    attrs[0].shaderLocation = 0;
    attrs[1].format = WGPUVertexFormat_Float32x2;
    attrs[1].offset = IM_OFFSETOF(ImDrawVert, uv);
    attrs[1].shaderLocation = 1;
    attrs[2].format = WGPUVertexFormat_Unorm8x4;
    attrs[2].offset = IM_OFFSETOF(ImDrawVert, col);
    attrs[2].shaderLocation = 2;
    WGPUVertexBufferLayout vbLayout{};
    vbLayout.arrayStride = sizeof(ImDrawVert);
    vbLayout.stepMode = WGPUVertexStepMode_Vertex;
    vbLayout.attributeCount = 3;
    vbLayout.attributes = attrs;

    WGPUBlendState blend{};
    blend.color.srcFactor = WGPUBlendFactor_SrcAlpha;
    blend.color.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    blend.color.operation = WGPUBlendOperation_Add;
    blend.alpha.srcFactor = WGPUBlendFactor_One;
    blend.alpha.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    blend.alpha.operation = WGPUBlendOperation_Add;

    WGPUColorTargetState colorTarget{};
    colorTarget.format = bd->rtFormat;
    colorTarget.blend = &blend;
    colorTarget.writeMask = WGPUColorWriteMask_All;

    WGPURenderPipelineDescriptor desc{};
    desc.layout = pipelineLayout;
    desc.vertex.module = vsModule;
    desc.vertex.entryPoint = WGPUStringView{"vs_main", 6};
    desc.vertex.bufferCount = 1;
    desc.vertex.buffers = &vbLayout;
    desc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    desc.primitive.frontFace = WGPUFrontFace_CCW;
    desc.primitive.cullMode = WGPUCullMode_None;
    desc.primitive.stripIndexFormat = WGPUIndexFormat_Undefined;
    WGPUFragmentState fragState{};
    fragState.module = fsModule;
    fragState.entryPoint = WGPUStringView{"fs_main", 6};
    fragState.targetCount = 1;
    fragState.targets = &colorTarget;
    desc.fragment = &fragState;
    desc.multisample.count = 1;
    bd->renderPipeline = wgpuDeviceCreateRenderPipeline(device, &desc);
}

static void ImGui_ImplWGPU_CreateUniformBuffer() {
    ImGui_ImplWGPU_Data *bd = ImGui_ImplWGPU_GetBackendData();
    WGPUDevice device = bd->wgpuDevice;

    bd->uniformBufferSize = 64;
    WGPUBufferDescriptor desc{};
    desc.size = bd->uniformBufferSize;
    desc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    desc.mappedAtCreation = false;
    bd->uniformBuffer = wgpuDeviceCreateBuffer(device, &desc);
}

bool ImGui_ImplWGPU_CreateFontsTexture() {
    ImGuiIO &io = ImGui::GetIO();
    ImGui_ImplWGPU_Data *bd = ImGui_ImplWGPU_GetBackendData();
    WGPUDevice device = bd->wgpuDevice;

    unsigned char *pixels;
    int width, height, size;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height, &size);
    size_t stride = static_cast<size_t>(width) * 4;

    WGPUTextureDescriptor texDesc{};
    texDesc.dimension = WGPUTextureDimension_2D;
    texDesc.size.width = width;
    texDesc.size.height = height;
    texDesc.size.depthOrArrayLayers = 1;
    texDesc.sampleCount = 1;
    texDesc.format = WGPUTextureFormat_RGBA8Unorm;
    texDesc.mipLevelCount = 1;
    texDesc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
    bd->fontTexture = wgpuDeviceCreateTexture(device, &texDesc);

    WGPUTexelCopyBufferLayout layout{};
    layout.offset = 0;
    layout.bytesPerRow = stride;
    layout.rowsPerImage = height;
    WGPUTexelCopyTextureInfo copyDst{};
    copyDst.texture = bd->fontTexture;
    copyDst.mipLevel = 0;
    copyDst.aspect = WGPUTextureAspect_All;
    WGPUExtent3D extent{static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};
    wgpuQueueWriteTexture(bd->defaultQueue, &copyDst, pixels, stride * height, &layout, &extent);

    WGPUTextureViewDescriptor viewDesc{};
    viewDesc.format = WGPUTextureFormat_RGBA8Unorm;
    viewDesc.dimension = WGPUTextureViewDimension_2D;
    viewDesc.baseMipLevel = 0;
    viewDesc.mipLevelCount = 1;
    viewDesc.baseArrayLayer = 0;
    viewDesc.arrayLayerCount = 1;
    bd->fontTextureView = wgpuTextureCreateView(bd->fontTexture, &viewDesc);

    WGPUSamplerDescriptor samplerDesc{};
    samplerDesc.minFilter = WGPUFilterMode_Linear;
    samplerDesc.magFilter = WGPUFilterMode_Linear;
    samplerDesc.mipmapFilter = WGPUMipmapFilterMode_Linear;
    samplerDesc.addressModeU = WGPUAddressMode_ClampToEdge;
    samplerDesc.addressModeV = WGPUAddressMode_ClampToEdge;
    samplerDesc.addressModeW = WGPUAddressMode_ClampToEdge;
    bd->fontSampler = wgpuDeviceCreateSampler(device, &samplerDesc);

    WGPUBindGroupEntry entries[3] = {};
    entries[0].binding = 0;
    entries[0].buffer = bd->uniformBuffer;
    entries[0].size = bd->uniformBufferSize;
    entries[1].binding = 1;
    entries[1].sampler = bd->fontSampler;
    entries[2].binding = 2;
    entries[2].textureView = bd->fontTextureView;

    WGPUBindGroupDescriptor bgDesc{};
    bgDesc.layout = bd->bindGroupLayout;
    bgDesc.entryCount = 3;
    bgDesc.entries = entries;
    bd->bindGroup = wgpuDeviceCreateBindGroup(device, &bgDesc);

    io.Fonts->SetTexID((ImTextureID)(intptr_t)bd->fontTextureView);
    return true;
}

void ImGui_ImplWGPU_InvalidateDeviceObjects() {
    ImGui_ImplWGPU_Data *bd = ImGui_ImplWGPU_GetBackendData();
    if (!bd) return;
    if (bd->fontTexture) { wgpuTextureRelease(bd->fontTexture); bd->fontTexture = nullptr; }
    if (bd->fontTextureView) { wgpuTextureViewRelease(bd->fontTextureView); bd->fontTextureView = nullptr; }
    if (bd->fontSampler) { wgpuSamplerRelease(bd->fontSampler); bd->fontSampler = nullptr; }
    if (bd->bindGroup) { wgpuBindGroupRelease(bd->bindGroup); bd->bindGroup = nullptr; }
    if (bd->bindGroupLayout) { wgpuBindGroupLayoutRelease(bd->bindGroupLayout); bd->bindGroupLayout = nullptr; }
    if (bd->uniformBuffer) { wgpuBufferRelease(bd->uniformBuffer); bd->uniformBuffer = nullptr; }
    if (bd->renderPipeline) { wgpuRenderPipelineRelease(bd->renderPipeline); bd->renderPipeline = nullptr; }
    if (ImGui::GetCurrentContext() && ImGui::GetIO().Fonts)
        ImGui::GetIO().Fonts->SetTexID(0);
}

bool ImGui_ImplWGPU_Init(WGPUDevice device, int num_frames_in_flight, WGPUTextureFormat rt_format) {
    ImGuiIO &io = ImGui::GetIO();
    IM_ASSERT(io.BackendRendererUserData == NULL && "Already initialized a renderer backend!");

    ImGui_ImplWGPU_Data *bd = IM_NEW(ImGui_ImplWGPU_Data)();
    io.BackendRendererUserData = (void *)bd;
    io.BackendRendererName = "imgui_impl_webgpu";
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;

    bd->wgpuDevice = device;
    bd->rtFormat = rt_format;
    bd->numFramesInFlight = num_frames_in_flight;
    bd->defaultQueue = wgpuDeviceGetQueue(device);

    ImGui_ImplWGPU_CreateUniformBuffer();
    ImGui_ImplWGPU_CreatePipeline();
    ImGui_ImplWGPU_CreateFontsTexture();

    return true;
}

void ImGui_ImplWGPU_Shutdown() {
    ImGui_ImplWGPU_Data *bd = ImGui_ImplWGPU_GetBackendData();
    IM_ASSERT(bd != NULL && "No renderer backend to shutdown, or already shutdown?");
    ImGui_ImplWGPU_InvalidateDeviceObjects();
    if (bd->defaultQueue) { wgpuQueueRelease(bd->defaultQueue); bd->defaultQueue = nullptr; }
    if (bd->wgpuDevice) { wgpuDeviceRelease(bd->wgpuDevice); bd->wgpuDevice = nullptr; }
    ImGuiIO &io = ImGui::GetIO();
    io.BackendRendererName = NULL;
    io.BackendRendererUserData = NULL;
    IM_DELETE(bd);
}

void ImGui_ImplWGPU_NewFrame() {
    ImGui_ImplWGPU_Data *bd = ImGui_ImplWGPU_GetBackendData();
    IM_ASSERT(bd != NULL && "Did you call ImGui_ImplWGPU_Init()?");
    bd->frameIndex = (bd->frameIndex + 1) % bd->numFramesInFlight;
}

void ImGui_ImplWGPU_RenderDrawData(ImDrawData *draw_data, WGPURenderPassEncoder pass_encoder) {
    ImGui_ImplWGPU_Data *bd = ImGui_ImplWGPU_GetBackendData();
    if (draw_data->CmdListsCount == 0) return;

    ImGui_ImplWGPU_SetupRenderState(draw_data, pass_encoder);

    // Will project scissor/clipping rectangles into framebuffer space.
    ImVec2 clip_off = draw_data->DisplayPos;
    ImVec2 clip_scale = draw_data->FramebufferScale;

    const float fb_width = draw_data->DisplaySize.x * clip_scale.x;
    const float fb_height = draw_data->DisplaySize.y * clip_scale.y;
    if (fb_width <= 0.0f || fb_height <= 0.0f) return;

    for (int n = 0; n < draw_data->CmdListsCount; n++) {
        const ImDrawList *cmd_list = draw_data->CmdLists[n];

        // Create and upload vertex/index buffers.
        size_t vbSize = (size_t)cmd_list->VtxBuffer.Size * sizeof(ImDrawVert);
        size_t ibSize = (size_t)cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx);
        WGPUBuffer vb = nullptr;
        WGPUBuffer ib = nullptr;

        WGPUBufferDescriptor vbDesc{};
        vbDesc.size = vbSize;
        vbDesc.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
        vbDesc.mappedAtCreation = false;
        vb = wgpuDeviceCreateBuffer(bd->wgpuDevice, &vbDesc);
        wgpuQueueWriteBuffer(bd->defaultQueue, vb, 0, cmd_list->VtxBuffer.Data, vbSize);

        WGPUBufferDescriptor ibDesc{};
        ibDesc.size = ibSize;
        ibDesc.usage = WGPUBufferUsage_Index | WGPUBufferUsage_CopyDst;
        ibDesc.mappedAtCreation = false;
        ib = wgpuDeviceCreateBuffer(bd->wgpuDevice, &ibDesc);
        wgpuQueueWriteBuffer(bd->defaultQueue, ib, 0, cmd_list->IdxBuffer.Data, ibSize);

        wgpuRenderPassEncoderSetVertexBuffer(pass_encoder, 0, vb, 0, vbSize);
        wgpuRenderPassEncoderSetIndexBuffer(pass_encoder, ib,
                                            sizeof(ImDrawIdx) == 2 ? WGPUIndexFormat_Uint16
                                                                   : WGPUIndexFormat_Uint32,
                                            0, ibSize);

        for (int cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; cmd_i++) {
            const ImDrawCmd *pcmd = &cmd_list->CmdBuffer[cmd_i];
            if (pcmd->UserCallback != NULL) {
                pcmd->UserCallback(cmd_list, pcmd);
            } else {
                // Project scissor/clipping rectangles into framebuffer space.
                ImVec2 clip_min((pcmd->ClipRect.x - clip_off.x) * clip_scale.x,
                                (pcmd->ClipRect.y - clip_off.y) * clip_scale.y);
                ImVec2 clip_max((pcmd->ClipRect.z - clip_off.x) * clip_scale.x,
                                (pcmd->ClipRect.w - clip_off.y) * clip_scale.y);
                if (clip_min.x < 0.0f) clip_min.x = 0.0f;
                if (clip_min.y < 0.0f) clip_min.y = 0.0f;
                if (clip_max.x > fb_width) clip_max.x = fb_width;
                if (clip_max.y > fb_height) clip_max.y = fb_height;
                if (clip_max.x <= clip_min.x || clip_max.y <= clip_min.y) continue;

                wgpuRenderPassEncoderSetScissorRect(pass_encoder,
                                                    (uint32_t)clip_min.x, (uint32_t)clip_min.y,
                                                    (uint32_t)(clip_max.x - clip_min.x),
                                                    (uint32_t)(clip_max.y - clip_min.y));

                // Bind font texture when the texture id changes.
                WGPUTextureView fontTextureView =
                    (WGPUTextureView)(intptr_t)pcmd->TextureId;
                if (fontTextureView != bd->fontTextureView) {
                    WGPUBindGroupEntry entries[3] = {};
                    entries[0].binding = 0;
                    entries[0].buffer = bd->uniformBuffer;
                    entries[0].size = bd->uniformBufferSize;
                    entries[1].binding = 1;
                    entries[1].sampler = bd->fontSampler;
                    entries[2].binding = 2;
                    entries[2].textureView = fontTextureView;
                    WGPUBindGroupDescriptor bgDesc{};
                    bgDesc.layout = bd->bindGroupLayout;
                    bgDesc.entryCount = 3;
                    bgDesc.entries = entries;
                    WGPUBindGroup bg = wgpuDeviceCreateBindGroup(bd->wgpuDevice, &bgDesc);
                    wgpuRenderPassEncoderSetBindGroup(pass_encoder, 0, bg, 0, nullptr);
                    wgpuBindGroupRelease(bg);
                }

                wgpuRenderPassEncoderDrawIndexed(pass_encoder, pcmd->ElemCount, 1,
                                                 pcmd->IdxOffset, (int32_t)pcmd->VtxOffset, 0);
            }
        }

        wgpuBufferRelease(vb);
        wgpuBufferRelease(ib);
    }
}

#endif  // EVENGINE_WEBGPU
