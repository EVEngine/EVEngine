// Reloads emitters when their JSON config changes, and re-binds emitter
// textures when the image they reference changes.
//
// Registered at kConsumer priority so graphics has already re-uploaded the
// texture by the time the emitters pick it up again.

#include "common/AssetReloader.h"
#include "common/Capability.h"
#include "common/ECS.h"
#include "common/Module.h"
#include "graphics/Graphics.h"
#include "particles/ParticleConfig.h"
#include "particles/ParticleEmitter.h"

#include <cctype>
#include <string>

namespace eve::particles {
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

/** Normalizes the way filesystem::HotReload does, so stored paths compare equal. */
std::string normalize(std::string path) {
    for (char &c : path) {
        if (c == '\\') c = '/';
    }
    while (path.size() >= 2 && path[0] == '.' && path[1] == '/') path.erase(0, 2);
    while (path.size() > 1 && path.back() == '/') path.pop_back();
    return path;
}

class ParticleReloader : public eve::caps::IAssetReloader {
public:
    const char *reloadKind() const override { return "particle"; }

    bool handlesPath(const std::string &normPath) const override {
        const std::string ext = extensionOf(normPath);
        return ext == ".json" || isImagePath(normPath);
    }

    bool reload(const std::string &normPath) override {
        if (ecs::current()->getManager<ParticleEmitter>() == nullptr) return false;
        return isImagePath(normPath) ? rebindTexture(normPath) : reloadConfig(normPath);
    }

private:
    static bool reloadConfig(const std::string &normPath) {
        int reloaded = 0;
        auto view = ecs::View<ParticleEmitter, ParticleEmitter::Config, ParticleEmitter::Resource>();
        for (auto it = view.begin(); it != view.end(); ++it) {
            auto [cfg, res] = *it;
            if (!cfg->entity || res->path.empty()) continue;
            if (normalize(res->path) != normPath) continue;
            if (reloadConfigFile(cfg->entity, nullptr)) ++reloaded;
        }
        return reloaded > 0;
    }

    static bool rebindTexture(const std::string &normPath) {
        auto *gfx = eve::ModuleManager::getInstance<eve::graphics::Graphics>("Graphics");
        if (!gfx) return false;

        bool any = false;
        auto view = ecs::View<ParticleEmitter, ParticleEmitter::Config, ParticleEmitter::Resource>();
        for (auto it = view.begin(); it != view.end(); ++it) {
            auto [cfg, res] = *it;
            if (!cfg->entity || res->texturePath.empty()) continue;
            if (normalize(res->texturePath) != normPath) continue;
            try {
                cfg->entity->setTexture(gfx->newTextureFromFile(normPath));
                any = true;
            } catch (...) {
            }
        }
        return any;
    }
};

struct Register {
    Register() {
        static ParticleReloader reloader;
        eve::cap::addListener<eve::caps::IAssetReloader>(&reloader,
                                                         eve::caps::IAssetReloader::kConsumer);
    }
} g_register;

}  // namespace
}  // namespace eve::particles
