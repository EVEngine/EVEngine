#include "filesystem/HotReload.h"

#include "common/Module.h"
#include "filesystem/Filesystem.h"
#include "graphics/Graphics.h"
#include "map/TileConfig.h"
#include "map/TileLayer.h"
#include "particles/ParticleConfig.h"
#include "particles/ParticleEmitter.h"

#include <cctype>
#include <simplesquirrel/simplesquirrel.hpp>
#include <string>
#include <vector>

namespace eve::filesystem {
namespace {

std::string toLower(std::string s) {
    for (char &c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string extensionOf(const std::string &path) {
    auto pos = path.find_last_of('.');
    if (pos == std::string::npos) return {};
    return toLower(path.substr(pos));
}

std::string joinDir(const std::string &dir, const std::string &name) {
    if (dir.empty() || dir == ".") return name;
    if (dir.back() == '/' || dir.back() == '\\') return dir + name;
    return dir + "/" + name;
}

}  // namespace

Module_IMPL(HotReload, new HotReload());

std::string HotReload::normalizePath(std::string path) {
    for (char &c : path) {
        if (c == '\\') c = '/';
    }
    while (path.size() >= 2 && path[0] == '.' && path[1] == '/') path.erase(0, 2);
    while (path.size() > 1 && path.back() == '/') path.pop_back();
    return path;
}

void HotReload::bind(std::string path, std::string kind) {
    path = normalizePath(std::move(path));
    if (path.empty()) return;
    if (kind.empty()) kind = "auto";
    bindings_[path] = kind;
}

void HotReload::unbind(std::string path) {
    bindings_.erase(normalizePath(std::move(path)));
}

bool HotReload::isImagePath(const std::string &normPath) const {
    const std::string ext = extensionOf(normPath);
    return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga" ||
           ext == ".gif" || ext == ".webp" || ext == ".exr" || ext == ".hdr";
}

bool HotReload::isJsonPath(const std::string &normPath) const {
    return extensionOf(normPath) == ".json";
}

bool HotReload::reloadParticles(const std::string &normPath) {
    if (ecs::ComponentManager<particles::ParticleEmitter>::inst().registy == nullptr) return false;

    int reloaded = 0;
    auto view = ecs::View<particles::ParticleEmitter, particles::ParticleEmitter::Config,
                          particles::ParticleEmitter::Resource>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [cfg, res] = *it;
        if (!cfg->entity || res->path.empty()) continue;
        if (normalizePath(res->path) != normPath) continue;
        if (particles::reloadConfigFile(cfg->entity, nullptr)) ++reloaded;
    }
    return reloaded > 0;
}

bool HotReload::reloadTilemaps(const std::string &normPath) {
    if (ecs::ComponentManager<map::TileLayer>::inst().registy == nullptr) return false;

    int reloaded = 0;
    auto view = ecs::View<map::TileLayer, map::TileLayer::Config, map::TileLayer::Resource>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [cfg, res] = *it;
        if (!cfg->entity || res->path.empty()) continue;
        if (normalizePath(res->path) != normPath) continue;
        if (map::reloadConfigFile(cfg->entity, nullptr)) ++reloaded;
    }
    return reloaded > 0;
}

bool HotReload::reloadTextures(const std::string &normPath) {
    auto *gfx = eve::ModuleManager::getInstance<eve::graphics::Graphics>("Graphics");
    if (!gfx) return false;

    bool ok = gfx->reloadTextureFromFile(normPath);

    // Emitters that reference this texture path: force re-bind via config reload or setTexture.
    if (ecs::ComponentManager<particles::ParticleEmitter>::inst().registy != nullptr) {
        auto view = ecs::View<particles::ParticleEmitter, particles::ParticleEmitter::Config,
                              particles::ParticleEmitter::Resource>();
        for (auto it = view.begin(); it != view.end(); ++it) {
            auto [cfg, res] = *it;
            if (!cfg->entity || res->texturePath.empty()) continue;
            if (normalizePath(res->texturePath) != normPath) continue;
            try {
                graphics::Texture *tex = gfx->newTextureFromFile(normPath);
                cfg->entity->setTexture(tex);
                ok = true;
            } catch (...) {
            }
        }
    }

    // Tile layers that reference this atlas path.
    if (ecs::ComponentManager<map::TileLayer>::inst().registy != nullptr) {
        auto view = ecs::View<map::TileLayer, map::TileLayer::Config, map::TileLayer::Resource,
                              map::TileLayer::Tileset>();
        for (auto it = view.begin(); it != view.end(); ++it) {
            auto [cfg, res, ts] = *it;
            if (!cfg->entity || res->texturePath.empty()) continue;
            if (normalizePath(res->texturePath) != normPath) continue;
            try {
                graphics::Texture *tex = gfx->newTextureFromFile(normPath);
                cfg->entity->setTileset(tex, ts->firstGid, ts->columns, ts->margin, ts->spacing);
                ok = true;
            } catch (...) {
            }
        }
    }
    return ok;
}

bool HotReload::tryReload(std::string path) {
    const std::string norm = normalizePath(std::move(path));
    if (norm.empty()) return false;

    std::string kind = "auto";
    auto bit = bindings_.find(norm);
    if (bit != bindings_.end()) kind = bit->second;

    bool any = false;
    const bool wantParticle = (kind == "auto" || kind == "particle") && isJsonPath(norm);
    const bool wantTilemap = (kind == "auto" || kind == "tilemap") && isJsonPath(norm);
    const bool wantTexture = (kind == "auto" || kind == "texture") && isImagePath(norm);

    if (wantParticle) any = reloadParticles(norm) || any;
    if (wantTilemap) any = reloadTilemaps(norm) || any;
    if (wantTexture) any = reloadTextures(norm) || any;

    // Bound but unknown extension: still try when kind is explicit.
    if (kind == "particle" && !wantParticle) any = reloadParticles(norm) || any;
    if (kind == "tilemap" && !wantTilemap) any = reloadTilemaps(norm) || any;
    if (kind == "texture" && !wantTexture) any = reloadTextures(norm) || any;

    return any;
}

int HotReload::watchTree(std::string root) {
    auto *fs = Filesystem::create();
    if (!fs) return 0;
    root = normalizePath(std::move(root));
    if (root.empty()) root = ".";

    int added = 0;
    std::vector<std::string> stack;
    stack.push_back(root == "." ? std::string(".") : root);

    while (!stack.empty()) {
        std::string dir = stack.back();
        stack.pop_back();
        if (fs->watch(dir)) ++added;

        std::vector<std::string> items;
        try {
            if (dir == "." || dir.empty())
                items = fs->getDirectoryItems("");
            else
                items = fs->getDirectoryItems(dir);
        } catch (...) {
            continue;
        }

        for (const auto &name : items) {
            if (name.empty() || name == "." || name == "..") continue;
            const std::string child = (dir == "." || dir.empty()) ? name : joinDir(dir, name);
            Filesystem::Info info{};
            if (!fs->getInfo(child, info)) continue;
            if (info.type == "directory") stack.push_back(child);
        }
    }
    return added;
}

void HotReload::expose(ssq::Table &table) {
    auto cls = table.addClass(name, HotReload::create, false);
    expose(cls);
}

void HotReload::expose(ssq::Class &cls) {
    cls.addFunc("getName", &HotReload::getName);
    cls.addFunc("bind", &HotReload::bind);
    cls.addFunc("unbind", &HotReload::unbind);
    cls.addFunc("tryReload", &HotReload::tryReload);
    cls.addFunc("watchTree", &HotReload::watchTree);
}

}  // namespace eve::filesystem
