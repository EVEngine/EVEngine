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

// Header-only Assimp mesh types for newMeshFromAssimp. The Assimp library is
// not linked on Emscripten; only these POD headers are required here.
#include <assimp/matrix3x3.h>
#include <assimp/matrix4x4.h>
#include <assimp/mesh.h>
#include <assimp/vector3.h>

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

Mesh* Graphics::newMeshFromArrays(const float* posXYZ, const float* nrmXYZ, const float* uvST, int vertexCount,
                                  const uint32_t* indices, int indexCount) {
    return newMeshFromArraysColored(posXYZ, nrmXYZ, uvST, nullptr, vertexCount, indices, indexCount);
}

Mesh* Graphics::newMeshFromArraysColored(const float* posXYZ, const float* nrmXYZ, const float* uvST,
                                         const float* colorRGBA, int vertexCount, const uint32_t* indices,
                                         int indexCount) {
    if (vertexCount <= 0 || !posXYZ) throw Exception("newMeshFromArrays: invalid vertex data");
    if (indexCount < 0 || (indexCount > 0 && (!indices || indexCount % 3 != 0)))
        throw Exception("newMeshFromArrays: invalid index data");
    for (int i = 0; i < indexCount; ++i) {
        if (indices[i] >= uint32_t(vertexCount)) throw Exception("newMeshFromArrays: index out of range");
    }

    std::vector<MeshVertex> verts(static_cast<size_t>(vertexCount));
    for (int i = 0; i < vertexCount; ++i) {
        auto& v = verts[static_cast<size_t>(i)];
        v.pos   = {posXYZ[i * 3 + 0], posXYZ[i * 3 + 1], posXYZ[i * 3 + 2]};
        if (nrmXYZ) {
            v.normal = {nrmXYZ[i * 3 + 0], nrmXYZ[i * 3 + 1], nrmXYZ[i * 3 + 2]};
        } else {
            v.normal = {0.f, 0.f, 1.f};
        }
        if (uvST) {
            v.uv = {uvST[i * 2 + 0], uvST[i * 2 + 1]};
        } else {
            v.uv = {0.f, 0.f};
        }
        if (colorRGBA) v.color = {colorRGBA[i * 4], colorRGBA[i * 4 + 1], colorRGBA[i * 4 + 2], colorRGBA[i * 4 + 3]};
    }

    auto gpu          = std::make_unique<GpuMesh>();
    gpu->vertexCount  = uint32_t(vertexCount);
    gpu->indexCount   = indexCount > 0 ? uint32_t(indexCount) : 0;
    gpu->vertexStride = sizeof(MeshVertex);
    gpu->cpuVertices  = verts;

    WGPUBufferDescriptor vbd{};
    vbd.label            = sv("eve_mesh_vb");
    vbd.size             = verts.size() * sizeof(MeshVertex);
    vbd.usage            = WGPUBufferUsage_CopyDst | WGPUBufferUsage_Vertex | WGPUBufferUsage_Storage;
    vbd.mappedAtCreation = false;
    gpu->vertexBuffer    = device.CreateBuffer(reinterpret_cast<const wgpu::BufferDescriptor*>(&vbd));
    queue.WriteBuffer(gpu->vertexBuffer, 0, verts.data(), vbd.size);
    gpu->vertexCapacity = vbd.size;

    if (indexCount > 0) {
        // 16-bit index format halves index memory for meshes with <= 65535 verts.
        if (vertexCount <= 65535) {
            std::vector<uint16_t> idx16;
            idx16.reserve(indexCount);
            for (int i = 0; i < indexCount; ++i) idx16.push_back(uint16_t(indices[i]));
            WGPUBufferDescriptor ibd{};
            ibd.label = sv("eve_mesh_ib");
            if (idx16.size() % 2 != 0) idx16.push_back(0);  // WriteBuffer size must align to 4.
            ibd.size             = uint64_t(idx16.size()) * sizeof(uint16_t);
            ibd.usage            = WGPUBufferUsage_CopyDst | WGPUBufferUsage_Index | WGPUBufferUsage_Storage;
            ibd.mappedAtCreation = false;
            gpu->indexBuffer     = device.CreateBuffer(reinterpret_cast<const wgpu::BufferDescriptor*>(&ibd));
            queue.WriteBuffer(gpu->indexBuffer, 0, idx16.data(), ibd.size);
            gpu->indexFormat   = wgpu::IndexFormat::Uint16;
            gpu->indexCapacity = ibd.size;
        } else {
            WGPUBufferDescriptor ibd{};
            ibd.label            = sv("eve_mesh_ib");
            ibd.size             = uint64_t(indexCount) * sizeof(uint32_t);
            ibd.usage            = WGPUBufferUsage_CopyDst | WGPUBufferUsage_Index | WGPUBufferUsage_Storage;
            ibd.mappedAtCreation = false;
            gpu->indexBuffer     = device.CreateBuffer(reinterpret_cast<const wgpu::BufferDescriptor*>(&ibd));
            queue.WriteBuffer(gpu->indexBuffer, 0, indices, ibd.size);
            gpu->indexFormat   = wgpu::IndexFormat::Uint32;
            gpu->indexCapacity = ibd.size;
        }
    }

    auto* mesh           = new Mesh();
    mesh->indexCount     = indexCount;
    mesh->gpuVertexCount = vertexCount;
    mesh->gpuHandle      = gpu.get();
    mesh->computeBounds(posXYZ, vertexCount);
    ownedGpuMeshes.push_back(std::move(gpu));
    ownedMeshes.push_back(std::unique_ptr<Mesh>(mesh));
    return mesh;
}

eve::Result<eve::graphics::MeshBackendDescriptor> Graphics::describeMesh(Mesh* mesh) const {
    if (!mesh || !mesh->gpuHandle)
        return eve::Result<eve::graphics::MeshBackendDescriptor>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::NotFound, "mesh has no backend upload", {}, {}, "graphics.mesh.describe"));
    const auto* gpu = static_cast<const GpuMesh*>(mesh->gpuHandle);
    const auto  owned =
        std::find_if(ownedGpuMeshes.begin(), ownedGpuMeshes.end(),
                     [gpu](const std::unique_ptr<GpuMesh>& candidate) { return candidate.get() == gpu; });
    if (owned == ownedGpuMeshes.end())
        return eve::Result<eve::graphics::MeshBackendDescriptor>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::NotFound, "mesh is not owned by this graphics", {}, {}, "graphics.mesh.describe"));
    return eve::Result<eve::graphics::MeshBackendDescriptor>::success(eve::graphics::MeshBackendDescriptor{
        gpu->vertexCount, gpu->indexCount, gpu->vertexStride, gpu->indexFormat == wgpu::IndexFormat::Uint16 ? 2u : 4u});
}

Mesh* Graphics::newMeshFromAssimp(const ::aiMesh& mesh) { return newMeshFromAssimp(mesh, glm::mat4(1.f)); }

Mesh* Graphics::newMeshFromAssimp(const ::aiMesh& mesh, const glm::mat4& worldTransform) {
    // glm is column-major; Assimp aiMatrix4x4 is row-major.
    const aiMatrix4x4 world(worldTransform[0][0], worldTransform[1][0], worldTransform[2][0], worldTransform[3][0],
                            worldTransform[0][1], worldTransform[1][1], worldTransform[2][1], worldTransform[3][1],
                            worldTransform[0][2], worldTransform[1][2], worldTransform[2][2], worldTransform[3][2],
                            worldTransform[0][3], worldTransform[1][3], worldTransform[2][3], worldTransform[3][3]);

    std::vector<float>    pos, nrm, uv;
    std::vector<uint32_t> idx;
    pos.reserve(mesh.mNumVertices * 3);
    nrm.reserve(mesh.mNumVertices * 3);
    uv.reserve(mesh.mNumVertices * 2);
    idx.reserve(mesh.mNumFaces * 3);

    for (unsigned i = 0; i < mesh.mNumVertices; ++i) {
        aiVector3D p = world * mesh.mVertices[i];
        pos.push_back(p.x);
        pos.push_back(p.y);
        pos.push_back(p.z);
        if (mesh.mNormals) {
            aiMatrix3x3 rot(world);
            aiVector3D  n = rot * mesh.mNormals[i];
            n.Normalize();
            nrm.push_back(n.x);
            nrm.push_back(n.y);
            nrm.push_back(n.z);
        } else {
            nrm.push_back(0.f);
            nrm.push_back(0.f);
            nrm.push_back(1.f);
        }
        if (mesh.mTextureCoords[0]) {
            uv.push_back(mesh.mTextureCoords[0][i].x);
            uv.push_back(mesh.mTextureCoords[0][i].y);
        } else {
            uv.push_back(0.f);
            uv.push_back(0.f);
        }
    }
    for (unsigned f = 0; f < mesh.mNumFaces; ++f) {
        for (unsigned v = 0; v < mesh.mFaces[f].mNumIndices; ++v) idx.push_back(mesh.mFaces[f].mIndices[v]);
    }
    Mesh* m = newMeshFromArrays(pos.data(), nrm.data(), uv.data(), int(mesh.mNumVertices), idx.data(), int(idx.size()));
    if (m) m->captureImportedAttributes(mesh);
    if (m && mesh.mNumAnimMeshes > 0) {
        m->initMorphBase(int(mesh.mNumVertices), pos.data(), nrm.data(), uv.data());
        for (unsigned a = 0; a < mesh.mNumAnimMeshes; ++a) {
            const aiAnimMesh*  am = mesh.mAnimMeshes[a];
            std::vector<float> absPos(am->mNumVertices * 3);
            for (unsigned i = 0; i < am->mNumVertices; ++i) {
                absPos[i * 3 + 0] = am->mVertices[i].x;
                absPos[i * 3 + 1] = am->mVertices[i].y;
                absPos[i * 3 + 2] = am->mVertices[i].z;
            }
            std::string name = am->mName.C_Str();
            if (name.empty()) name = "morph" + std::to_string(a);
            m->addMorphTargetAbsolute(name, absPos.data());
            if (am->mNormals) {
                std::vector<float> deltas(am->mNumVertices * 3u);
                for (unsigned i = 0; i < am->mNumVertices; ++i) {
                    deltas[i * 3]     = am->mNormals[i].x - nrm[i * 3];
                    deltas[i * 3 + 1] = am->mNormals[i].y - nrm[i * 3 + 1];
                    deltas[i * 3 + 2] = am->mNormals[i].z - nrm[i * 3 + 2];
                }
                auto normals = m->setMorphNormalDeltas(name, deltas);
                if (!normals.ok()) throw Exception("Invalid imported morph normals: %s", name.c_str());
            }
        }
    }
    return m;
}

bool Graphics::bakeMeshMorph(Mesh* mesh) {
    if (!mesh || !mesh->hasMorphData() || !mesh->isMorphDirty()) return false;
    auto* gpu = static_cast<GpuMesh*>(mesh->gpuHandle);
    if (!gpu || !gpu->vertexBuffer) return false;
    std::vector<float> pos, nrm;
    mesh->computeMorphedPositions(pos, nrm);
    if (pos.empty()) return false;
    mesh->computeBounds(pos.data(), mesh->getVertexCount());
    auto& verts = gpu->cpuVertices;
    if (verts.size() != static_cast<size_t>(mesh->getVertexCount())) return false;
    const auto& uvs = mesh->baseUv();
    for (int i = 0; i < mesh->getVertexCount(); ++i) {
        auto& v = verts[static_cast<size_t>(i)];
        v.pos   = {pos[i * 3 + 0], pos[i * 3 + 1], pos[i * 3 + 2]};
        if (nrm.size() >= size_t((i + 1) * 3)) {
            v.normal = {nrm[i * 3 + 0], nrm[i * 3 + 1], nrm[i * 3 + 2]};
        } else {
            v.normal = {0.f, 0.f, 1.f};
        }
        if (uvs.size() >= size_t((i + 1) * 2)) {
            v.uv = {uvs[i * 2 + 0], uvs[i * 2 + 1]};
        } else {
            v.uv = {0.f, 0.f};
        }
    }
    queue.WriteBuffer(gpu->vertexBuffer, 0, verts.data(), verts.size() * sizeof(MeshVertex));
    mesh->markMorphClean();
    return true;
}


bool Graphics::setMeshSkinningData(Mesh* mesh, const uint16_t* joints4, const float* weights4, int vertexCount) {
    if (!mesh || !mesh->gpuHandle || !joints4 || !weights4) return false;
    auto* gpu = static_cast<GpuMesh*>(mesh->gpuHandle);
    if (vertexCount <= 0 || gpu->cpuVertices.size() != static_cast<size_t>(vertexCount)) return false;
    for (int i = 0; i < vertexCount; ++i) {
        const size_t base = static_cast<size_t>(i) * 4u;
        auto&        v    = gpu->cpuVertices[static_cast<size_t>(i)];
        v.joints          = glm::u16vec4(joints4[base], joints4[base + 1], joints4[base + 2], joints4[base + 3]);
        v.weights         = glm::vec4(weights4[base], weights4[base + 1], weights4[base + 2], weights4[base + 3]);
    }
    queue.WriteBuffer(gpu->vertexBuffer, 0, gpu->cpuVertices.data(), gpu->cpuVertices.size() * sizeof(MeshVertex));
    mesh->markGpuSkinned(true);
    return true;
}

bool Graphics::releaseMesh(Mesh* mesh) {
    if (!mesh || !mesh->gpuHandle) return false;

    auto* gpu   = static_cast<GpuMesh*>(mesh->gpuHandle);
    auto  gpuIt = std::find_if(ownedGpuMeshes.begin(), ownedGpuMeshes.end(),
                               [&](const std::unique_ptr<GpuMesh>& g) { return g.get() == gpu; });
    if (gpuIt == ownedGpuMeshes.end()) return false;

    auto meshIt = std::find_if(ownedMeshes.begin(), ownedMeshes.end(),
                               [&](const std::unique_ptr<Mesh>& m) { return m.get() == mesh; });
    if (meshIt == ownedMeshes.end()) return false;

    mesh->gpuHandle = nullptr;
    ownedGpuMeshes.erase(gpuIt);
    // Transfer the CPU facade to the caller instead of destroying it.
    (void)meshIt->release();
    ownedMeshes.erase(meshIt);
    return true;
}

Mesh* Graphics::newMeshSphere(int slices, int stacks) {
    std::vector<float>    pos, nrm, uv;
    std::vector<uint32_t> idx;
    for (int y = 0; y <= stacks; ++y) {
        for (int x = 0; x <= slices; ++x) {
            float u     = float(x) / float(slices);
            float v     = float(y) / float(stacks);
            float theta = u * 2.f * glm::pi<float>();
            float phi   = v * glm::pi<float>();
            float sx    = std::sin(phi) * std::cos(theta);
            float sy    = std::cos(phi);
            float sz    = std::sin(phi) * std::sin(theta);
            pos.push_back(sx);
            pos.push_back(sy);
            pos.push_back(sz);
            nrm.push_back(sx);
            nrm.push_back(sy);
            nrm.push_back(sz);
            uv.push_back(u);
            uv.push_back(v);
        }
    }
    for (int y = 0; y < stacks; ++y) {
        for (int x = 0; x < slices; ++x) {
            int a = y * (slices + 1) + x;
            int b = a + slices + 1;
            idx.push_back(a);
            idx.push_back(a + 1);
            idx.push_back(b);
            idx.push_back(b);
            idx.push_back(a + 1);
            idx.push_back(b + 1);
        }
    }
    return newMeshFromArrays(pos.data(), nrm.data(), uv.data(), int(pos.size() / 3), idx.data(), int(idx.size()));
}

Mesh* Graphics::newMeshCylinder(int slices, int stacks, bool caps) {
    std::vector<float>    pos, nrm, uv;
    std::vector<uint32_t> idx;
    for (int y = 0; y <= stacks; ++y) {
        for (int x = 0; x <= slices; ++x) {
            float u     = float(x) / float(slices);
            float v     = float(y) / float(stacks);
            float theta = u * 2.f * glm::pi<float>();
            pos.push_back(std::cos(theta));
            pos.push_back(v * 2.f - 1.f);
            pos.push_back(std::sin(theta));
            nrm.push_back(std::cos(theta));
            nrm.push_back(0.f);
            nrm.push_back(std::sin(theta));
            uv.push_back(u);
            uv.push_back(v);
        }
    }
    for (int y = 0; y < stacks; ++y) {
        for (int x = 0; x < slices; ++x) {
            int a = y * (slices + 1) + x;
            int b = a + slices + 1;
            idx.push_back(a);
            idx.push_back(b);
            idx.push_back(a + 1);
            idx.push_back(b);
            idx.push_back(b + 1);
            idx.push_back(a + 1);
        }
    }
    if (caps) {
        int base = int(pos.size() / 3);
        pos.push_back(0.f);
        pos.push_back(-1.f);
        pos.push_back(0.f);
        nrm.push_back(0.f);
        nrm.push_back(-1.f);
        nrm.push_back(0.f);
        uv.push_back(0.5f);
        uv.push_back(0.5f);
        for (int x = 0; x < slices; ++x) {
            float theta0 = float(x) / float(slices) * 2.f * glm::pi<float>();
            float theta1 = float(x + 1) / float(slices) * 2.f * glm::pi<float>();
            int   i0     = int(pos.size() / 3);
            pos.push_back(std::cos(theta0));
            pos.push_back(-1.f);
            pos.push_back(std::sin(theta0));
            nrm.push_back(0.f);
            nrm.push_back(-1.f);
            nrm.push_back(0.f);
            uv.push_back(0.5f + 0.5f * std::cos(theta0));
            uv.push_back(0.5f + 0.5f * std::sin(theta0));
            int i1 = int(pos.size() / 3);
            pos.push_back(std::cos(theta1));
            pos.push_back(-1.f);
            pos.push_back(std::sin(theta1));
            nrm.push_back(0.f);
            nrm.push_back(-1.f);
            nrm.push_back(0.f);
            uv.push_back(0.5f + 0.5f * std::cos(theta1));
            uv.push_back(0.5f + 0.5f * std::sin(theta1));
            idx.push_back(base);
            idx.push_back(i0);
            idx.push_back(i1);
        }
        base = int(pos.size() / 3);
        pos.push_back(0.f);
        pos.push_back(1.f);
        pos.push_back(0.f);
        nrm.push_back(0.f);
        nrm.push_back(1.f);
        nrm.push_back(0.f);
        uv.push_back(0.5f);
        uv.push_back(0.5f);
        for (int x = 0; x < slices; ++x) {
            float theta0 = float(x) / float(slices) * 2.f * glm::pi<float>();
            float theta1 = float(x + 1) / float(slices) * 2.f * glm::pi<float>();
            int   i0     = int(pos.size() / 3);
            pos.push_back(std::cos(theta0));
            pos.push_back(1.f);
            pos.push_back(std::sin(theta0));
            nrm.push_back(0.f);
            nrm.push_back(1.f);
            nrm.push_back(0.f);
            uv.push_back(0.5f + 0.5f * std::cos(theta0));
            uv.push_back(0.5f + 0.5f * std::sin(theta0));
            int i1 = int(pos.size() / 3);
            pos.push_back(std::cos(theta1));
            pos.push_back(1.f);
            pos.push_back(std::sin(theta1));
            nrm.push_back(0.f);
            nrm.push_back(1.f);
            nrm.push_back(0.f);
            uv.push_back(0.5f + 0.5f * std::cos(theta1));
            uv.push_back(0.5f + 0.5f * std::sin(theta1));
            idx.push_back(base);
            idx.push_back(i1);
            idx.push_back(i0);
        }
    }
    return newMeshFromArrays(pos.data(), nrm.data(), uv.data(), int(pos.size() / 3), idx.data(), int(idx.size()));
}

}  // namespace eve::graphics::webgpu
