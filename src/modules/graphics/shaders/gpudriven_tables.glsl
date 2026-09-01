// Shared std430 layouts for the GPU-driven path. Keep in sync with
// src/modules/graphics/GpuDrivenTypes.h (mirrored one-to-one).

struct GpuMeshRecord {
    vec4 boundsCenterRadius;
    uint vertexOffset;
    uint vertexCount;
    uint indexOffset;
    uint indexCount;
    uint indexType;
    uint firstIndex;
    uint vertexBase;
    uint lodGroupId;
    uint pad0;
    uint pad1;
    uint pad2;
    uint pad3;
};

struct GpuMaterialRecord {
    vec4 tint;
    vec4 pbr;        // x = metallic, y = roughness, z = receiveShadow, w = receiveLight
    vec4 texBomb;
    vec4 parallax;
    uint textureSlots[4];  // xyzw = albedo / normal / height / env
    uint shadingModel;
    uint flags;
    uint pad0;
    uint pad1;
};

struct GpuInstance {
    mat4 model;
    uint meshId;
    uint materialId;
    uint flags;
    uint lodGroupId;
    uvec4 reflectionProbeSlots;
    vec4 reflectionProbeCenter[2];
    vec4 reflectionProbeExtent[2];
};

const uint kInvalidSlot = 0xFFFFFFFFu;
