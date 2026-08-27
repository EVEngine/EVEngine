// Loads FontData through the unified resource cache (common/Resource.h).
//
// The cache key carries the pixel size ("fonts/a.ttf?size=16"), so the same
// file at two sizes is two distinct cached resources, while repeated loads at
// one size share a single decoded face.

#include "common/AssetReloader.h"
#include "common/Capability.h"
#include "common/Exception.h"
#include "common/Module.h"
#include "common/Resource.h"
#include "filesystem/FileData.h"
#include "filesystem/Filesystem.h"
#include "font/Font.h"
#include "font/FontData.h"

#include <cctype>
#include <cstdlib>
#include <memory>
#include <string>

namespace eve::font {
namespace {

std::string extensionOf(const std::string &path) {
    const auto pos = path.find_last_of('.');
    if (pos == std::string::npos) return {};
    std::string ext = path.substr(pos);
    for (char &c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext;
}

bool isFontPath(const std::string &path) {
    const std::string ext = extensionOf(path);
    return ext == ".ttf" || ext == ".otf" || ext == ".ttc";
}

int queryInt(const std::string &key, const std::string &name, int fallback) {
    const auto q = key.find('?');
    if (q == std::string::npos) return fallback;

    std::string rest = key.substr(q + 1);
    size_t pos = 0;
    while (pos < rest.size()) {
        const size_t amp = rest.find('&', pos);
        const std::string pair =
            rest.substr(pos, amp == std::string::npos ? std::string::npos : amp - pos);
        const auto eq = pair.find('=');
        if (eq != std::string::npos && pair.substr(0, eq) == name)
            return std::atoi(pair.c_str() + eq + 1);
        if (amp == std::string::npos) break;
        pos = amp + 1;
    }
    return fallback;
}

class FontLoader : public eve::caps::IAssetReloader {
public:
    const char *reloadKind() const override { return "font"; }

    bool handlesPath(const std::string &normPath) const override {
        return isFontPath(eve::ResourceManager::pathOfKey(normPath));
    }

    /**
     * @brief Participate in the canonical cache reload protocol.
     * @return `false`; ResourceManager is the sole CPU cache owner and performs
     *         the reload before any consumer is notified.
     */
    eve::Result<bool> reload(const std::string &) override {
        return eve::Result<bool>::success(false);
    }

    eve::Resource *load(const std::string &key) override {
        const std::string path = eve::ResourceManager::pathOfKey(key);
        if (!isFontPath(path)) return nullptr;
        const int size = queryInt(key, "size", 16);

        auto *fs = filesystem::Filesystem::create();
        if (!fs) return nullptr;
        std::unique_ptr<filesystem::FileData> fd(fs->read(path));
        if (!fd || fd->getData() == nullptr || fd->getSize() == 0)
            throw eve::Exception("Could not read font file: %s", path.c_str());

        auto *mod = Font::create();
        if (!mod) throw eve::Exception("eve::font module is not loaded");
        return mod->newFontData(fd.get(), size);
    }
};

struct Register {
    Register() {
        static FontLoader loader;
        eve::cap::addListener<eve::caps::IAssetReloader>(&loader, eve::caps::IAssetReloader::kCache);
    }
} g_register;

}  // namespace
}  // namespace eve::font
