#include <algorithm>
#include <cstring>
#include "common/Exception.h"
#include "graphics/webgpu/BindGroupLayoutBuilder.h"
#include "graphics/webgpu/Graphics.h"
#include "graphics/webgpu/PipelineBuilder.h"
#include "graphics/webgpu/wgsl_shaders.h"

namespace eve::graphics::webgpu {
void Graphics::createVoxelPipelines() {
    voxelSetLayout      = makeVoxelBindGroupLayout();
    voxelPipelineLayout = makeVoxelPipelineLayout();

    WGPUVertexAttribute attrs[1] = {};
    attrs[0].format              = WGPUVertexFormat_Float32x2;  // corner
    attrs[0].offset              = 0;
    attrs[0].shaderLocation      = 0;
    WGPUVertexBufferLayout cornerVb{};
    cornerVb.arrayStride    = 8;
    cornerVb.stepMode       = WGPUVertexStepMode_Vertex;
    cornerVb.attributeCount = 1;
    cornerVb.attributes     = attrs;

    WGPUVertexAttribute packedAttr{};
    packedAttr.format         = WGPUVertexFormat_Uint32;  // packed rect word
    packedAttr.offset         = 0;
    packedAttr.shaderLocation = 1;
    WGPUVertexBufferLayout packedVb{};
    packedVb.arrayStride    = 4;
    packedVb.stepMode       = WGPUVertexStepMode_Instance;
    packedVb.attributeCount = 1;
    packedVb.attributes     = &packedAttr;

    WGPUVertexAttribute aoAttr{};
    aoAttr.format         = WGPUVertexFormat_Uint32;  // 2 bits per corner
    aoAttr.offset         = 0;
    aoAttr.shaderLocation = 2;
    WGPUVertexBufferLayout aoVb{};
    aoVb.arrayStride    = 4;
    aoVb.stepMode       = WGPUVertexStepMode_Instance;
    aoVb.attributeCount = 1;
    aoVb.attributes     = &aoAttr;

    WGPUVertexBufferLayout vbs[3] = {cornerVb, packedVb, aoVb};

    WGPUDepthStencilState ds{};
    ds.format            = WGPUTextureFormat_Depth32Float;
    ds.depthWriteEnabled = WGPUOptionalBool_True;
    ds.depthCompare      = WGPUCompareFunction_Less;
    ds.stencilReadMask   = 0;
    ds.stencilWriteMask  = 0;

    WGPUColorTargetState target{};
    target.format    = sceneColorFormat;
    target.blend     = nullptr;
    target.writeMask = WGPUColorWriteMask_All;

    WGPURenderPipelineDescriptor pd{};
    pd.label                      = sv("eve_voxel");
    pd.layout                     = voxelPipelineLayout.Get();
    wgpu::ShaderModule vertModule = makeWgslModule(device, kVoxelRectVertWgsl);
    wgpu::ShaderModule fragModule = makeWgslModule(device, kVoxelRectFragWgsl);
    pd.vertex.module              = vertModule.Get();
    pd.vertex.entryPoint          = sv("vs_main");
    pd.vertex.bufferCount         = 3;
    pd.vertex.buffers             = vbs;
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
    pd.depthStencil               = &ds;
    pd.multisample.count          = sceneColorSamples;
    // Zero-init would leave mask=0, which discards every fragment
    // (sampleMask=0). The WebGPU default is 0xFFFFFFFF (all samples).
    pd.multisample.mask = 0xFFFFFFFFu;
    voxelRectPipeline   = device.CreateRenderPipeline(reinterpret_cast<const wgpu::RenderPipelineDescriptor *>(&pd));

    // Unit quad for instanced voxel faces (2 triangles, corner + packed uv slot).
    float                quad[8] = {0, 0, 1, 0, 1, 1, 0, 1};
    WGPUBufferDescriptor bd{};
    bd.label            = sv("eve_voxel_quad");
    bd.size             = sizeof(quad);
    bd.usage            = WGPUBufferUsage_CopyDst | WGPUBufferUsage_Vertex;
    bd.mappedAtCreation = false;
    voxelUnitQuadVerts  = device.CreateBuffer(reinterpret_cast<const wgpu::BufferDescriptor *>(&bd));
    queue.WriteBuffer(voxelUnitQuadVerts, 0, quad, sizeof(quad));

    uint32_t             indices[6] = {0, 1, 2, 2, 3, 0};
    WGPUBufferDescriptor ibd{};
    ibd.label            = sv("eve_voxel_quad_idx");
    ibd.size             = sizeof(indices);
    ibd.usage            = WGPUBufferUsage_CopyDst | WGPUBufferUsage_Index;
    ibd.mappedAtCreation = false;
    voxelUnitQuadIndices = device.CreateBuffer(reinterpret_cast<const wgpu::BufferDescriptor *>(&ibd));
    queue.WriteBuffer(voxelUnitQuadIndices, 0, indices, sizeof(indices));
}

void Graphics::drawVoxelFaceInstances(const uint32_t *packed, int count, float originX, float originY, float originZ,
                                      const std::string &faceDir, Texture *atlas, int tilesPerRow, const uint32_t *ao) {
    if (!device || !frame3DStarted || count <= 0 || !packed) return;
    frameHad3DThisFrame = true;
    frameHad3D          = true;

    int face = -1;
    if (faceDir == "posX" || faceDir == "+x")
        face = 0;
    else if (faceDir == "negX" || faceDir == "-x")
        face = 1;
    else if (faceDir == "posY" || faceDir == "+y")
        face = 2;
    else if (faceDir == "negY" || faceDir == "-y")
        face = 3;
    else if (faceDir == "posZ" || faceDir == "+z")
        face = 4;
    else if (faceDir == "negZ" || faceDir == "-z")
        face = 5;
    else
        throw Exception("drawVoxelFaceInstances: unknown faceDir '%s'", faceDir.c_str());

    VoxelDraw d;
    d.count                = uint32_t(count);
    d.atlas                = gpuForTextureOrWhite(atlas);
    d.viewProj             = mesh3dViewProj;
    d.chunkOrigin          = glm::vec4(originX, originY, originZ, float(face));
    d.atlasInfo            = glm::vec4(float(std::max(1, tilesPerRow)), 0.f, 0.f, 0.f);
    d.tint                 = glm::vec4(1.f);
    d.instanceBufferOffset = 0;
    d.pushUboOffset        = 0;

    // Upload packed instances + the parallel AO word (2 bits per corner) into
    // the per-frame instance arenas.
    auto &arena       = voxelInstanceArena;
    auto &aoArena     = voxelAoArena;
    auto  ensureArena = [&](VertexArena &a) {
        if (!a.buffer) {
            WGPUBufferDescriptor bd{};
            bd.label            = sv("eve_voxel_instances");
            bd.size             = 1u << 20;
            bd.usage            = WGPUBufferUsage_CopyDst | WGPUBufferUsage_Vertex;
            bd.mappedAtCreation = false;
            a.buffer            = device.CreateBuffer(reinterpret_cast<const wgpu::BufferDescriptor *>(&bd));
            a.capacity          = 1u << 20;
        }
        uint64_t need = uint64_t(count) * 4;
        if (a.used + need > a.capacity) {
            uint64_t             cap = a.capacity * 2;
            WGPUBufferDescriptor bd{};
            bd.label            = sv("eve_voxel_instances");
            bd.size             = cap;
            bd.usage            = WGPUBufferUsage_CopyDst | WGPUBufferUsage_Vertex;
            bd.mappedAtCreation = false;
            a.buffer            = device.CreateBuffer(reinterpret_cast<const wgpu::BufferDescriptor *>(&bd));
            a.capacity          = cap;
            a.used              = 0;
        }
    };
    ensureArena(arena);
    ensureArena(aoArena);
    const uint32_t        defaultAO = 0xFFu;  // all four corners AO=3 (full bright)
    std::vector<uint32_t> aoDefaults;
    if (!ao) {
        aoDefaults.assign(size_t(count), defaultAO);
        ao = aoDefaults.data();
    }
    const uint64_t need    = uint64_t(count) * 4;
    d.instanceBufferOffset = static_cast<uint32_t>(arena.used);
    queue.WriteBuffer(arena.buffer, arena.used, packed, need);
    arena.used += need;
    d.aoBufferOffset = static_cast<uint32_t>(aoArena.used);
    queue.WriteBuffer(aoArena.buffer, aoArena.used, ao, need);
    aoArena.used += need;
    voxelDraws.push_back(d);
}

void Graphics::flushVoxelDraws(wgpu::RenderPassEncoder pass, WGPUTextureFormat format) {
    if (voxelDraws.empty()) return;
    if (!voxelRectPipeline) createVoxelPipelines();
    auto &uboArena = currentUboArena();
    ensureUboArena(uboArena, uboArena.used + voxelDraws.size() * 512);
    pass.SetPipeline(voxelRectPipeline);

    for (auto &d : voxelDraws) {
        uint32_t offset = uboArena.alloc(256, 256);
        struct VoxelPC {
            glm::mat4 viewProj;
            glm::vec4 chunkOrigin;
            glm::vec4 atlasInfo;
            glm::vec4 tint;
        } pc;
        pc.viewProj    = d.viewProj;
        pc.chunkOrigin = d.chunkOrigin;
        pc.atlasInfo   = d.atlasInfo;
        pc.tint        = d.tint;
        queue.WriteBuffer(uboArena.buffer, offset, &pc, sizeof(pc));

        WGPUBindGroupEntry entries[3]{};
        entries[0].binding     = 0;
        entries[0].buffer      = uboArena.buffer.Get();
        entries[0].size        = sizeof(pc);
        entries[1].binding     = 1;
        entries[1].textureView = d.atlas->view.Get();
        entries[2].binding     = 2;
        entries[2].sampler     = d.atlas->sampler.Get();
        WGPUBindGroupDescriptor bgd{};
        bgd.layout                 = voxelSetLayout.Get();
        bgd.entryCount             = 3;
        bgd.entries                = entries;
        wgpu::BindGroup bg         = device.CreateBindGroup(reinterpret_cast<const wgpu::BindGroupDescriptor *>(&bgd));
        uint32_t        offsets[1] = {offset};
        pass.SetBindGroup(0, bg, 1, offsets);

        pass.SetVertexBuffer(0, voxelUnitQuadVerts, 0, 32);
        pass.SetVertexBuffer(1, voxelInstanceArena.buffer, d.instanceBufferOffset, uint64_t(d.count) * 4);
        pass.SetVertexBuffer(2, voxelAoArena.buffer, d.aoBufferOffset, uint64_t(d.count) * 4);
        pass.SetIndexBuffer(voxelUnitQuadIndices, wgpu::IndexFormat::Uint32, 0, 24);
        pass.DrawIndexed(6, d.count, 0, 0, 0);
    }
    voxelDraws.clear();
}

}  // namespace eve::graphics::webgpu
