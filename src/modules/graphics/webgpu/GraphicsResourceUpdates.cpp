#include <algorithm>
#include <cstring>
#include <memory>
#include "common/Exception.h"
#include "common/Resource.h"
#include "filesystem/Filesystem.h"
#include "graphics/Mesh.h"
#include "graphics/Texture.h"
#include "graphics/webgpu/Graphics.h"
#include "image/Image.h"
#include "image/ImageData.h"
namespace eve::graphics::webgpu {
namespace {
WGPUStringView sv(const char* s) { return {s, std::strlen(s)}; }
std::string    normalizeTexPath(std::string path) {
    std::replace(path.begin(), path.end(), '\\', '/');
    while (path.starts_with("./")) path.erase(0, 2);
    while (path.size() > 1 && path.back() == '/') path.pop_back();
    return path;
}
}  // namespace
ResultRef<Texture> Graphics::requestFileTexture(const std::string& filename) {
    if (filename.empty())
        return ResultRef<Texture>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "file texture path is empty", {}, {}, "graphics.texture.request"));
    try {
        const std::string key = normalizeTexPath(filename);
        auto              it  = texturesByPath.find(key);
        if (it != texturesByPath.end() && it->second) return ResultRef<Texture>::success(std::ref(*it->second));
        if (!fileTextureSourceExists(filename) && !fileTextureSourceExists(key))
            return ResultRef<Texture>::failure(Diagnostic::error(DiagnosticCode::NotFound,
                                                                 "file texture source is absent: " + filename, {}, {},
                                                                 "graphics.texture.request"));
        requestFileImageDecode(key);
        auto texture = std::make_unique<Texture>();
        texture->markDeferredFilePixels(this);
        Texture*            raw = texture.get();
        DeferredFileTexture pending{key, raw};
        if (ownedTextures.size() == ownedTextures.capacity())
            ownedTextures.reserve(std::max(size_t(1), ownedTextures.capacity() * 2));
        if (deferredFileTextures_.size() == deferredFileTextures_.capacity())
            deferredFileTextures_.reserve(std::max(size_t(1), deferredFileTextures_.capacity() * 2));
        texturesByPath.insert_or_assign(key, raw);
        ownedTextures.push_back(std::move(texture));
        deferredFileTextures_.push_back(std::move(pending));
        return ResultRef<Texture>::success(std::ref(*raw));
    } catch (const std::exception& error) {
        return ResultRef<Texture>::failure(
            Diagnostic::error(DiagnosticCode::Failed, error.what(), {}, {}, "graphics.texture.request"));
    }
}

bool Graphics::uploadDeferredFileTexture(Texture* texture, image::ImageData* data) {
    if (!texture || !data) return false;
    if (texture->gpuHandle)
        return updateTexture(texture, data->getWidth(), data->getHeight(),
                             static_cast<const uint8_t*>(data->getData()));
    TextureCreateInfo info;
    info.sampler   = texture->sampler;
    Texture* fresh = newTexture(data, info);
    if (!fresh || !fresh->gpuHandle) return false;
    texture->gpuHandle   = fresh->gpuHandle;
    texture->width       = fresh->width;
    texture->height      = fresh->height;
    texture->pixelWidth  = fresh->pixelWidth;
    texture->pixelHeight = fresh->pixelHeight;
    texture->mipmapCount = fresh->mipmapCount;
    texture->sampler     = fresh->sampler;
    fresh->gpuHandle     = nullptr;
    auto texIt           = std::find_if(ownedTextures.begin(), ownedTextures.end(),
                                        [&](const std::unique_ptr<Texture>& t) { return t.get() == fresh; });
    if (texIt == ownedTextures.end()) return false;
    (void)texIt->release();
    ownedTextures.erase(texIt);
    delete fresh;
    return true;
}

bool Graphics::reloadTextureFromFile(const std::string& filename) {
    const std::string key = normalizeTexPath(filename);
    auto              it  = texturesByPath.find(key);
    if (it == texturesByPath.end()) return false;

    ensureFileTexturesReady();
    applyPendingResourceChanges();

    // The provider hands back a cache-owned ImageData; the pin keeps it alive until
    // the pixels have been copied out of it.
    image::ImageData* data = nullptr;
    eve::ResourcePin  keepAlive;
    try {
        auto* imgMod = image::Image::create();
        data         = imgMod->newImageDataFromFile(filename);
        if (data != nullptr) {
            auto pinned = eve::ResourceManager::getInstance().pin(data);
            if (!pinned.ok()) return false;
            keepAlive = std::move(pinned).takeValue();
            // The pin is the authority from here on; the borrowed pointer may have gone
            // stale before the pin was taken.
            data = static_cast<image::ImageData*>(keepAlive.get());
        }
    } catch (...) {
        return false;
    }
    if (!data) return false;

    Texture* tex = it->second;
    return updateTexture(tex, data->getWidth(), data->getHeight(), static_cast<const uint8_t*>(data->getData()));
}

bool Graphics::updateMeshVertices(Mesh* mesh, const float* posXYZ, const float* nrmXYZ, const float* uvST,
                                  int vertexCount, const uint32_t* indices, int indexCount) {
    if (!mesh || !mesh->gpuHandle || !posXYZ || vertexCount <= 0 || indexCount < 0) return false;
    if (indexCount > 0 && (!indices || indexCount % 3 != 0)) return false;
    for (int i = 0; i < indexCount; ++i) {
        if (indices[i] >= uint32_t(vertexCount)) return false;
    }
    auto*      gpu = static_cast<GpuMesh*>(mesh->gpuHandle);
    const auto owned =
        std::find_if(ownedGpuMeshes.begin(), ownedGpuMeshes.end(),
                     [gpu](const std::unique_ptr<GpuMesh>& candidate) { return candidate.get() == gpu; });
    if (owned == ownedGpuMeshes.end()) return false;

    auto& verts = gpu->updateVertices;
    verts.clear();
    verts.reserve(size_t(vertexCount) * 8u);
    for (int i = 0; i < vertexCount; ++i) {
        verts.insert(verts.end(), posXYZ + size_t(i) * 3u, posXYZ + size_t(i) * 3u + 3u);
        if (nrmXYZ)
            verts.insert(verts.end(), nrmXYZ + size_t(i) * 3u, nrmXYZ + size_t(i) * 3u + 3u);
        else
            verts.insert(verts.end(), {0.f, 0.f, 1.f});
        if (uvST)
            verts.insert(verts.end(), uvST + size_t(i) * 2u, uvST + size_t(i) * 2u + 2u);
        else
            verts.insert(verts.end(), {0.f, 0.f});
    }

    const uint64_t vertexBytes = verts.size() * sizeof(float);
    if (vertexBytes > gpu->vertexCapacity) {
        WGPUBufferDescriptor desc{};
        desc.label          = sv("eve_mesh_dynamic_vb");
        desc.size           = vertexBytes;
        desc.usage          = WGPUBufferUsage_CopyDst | WGPUBufferUsage_Vertex | WGPUBufferUsage_Storage;
        gpu->vertexBuffer   = device.CreateBuffer(reinterpret_cast<const wgpu::BufferDescriptor*>(&desc));
        gpu->vertexCapacity = vertexBytes;
    }
    queue.WriteBuffer(gpu->vertexBuffer, 0, verts.data(), vertexBytes);
    gpu->vertexCount = uint32_t(vertexCount);

    if (indexCount > 0) {
        const wgpu::IndexFormat format = vertexCount <= 65535 ? wgpu::IndexFormat::Uint16 : wgpu::IndexFormat::Uint32;
        auto&                   idx16  = gpu->updateIndices16;
        idx16.clear();
        const void* indexData  = indices;
        uint64_t    indexBytes = uint64_t(indexCount) * sizeof(uint32_t);
        if (format == wgpu::IndexFormat::Uint16) {
            idx16.reserve(size_t(indexCount) + 1u);
            for (int i = 0; i < indexCount; ++i) idx16.push_back(uint16_t(indices[i]));
            if (idx16.size() % 2 != 0) idx16.push_back(0);
            indexData  = idx16.data();
            indexBytes = idx16.size() * sizeof(uint16_t);
        }
        if (format != gpu->indexFormat || indexBytes > gpu->indexCapacity) {
            WGPUBufferDescriptor desc{};
            desc.label         = sv("eve_mesh_dynamic_ib");
            desc.size          = indexBytes;
            desc.usage         = WGPUBufferUsage_CopyDst | WGPUBufferUsage_Index | WGPUBufferUsage_Storage;
            gpu->indexBuffer   = device.CreateBuffer(reinterpret_cast<const wgpu::BufferDescriptor*>(&desc));
            gpu->indexCapacity = indexBytes;
        }
        queue.WriteBuffer(gpu->indexBuffer, 0, indexData, indexBytes);
        gpu->indexFormat = format;
        gpu->indexCount  = uint32_t(indexCount);
    }

    mesh->computeBounds(posXYZ, vertexCount);
    mesh->gpuVertexCount = int(gpu->vertexCount);
    mesh->indexCount     = int(gpu->indexCount);
    return true;
}
}  // namespace eve::graphics::webgpu
