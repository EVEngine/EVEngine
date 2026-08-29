// Refreshes path-cached GPU textures when their image file changes on disk.
//
// Registered at kTexture priority so the texture is re-uploaded before the
// modules that sample it (particle emitters, tile layers) re-bind.

#include "common/AssetReloader.h"
#include "common/Capability.h"
#include "common/Module.h"
#include "graphics/Graphics.h"

#include <cctype>
#include <string>

namespace eve::graphics {
namespace {

std::string extensionOf(const std::string &path) {
    const auto pos = path.find_last_of('.');
    if (pos == std::string::npos) return {};
    std::string ext = path.substr(pos);
    for (char &c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext;
}

bool isImagePath(const std::string &path) {
    const std::string ext = extensionOf(path);
    return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga" ||
           ext == ".gif" || ext == ".webp" || ext == ".exr" || ext == ".hdr";
}

class TextureReloader : public eve::caps::IAssetReloader {
public:
    const char *reloadKind() const override { return "texture"; }

    bool handlesPath(const std::string &normPath) const override { return isImagePath(normPath); }

    eve::Result<bool> reload(const std::string &normPath) override {
        auto *gfx = eve::ModuleManager::getInstance<Graphics>("Graphics");
        if (!gfx) return eve::Result<bool>::success(false);
        return eve::Result<bool>::success(gfx->reloadTextureFromFile(normPath));
    }
};

struct Register {
    Register() {
        static TextureReloader reloader;
        eve::cap::addListener<eve::caps::IAssetReloader>(&reloader,
                                                         eve::caps::IAssetReloader::kTexture);
    }
} g_register;

}  // namespace
}  // namespace eve::graphics
