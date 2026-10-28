// Shared std140/std430 layouts for the GPU cull chain (stage 2).
// Keep in sync with GpuCullParams in src/modules/graphics/vulkan/GpuDriven.h
// and with gpudriven_tables.glsl (GpuMeshRecord / GpuInstance).

layout(set = 1, binding = 6, std140) uniform CullParams {
    mat4  viewProj;          // 0
    vec4  frustumPlanes[6];  // 64
    vec4  cameraPos;         // 160
    vec4  screen;            // 176: x=width, y=height, z=invW, w=invH
    vec4  clipNearFar;       // 192: x=near, y=far, z=projScaleY, w=doHZB
    vec4  hzbInfo;           // 208: x=maxMip, y=slotWordBase, z=mip0Width, w=mip0Height
    uvec4 counts;            // 224: x=instanceCount, y=bucketCount, z=maxBuckets, w=0
} params;

// HZB buffer layout (per frame slot):
//   words [slotWordBase + 0 .. 15]     = uint offsets[mip] (relative to slot data start)
//   words [slotWordBase + 16 + off]    = R32F depth per mip (max downsample)
const uint kHZBHeaderWords = 16u;

// Frustum plane convention: dot(plane.xyz, p) + plane.w > 0  -> inside.
// Gribb-Hartmann from a row-major-ish viewProj; caller passes normalized planes.
