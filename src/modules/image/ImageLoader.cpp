// Loads ImageData through the unified resource cache (common/Resource.h).
//
// Registered as an eve::caps::IAssetReloader provider: ResourceManager asks it
// to load() on a cache miss and to produce a fresh ImageData on hot reload,
// which is adopted into the cached instance in place (identity stays stable).
// reload() is intentionally not implemented here -- the GPU-side refresh is
// owned by graphics (TextureReloader) and runs after the cache refresh
// (kCache < kTexture).

#include "common/AssetReloader.h"
#include "common/Capability.h"
#include "common/Exception.h"
#include "common/Module.h"
#include "common/Resource.h"
#include "filesystem/FileData.h"
#include "filesystem/Filesystem.h"
#include "image/Image.h"
#include "image/ImageData.h"

#include <cctype>
#include <memory>
#include <string>

namespace eve::image {
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

class ImageLoader : public eve::caps::IAssetReloader {
public:
    const char *reloadKind() const override { return "image"; }

    bool handlesPath(const std::string &normPath) const override {
        return isImagePath(eve::ResourceManager::pathOfKey(normPath));
    }

    /**
     * @brief Participate in the canonical cache reload protocol.
     * @return `false` because ResourceManager owns CPU cache refresh; the
     *         graphics TextureReloader performs the later GPU refresh.
     * @remarks This provider must not decode a second copy during dispatch.
     */
    eve::Result<bool> reload(const std::string &) override {
        return eve::Result<bool>::success(false);
    }

    eve::Resource *load(const std::string &key) override {
        const std::string path = eve::ResourceManager::pathOfKey(key);
        if (!isImagePath(path)) return nullptr;

        auto *fs = filesystem::Filesystem::create();
        if (!fs) return nullptr;
        std::unique_ptr<filesystem::FileData> fd(fs->read(path));
        if (!fd || fd->getData() == nullptr || fd->getSize() == 0)
            throw eve::Exception("Could not read image file: %s", path.c_str());

        auto *mod = Image::create();
        if (!mod) throw eve::Exception("eve::image module is not loaded");
        return mod->newImageData(fd.get());
    }
};

struct Register {
    Register() {
        static ImageLoader loader;
        eve::cap::addListener<eve::caps::IAssetReloader>(&loader, eve::caps::IAssetReloader::kCache);
    }
} g_register;

}  // namespace
}  // namespace eve::image
