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

struct GpuVegetationVertexRecord {
    vec4 tangent;
    vec4 bitangent;
    vec4 factors0;
    vec4 factors1;
    vec4 deformation0;
    vec4 deformation1;
    vec4 deformation2;
};

struct GpuMaterialRecord {
    vec4 tint;
    vec4 pbr;        // x = metallic, y = roughness, z = receiveShadow, w = receiveLight
    vec4 texBomb;
    vec4 parallax;
    vec4 surface;    // x = alpha cutoff, y = surface mode (0 opaque, 1 masked, 2 transparent)
    uint textureSlots[4];  // xyzw = albedo / normal / height / env
    uint shadingModel;
    uint flags;        // bit2 = cylindrical camera-facing card
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
    vec4 color;
    vec4 terrainWave;
    vec4 terrainWaveTint;
};

const uint kInvalidSlot = 0xFFFFFFFFu;
