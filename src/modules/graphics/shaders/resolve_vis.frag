#version 450

#extension GL_GOOGLE_include_directive : enable
#include "gpudriven_tables.glsl"
#include "tex_cell_bomb.glsl"
#include "parallax_map.glsl"
#include "tonemap.glsl"

struct Light3D {
    vec4 posRadius;
    vec4 color;
};

layout(set = 0, binding = 0, std140) uniform Frame {
    mat4 mvp;
    mat4 model;
    vec4 lightDirIntensity;
    vec4 lightColor;
    vec4 tint;
    vec4 cameraPos;
    vec4 ambient;
    Light3D lights[8];
    vec4 texBomb;
    vec4 parallax;
    mat4 view;
    vec4 clipInfo;
    vec4 cloud;
    vec4 cloudWind;
    vec4 bindlessEnv;
} ubo;

layout(set = 0, binding = 4, std140) uniform ShadowFrame {
    mat4 lightVP[3];
    vec4 splits;
    vec4 bias;
    vec4 cascadeBias;
    vec4 cascadeTexel;
} shadow;

layout(set = 0, binding = 5) uniform sampler2DArrayShadow shadowMap;

layout(set = 1, binding = 0) uniform sampler2D textures[1024];
layout(set = 1, binding = 1) uniform samplerCube cubemaps[64];
layout(set = 1, binding = 2, std430) readonly buffer Meshes {
    GpuMeshRecord meshes[];
};
layout(set = 1, binding = 3, std430) readonly buffer Materials {
    GpuMaterialRecord materials[];
};
layout(set = 1, binding = 4, std430) readonly buffer Instances {
    GpuInstance instances[];
};
layout(set = 1, binding = 15) uniform usampler2D visID;
layout(set = 1, binding = 16) uniform sampler2D visBary;
layout(set = 1, binding = 18, std430) readonly buffer VertexPositions {
    vec4 positions[];
};
layout(set = 1, binding = 19, std430) readonly buffer VertexNormals {
    vec4 normals[];
};
layout(set = 1, binding = 20, std430) readonly buffer VertexUvs {
    vec2 uvs[];
};
layout(set = 1, binding = 21, std430) readonly buffer IndexPool {
    uint indices[];
};
layout(set = 1, binding = 22, std430) readonly buffer VgPositions {
    float vgPos[];
};
layout(set = 1, binding = 23, std430) readonly buffer VgTriangles {
    uint vgTri[];
};
layout(set = 1, binding = 24, std430) readonly buffer VgClusters {
    uvec4 vgCl[];
};
layout(set = 1, binding = 25, std430) readonly buffer VgClusterAssets {
    uint vgClusterAssets[];
};
layout(set = 1, binding = 28, std430) readonly buffer VgAssetMaterials {
    uint vgAssetMaterials[];
};
layout(set = 1, binding = 29, std430) readonly buffer VgAssetModels {
    mat4 vgAssetModels[];
};

#include "pbr_shade.glsl"

layout(location = 0) out vec4 outColor;

void main() {
    ivec2 px = ivec2(gl_FragCoord.xy);
    uvec2 vis = texelFetch(visID, px, 0).rg;
    if (vis.x == kInvalidSlot)
        discard;

    vec3 bary = vec3(texelFetch(visBary, px, 0).rg, 0.0);
    bary.z = 1.0 - bary.x - bary.y;
    bary = clamp(bary, 0.0, 1.0);
    bary /= max(bary.x + bary.y + bary.z, 1e-6);

    vec4 world;
    vec3 worldN;
    vec2 uv;
    GpuMaterialRecord m;
    vec3 viewPos;
    if ((vis.x & 0x80000000u) != 0u) {
        // Virtual-geometry cluster: flat shading from the cluster stream.
        uint clusterId = vis.x & 0x7FFFFFFFu;
        uint asset = vgClusterAssets[clusterId];
        m = materials[vgAssetMaterials[asset]];
        mat4 model = vgAssetModels[asset];
        uint triBase = vis.y;
        uint i0 = vgTri[triBase + 0u];
        uint i1 = vgTri[triBase + 1u];
        uint i2 = vgTri[triBase + 2u];
        vec3 p0 = vec3(vgPos[3u * i0 + 0u], vgPos[3u * i0 + 1u], vgPos[3u * i0 + 2u]);
        vec3 p1 = vec3(vgPos[3u * i1 + 0u], vgPos[3u * i1 + 1u], vgPos[3u * i1 + 2u]);
        vec3 p2 = vec3(vgPos[3u * i2 + 0u], vgPos[3u * i2 + 1u], vgPos[3u * i2 + 2u]);
        vec4 w0 = model * vec4(p0, 1.0);
        vec4 w1 = model * vec4(p1, 1.0);
        vec4 w2 = model * vec4(p2, 1.0);
        world = w0 * bary.x + w1 * bary.y + w2 * bary.z;
        // Same face normal the vis pass computed (world-space edges).
        worldN = normalize(cross(w1.xyz - w0.xyz, w2.xyz - w0.xyz));
        uv = vec2(0.0);
        viewPos = (ubo.view * world).xyz;
    } else {
        uint inst = vis.x;
        GpuInstance gi = instances[inst];
        GpuMeshRecord mesh = meshes[gi.meshId];
        m = materials[gi.materialId];

        uint tri = vis.y;
        uint i0 = indices[tri + 0u];
        uint i1 = indices[tri + 1u];
        uint i2 = indices[tri + 2u];
        vec3 objPos = positions[mesh.vertexOffset + i0].xyz * bary.x +
                      positions[mesh.vertexOffset + i1].xyz * bary.y +
                      positions[mesh.vertexOffset + i2].xyz * bary.z;
        vec3 objNrm = normals[mesh.vertexOffset + i0].xyz * bary.x +
                      normals[mesh.vertexOffset + i1].xyz * bary.y +
                      normals[mesh.vertexOffset + i2].xyz * bary.z;
        uv = uvs[mesh.vertexOffset + i0].xy * bary.x +
             uvs[mesh.vertexOffset + i1].xy * bary.y +
             uvs[mesh.vertexOffset + i2].xy * bary.z;
        world = gi.model * vec4(objPos, 1.0);
        mat3 normalMat = transpose(inverse(mat3(gi.model)));
        worldN = normalize(normalMat * objNrm);
        viewPos = (ubo.view * world).xyz;
    }

    vec3 worldPos = world.xyz;
    vec3 N = worldN;
    vec3 V = normalize(ubo.cameraPos.xyz - worldPos);
    if (dot(N, V) < 0.0)
        N = -N;

    vec3 color = (vis.x & 0x80000000u) != 0u
                     ? shadeGpuDrivenPixelFlat(m, N, V, worldPos, viewPos)
                     : shadeGpuDrivenPixel(m, N, V, worldPos, viewPos, uv);

    // Write the same depth the forward pass would have: hair/transparent
    // geometry later in the scene pass depth-tests against the opaque depth.
    vec4 clip = ubo.mvp * vec4(worldPos, 1.0);
    gl_FragDepth = clip.z / max(clip.w, 1e-6);

    float nearZ = max(ubo.clipInfo.x, 1e-4);
    float farZ = max(ubo.clipInfo.y, nearZ + 1e-3);
    float viewZ = max(-viewPos.z, 0.0);
    float linear01 = clamp((viewZ - nearZ) / (farZ - nearZ), 0.0, 1.0);
    outColor = vec4(color, linear01);
}
