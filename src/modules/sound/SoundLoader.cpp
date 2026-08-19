// Loads SoundData through the unified resource cache (common/Resource.h).
//
// Decoded PCM is the largest CPU asset in the engine, so repeated loads of one
// file share a single buffer; a file change refreshes it in place.

#include "common/AssetReloader.h"
#include "common/Capability.h"
#include "common/Exception.h"
#include "common/Module.h"
#include "common/Resource.h"
#include "filesystem/FileData.h"
#include "filesystem/Filesystem.h"
#include "sound/Sound.h"
#include "sound/SoundData.h"

#include <cctype>
#include <memory>
#include <string>

namespace eve::sound {
namespace {

std::string extensionOf(const std::string &path) {
    const auto pos = path.find_last_of('.');
    if (pos == std::string::npos) return {};
    std::string ext = path.substr(pos);
    for (char &c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext;
}

bool isSoundPath(const std::string &path) {
    const std::string ext = extensionOf(path);
    return ext == ".wav" || ext == ".ogg" || ext == ".mp3" || ext == ".flac" || ext == ".mod" ||
           ext == ".xm" || ext == ".s3m" || ext == ".it";
}

class SoundLoader : public eve::caps::IAssetReloader {
public:
    const char *reloadKind() const override { return "sound"; }

    bool handlesPath(const std::string &normPath) const override {
        return isSoundPath(eve::ResourceManager::pathOfKey(normPath));
    }

    eve::Resource *load(const std::string &key) override {
        const std::string path = eve::ResourceManager::pathOfKey(key);
        if (!isSoundPath(path)) return nullptr;

        auto *fs = filesystem::Filesystem::create();
        if (!fs) return nullptr;
        std::unique_ptr<filesystem::FileData> fd(fs->read(path));
        if (!fd || fd->getData() == nullptr || fd->getSize() == 0)
            throw eve::Exception("Could not read sound file: %s", path.c_str());

        auto *mod = Sound::create();
        if (!mod) throw eve::Exception("eve::sound module is not loaded");
        return mod->newSoundData(fd.get());
    }
};

struct Register {
    Register() {
        static SoundLoader loader;
        eve::cap::addListener<eve::caps::IAssetReloader>(&loader, eve::caps::IAssetReloader::kCache);
    }
} g_register;

}  // namespace
}  // namespace eve::sound
