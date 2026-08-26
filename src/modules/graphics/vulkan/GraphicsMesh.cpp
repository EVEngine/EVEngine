// Vulkan backend implementation — mesh creation and drawing.
//
// Re-split from the merged dev single-TU Graphics.cpp (pure move;
// dev changes preserved). Shared helpers live in GraphicsInternal.h.

#include "graphics/vulkan/Graphics.h"
#include "graphics/vulkan/Canvas.h"
#include "graphics/Light.h"
#include "graphics/AntiAliasing.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>
#if !defined(_WIN32)
#include <unistd.h>
#endif

#include "common/Exception.h"
#include "common/StartupTiming.h"
#include "common/config.h"
#include "filesystem/Filesystem.h"
#include "image/Image.h"
#include "image/ImageData.h"
#include "zeroerr/assert.h"

#include <memory>


#include <assimp/mesh.h>
#include <assimp/matrix3x3.h>
#include <assimp/matrix4x4.h>
#include <assimp/vector3.h>
#include <glm/gtc/matrix_transform.hpp>

#include "graphics/vulkan/GraphicsInternal.h"

namespace eve::graphics::vulkan {

// --- Mesh creation and drawing ------------------------------------------------

Mesh *Graphics::newMeshFromAssimp(const ::aiMesh &mesh) {
    ASSERT(initialized);
    if (!initialized) throw Exception("newMeshFromAssimp: graphics not initialized");
    if (mesh.mNumVertices == 0 || mesh.mNumFaces == 0)
        throw Exception("newMeshFromAssimp: empty mesh");

    std::vector<MeshVertex> verts;
    verts.reserve(mesh.mNumVertices);
    std::vector<float> basePos;
    std::vector<float> baseNrm;
    std::vector<float> baseUv;
    basePos.reserve(mesh.mNumVertices * 3);
    baseNrm.reserve(mesh.mNumVertices * 3);
    baseUv.reserve(mesh.mNumVertices * 2);
    for (unsigned i = 0; i < mesh.mNumVertices; ++i) {
        MeshVertex v{};
        v.pos = {mesh.mVertices[i].x, mesh.mVertices[i].y, mesh.mVertices[i].z};
        if (mesh.HasNormals())
            v.normal = {mesh.mNormals[i].x, mesh.mNormals[i].y, mesh.mNormals[i].z};
        else
            v.normal = {0.f, 1.f, 0.f};
        if (mesh.HasTextureCoords(0))
            v.uv = {mesh.mTextureCoords[0][i].x, mesh.mTextureCoords[0][i].y};
        else
            v.uv = {0.f, 0.f};
        verts.push_back(v);
        basePos.push_back(v.pos.x);
        basePos.push_back(v.pos.y);
        basePos.push_back(v.pos.z);
        baseNrm.push_back(v.normal.x);
        baseNrm.push_back(v.normal.y);
        baseNrm.push_back(v.normal.z);
        baseUv.push_back(v.uv.x);
        baseUv.push_back(v.uv.y);
    }

    std::vector<uint32_t> indices;
    indices.reserve(mesh.mNumFaces * 3);
    for (unsigned f = 0; f < mesh.mNumFaces; ++f) {
        const aiFace &face = mesh.mFaces[f];
        if (face.mNumIndices != 3) continue;
        indices.push_back(face.mIndices[0]);
        indices.push_back(face.mIndices[1]);
        indices.push_back(face.mIndices[2]);
    }
    if (indices.empty()) throw Exception("newMeshFromAssimp: no triangle faces");

    std::unique_ptr<GpuMesh> gpu;
    if (mesh.mNumVertices <= 65535u) {
        std::vector<uint16_t> idx16;
        idx16.reserve(indices.size());
        for (uint32_t i : indices) idx16.push_back(uint16_t(i));
        gpu = uploadGpuMesh16(device, frameToken(), verts, idx16);
    } else {
        gpu = uploadGpuMesh(device, frameToken(), verts, indices);
    }
    auto handle = makeMeshHandle(*gpu);
    handle->captureImportedAttributes(mesh);
    // Retain the CPU morph base pose only when the mesh actually has morphs;
    // otherwise the base pos/nrm/uv copies would linger at ~32B/vertex for no reason.
    if (mesh.mNumAnimMeshes > 0) {
        handle->initMorphBase(int(mesh.mNumVertices), basePos.data(), baseNrm.data(), baseUv.data());
    }
    // Assimp morph targets (VRM / glTF blend shapes often land here).
    for (unsigned m = 0; m < mesh.mNumAnimMeshes; ++m) {
        const aiAnimMesh *am = mesh.mAnimMeshes[m];
        if (!am || !am->mVertices || am->mNumVertices != mesh.mNumVertices) continue;
        std::string morphName =
            am->mName.length ? am->mName.C_Str() : ("morph" + std::to_string(m));
        std::vector<float> absPos(size_t(am->mNumVertices) * 3u);
        for (unsigned i = 0; i < am->mNumVertices; ++i) {
            absPos[size_t(i) * 3u + 0] = am->mVertices[i].x;
            absPos[size_t(i) * 3u + 1] = am->mVertices[i].y;
            absPos[size_t(i) * 3u + 2] = am->mVertices[i].z;
        }
        handle->addMorphTargetAbsolute(morphName, absPos.data());
    }
    handle->markMorphClean();
    Mesh *raw = handle.get();
    assignMeshBounds(raw, verts);
    registerMeshRecord(gpu.get());
    ownedGpuMeshes.push_back(std::move(gpu));
    ownedMeshes.push_back(std::move(handle));
    return raw;
}

Mesh *Graphics::newMeshFromAssimp(const ::aiMesh &mesh, const aiMatrix4x4 &worldTransform) {
    ASSERT(initialized);
    if (!initialized) throw Exception("newMeshFromAssimp: graphics not initialized");
    if (mesh.mNumVertices == 0 || mesh.mNumFaces == 0)
        throw Exception("newMeshFromAssimp: empty mesh");

    std::vector<aiVector3D> positions(mesh.mNumVertices);
    std::vector<aiVector3D> normals(mesh.mNumVertices);
    aiMatrix3x3 nmat(worldTransform);
    const float ndet = nmat.Determinant();
    if (std::fabs(ndet) > 1e-8f) {
        nmat.Inverse();
        nmat.Transpose();
    }
    for (unsigned i = 0; i < mesh.mNumVertices; ++i) {
        positions[i] = worldTransform * mesh.mVertices[i];
        if (mesh.HasNormals()) {
            normals[i] = nmat * mesh.mNormals[i];
            normals[i].Normalize();
        } else {
            normals[i] = aiVector3D(0.f, 1.f, 0.f);
        }
    }

    // Negative determinant mirrors the mesh: winding flips while inverse-transpose
    // keeps normals consistent, so back-face cull would hide the visible side.
    const bool flipWinding = worldTransform.Determinant() < 0.f;
    std::vector<aiFace> flippedFaces;
    std::vector<unsigned> flippedIdx;
    if (flipWinding) {
        flippedFaces.resize(mesh.mNumFaces);
        flippedIdx.resize(size_t(mesh.mNumFaces) * 3u);
        for (unsigned f = 0; f < mesh.mNumFaces; ++f) {
            const aiFace &src = mesh.mFaces[f];
            aiFace &dst = flippedFaces[f];
            dst.mNumIndices = src.mNumIndices;
            if (src.mNumIndices == 3 && src.mIndices) {
                unsigned *idx = flippedIdx.data() + size_t(f) * 3u;
                idx[0] = src.mIndices[0];
                idx[1] = src.mIndices[2];
                idx[2] = src.mIndices[1];
                dst.mIndices = idx;
            } else {
                dst.mIndices = src.mIndices;
            }
        }
    }

    // Non-owning view — do not let aiMesh destructor free borrowed pointers.
    aiMesh tmp{};
    tmp.mPrimitiveTypes = mesh.mPrimitiveTypes;
    tmp.mNumVertices = mesh.mNumVertices;
    tmp.mVertices = positions.data();
    tmp.mNormals = normals.data();
    tmp.mNumFaces = mesh.mNumFaces;
    tmp.mFaces = flipWinding ? flippedFaces.data() : mesh.mFaces;
    tmp.mMaterialIndex = mesh.mMaterialIndex;
    tmp.mNumAnimMeshes = mesh.mNumAnimMeshes;
    tmp.mAnimMeshes = mesh.mAnimMeshes;
    if (mesh.HasTextureCoords(0)) {
        tmp.mTextureCoords[0] = mesh.mTextureCoords[0];
        tmp.mNumUVComponents[0] = mesh.mNumUVComponents[0];
    }
    Mesh *out = newMeshFromAssimp(tmp);
    tmp.mVertices = nullptr;
    tmp.mNormals = nullptr;
    tmp.mFaces = nullptr;
    tmp.mTextureCoords[0] = nullptr;
    tmp.mAnimMeshes = nullptr;
    return out;
}

Mesh *Graphics::newMeshFromArrays(const float *posXYZ, const float *nrmXYZ, const float *uvST,
                                  int vertexCount, const uint32_t *indices, int indexCount) {
    ASSERT(initialized);
    if (!initialized) throw Exception("newMeshFromArrays: graphics not initialized");
    if (!posXYZ || vertexCount <= 0) throw Exception("newMeshFromArrays: empty positions");
    if (!indices || indexCount < 3) throw Exception("newMeshFromArrays: empty indices");
    if (indexCount % 3 != 0) throw Exception("newMeshFromArrays: indexCount must be multiple of 3");

    std::vector<MeshVertex> verts(static_cast<size_t>(vertexCount));
    for (int i = 0; i < vertexCount; ++i) {
        MeshVertex &v = verts[static_cast<size_t>(i)];
        v.pos = {posXYZ[size_t(i) * 3u], posXYZ[size_t(i) * 3u + 1u], posXYZ[size_t(i) * 3u + 2u]};
        if (nrmXYZ)
            v.normal = {nrmXYZ[size_t(i) * 3u], nrmXYZ[size_t(i) * 3u + 1u],
                        nrmXYZ[size_t(i) * 3u + 2u]};
        else
            v.normal = {0.f, 1.f, 0.f};
        if (uvST)
            v.uv = {uvST[size_t(i) * 2u], uvST[size_t(i) * 2u + 1u]};
        else
            v.uv = {0.f, 0.f};
    }

    std::vector<uint32_t> idx(indices, indices + indexCount);
    for (uint32_t id : idx) {
        if (int(id) >= vertexCount) throw Exception("newMeshFromArrays: index out of range");
    }

    auto gpu = uploadGpuMesh(device, frameToken(), verts, idx);
    auto handle = makeMeshHandle(*gpu);
    Mesh *raw = handle.get();
    raw->computeBounds(posXYZ, vertexCount);
    registerMeshRecord(gpu.get());
    ownedGpuMeshes.push_back(std::move(gpu));
    ownedMeshes.push_back(std::move(handle));
    return raw;
}

bool Graphics::bakeMeshMorph(Mesh *mesh) {
    if (!mesh || !mesh->gpuHandle || !mesh->hasMorphData() || !mesh->isMorphDirty()) return false;
    if (!initialized) return false;

    std::vector<float> pos;
    std::vector<float> nrm;
    mesh->computeMorphedPositions(pos, nrm);
    const int vc = mesh->getVertexCount();
    if (vc <= 0 || int(pos.size()) < vc * 3) return false;
    mesh->computeBounds(pos.data(), vc);

    std::vector<MeshVertex> verts(static_cast<size_t>(vc));
    const auto &uv = mesh->baseUv();
    for (int i = 0; i < vc; ++i) {
        MeshVertex &v = verts[static_cast<size_t>(i)];
        v.pos = {pos[size_t(i) * 3u + 0], pos[size_t(i) * 3u + 1], pos[size_t(i) * 3u + 2]};
        if (int(nrm.size()) >= (i + 1) * 3)
            v.normal = {nrm[size_t(i) * 3u + 0], nrm[size_t(i) * 3u + 1], nrm[size_t(i) * 3u + 2]};
        else
            v.normal = {0.f, 1.f, 0.f};
        if (int(uv.size()) >= (i + 1) * 2)
            v.uv = {uv[size_t(i) * 2u + 0], uv[size_t(i) * 2u + 1]};
        else
            v.uv = {0.f, 0.f};
    }

    auto *gpu = static_cast<GpuMesh *>(mesh->gpuHandle);
    // Ring-buffered host-visible VBO: the next copy is kDynamicVertexCopies
    // frames old, so overwriting it never races with in-flight draws — no
    // device-wide wait (see writeDynamicMesh).
    ensureDynamicRing(*gpu);
    writeDynamicMesh(*gpu, verts, getDevice(), frameToken(), nullptr, 0);
    mesh->markMorphClean();
    return true;
}

bool Graphics::updateMeshVertices(Mesh *mesh, const float *posXYZ, const float *nrmXYZ,
                                  const float *uvST, int vertexCount, const uint32_t *indices,
                                  int indexCount) {
    if (!initialized || !mesh || !mesh->gpuHandle) return false;
    if (!posXYZ || vertexCount <= 0 || indexCount < 0) return false;
    if (indexCount > 0 && (indexCount % 3 != 0 || !indices)) return false;
    for (int i = 0; i < indexCount; ++i) {
        if (indices[i] >= uint32_t(vertexCount)) return false;
    }

    std::vector<MeshVertex> verts(static_cast<size_t>(vertexCount));
    for (int i = 0; i < vertexCount; ++i) {
        MeshVertex &v = verts[static_cast<size_t>(i)];
        v.pos = {posXYZ[size_t(i) * 3u], posXYZ[size_t(i) * 3u + 1u], posXYZ[size_t(i) * 3u + 2u]};
        if (nrmXYZ)
            v.normal = {nrmXYZ[size_t(i) * 3u], nrmXYZ[size_t(i) * 3u + 1u],
                        nrmXYZ[size_t(i) * 3u + 2u]};
        else
            v.normal = {0.f, 1.f, 0.f};
        if (uvST)
            v.uv = {uvST[size_t(i) * 2u], uvST[size_t(i) * 2u + 1u]};
        else
            v.uv = {0.f, 0.f};
    }

    auto *gpu = static_cast<GpuMesh *>(mesh->gpuHandle);
    mesh->computeBounds(posXYZ, vertexCount);
    // Same ring-buffer approach as bakeMeshMorph: never wait on in-flight
    // frames, just write the next copy.
    ensureDynamicRing(*gpu);
    writeDynamicMesh(*gpu, verts, getDevice(), frameToken(), indices, indexCount);
    mesh->gpuVertexCount = int(gpu->vertexCount);
    mesh->indexCount = int(gpu->indexCount);
    return true;
}

bool Graphics::setMeshSkinningData(Mesh *mesh, const uint16_t *joints4, const float *weights4,
                                   int vertexCount) {
    if (!initialized || !mesh || !mesh->gpuHandle || !joints4 || !weights4) return false;
    auto *gpu = static_cast<GpuMesh *>(mesh->gpuHandle);
    if (vertexCount <= 0 || uint32_t(vertexCount) != gpu->vertexCount) return false;

    std::vector<MeshVertex> verts(static_cast<size_t>(vertexCount));
    auto &source = meshDrawVertices(*gpu);
    void *mapped = source.map();
    if (!mapped) return false;
    std::memcpy(verts.data(), mapped, verts.size() * sizeof(MeshVertex));
    source.unmap();
    for (int i = 0; i < vertexCount; ++i) {
        const size_t base = static_cast<size_t>(i) * 4u;
        verts[static_cast<size_t>(i)].joints =
            glm::u16vec4(joints4[base], joints4[base + 1], joints4[base + 2], joints4[base + 3]);
        verts[static_cast<size_t>(i)].weights =
            glm::vec4(weights4[base], weights4[base + 1], weights4[base + 2], weights4[base + 3]);
    }
    ensureDynamicRing(*gpu);
    writeDynamicMesh(*gpu, verts, getDevice(), frameToken(), nullptr, 0);
    mesh->markGpuSkinned(true);
    return true;
}

bool Graphics::releaseMesh(Mesh *mesh) {
    if (!mesh || !mesh->gpuHandle) return false;

    auto *gpu = static_cast<GpuMesh *>(mesh->gpuHandle);
    auto gpuIt = std::find_if(ownedGpuMeshes.begin(), ownedGpuMeshes.end(),
                              [&](const std::unique_ptr<GpuMesh> &g) {
                                  return g.get() == gpu;
                              });
    if (gpuIt == ownedGpuMeshes.end()) return false;

    auto meshIt = std::find_if(ownedMeshes.begin(), ownedMeshes.end(),
                               [&](const std::unique_ptr<Mesh> &m) {
                                   return m.get() == mesh;
                               });
    if (meshIt == ownedMeshes.end()) return false;

    // An in-flight draw may still read the vertex/index buffers; drain first.
    waitForSharedGpuResources();
    mesh->gpuHandle = nullptr;
    ownedGpuMeshes.erase(gpuIt);
    // Transfer the CPU facade to the caller instead of destroying it.
    (void)meshIt->release();
    ownedMeshes.erase(meshIt);
    return true;
}

Mesh *Graphics::newMeshSphere(int slices, int stacks) {
    ASSERT(initialized);
    if (!initialized) throw Exception("newMeshSphere: graphics not initialized");
    if (slices < 3) slices = 3;
    if (stacks < 2) stacks = 2;
    if (slices > 256) slices = 256;
    if (stacks > 128) stacks = 128;

    constexpr float kPi = 3.14159265358979323846f;
    constexpr float kTwoPi = kPi * 2.f;

    // Layout (stride = slices+1, seam column duplicated for continuous U):
    //   row 0:          north pole verts (same pos, unique U)
    //   row 1..stacks-1: latitude rings
    //   row stacks:     south pole verts
    // One pole vertex per longitude avoids a single-fan UV singularity and
    // keeps every triangle non-degenerate.
    const int stride = slices + 1;
    const int rows = stacks + 1;  // includes both pole rows
    std::vector<MeshVertex> verts;
    verts.reserve(size_t(stride) * size_t(rows));

    auto pushVert = [&](float px, float py, float pz, float u, float v) {
        MeshVertex vert{};
        vert.pos = {px, py, pz};
        vert.normal = {px, py, pz};  // unit sphere
        vert.uv = {u, v};
        verts.push_back(vert);
    };

    for (int y = 0; y <= stacks; ++y) {
        const float fv = float(y) / float(stacks);
        const float phi = fv * kPi;
        const float sinPhi = std::sin(phi);
        const float cosPhi = std::cos(phi);
        for (int x = 0; x <= slices; ++x) {
            const float u = float(x) / float(slices);
            const float theta = u * kTwoPi;
            // Exact poles: collapse ring to a point but keep per-slice UVs.
            if (y == 0)
                pushVert(0.f, 1.f, 0.f, u, 0.f);
            else if (y == stacks)
                pushVert(0.f, -1.f, 0.f, u, 1.f);
            else
                pushVert(sinPhi * std::cos(theta), cosPhi, sinPhi * std::sin(theta), u, fv);
        }
    }

    std::vector<uint32_t> indices;
    indices.reserve(size_t(slices) * size_t(stacks) * 6u);

    // Quads between consecutive rows. Winding must be consistent for outward
    // faces (object-space CCW; mesh pipelines use Clockwise frontFace after the
    // Vulkan Y flip in perspectiveVulkanRH_ZO). Use the same winding that
    // closed the south pole in-game: rowA[x], rowB[x], rowA[x+1] /
    // rowA[x+1], rowB[x], rowB[x+1] — derived from ring→next with
    // (i0,i2,i1)+(i1,i2,i3) which equals (lon,lat)->(lon,lat+1)->(lon+1,lat).
    for (int y = 0; y < stacks; ++y) {
        const uint32_t row0 = uint32_t(y * stride);
        const uint32_t row1 = uint32_t((y + 1) * stride);
        for (int x = 0; x < slices; ++x) {
            const uint32_t i0 = row0 + uint32_t(x);
            const uint32_t i1 = row0 + uint32_t(x + 1);
            const uint32_t i2 = row1 + uint32_t(x);
            const uint32_t i3 = row1 + uint32_t(x + 1);
            // Outward for RH Y-up (verified against south-cap fix): i0,i2,i1 + i1,i2,i3
            indices.push_back(i0);
            indices.push_back(i2);
            indices.push_back(i1);
            indices.push_back(i1);
            indices.push_back(i2);
            indices.push_back(i3);
        }
    }

    auto gpu = uploadGpuMesh(device, frameToken(), verts, indices);
    auto handle = makeMeshHandle(*gpu);
    Mesh *raw = handle.get();
    assignMeshBounds(raw, verts);
    registerMeshRecord(gpu.get());
    ownedGpuMeshes.push_back(std::move(gpu));
    ownedMeshes.push_back(std::move(handle));
    return raw;
}

Mesh *Graphics::newMeshCylinder(int slices, int stacks, bool caps) {
    ASSERT(initialized);
    if (!initialized) throw Exception("newMeshCylinder: graphics not initialized");
    if (slices < 3) slices = 3;
    if (stacks < 1) stacks = 1;
    if (slices > 256) slices = 256;
    if (stacks > 128) stacks = 128;

    constexpr float kPi = 3.14159265358979323846f;
    constexpr float kTwoPi = kPi * 2.f;
    constexpr float kRadius = 1.f;
    constexpr float kHalfH = 1.f;  // height 2, Y from -1..1

    const int stride = slices + 1;  // duplicated seam for continuous U
    const int sideRows = stacks + 1;
    std::vector<MeshVertex> verts;
    verts.reserve(size_t(stride) * size_t(sideRows) + size_t(caps ? 2 * (slices + 2) : 0));

    auto pushVert = [&](float px, float py, float pz, float nx, float ny, float nz, float u,
                        float v) {
        MeshVertex vert{};
        vert.pos = {px, py, pz};
        vert.normal = {nx, ny, nz};
        vert.uv = {u, v};
        verts.push_back(vert);
    };

    // Side wall: outward normals in XZ.
    for (int y = 0; y <= stacks; ++y) {
        const float fv = float(y) / float(stacks);
        const float py = kHalfH - fv * (2.f * kHalfH);
        for (int x = 0; x <= slices; ++x) {
            const float u = float(x) / float(slices);
            const float theta = u * kTwoPi;
            const float cx = std::cos(theta);
            const float sz = std::sin(theta);
            pushVert(kRadius * cx, py, kRadius * sz, cx, 0.f, sz, u, fv);
        }
    }

    std::vector<uint32_t> indices;
    indices.reserve(size_t(slices) * size_t(stacks) * 6u +
                    size_t(caps ? slices * 2 * 3 : 0));

    for (int y = 0; y < stacks; ++y) {
        const uint32_t row0 = uint32_t(y * stride);
        const uint32_t row1 = uint32_t((y + 1) * stride);
        for (int x = 0; x < slices; ++x) {
            const uint32_t i0 = row0 + uint32_t(x);
            const uint32_t i1 = row0 + uint32_t(x + 1);
            const uint32_t i2 = row1 + uint32_t(x);
            const uint32_t i3 = row1 + uint32_t(x + 1);
            indices.push_back(i0);
            indices.push_back(i2);
            indices.push_back(i1);
            indices.push_back(i1);
            indices.push_back(i2);
            indices.push_back(i3);
        }
    }

    if (caps) {
        // Top cap (y = +1, normal +Y) — fan from center.
        const uint32_t topCenter = uint32_t(verts.size());
        pushVert(0.f, kHalfH, 0.f, 0.f, 1.f, 0.f, 0.5f, 0.5f);
        const uint32_t topRing = uint32_t(verts.size());
        for (int x = 0; x <= slices; ++x) {
            const float u = float(x) / float(slices);
            const float theta = u * kTwoPi;
            const float cx = std::cos(theta);
            const float sz = std::sin(theta);
            pushVert(kRadius * cx, kHalfH, kRadius * sz, 0.f, 1.f, 0.f, 0.5f + 0.5f * cx,
                     0.5f + 0.5f * sz);
        }
        for (int x = 0; x < slices; ++x) {
            indices.push_back(topCenter);
            indices.push_back(topRing + uint32_t(x));
            indices.push_back(topRing + uint32_t(x + 1));
        }

        // Bottom cap (y = -1, normal -Y).
        const uint32_t botCenter = uint32_t(verts.size());
        pushVert(0.f, -kHalfH, 0.f, 0.f, -1.f, 0.f, 0.5f, 0.5f);
        const uint32_t botRing = uint32_t(verts.size());
        for (int x = 0; x <= slices; ++x) {
            const float u = float(x) / float(slices);
            const float theta = u * kTwoPi;
            const float cx = std::cos(theta);
            const float sz = std::sin(theta);
            pushVert(kRadius * cx, -kHalfH, kRadius * sz, 0.f, -1.f, 0.f, 0.5f + 0.5f * cx,
                     0.5f + 0.5f * sz);
        }
        for (int x = 0; x < slices; ++x) {
            // CW when viewed from below so outward (-Y) faces are CCW from outside.
            indices.push_back(botCenter);
            indices.push_back(botRing + uint32_t(x + 1));
            indices.push_back(botRing + uint32_t(x));
        }
    }

    auto gpu = uploadGpuMesh(device, frameToken(), verts, indices);
    auto handle = makeMeshHandle(*gpu);
    Mesh *raw = handle.get();
    assignMeshBounds(raw, verts);
    registerMeshRecord(gpu.get());
    ownedGpuMeshes.push_back(std::move(gpu));
    ownedMeshes.push_back(std::move(handle));
    return raw;
}

void Graphics::drawMesh(Mesh *mesh, const glm::mat4 &model, Texture *texture, const Color &tint) {
    drawMeshShader(mesh, model, texture, tint, nullptr);
}

void Graphics::drawMeshShader(Mesh *mesh, const glm::mat4 &model, Texture *texture, const Color &tint,
                              Shader *shader) {
    ASSERT(initialized);
    ASSERT(mesh != nullptr);
    if (!initialized) throw Exception("drawMesh: graphics not initialized");
    if (!mesh || !mesh->gpuHandle) throw Exception("drawMesh: null mesh");
    if (!swapchainPassOpen && !offscreen3DPassOpen)
        throw Exception("drawMesh: call begin3DFrame first");
    if (!mesh3dPipeline) throw Exception("drawMesh: mesh3d pipeline missing");

    if (shader) {
        if (shader->getKind() != Shader::Kind::eMesh3D)
            throw Exception("drawMesh: shader is not a Mesh3D shader (use newMeshShader*)");
        if (!shader->gpuHandle) throw Exception("drawMesh: shader has no GPU pipeline");
    }

    auto *gpuMesh = static_cast<GpuMesh *>(mesh->gpuHandle);
    Texture *tex = texture ? texture : whiteTexture;
    if (!tex || !tex->gpuHandle) throw Exception("drawMesh: missing texture");
    auto *gpuTex = static_cast<GpuTexture *>(tex->gpuHandle);

    ensureFlatNormalTexture3D();
    Texture *ntex = mesh3dNormalTexture ? mesh3dNormalTexture : flatNormalTexture3D;
    if (!ntex || !ntex->gpuHandle) throw Exception("drawMesh: missing normal texture");
    auto *gpuNormal = static_cast<GpuTexture *>(ntex->gpuHandle);

    ensureFlatHeightTexture3D();
    Texture *htex = mesh3dHeightTexture ? mesh3dHeightTexture : flatHeightTexture3D;
    if (!htex || !htex->gpuHandle) throw Exception("drawMesh: missing height texture");
    auto *gpuHeight = static_cast<GpuTexture *>(htex->gpuHandle);

    Texture *depthTex = mesh3dSceneDepthTexture ? mesh3dSceneDepthTexture : whiteTexture;
    if (!depthTex || !depthTex->gpuHandle) throw Exception("drawMesh: missing scene depth texture");
    auto *gpuDepth = static_cast<GpuTexture *>(depthTex->gpuHandle);

    // Screen-space decal layer (bindings 8/9/10). Only sampled when the decal
    // pass actually recorded this frame; otherwise the transparent/flat
    // placeholders make the blend a no-op.
    ensureDecalPlaceholders();
    GpuTexture *gpuDecalAlb = nullptr;
    GpuTexture *gpuDecalNrm = nullptr;
    GpuTexture *gpuDecalPrm = nullptr;
    if (decalLayerFresh) {
        if (auto *dslot = currentDecalSlot()) {
            gpuDecalAlb = &dslot->albedoGpu;
            gpuDecalNrm = &dslot->normalGpu;
            gpuDecalPrm = &dslot->paramsGpu;
        }
    }
    if (!gpuDecalAlb) {
        gpuDecalAlb = static_cast<GpuTexture *>(decalFlatAlbedo->gpuHandle);
        gpuDecalNrm = static_cast<GpuTexture *>(decalFlatNormal->gpuHandle);
        gpuDecalPrm = static_cast<GpuTexture *>(decalFlatParams->gpuHandle);
    }

    ensureDefaultEnvCubemap();
    Texture *envTex = mesh3dEnvTexture ? mesh3dEnvTexture : defaultEnvCubemap;
    if (!envTex || !envTex->gpuHandle) throw Exception("drawMesh: missing env cubemap");
    auto *gpuEnv = static_cast<GpuTexture *>(envTex->gpuHandle);
    if (!gpuEnv->isCube) throw Exception("drawMesh: env texture is not a cubemap");
    const float envIntensity = (mesh3dEnvTexture && mesh3dEnvIntensity > 0.f) ? mesh3dEnvIntensity : 0.f;

    const bool useClustered = mesh3dClusteredActive && !shader && mesh3dClusteredPipeline &&
                              mesh3dSurfaceMode != SurfaceMode::Transparent && !mesh->hasGpuSkinning();
    auto &cb = currentPresentCb();

    auto makeShadowUbo = [&]() {
        ShadowUBO s = mesh3dShadows.ubo;
        if (!mesh3dShadows.active) {
            s.bias.y = 0.f;
            s.splits.w = 0.f;
        }
        s.bias.z = mesh3dShadowReceive ? 1.f : 0.f;
        return s;
    };

    if (useClustered) {
        Mesh3DClusteredUBO ubo{};
        ubo.model = model;
        ubo.mvp = mesh3dFrameUbo.mvp * model;
        ubo.view = mesh3dClustered.view;
        ubo.lightDir = mesh3dClustered.primaryDir;
        ubo.lightColor = glm::vec4(glm::vec3(mesh3dClustered.primaryColor), envIntensity);
        ubo.tint = glm::vec4(tint.r, tint.g, tint.b, tint.a);
        ubo.cameraPos = glm::vec4(glm::vec3(mesh3dFrameUbo.cameraPos), mesh3dRoughness);
        ubo.ambient = glm::vec4(glm::vec3(mesh3dClustered.ambient), mesh3dMetallic);
        ubo.gridInfo = mesh3dClustered.gridInfo;
        ubo.clipInfo = mesh3dClustered.clipInfo;
        float surfaceCode = float(int(mesh3dSurfaceMode));
        if (mesh3dSurfaceMode == SurfaceMode::Masked && mesh3dAlphaTechnique == "dither")
            surfaceCode = 3.f;
        else if (mesh3dSurfaceMode == SurfaceMode::Masked &&
                 mesh3dAlphaTechnique == "coverage")
            surfaceCode = 4.f;
        ubo.texBomb = glm::vec4(mesh3dTexBombScale, mesh3dTexBombStrength, mesh3dTexBombRot,
                                surfaceCode);
        ubo.parallax =
            glm::vec4(mesh3dParallaxScale, mesh3dParallaxMinLayers, mesh3dParallaxMaxLayers,
                      mesh3dAlphaCutoff);
        ubo.envProbeCenter = glm::vec4(mesh3dEnvProbeCenter, 1.f);
        ubo.envProbeExtent = glm::vec4(mesh3dEnvProbeExtent, 0.f);
        for (int i = 0; i < ReflectionProbeUpload::kMaxProbes; ++i) {
            if (i >= mesh3dReflectionProbes.count) continue;
            const auto &probe = mesh3dReflectionProbes.probes[i];
            if (!probe.cubemap || !probe.cubemap->gpuHandle ||
                !static_cast<GpuTexture *>(probe.cubemap->gpuHandle)->isCube)
                continue;
            ubo.reflectionProbeCenter[i] = glm::vec4(probe.center, probe.intensity);
            ubo.reflectionProbeExtent[i] = glm::vec4(probe.extent, probe.blendDistance);
        }

        auto &cfslots = currentMesh3dClusteredFrameSlots();
        if (cfslots.drawIndex >= cfslots.capacity) {
            std::fprintf(stderr,
                         "[vulkan] clustered mesh3d UBO ring exhausted (%zu draws); draw skipped\n",
                         cfslots.capacity);
            return;
        }
        const size_t slot = cfslots.drawIndex++;
        ensureMesh3dStrides();
        const uint32_t uboOffset = uint32_t(slot) * mesh3dClusteredUboStride;
        const uint32_t shadowOffset = uint32_t(slot) * shadowUboStride;
        updateRingLocal(cfslots.uboRing, uboOffset, &ubo, sizeof(ubo));
        const ShadowUBO shadow = makeShadowUbo();
        updateRingLocal(cfslots.shadowRing, shadowOffset, &shadow, sizeof(shadow));
        vk::DescriptorSet set =
            mesh3dClusteredSetFor(gpuTex, gpuNormal, gpuEnv, gpuHeight, gpuDecalAlb, gpuDecalNrm,
                                  gpuDecalPrm, cfslots);
        const uint32_t dynOffsets[2] = {uboOffset, shadowOffset};

        if (mesh3dClusteredPipeline != lastMesh3dClusteredPipeline) {
            cb.bindPipeline(vk::PipelineBindPoint::eGraphics, mesh3dClusteredPipeline);
            lastMesh3dClusteredPipeline = mesh3dClusteredPipeline;
        }
        cb.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, mesh3dClusteredPipelineLayout, 0, 1,
                              &set, 2, dynOffsets);
        drawIndexedMesh(cb, *gpuMesh);
        return;
    }

    Mesh3DUBO ubo = mesh3dFrameUbo;
    ubo.model = model;
    ubo.mvp = mesh3dFrameUbo.mvp * model;
    ubo.tint = glm::vec4(tint.r, tint.g, tint.b, tint.a);
    ubo.ambient = glm::vec4(glm::vec3(mesh3dLighting.ambient), mesh3dMetallic);
    const int lightCount = std::max(0, std::min(mesh3dLighting.count, Lighting3DPack::kMaxLights));
    ubo.lightDir.w = float(lightCount);
    ubo.cameraPos.w = mesh3dRoughness;
    ubo.lightColor.w = envIntensity;
    float surfaceCode = float(int(mesh3dSurfaceMode));
    if (mesh3dSurfaceMode == SurfaceMode::Masked && mesh3dAlphaTechnique == "dither")
        surfaceCode = 3.f;
    else if (mesh3dSurfaceMode == SurfaceMode::Masked && mesh3dAlphaTechnique == "coverage")
        surfaceCode = 4.f;
    ubo.texBomb = glm::vec4(mesh3dTexBombScale, mesh3dTexBombStrength, mesh3dTexBombRot,
                            surfaceCode);
    ubo.parallax =
        glm::vec4(mesh3dParallaxScale, mesh3dParallaxMinLayers, mesh3dParallaxMaxLayers,
                  mesh3dAlphaCutoff);
    for (int i = 0; i < ReflectionProbeUpload::kMaxProbes; ++i) {
        if (i >= mesh3dReflectionProbes.count) continue;
        const auto &probe = mesh3dReflectionProbes.probes[i];
        if (!probe.cubemap || !probe.cubemap->gpuHandle ||
            !static_cast<GpuTexture *>(probe.cubemap->gpuHandle)->isCube)
            continue;
        ubo.reflectionProbeCenter[i] = glm::vec4(probe.center, probe.intensity);
        ubo.reflectionProbeExtent[i] = glm::vec4(probe.extent, probe.blendDistance);
    }
    if (mesh->hasGpuSkinning()) {
        const int paletteCount = std::min(mesh->getSkinPaletteCount(), Mesh::kMaxSkinBones);
        ubo.skinInfo.x         = static_cast<float>(paletteCount);
        const auto &palette    = mesh->skinPalette();
        for (int i = 0; i < paletteCount; ++i) {
            const float *matrix = palette.data() + static_cast<size_t>(i) * 16u;
            for (int column = 0; column < 4; ++column)
                for (int row = 0; row < 4; ++row)
                    ubo.skinBones[i][column][row] = matrix[column * 4 + row];
        }
    }
    for (int i = 0; i < lightCount; ++i) ubo.lights[i] = mesh3dLighting.lights[i];
    int dirI = -1;
    for (int i = 0; i < lightCount; ++i) {
        if (mesh3dLighting.lights[i].posRadius.w <= 0.f) {
            dirI = i;
            break;
        }
    }
    if (dirI >= 0) {
        glm::vec3 d(mesh3dLighting.lights[dirI].posRadius);
        if (glm::length(d) < 1e-6f) d = glm::vec3(0.f, 1.f, 0.f);
        else d = glm::normalize(d);
        ubo.lightDir = glm::vec4(d, float(lightCount));
        ubo.lightColor = glm::vec4(glm::vec3(mesh3dLighting.lights[dirI].color), envIntensity);
    } else {
        ubo.lightDir = glm::vec4(0.f, 1.f, 0.f, float(lightCount));
        ubo.lightColor = glm::vec4(0.f, 0.f, 0.f, envIntensity);
    }

    auto &fslots = currentMesh3dFrameSlots();
    if (fslots.drawIndex >= fslots.capacity) {
        std::fprintf(stderr, "[vulkan] mesh3d UBO ring exhausted (%zu draws); draw skipped\n",
                     fslots.capacity);
        return;
    }
    const size_t slot = fslots.drawIndex++;
    ensureMesh3dStrides();
    const uint32_t uboOffset = uint32_t(slot) * mesh3dUboStride;
    const uint32_t shadowOffset = uint32_t(slot) * shadowUboStride;
    updateRingLocal(fslots.uboRing, uboOffset, &ubo, sizeof(ubo));
    const ShadowUBO shadow = makeShadowUbo();
    updateRingLocal(fslots.shadowRing, shadowOffset, &shadow, sizeof(shadow));
    vk::DescriptorSet set = mesh3dSetFor(gpuTex, gpuNormal, gpuEnv, gpuHeight, gpuDepth,
                                         gpuDecalAlb, gpuDecalNrm, gpuDecalPrm, fslots);
    const uint32_t dynOffsets[2] = {uboOffset, shadowOffset};

    if (shader) {
        auto *gs = static_cast<GpuShader *>(shader->gpuHandle);
        vk::Pipeline activePipeline = offscreen3DPassOpen
                                          ? (offscreen3DHDRActive
                                                 ? gs->mesh3dHdrOffscreenPipeline
                                                 : gs->mesh3dOffscreenPipeline)
                                          : gs->mesh3dPipeline;
        if (shader->isXray() && !offscreen3DPassOpen) {
            // X-ray silhouette pass: depth test/write off + alpha blend so the
            // occluded part paints over the building. The pipeline is created
            // with the shader (see newMeshShaderFromSpv); do not compile it
            // here — a render pass is already open.
            activePipeline = gs->mesh3dXrayPipeline;
        }
        if (!activePipeline) return;
        if (activePipeline != lastMesh3dPipeline) {
            cb.bindPipeline(vk::PipelineBindPoint::eGraphics, activePipeline);
            lastMesh3dPipeline = activePipeline;
        }
        cb.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, mesh3dShaderPipelineLayout, 0, 1, &set,
                              2, dynOffsets);
        cb.pushConstants(mesh3dShaderPipelineLayout,
                         vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0,
                         Shader::kPushConstantBytes, shader->pushConstantData());
    } else {
        const bool transparent = mesh3dSurfaceMode == SurfaceMode::Transparent;
        const BlendMode blend = transparent ? mesh3dSurfaceBlend : BlendMode::Opaque;
        const bool depthWrite = !transparent || mesh3dSurfaceDepthWrite;
        const size_t pipelineIndex =
            mesh3dPipelineIndex(blend, depthWrite, mesh3dSurfaceDoubleSided);
        const vk::Pipeline pipe = offscreen3DPassOpen
                                      ? (offscreen3DHDRActive
                                             ? hdrOffscreen3DSurfacePipelines[pipelineIndex]
                                             : offscreen3DSurfacePipelines[pipelineIndex])
                                      : mesh3dSurfacePipelines[pipelineIndex];
        if (pipe != lastMesh3dPipeline) {
            cb.bindPipeline(vk::PipelineBindPoint::eGraphics, pipe);
            lastMesh3dPipeline = pipe;
        }
        cb.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, mesh3dPipelineLayout, 0, 1, &set, 2,
                              dynOffsets);
    }
    drawIndexedMesh(cb, *gpuMesh);
}


}  // namespace eve::graphics::vulkan
