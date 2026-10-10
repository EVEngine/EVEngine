#include <memory>
#include "common/Exception.h"
#include "common/Resource.h"
#include "common/StartupTiming.h"
#include "filesystem/FileData.h"
#include "filesystem/Filesystem.h"
#include "graphics/Graphics.h"
#include "graphics/Texture.h"
#include "image/Image.h"
#include "image/ImageData.h"
namespace eve::graphics {
Texture* Graphics::newTextureFromFile(const std::string& filename) {
    auto requested = requestFileTexture(filename);
    if (!requested.ok()) throw eve::Exception("%s", requested.status().describe().c_str());
    return &requested.value().get();
}

ResultRef<Texture> Graphics::loadTexture(const std::string& filename) {
    try {
        auto requested = requestFileTexture(filename);
        if (!requested.ok()) return ResultRef<Texture>::failure(requested.status());
        auto* texture = &requested.value().get();
        if (texture->hasDeferredFilePixels()) ensureFileTexturesReady();
        if (!texture->gpuHandle)
            return ResultRef<Texture>::failure(Diagnostic::error(
                DiagnosticCode::Failed, "file texture did not become resident", {}, {}, "graphics.texture.load"));
        return ResultRef<Texture>::success(std::ref(*texture));
    } catch (const std::exception& error) {
        return ResultRef<Texture>::failure(
            Diagnostic::error(DiagnosticCode::Failed, error.what(), {}, {}, "graphics.texture.load"));
    }
}
void Graphics::ensureFileTexturesReady() {
    if (deferredFileTextures_.empty() || realizingFileTextures_) return;
    realizingFileTextures_ = true;
    StartupStage stage("graphics: realize file textures");
    struct Guard {
        Graphics* g;
        ~Guard() { g->realizingFileTextures_ = false; }
    } guard{this};

    std::vector<DeferredFileTexture> pending = std::move(deferredFileTextures_);
    deferredFileTextures_.clear();

    auto restoreUnrealized = [&]() {
        for (const auto& item : pending) {
            if (item.texture && item.texture->hasDeferredFilePixels()) deferredFileTextures_.push_back(item);
        }
    };

    auto& resources = eve::ResourceManager::getInstance();
    for (const auto& item : pending) {
        auto waited = resources.waitFor(item.key);
        if (!waited.ok()) {
            restoreUnrealized();
            throw eve::Exception("%s", waited.status().describe().c_str());
        }
    }

    try {
        for (const auto& item : pending) {
            auto waited = resources.waitFor(item.key);
            if (!waited.ok()) throw eve::Exception("%s", waited.status().describe().c_str());
            auto* data = dynamic_cast<image::ImageData*>(&waited.value().get());
            if (!data || !uploadDeferredFileTexture(item.texture, data))
                throw eve::Exception("newTextureFromFile: GPU upload failed '%s'", item.key.c_str());
            if (item.texture) item.texture->clearDeferredFilePixels();
        }
    } catch (...) {
        restoreUnrealized();
        throw;
    }
}

Texture* Graphics::newTextureFromFileRepeated(const std::string& filename, bool repeatU, bool repeatV) {
    if (filename.empty()) throw eve::Exception("newTextureFromFileRepeated: empty filename");
    auto*                                      fs = eve::filesystem::Filesystem::create();
    std::unique_ptr<eve::filesystem::FileData> fileData(fs->read(filename));
    if (!fileData) throw eve::Exception("newTextureFromFileRepeated: failed to read '%s'", filename.c_str());
    auto*                                  imgMod = eve::image::Image::create();
    std::unique_ptr<eve::image::ImageData> data(imgMod->newImageData(fileData.get()));
    return newTextureFromImageData(data.get(), repeatU, repeatV);
}
}  // namespace eve::graphics
