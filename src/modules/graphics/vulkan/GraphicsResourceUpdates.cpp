#include <algorithm>
#include <cstring>
#include <memory>
#include "common/Exception.h"
#include "common/Resource.h"
#include "filesystem/Filesystem.h"
#include "graphics/Mesh.h"
#include "graphics/Texture.h"
#include "graphics/vulkan/Graphics.h"
#include "graphics/vulkan/GraphicsInternal.h"
#include "image/Image.h"
#include "image/ImageData.h"
namespace eve::graphics::vulkan {
ResultRef<Texture> Graphics::requestFileTexture(const std::string &filename) {
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
        Texture            *raw = texture.get();
        DeferredFileTexture pending{key, raw};
        if (ownedTextures.size() == ownedTextures.capacity())
            ownedTextures.reserve(std::max(size_t(1), ownedTextures.capacity() * 2));
        if (deferredFileTextures_.size() == deferredFileTextures_.capacity())
            deferredFileTextures_.reserve(std::max(size_t(1), deferredFileTextures_.capacity() * 2));
        texturesByPath.insert_or_assign(key, raw);
        ownedTextures.push_back(std::move(texture));
        deferredFileTextures_.push_back(std::move(pending));
        return ResultRef<Texture>::success(std::ref(*raw));
    } catch (const std::exception &error) {
        return ResultRef<Texture>::failure(
            Diagnostic::error(DiagnosticCode::Failed, error.what(), {}, {}, "graphics.texture.request"));
    }
}

bool Graphics::uploadDeferredFileTexture(Texture *texture, image::ImageData *data) {
    return replaceTexturePixels(texture, data);
}

bool Graphics::reloadTextureFromFile(const std::string &filename) {
    if (filename.empty()) return false;
    const std::string key = normalizeTexPath(filename);
    auto              it  = texturesByPath.find(key);
    if (it == texturesByPath.end() || !it->second) return false;

    ensureFileTexturesReady();
    applyPendingResourceChanges();
    // The provider hands back a cache-owned ImageData; the pin keeps it alive until
    // the pixels have been copied out of it.
    image::ImageData *data = nullptr;
    eve::ResourcePin  keepAlive;
    try {
        auto *imgMod = image::Image::create();
        data         = imgMod->newImageDataFromFile(filename);
        if (data != nullptr) {
            auto pinned = eve::ResourceManager::getInstance().pin(data);
            if (!pinned.ok()) return false;
            keepAlive = std::move(pinned).takeValue();
            // The pin is the authority from here on; the borrowed pointer may have gone
            // stale before the pin was taken.
            data = static_cast<image::ImageData *>(keepAlive.get());
        }
    } catch (...) {
        return false;
    }
    if (!data) return false;
    return replaceTexturePixels(it->second, data);
}
}  // namespace eve::graphics::vulkan
