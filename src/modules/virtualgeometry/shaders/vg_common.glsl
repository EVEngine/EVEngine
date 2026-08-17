#ifndef VG_COMMON_GLSL
#define VG_COMMON_GLSL

// Shared layout for the virtual-geometry compute passes. Every pass is a GLSL
// 4.50 compute shader bound through the gpgpu backend (set 0, storage buffers).
// Bindings must stay in sync with virtualgeometry/vulkan/VulkanVirtualGeometry.cpp.

#define VG_BIND_POSITIONS 0u
#define VG_BIND_TRIANGLES 1u
#define VG_BIND_CLUSTERS  2u
#define VG_BIND_VISIBLE   3u
#define VG_BIND_PIXELS    4u
#define VG_BIND_UNIFORMS  5u
#define VG_BIND_STATS     6u

// Max triangles any cluster may hold. Must match
// VirtualGeometryBuilder::Options::maxTrianglesPerCluster.
#define VG_MAX_TRI_PER_CLUSTER 124u

// Pack the depth (high 16 bits, 0 = near) and cluster id (low 16 bits) into a
// single uint so a single atomicMin yields a consistent (depth, cluster) pair.
uint vg_pack(uint depthBits, uint clusterId) {
    return (min(depthBits, 0xFFFFu) << 16) | (clusterId & 0xFFFFu);
}
uint vg_depthOf(uint packed)  { return packed >> 16; }
uint vg_clusterOf(uint packed){ return packed & 0xFFFFu; }

struct VgUniforms {
    mat4 viewProj;   // [0..15]
    mat4 model;      // [16..31]
    vec4 cameraPos;  // [32..35] xyz = camera, w = 0
    vec4 params;     // [36..39] x=viewW, y=viewH, z=projScale, w=errorPx
    vec4 frustum[6]; // [40..63] world-space planes (xyz normal, w distance)
    vec4 misc;       // [64..67] x = clusterCount
};

layout(set = 0, binding = VG_BIND_POSITIONS, std430) readonly buffer VgPositions   { float vgPos[]; };
layout(set = 0, binding = VG_BIND_TRIANGLES, std430) readonly buffer VgTriangles   { uint  vgTri[]; };
layout(set = 0, binding = VG_BIND_CLUSTERS,  std430) readonly buffer VgClusters    { uvec4 vgCl[]; };
layout(set = 0, binding = VG_BIND_VISIBLE,   std430) buffer       VgVisible       { uint vgCounter; uint vgVis[]; };
layout(set = 0, binding = VG_BIND_PIXELS,    std430) buffer       VgPixels        { uint vgPix[]; };
layout(set = 0, binding = VG_BIND_UNIFORMS,  std430) readonly buffer VgUniformBlock{ VgUniforms vgU; };
layout(set = 0, binding = VG_BIND_STATS,     std430) buffer       VgStats         { uvec4 vgStat[]; };

vec3 vg_clusterCenter(uint cid) {
    uint b = cid * 4u;
    return vec3(uintBitsToFloat(vgCl[b + 0u].x),
                uintBitsToFloat(vgCl[b + 0u].y),
                uintBitsToFloat(vgCl[b + 0u].z));
}
float vg_clusterRadius(uint cid) { return uintBitsToFloat(vgCl[cid * 4u + 0u].w); }
uint  vg_clusterTriStart(uint cid) { return vgCl[cid * 4u + 1u].x; }
uint  vg_clusterTriCount(uint cid) { return vgCl[cid * 4u + 1u].y; }
uint  vg_clusterParent(uint cid)   { return vgCl[cid * 4u + 1u].w; }
float vg_clusterErrorR(uint cid)   { return uintBitsToFloat(vgCl[cid * 4u + 2u].x); }
float vg_clusterErrorRS(uint cid)  { return uintBitsToFloat(vgCl[cid * 4u + 2u].y); }
uint  vg_clusterChildCount(uint cid) { return vgCl[cid * 4u + 2u].z; }

#endif
